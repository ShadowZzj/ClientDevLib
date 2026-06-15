#pragma once
#include "../entity/CLocalPlayer.h"
#include "../util/NetLog.h"
#include <json.hpp>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace GGTB
{

// 自动交易 —— broker 远控版(原 ImGui AutoTradeModule 已下线)。
//
// 配置全部由 web broker 下发(setAutoTradeConfig 命令),所有角色共用同一份:
// enabled / acceptAll / autoLockConfirm / acceptDelayMs / confirmDelayMs / whitelist。
// 本端不再画 ImGui,也不再写 per-character config.json —— 配置只活在 broker 的
// auto_trade.json 里,DLL 每次连上 broker 会被重新 push 一次。
//
// 交易状态机仍留在 DLL(不能搬到 broker):
//   1. 收到交易请求 (GC 0x07CC62, body[0]=发起方 id) -> 发 CG 412028 同意
//   2. 对方锁定自己那一侧 (GC 0x07D050) -> 发 CG 412033 锁定,延时后发 412035 确认
// 触发信号由 NetLog 的 recv hook 写进 atomic,本 worker 跨线程轮询 —— 管道往返
// 跟不上这种 lock-step 协议,所以保持本地轮询。
//
// 接受方永远不发 411030/411031/411032(加物品/钱),对自己零风险:最坏是"白同意
// 一笔空交易"。安全门(白名单/同意后才反应锁定)同原模块。
//
// 单例 + 自带 worker 线程。HackThread 调 Start()(起 worker)/ Stop()(停 + join)。
// 配置由 IO 线程经 SetConfig 写入,worker 每轮快照读取,全程 mutex 保护。
class AutoTradeController
{
  public:
    static AutoTradeController &Instance()
    {
        static AutoTradeController s_instance;
        return s_instance;
    }

    void Start()
    {
        if (started_.exchange(true))
            return;
        stop_.store(false);
        worker_ = std::thread([this] { WorkerLoop(); });
    }

    void Stop()
    {
        if (!started_.exchange(false))
            return;
        stop_.store(true);
        if (worker_.joinable())
            worker_.join();
    }

    // broker 下发的共享配置。线程安全。未提供的字段保留旧值。
    void SetConfig(const nlohmann::json &j)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        cfg_.enabled         = j.value("enabled", cfg_.enabled);
        cfg_.acceptAll       = j.value("acceptAll", cfg_.acceptAll);
        cfg_.autoLockConfirm = j.value("autoLockConfirm", cfg_.autoLockConfirm);
        cfg_.acceptDelayMs   = j.value("acceptDelayMs", cfg_.acceptDelayMs);
        cfg_.confirmDelayMs  = j.value("confirmDelayMs", cfg_.confirmDelayMs);
        if (cfg_.acceptDelayMs < 0)     cfg_.acceptDelayMs = 0;
        if (cfg_.acceptDelayMs > 10000) cfg_.acceptDelayMs = 10000;
        if (cfg_.confirmDelayMs < 100)  cfg_.confirmDelayMs = 100;
        if (cfg_.confirmDelayMs > 5000) cfg_.confirmDelayMs = 5000;
        if (j.contains("whitelist") && j["whitelist"].is_array())
        {
            cfg_.whitelist.clear();
            for (auto &e : j["whitelist"])
                if (e.is_string())
                    cfg_.whitelist.push_back(e.get<std::string>());
        }
        spdlog::info("GGTB::AutoTrade: config updated enabled={} acceptAll={} "
                     "autoLockConfirm={} acceptDelayMs={} confirmDelayMs={} wl={}",
                     cfg_.enabled, cfg_.acceptAll, cfg_.autoLockConfirm,
                     cfg_.acceptDelayMs, cfg_.confirmDelayMs, cfg_.whitelist.size());
    }

    // 运行时状态快照,给 broker 的 getAutoTradeStatus 用。
    nlohmann::json GetStatus() const
    {
        std::lock_guard<std::mutex> lk(mutex_);
        nlohmann::json j;
        j["enabled"]         = cfg_.enabled;
        j["acceptAll"]       = cfg_.acceptAll;
        j["autoLockConfirm"] = cfg_.autoLockConfirm;
        j["acceptDelayMs"]   = cfg_.acceptDelayMs;
        j["confirmDelayMs"]  = cfg_.confirmDelayMs;
        j["whitelist"]       = cfg_.whitelist;
        j["state"]           = state_.load();
        j["tradesAccepted"]  = tradesAccepted_;
        j["tradesDone"]      = tradesDone_;
        j["status"]          = status_;
        j["statusAgeMs"]     = statusMs_ ? static_cast<long long>(GetTickCount() - statusMs_) : 0;
        return j;
    }

  private:
    struct Config
    {
        bool                     enabled         = false;
        bool                     acceptAll       = false;
        bool                     autoLockConfirm = true;
        int                      acceptDelayMs   = 600;
        int                      confirmDelayMs  = 300;
        std::vector<std::string> whitelist;
    };

    AutoTradeController() = default;
    ~AutoTradeController() { Stop(); }
    AutoTradeController(const AutoTradeController &)            = delete;
    AutoTradeController &operator=(const AutoTradeController &) = delete;

    void WorkerLoop()
    {
        spdlog::info("GGTB::AutoTrade: worker thread started");
        // 启动时同步一次 tick 基线,避免对加载前发生的请求/锁定补触发。
        lastReqTick_  = NetLog::GetLastTradeRequestTickMs(nullptr);
        lastLockTick_ = NetLog::GetLastTradePeerLockTickMs();

        while (!stop_.load())
        {
            Config cfg = SnapshotConfig();
            if (cfg.enabled)
            {
                PollOnce(cfg);
            }
            else
            {
                // 禁用期间持续推进基线,这样重新启用后不会对历史请求/锁定补触发。
                lastReqTick_  = NetLog::GetLastTradeRequestTickMs(nullptr);
                lastLockTick_ = NetLog::GetLastTradePeerLockTickMs();
                state_.store(kStateIdle);
            }

            for (int i = 0; i < 10 && !stop_.load(); ++i)
                Sleep(10);
        }
        spdlog::info("GGTB::AutoTrade: worker thread exited");
    }

    Config SnapshotConfig() const
    {
        std::lock_guard<std::mutex> lk(mutex_);
        return cfg_;
    }

    void PollOnce(const Config &cfg)
    {
        uint32_t requesterId = 0;
        DWORD    reqTick = NetLog::GetLastTradeRequestTickMs(&requesterId);
        if (reqTick != 0 && reqTick != lastReqTick_)
        {
            lastReqTick_ = reqTick;
            HandleRequest(cfg, requesterId);
        }

        if (state_.load() == kStateAccepted)
        {
            // control 32 在收到请求时可能还没被引擎建好(recv→派发竞态),逐帧重试
            // 开窗直到成功或会话超时。OpenLocalTradeWindow 自身幂等、未建好返回 false。
            if (windowOpenPending_ && OpenLocalTradeWindow())
                windowOpenPending_ = false;

            DWORD lockTick = NetLog::GetLastTradePeerLockTickMs();
            if (lockTick != 0 && lockTick != lastLockTick_)
            {
                lastLockTick_ = lockTick;
                HandlePeerLock(cfg);
            }
            else if (GetTickCount() - sessionStartMs_ > kSessionTimeoutMs)
            {
                windowOpenPending_ = false;
                state_.store(kStateIdle);
                SetStatus(u8"会话超时(对方未锁定), 已重置");
            }
        }
    }

    void HandleRequest(const Config &cfg, uint32_t requesterId)
    {
        std::string name = LookupAroundPlayerNameById(requesterId);
        std::string who  = name.empty()
                               ? (std::string("id=") + std::to_string(requesterId))
                               : name;

        if (!ShouldAccept(cfg, name))
        {
            spdlog::info("GGTB::AutoTrade: 忽略交易请求(不在白名单): {}", who);
            SetStatus(std::string(u8"忽略(不在白名单): ") + who);
            return;
        }

        // 模拟人手反应的接受延时:收到请求后等一会再同意,避免瞬秒应答显得异常。
        if (cfg.acceptDelayMs > 0)
            Sleep(static_cast<DWORD>(cfg.acceptDelayMs));

        // 复刻手动点「接受」按钮:先本地开交易窗口,再发 412028。只发包不开窗的话
        // 窗口会停在请求态,直到对方放物品才被动弹出。control 32 可能因 recv→派发
        // 竞态还没建好,失败就标记 pending,PollOnce 在 kStateAccepted 期间逐帧重试。
        windowOpenPending_ = !OpenLocalTradeWindow();

        if (!SendTradeAccept(requesterId))
        {
            windowOpenPending_ = false;
            SetStatus(std::string(u8"发送同意失败: ") + who);
            return;
        }

        // 只对"同意之后"出现的锁定做反应:把锁定基线推到当前值。
        lastLockTick_   = NetLog::GetLastTradePeerLockTickMs();
        sessionStartMs_ = GetTickCount();
        state_.store(kStateAccepted);
        {
            std::lock_guard<std::mutex> lk(mutex_);
            ++tradesAccepted_;
        }
        spdlog::info("GGTB::AutoTrade: 已同意交易请求: {}", who);
        SetStatus(std::string(u8"已同意: ") + who);
    }

    void HandlePeerLock(const Config &cfg)
    {
        if (!cfg.autoLockConfirm)
        {
            state_.store(kStateIdle);
            SetStatus(u8"对方已锁定(自动锁定/确认已关闭, 需手动)");
            return;
        }

        SendTradeLock();                              // 412033
        Sleep(static_cast<DWORD>(cfg.confirmDelayMs)); // 锁定到确认的节流
        SendTradeConfirm();                           // 412035
        state_.store(kStateIdle);
        {
            std::lock_guard<std::mutex> lk(mutex_);
            ++tradesDone_;
        }
        spdlog::info("GGTB::AutoTrade: 已锁定并确认, 交易完成");
        SetStatus(u8"已锁定并确认, 交易完成");
    }

    bool ShouldAccept(const Config &cfg, const std::string &name)
    {
        if (cfg.acceptAll)
            return true;
        if (name.empty())
            return false; // 白名单模式下无法核实身份(AOI 外/读取失败) -> 拒绝
        for (auto &w : cfg.whitelist)
            if (w == name)
                return true;
        return false;
    }

    void SetStatus(const std::string &s)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        status_   = s;
        statusMs_ = GetTickCount();
    }

    // ----- 配置(IO 线程写, worker 读)-----
    Config cfg_;

    // ----- worker 状态(仅 worker 线程访问)-----
    std::atomic<int> state_{0}; // kStateIdle / kStateAccepted
    DWORD            lastReqTick_       = 0;
    DWORD            lastLockTick_      = 0;
    DWORD            sessionStartMs_    = 0;
    bool             windowOpenPending_ = false;

    // ----- 状态快照(mutex_)-----
    std::string status_;
    DWORD       statusMs_       = 0;
    int         tradesAccepted_ = 0;
    int         tradesDone_     = 0;

    mutable std::mutex mutex_;
    std::atomic<bool>  started_{false};
    std::atomic<bool>  stop_{false};
    std::thread        worker_;

    static constexpr DWORD kSessionTimeoutMs = 30000;
    static constexpr int   kStateIdle        = 0;
    static constexpr int   kStateAccepted    = 1;
};

} // namespace GGTB
