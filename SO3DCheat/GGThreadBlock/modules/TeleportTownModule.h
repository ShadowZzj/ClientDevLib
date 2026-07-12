#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include "ScreenOverlay.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <thread>

namespace GGTB
{

// 回城传送 —— 樂園鎮 / 獅子城。两个按钮都走 ReviveToTown(412017):
// 樂園鎮 reviveMode=1,獅子城 reviveMode=2(抓包确认 body=[02 00 00 00])。
// 原先钉在菜单右上角,现移进「传送」标签页。无持久化状态(纯一次性动作),
// CanAutoPause=false:手动回城不是会泄露给附近玩家的作弊效果,不该被 NPG 禁掉。
//
// 废墟传送(废墟回声谷 / 废墟乐园镇):是一条多步序列 —— 先回狮子城,等 3s 让传送 +
// 场景加载稳定,再寻路到狮子城里的传送 NPC(19811,落点 226,155),最后发一条
// CG_NPC_DIALOG_SELECT(411026)选废墟对应的对话项。序列含 5s 等待与寻路轮询,绝不能
// 在 OnRender(UI 线程,且只在菜单打开时才跑)里阻塞,所以丢给一个一次性 worker 线程
// 跑;同一时刻只允许一条序列(busy_ 互斥)。从 worker 线程调 ReviveToTown / MoveTo /
// SendDialogSelect 是本项目既有做法(AutoFishing / AutoDelegation 都这么干),都 SEH 包过。
class TeleportTownModule : public IModule
{
  public:
    TeleportTownModule() : IModule(u8"城镇传送") {}

    ~TeleportTownModule() override
    {
        stop_.store(true);
        if (worker_.joinable())
            worker_.join();
    }

    bool CanAutoPause() const override { return false; }
    Tab  GetTab() const override { return Tab::Teleport; }

    void OnRender() override
    {
        // 本模块无"启用"勾选(纯按钮动作),标题不再由 Setting 统一画,自己带一个。
        ImGui::TextUnformatted(u8"城镇传送");
        ImGui::SameLine();
        const ImVec2 btn(120.0f, 0.0f);
        if (ImGui::Button(u8"樂園鎮", btn))
        {
            bool ok = ReviveToTown(true, 1);
            ScreenOverlay::ShowToast(ok ? u8"樂園鎮：已送出" : u8"樂園鎮：发送失败", 3000);
            spdlog::info("GGTB::Teleport: 樂園鎮 ReviveToTown(mode=1) ok={}", ok);
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"獅子城", btn))
        {
            bool ok = ReviveToTown(true, 2);
            ScreenOverlay::ShowToast(ok ? u8"獅子城：已送出" : u8"獅子城：发送失败", 3000);
            spdlog::info("GGTB::Teleport: 獅子城 ReviveToTown(mode=2) ok={}", ok);
        }
        ImGui::TextDisabled(u8"点击直接回城(由服务端决定落点)");

        // ---- 废墟传送:回狮子城 -> 等 5s -> 寻路到 NPC -> 发对话项 ----
        ImGui::Separator();
        ImGui::TextUnformatted(u8"废墟传送");
        ImGui::SameLine();
        const bool busy = busy_.load(std::memory_order_acquire);
        ImGui::BeginDisabled(busy);
        if (ImGui::Button(u8"废墟回声谷", btn))
            StartRuinsSequence(kOptRuinsEchoValley, u8"废墟回声谷");
        ImGui::SameLine();
        if (ImGui::Button(u8"废墟乐园镇", btn))
            StartRuinsSequence(kOptRuinsParadiseTown, u8"废墟乐园镇");
        ImGui::EndDisabled();
        ImGui::TextDisabled(u8"先传狮子城,等3秒寻路到(226,155)距离<1再与NPC对话");

        if (busy)
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), u8"%s 传送中: %s",
                               seqLabel_, PhaseText(seqPhase_.load(std::memory_order_relaxed)));
    }

  private:
    // 废墟传送共用的本地 NPC 落点 + 对话:回狮子城后寻路到该 tile,SendDialogSelect 选废墟。
    static constexpr float    kRuinsNpcX             = 226.0f;
    static constexpr float    kRuinsNpcY             = 155.0f;
    static constexpr uint32_t kRuinsNpcId            = 19129;
    static constexpr uint32_t kOptRuinsEchoValley    = 10282; // 废墟回声谷
    static constexpr uint32_t kOptRuinsParadiseTown  = 10281; // 废墟乐园镇
    static constexpr DWORD    kWarpSettleMs          = 3000;   // 回城后等场景加载
    static constexpr DWORD    kWalkTimeoutMs         = 15000;  // 寻路到达上限,超时也发对话
    static constexpr DWORD    kMoveReissueMs         = 2000;   // 寻路途中重发 MoveTo 周期
    static constexpr float    kArriveDistSq          = 1.0f;   // 距落点 <1 单位算到达

    enum Phase
    {
        kPhaseWarp   = 0,
        kPhaseWalk   = 1,
        kPhaseDialog = 2,
    };

    static const char *PhaseText(int p)
    {
        switch (p)
        {
        case kPhaseWalk:   return u8"寻路中";
        case kPhaseDialog: return u8"发送对话";
        default:           return u8"回狮子城";
        }
    }

    // 在 UI 线程(OnRender)调用:启动一条废墟传送序列。busy_ 防重入;上一条已结束的
    // worker 在这里 join 回收(线程结束时已把 busy_ 置 false,join 立即返回)。
    void StartRuinsSequence(uint32_t optId, const char *label)
    {
        if (busy_.exchange(true, std::memory_order_acq_rel))
        {
            ScreenOverlay::ShowToast(u8"传送序列进行中,请稍候", 2500);
            return;
        }
        if (worker_.joinable())
            worker_.join();
        seqLabel_ = label; // 仅 UI 线程读写(OnRender 显示),无需同步
        seqPhase_.store(kPhaseWarp, std::memory_order_relaxed);
        worker_ = std::thread([this, optId, label] { RunRuinsSequence(optId, label); });
    }

    void RunRuinsSequence(uint32_t optId, const char *label)
    {
        spdlog::info("GGTB::Teleport: 废墟序列开始 {} optId={}", label, optId);

        // 1) 回狮子城(与「獅子城」按钮同:ReviveToTown mode=2)。
        seqPhase_.store(kPhaseWarp, std::memory_order_relaxed);
        bool warpOk = ReviveToTown(true, 2);
        spdlog::info("GGTB::Teleport: 废墟序列 回狮子城 ok={}", warpOk);

        // 2) 等 3s 让传送 + 场景加载完成(可被退出打断)。
        if (!SleepInterruptible(kWarpSettleMs))
        {
            busy_.store(false, std::memory_order_release);
            return;
        }

        // 3) 寻路到 NPC 落点,轮询到达(超时也继续 —— 服务端会校验距离,够近就行)。
        seqPhase_.store(kPhaseWalk, std::memory_order_relaxed);
        WalkToNpc();
        if (stop_.load())
        {
            busy_.store(false, std::memory_order_release);
            return;
        }

        // 4) 发废墟对应的对话项 raw 包(CG_NPC_DIALOG_SELECT 411026)。
        seqPhase_.store(kPhaseDialog, std::memory_order_relaxed);
        bool dlgOk = SendDialogSelect(kRuinsNpcId, optId);
        spdlog::info("GGTB::Teleport: 废墟序列 SendDialogSelect(npc={}, opt={}) ok={}",
                     kRuinsNpcId, optId, dlgOk);
        ScreenOverlay::ShowToast(dlgOk ? u8"废墟传送:已送出对话" : u8"废墟传送:对话发送失败",
                                 3000);

        busy_.store(false, std::memory_order_release);
    }

    // 寻路到 (kRuinsNpcX, kRuinsNpcY)。MoveTo 驱动引擎自身寻路队列;每 kMoveReissueMs
    // 重发一次以防中途被地形/卡顿打断(同目标重发无害,等价于玩家重复点地)。轮询本地
    // 坐标直到进入到达半径或超时。GetLocalPosition / MoveTo 均 SEH 包过,worker 可调。
    void WalkToNpc()
    {
        const DWORD start = GetTickCount();
        DWORD       lastMove = 0;
        while (!stop_.load() && static_cast<int>(GetTickCount() - start) < static_cast<int>(kWalkTimeoutMs))
        {
            DWORD now = GetTickCount();
            if (lastMove == 0 || static_cast<int>(now - lastMove) >= static_cast<int>(kMoveReissueMs))
            {
                MoveTo(kRuinsNpcX, kRuinsNpcY, /*action=*/1, /*targetId=*/0);
                lastMove = now;
            }

            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (GetLocalPosition(x, y, z))
            {
                float dx = x - kRuinsNpcX;
                float dy = y - kRuinsNpcY;
                if (dx * dx + dy * dy <= kArriveDistSq)
                {
                    spdlog::info("GGTB::Teleport: 废墟序列 已到达 NPC 落点 ({:.1f},{:.1f})", x, y);
                    return;
                }
            }
            if (!SleepInterruptible(200))
                return;
        }
        spdlog::info("GGTB::Teleport: 废墟序列 寻路超时,直接发对话(服务端校验距离)");
    }

    // 分片睡眠,stop_ 翻转(模块析构/退出)立即返回 false 让调用方收尾。
    bool SleepInterruptible(DWORD totalMs)
    {
        constexpr DWORD slice = 50;
        for (DWORD slept = 0; slept < totalMs; slept += slice)
        {
            if (stop_.load())
                return false;
            Sleep(slice);
        }
        return !stop_.load();
    }

    std::atomic<bool> busy_{false};      // 一次只允许一条废墟序列
    std::atomic<bool> stop_{false};      // 析构/退出时打断 worker
    std::atomic<int>  seqPhase_{kPhaseWarp};
    const char       *seqLabel_ = u8"";  // 仅 UI 线程访问
    std::thread       worker_;
};

} // namespace GGTB
