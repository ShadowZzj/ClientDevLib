#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include "ScreenOverlay.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>

namespace GGTB
{

// 定点移动 —— Ctrl+K toggle。开启瞬间把当前世界坐标快照成「定点」,之后 worker 每隔
// 「检测间隔」(UI 可调,默认 150ms)读一次玩家坐标:偏离定点就调 StartPathTo 走回去,
// 回到定点附近就静止。相当于把角色拴在定点上,被技能击退 / 手动走开都会自动寻路走回原地。
//
// 与「定点挂机」(StationaryFarm) 的区别:那个是每拍直接写坐标瞬移锁死,这个是走网格 A*
// 自动寻路走回去 —— 看起来像正常玩家在归位,而不是瞬移。
//
// CanAutoPause=false:自动走路是正常玩家行为,不是会泄露给附近真人的作弊补丁,不该被 NPG
// 拍停(而且归途被拍停反而更怪),因此 Ctrl+K「不受 NPG 影响」天然成立。
//
// 持久化:只存「检测间隔」(pollMs_),开关本身不持久化 —— 定点是「按下那一刻的当前位置」
// 的临时快照,跨会话/换地图后是无意义甚至危险的坐标(可能把人走进墙里),所以每次登录都
// 从关闭态开始;但用户调好的检测间隔应该记住。故 SaveState/LoadState 只碰 intervalMs,
// 不走 IModule 基类那套(基类会连 enabled 一起存/恢复)。
//
// StartPathTo/StopPath/GetPathProgress 是全局单例 PathWalker,和「自动寻路」模块共享。启用
// 本模块时不要同时用自动寻路手动走别处 —— 会互相打断。
class FixedPointMoveModule : public IModule
{
  public:
    FixedPointMoveModule() : IModule(u8"定点移动")
    {
        worker_ = std::thread([this] { WorkerLoop(); });
    }

    ~FixedPointMoveModule() override
    {
        stop_.store(true);
        if (worker_.joinable())
            worker_.join();
        StopPath(); // 卸载前取消可能在跑的归途,免得 PathWalker worker 悬着
    }

    bool CanAutoPause() const override { return false; }

    // Ctrl+K 热键调用(dllmain 泵)。返回切换后的启用态。
    bool Toggle() { return SetActive(!active_.load()); }

    void OnRender() override
    {
        bool cur = active_.load();
        bool box = cur;
        if (ImGui::Checkbox(u8"定点移动 (Ctrl+K)##fixedmove", &box) && box != cur)
            SetActive(box);

        // 检测间隔:worker 多久读一次坐标 / 判一次漂移。小=归位更跟手但读得勤,大=更省。
        // 持久化(点「保存」落盘),不受开关状态影响,随时可调。
        int iv = pollMs_.load();
        ImGui::PushItemWidth(160.0f);
        if (ImGui::SliderInt(u8"检测间隔##fixedmove", &iv, kMinPollMs, kMaxPollMs, "%d ms"))
            pollMs_.store(iv);
        ImGui::PopItemWidth();

        if (active_.load() && hasAnchor_.load())
        {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), u8"定点: (%.1f, %.1f)",
                               anchorX_.load(), anchorY_.load());

            PathProgress pg = GetPathProgress();
            switch (pg.state)
            {
            case PathState::Walking:
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f),
                                   u8"归位中 段%d  (%.1f,%.1f)", pg.segments, pg.curX, pg.curY);
                break;
            case PathState::Failed:
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), u8"归位失败: %s",
                                   pg.error.empty() ? u8"(未知)" : pg.error.c_str());
                break;
            default:
                ImGui::TextDisabled(u8"已在定点,待命");
                break;
            }
        }
        else
        {
            ImGui::TextDisabled(u8"关闭 —— Ctrl+K 在当前位置设定点");
        }
    }

    // 只持久化检测间隔;开关不持久化(见类头注释)。故意不调用 IModule::SaveState/
    // LoadState —— 那套会连 enabled 一起存/恢复,会让角色一登录就自动开启定点移动。
    std::string ConfigKey() const override { return "FixedPointMove"; }

    void SaveState(nlohmann::json &j) const override { j["intervalMs"] = pollMs_.load(); }

    void LoadState(const nlohmann::json &j) override
    {
        int iv = j.value("intervalMs", kDefaultPollMs);
        if (iv < kMinPollMs) iv = kMinPollMs;
        if (iv > kMaxPollMs) iv = kMaxPollMs;
        pollMs_.store(iv);
    }

  private:
    // 到达判定半径。取比 PathWalker 自身到达阈值(1.5)略大,形成迟滞:归途会在 ≤1.5
    // 处停下,而我们只在偏离 >2.0 时才重新发路径,避免边界来回抖。
    static constexpr float kArriveEps         = 2.0f;
    static constexpr float kSameTargetEps     = 1.0f;  // 目标是否仍是当前定点的比对容差
    static constexpr DWORD kReissueCooldownMs = 1200;  // 两次 StartPathTo 最小间隔,防抖/防失败刷屏
    static constexpr int   kDefaultPollMs     = 150;   // 检测间隔默认值(可 UI 调 + 持久化)
    static constexpr int   kMinPollMs         = 30;
    static constexpr int   kMaxPollMs         = 1000;
    static constexpr int   kSleepChunkMs      = 25;    // 分片 sleep 粒度,保证 stop_ 响应

    bool SetActive(bool on)
    {
        if (on == active_.load())
            return active_.load();

        if (on)
        {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (!GetLocalPosition(x, y, z))
            {
                spdlog::warn("GGTB::FixedMove: 读不到坐标,未启用");
                ScreenOverlay::ShowToast(u8"定点移动：读不到坐标", 2500);
                return false; // 保持关闭
            }
            anchorX_.store(x);
            anchorY_.store(y);
            hasAnchor_.store(true);
            lastIssueMs_.store(0);
            active_.store(true);
            enabled_ = true;
            spdlog::info("GGTB::FixedMove: 设定点 ({:.1f},{:.1f})", x, y);
            char buf[96];
            std::snprintf(buf, sizeof(buf), u8"定点移动：开 (%.1f, %.1f)", x, y);
            ScreenOverlay::ShowToast(buf, 2500);
        }
        else
        {
            active_.store(false);
            enabled_ = false;
            // hasAnchor_ / StopPath 交给 worker 下一拍处理,避免这里(可能是 D3D 或热键
            // 线程)与 worker 同时碰 PathWalker。
            spdlog::info("GGTB::FixedMove: 关闭");
            ScreenOverlay::ShowToast(u8"定点移动：关", 2000);
        }
        return active_.load();
    }

    void WorkerLoop()
    {
        bool prevActive = false;
        while (!stop_.load())
        {
            bool a = active_.load();
            if (a && hasAnchor_.load())
            {
                Supervise();
            }
            else if (!a && prevActive)
            {
                StopPath(); // 刚被关掉:取消归途(唯一从 worker 之外触发的收尾)
                hasAnchor_.store(false);
            }
            prevActive = a;

            // 分片 sleep 到「检测间隔」,每片检查 stop_,保证卸载/关闭能及时退出。
            int interval = pollMs_.load();
            if (interval < kMinPollMs) interval = kMinPollMs;
            for (int slept = 0; slept < interval && !stop_.load(); slept += kSleepChunkMs)
                Sleep(kSleepChunkMs);
        }
    }

    void Supervise()
    {
        float cx = 0.0f, cy = 0.0f, cz = 0.0f;
        if (!GetLocalPosition(cx, cy, cz))
            return; // 切图/未就绪,下一拍再看

        const float ax = anchorX_.load();
        const float ay = anchorY_.load();
        const float dx = cx - ax;
        const float dy = cy - ay;
        const float d2 = dx * dx + dy * dy;
        if (d2 <= kArriveEps * kArriveEps)
            return; // 已在定点

        // 已经在走回同一个定点就别打断(StartPathTo 会取消上一条路径,重发=原地打转)。
        PathProgress pg = GetPathProgress();
        const bool walkingHome = pg.state == PathState::Walking &&
                                 std::fabs(pg.targetX - ax) <= kSameTargetEps &&
                                 std::fabs(pg.targetY - ay) <= kSameTargetEps;
        if (walkingHome)
            return;

        const DWORD now = GetTickCount();
        if (lastIssueMs_.load() != 0 &&
            static_cast<int32_t>(now - lastIssueMs_.load()) < static_cast<int32_t>(kReissueCooldownMs))
            return;
        lastIssueMs_.store(now);

        const bool ok = StartPathTo(ax, ay, /*action=*/1, /*targetId=*/0);
        spdlog::info("GGTB::FixedMove: 偏离 {:.1f} -> StartPathTo({:.1f},{:.1f}) ok={}",
                     std::sqrt(d2), ax, ay, ok);
    }

    std::atomic<bool>  active_{false};
    std::atomic<bool>  hasAnchor_{false};
    std::atomic<float> anchorX_{0.0f};
    std::atomic<float> anchorY_{0.0f};
    std::atomic<int>   pollMs_{kDefaultPollMs};
    std::atomic<DWORD> lastIssueMs_{0};
    std::atomic<bool>  stop_{false};
    std::thread        worker_;
};

} // namespace GGTB
