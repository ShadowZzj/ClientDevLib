#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include "../util/NetLog.h"
#include <imgui/imgui.h>
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

// 自动交易。
//
// 流程(本端 = 接受方,全程只收不发物品):
//   1. 收到交易请求 (GC 0x07CC62, body[0]=发起方 id) -> 发 CG 412028 同意
//   2. 对方锁定自己那一侧 (GC 0x07D050) -> 发 CG 412033 锁定,延时后发 412035 确认
// 三条 CG 包都是单 DWORD body,走 CLocalPlayer 的 SendTradeAccept/Lock/Confirm
// (内部 Net__SendDword)。触发信号由 NetLog 的 recv hook 写进 atomic,本 worker
// 跨线程轮询 —— 跟 FireFullPower 读 GetLastSkillResultTickMs 一个套路。
//
// 接受方永远不发 411030/411031/411032(加物品/钱),所以对自己零风险:最坏情况是
// "白同意了一笔空交易"。是否真的成交完全由发起方放东西 + 双方确认决定。
//
// 安全门:
//   - acceptAll_=false(默认)时只接受白名单角色。发起方 id 经 LookupAroundPlayer-
//     NameById 反查角色名(匹配 CUser+112 userId);AOI 外或 id 与 userId 不一致
//     会返回空名 -> 白名单模式下安全拒绝。若实测发现名字总是空,用"接受所有人"兜底。
//     (已确认:GC_OnTradeRequest_07CC62 把 body[0] 交给 Trade_InitRequestWindow,
//      后者用 EntityManager__FindUserById 解析它 —— 所以 body[0] 就是 AOI 用户
//      列表里的 entity/userId,反查口径与游戏一致。)
//   - 只对"本端同意之后"出现的锁定做反应:同意时把 lock 基线推到当前 tick,会话用
//     sessionStartMs_ 兜底超时,避免手动交易/历史锁定被自动确认。
//
// 线程模型同 AutoPickup:ctor 起 worker / dtor join。UI 线程只读 mutex 下的快照。
//
// CanAutoPause()=false:交易对象本来就是身边的玩家,若被 NPG(附近有人就暂停)拍下去
// 这功能永远不工作,所以不参与自动暂停。自动同意不是"作弊视觉泄漏",无需对附近玩家隐藏。
class AutoTradeModule : public IModule
{
  public:
    AutoTradeModule() : IModule(u8"自动交易")
    {
        worker_ = std::thread([this] { WorkerLoop(); });
    }

    ~AutoTradeModule() override
    {
        stop_.store(true);
        if (worker_.joinable())
            worker_.join();
    }

    bool CanAutoPause() const override { return false; }

    void OnRender() override
    {
        ImGui::Checkbox(u8"启用##AutoTrade", &enabled_);
        ImGui::SameLine();
        ImGui::TextDisabled(u8"(有人交易我→自动同意→对方锁定→自动锁定+确认)");

        ImGui::Checkbox(u8"接受所有人##AutoTrade", &acceptAll_);
        ImGui::SameLine();
        if (acceptAll_)
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f),
                               u8"<- 危险: 任何人发起都自动同意");
        else
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f),
                               u8"<- 仅接受下方白名单角色");

        ImGui::Checkbox(u8"自动锁定并确认##AutoTrade", &autoLockConfirm_);
        if (!autoLockConfirm_)
        {
            ImGui::SameLine();
            ImGui::TextDisabled(u8"(关: 只自动同意, 锁定/确认手动)");
        }

        ImGui::SliderInt(u8"确认延时(ms)##AutoTrade", &confirmDelayMs_, 100, 1500);

        if (!acceptAll_)
            RenderWhitelist();

        // 状态快照
        std::string status;
        DWORD       statusAge = 0;
        int         accepted = 0, done = 0;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            status    = status_;
            statusAge = statusMs_ ? (GetTickCount() - statusMs_) : 0;
            accepted  = tradesAccepted_;
            done      = tradesDone_;
        }

        ImGui::Separator();
        if (!enabled_)
        {
            ImGui::TextDisabled(u8"未启用 (worker 仍在运行, 但不响应交易)");
        }
        else
        {
            const char *stateStr =
                state_.load() == kStateAccepted ? u8"已同意, 等待对方锁定" : u8"待命";
            ImGui::Text(u8"状态: %s", stateStr);
        }
        if (!status.empty())
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), u8"%s (%lus前)",
                               status.c_str(),
                               static_cast<unsigned long>(statusAge / 1000));
        ImGui::Text(u8"累计: 已同意 %d, 已完成 %d", accepted, done);
    }

    void OnShutdown() override {}
    void OnResume() override {}

    // Persistence
    std::string ConfigKey() const override { return "AutoTrade"; }
    void SaveState(nlohmann::json &j) const override
    {
        IModule::SaveState(j);
        j["acceptAll"]       = acceptAll_;
        j["autoLockConfirm"] = autoLockConfirm_;
        j["confirmDelayMs"]  = confirmDelayMs_;
        std::lock_guard<std::mutex> lk(mutex_);
        j["whitelist"]       = whitelist_;
    }
    void LoadState(const nlohmann::json &j) override
    {
        acceptAll_       = j.value("acceptAll", acceptAll_);
        autoLockConfirm_ = j.value("autoLockConfirm", autoLockConfirm_);
        confirmDelayMs_  = j.value("confirmDelayMs", confirmDelayMs_);
        if (j.contains("whitelist") && j["whitelist"].is_array())
        {
            std::lock_guard<std::mutex> lk(mutex_);
            whitelist_.clear();
            for (auto &e : j["whitelist"])
                if (e.is_string())
                    whitelist_.push_back(e.get<std::string>());
        }
        if (confirmDelayMs_ < 100)  confirmDelayMs_ = 100;
        if (confirmDelayMs_ > 1500) confirmDelayMs_ = 1500;
        IModule::LoadState(j);
    }

  private:
    void RenderWhitelist()
    {
        if (!ImGui::CollapsingHeader(u8"交易白名单##AutoTrade"))
            return;

        ImGui::TextDisabled(u8"只有白名单中的角色发起交易才会自动同意");

        static char inputBuf[64] = {};
        ImGui::SetNextItemWidth(220);
        ImGui::InputText(u8"角色名##ATwlInput", inputBuf, sizeof(inputBuf));
        ImGui::SameLine();
        if (ImGui::Button(u8"添加##ATwlAdd") && inputBuf[0])
        {
            AddWhitelist(inputBuf);
            inputBuf[0] = 0;
        }

        std::vector<std::string> wl;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            wl = whitelist_;
        }
        ImGui::Text(u8"当前白名单 (%d):", static_cast<int>(wl.size()));
        for (auto &n : wl)
        {
            ImGui::PushID(n.c_str());
            ImGui::BulletText("%s", n.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(u8"删除"))
                RemoveWhitelist(n);
            ImGui::PopID();
        }
    }

    void AddWhitelist(const std::string &name)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        for (auto &w : whitelist_)
            if (w == name)
                return;
        whitelist_.push_back(name);
    }

    void RemoveWhitelist(const std::string &name)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        whitelist_.erase(std::remove(whitelist_.begin(), whitelist_.end(), name),
                         whitelist_.end());
    }

    void WorkerLoop()
    {
        spdlog::info("GGTB::AutoTrade: worker thread started");
        // 启动时同步一次 tick 基线,避免对加载前发生的请求/锁定补触发。
        lastReqTick_  = NetLog::GetLastTradeRequestTickMs(nullptr);
        lastLockTick_ = NetLog::GetLastTradePeerLockTickMs();
        DWORD lastHeartbeat = 0;

        while (!stop_.load())
        {
            if (enabled_)
            {
                PollOnce();
            }
            else
            {
                // 禁用期间持续推进基线,这样重新启用后不会对一条历史请求/锁定补触发。
                lastReqTick_  = NetLog::GetLastTradeRequestTickMs(nullptr);
                lastLockTick_ = NetLog::GetLastTradePeerLockTickMs();
                state_.store(kStateIdle);
            }

            DWORD now = GetTickCount();
            if (enabled_ && now - lastHeartbeat > 15000)
            {
                spdlog::info("GGTB::AutoTrade: heartbeat — state={}, acceptAll={}, "
                             "accepted={}, done={}",
                             state_.load(), acceptAll_, tradesAccepted_, tradesDone_);
                lastHeartbeat = now;
            }

            for (int i = 0; i < 10 && !stop_.load(); ++i)
                Sleep(10);
        }
        spdlog::info("GGTB::AutoTrade: worker thread exited");
    }

    void PollOnce()
    {
        uint32_t requesterId = 0;
        DWORD    reqTick = NetLog::GetLastTradeRequestTickMs(&requesterId);
        if (reqTick != 0 && reqTick != lastReqTick_)
        {
            lastReqTick_ = reqTick;
            HandleRequest(requesterId);
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
                HandlePeerLock();
            }
            else if (GetTickCount() - sessionStartMs_ > kSessionTimeoutMs)
            {
                windowOpenPending_ = false;
                state_.store(kStateIdle);
                SetStatus(u8"会话超时(对方未锁定), 已重置");
            }
        }
    }

    void HandleRequest(uint32_t requesterId)
    {
        std::string name = LookupAroundPlayerNameById(requesterId);
        std::string who  = name.empty()
                               ? (std::string("id=") + std::to_string(requesterId))
                               : name;

        if (!ShouldAccept(name))
        {
            spdlog::info("GGTB::AutoTrade: 忽略交易请求(不在白名单): {}", who);
            SetStatus(std::string(u8"忽略(不在白名单): ") + who);
            return;
        }

        // 复刻手动点「接受」按钮:先本地开交易窗口(调引擎 Trade_OpenLocalExchange-
        // Window),再发 412028。只发包不开窗的话窗口会停在请求态,直到对方放物品才
        // 被动弹出 —— 这正是用户反馈的"窗口不正常弹出"。control 32 可能因 recv→派发
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

    void HandlePeerLock()
    {
        if (!autoLockConfirm_)
        {
            state_.store(kStateIdle);
            SetStatus(u8"对方已锁定(自动锁定/确认已关闭, 需手动)");
            return;
        }

        SendTradeLock();                            // 412033
        Sleep(static_cast<DWORD>(confirmDelayMs_)); // 锁定到确认的节流
        SendTradeConfirm();                         // 412035
        state_.store(kStateIdle);
        {
            std::lock_guard<std::mutex> lk(mutex_);
            ++tradesDone_;
        }
        spdlog::info("GGTB::AutoTrade: 已锁定并确认, 交易完成");
        SetStatus(u8"已锁定并确认, 交易完成");
    }

    bool ShouldAccept(const std::string &name)
    {
        if (acceptAll_)
            return true;
        if (name.empty())
            return false; // 白名单模式下无法核实身份(AOI 外/读取失败) -> 拒绝
        std::lock_guard<std::mutex> lk(mutex_);
        for (auto &w : whitelist_)
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

    // ----- 配置(UI 线程写, worker 读) -----
    bool acceptAll_       = false;
    bool autoLockConfirm_ = true;
    int  confirmDelayMs_  = 300;

    std::vector<std::string> whitelist_; // mutex_ 保护

    // ----- worker 状态(仅 worker 线程访问)-----
    std::atomic<int> state_{0}; // kStateIdle / kStateAccepted (UI 读)
    DWORD            lastReqTick_    = 0;
    DWORD            lastLockTick_   = 0;
    DWORD            sessionStartMs_ = 0;
    bool             windowOpenPending_ = false; // 仍需本地开交易窗口(PollOnce 重试)

    // ----- UI 快照(mutex_) -----
    std::string status_;
    DWORD       statusMs_       = 0;
    int         tradesAccepted_ = 0;
    int         tradesDone_     = 0;

    mutable std::mutex mutex_;
    std::atomic<bool>  stop_{false};
    std::thread        worker_;

    static constexpr DWORD kSessionTimeoutMs = 30000;
    static constexpr int   kStateIdle        = 0;
    static constexpr int   kStateAccepted    = 1;
};

} // namespace GGTB
