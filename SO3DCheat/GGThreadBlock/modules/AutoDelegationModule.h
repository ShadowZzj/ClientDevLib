#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <atomic>
#include <thread>

namespace GGTB
{

// 自动委托 (AutoDelegation).
//
// User configures three values: npcId + two dialogOptionIds. When enabled, a
// worker thread loops:
//   SendDialogSelect(npc, opt1) → sleep 1s → SendDialogSelect(npc, opt2) → sleep 1s → repeat.
//
// Each SendDialogSelect emits CG_NPC_DIALOG_SELECT (proto 411026) via
// Net__SendDialogSelect — the same packet the engine fires from
// OnNpcDialogConfirm_521603 / OnNpcDialogOption_Quest.
//
// NPG-exempt by design (CanAutoPause = false): NPC delegation is conversational
// and benign-looking on the wire; pausing it on nearby players would just block
// progress without hiding anything. NOT persisted (ConfigKey returns empty) —
// entered values reset to zero every launch.
class AutoDelegationModule : public IModule
{
  public:
    AutoDelegationModule() : IModule(u8"自动委托")
    {
        worker_ = std::thread([this] { WorkerLoop(); });
    }

    ~AutoDelegationModule() override
    {
        stop_.store(true);
        if (worker_.joinable())
            worker_.join();
    }

    bool CanAutoPause() const override { return false; }

    void OnRender() override
    {
        ImGui::Checkbox(u8"自动委托##AutoDelegation", &enabled_);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f),
                           u8"(不持久化, 不受NPG影响)");

        ImGui::InputInt(u8"npcID##AutoDelegation",     &npcId_);
        ImGui::InputInt(u8"对话ID1##AutoDelegation",   &dialogOpt1_);
        ImGui::InputInt(u8"对话ID2##AutoDelegation",   &dialogOpt2_);
        ImGui::SliderInt(u8"间隔(ms)##AutoDelegation", &intervalMs_, 100, 5000);

        if (npcId_ < 0)      npcId_      = 0;
        if (dialogOpt1_ < 0) dialogOpt1_ = 0;
        if (dialogOpt2_ < 0) dialogOpt2_ = 0;

        ImGui::Separator();
        if (!enabled_)
            ImGui::TextDisabled(u8"未启用 (worker 仍在运行,但不发包)");
        else
        {
            DWORD lastSendAge;
            int   lastStep;
            unsigned long sentTotal;
            {
                lastSendAge = lastSendMs_ ? (GetTickCount() - lastSendMs_) : 0;
                lastStep    = lastStep_;
                sentTotal   = static_cast<unsigned long>(sentTotal_);
            }
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                               u8"运行中: 上次发送步骤=%d, 距上次=%lums, 累计=%lu",
                               lastStep, static_cast<unsigned long>(lastSendAge),
                               sentTotal);
        }
    }

    void OnShutdown() override {}
    void OnResume()   override {}

    // Not persisted — see class comment.
    std::string ConfigKey() const override { return {}; }

  private:
    void WorkerLoop()
    {
        spdlog::info("GGTB::AutoDelegation: worker thread started");

        while (!stop_.load())
        {
            if (enabled_ && npcId_ > 0 && dialogOpt1_ > 0 && dialogOpt2_ > 0)
            {
                // Step 1: send the first dialog packet.
                SendDialogSelect(static_cast<uint32_t>(npcId_),
                                 static_cast<uint32_t>(dialogOpt1_));
                lastSendMs_ = GetTickCount();
                lastStep_   = 1;
                ++sentTotal_;

                // Sleep intervalMs_, sliced for stop_/disable responsiveness.
                if (!SleepInterruptible(static_cast<DWORD>(intervalMs_)))
                    continue;

                // User may have toggled off mid-sleep — re-check before step 2.
                if (!enabled_) continue;

                // Step 2: send the second dialog packet.
                SendDialogSelect(static_cast<uint32_t>(npcId_),
                                 static_cast<uint32_t>(dialogOpt2_));
                lastSendMs_ = GetTickCount();
                lastStep_   = 2;
                ++sentTotal_;

                // Sleep intervalMs_ before next iteration.
                if (!SleepInterruptible(static_cast<DWORD>(intervalMs_)))
                    continue;
            }
            else
            {
                // Idle — short sleep so toggle on / first-input is responsive.
                Sleep(100);
            }
        }
        spdlog::info("GGTB::AutoDelegation: worker thread exited");
    }

    // Returns false if stop_ flipped during the sleep (caller should bail).
    bool SleepInterruptible(DWORD totalMs)
    {
        constexpr DWORD slice = 50;
        for (DWORD slept = 0; slept < totalMs; slept += slice)
        {
            if (stop_.load()) return false;
            Sleep(slice);
        }
        return !stop_.load();
    }

    int npcId_      = 0;
    int dialogOpt1_ = 0;
    int dialogOpt2_ = 0;
    int intervalMs_ = 1000; // 默认 1 秒间隔

    DWORD         lastSendMs_ = 0;
    int           lastStep_   = 0;
    unsigned long sentTotal_  = 0;

    std::atomic<bool> stop_{false};
    std::thread       worker_;
};

} // namespace GGTB
