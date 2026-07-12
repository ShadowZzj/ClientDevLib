#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include "MultiCastModule.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <atomic>
#include <mutex>
#include <thread>

namespace GGTB
{

// Independent of FireFullPower and NPG: cast MakeBomb(325) on its own CD, wait
// 1s for the item to materialize, then drop every bomb stack in the bag.
// Intentionally NOT persisted — must be re-enabled each session.
class MakeBombDropModule : public IModule
{
  public:
    MakeBombDropModule() : IModule(u8"做原子弹")
    {
        worker_ = std::thread([this] { WorkerLoop(); });
    }

    ~MakeBombDropModule() override
    {
        stop_.store(true);
        if (worker_.joinable())
            worker_.join();
    }

    bool CanAutoPause() const override { return false; }
    Tab  GetTab() const override { return Tab::OP; }

    void OnRender() override
    {
        ImGui::Checkbox(u8"做原子弹##MakeBombDrop", &enabled_);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f),
                           u8"(不保存,不受NPG限制)");

        if (enabled_)
        {
            size_t dropped      = 0;
            DWORD  lastCycleAge = 0;
            {
                std::lock_guard<std::mutex> lk(mutex_);
                dropped      = lastDropCount_;
                lastCycleAge = lastCycleMs_ ? (GetTickCount() - lastCycleMs_) : 0;
            }
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.4f, 1.0f),
                               u8"运行中: 上轮丢弃 %d 组, 距上轮 %lums",
                               static_cast<int>(dropped),
                               static_cast<unsigned long>(lastCycleAge));
        }
    }

  private:
    void WorkerLoop()
    {
        while (!stop_.load())
        {
            if (enabled_)
                Cycle();

            for (int i = 0; i < 10 && !stop_.load(); ++i)
                Sleep(10);
        }
    }

    void Cycle()
    {
        // Throttle: only attempt once per ~2s to avoid hammering when CD isn't
        // ready yet.
        DWORD now = GetTickCount();
        if (now - lastAttemptMs_ < 2000)
            return;
        lastAttemptMs_ = now;

        auto skills  = GetLearnedSkills();
        bool cdReady = false;
        for (auto &info : skills)
        {
            if (info.skill.skillId == kSkillIdMakeBomb)
            {
                cdReady = (info.skill.leftCoolDown <= 0.0f);
                break;
            }
        }
        if (!cdReady)
            return;

        MultiCastModule::LockCast();
        MultiCastModule::SetThreadMakeBomb(true);
        CastMakeBomb(false);
        MultiCastModule::SetThreadMakeBomb(false);
        MultiCastModule::UnlockCast();
        spdlog::info("GGTB::MakeBombDrop: cast MakeBomb, waiting 1s before drop");

        for (int i = 0; i < 10 && !stop_.load() && enabled_; ++i)
            Sleep(100);

        if (!enabled_ || stop_.load())
            return;

        int dropped = DropAllBombsInBag();
        spdlog::info("GGTB::MakeBombDrop: dropped {} stacks", dropped);

        {
            std::lock_guard<std::mutex> lk(mutex_);
            lastDropCount_ = static_cast<size_t>(dropped);
            lastCycleMs_   = GetTickCount();
        }
    }

    DWORD  lastAttemptMs_ = 0;
    DWORD  lastCycleMs_   = 0;
    size_t lastDropCount_ = 0;

    std::mutex        mutex_;
    std::atomic<bool> stop_{false};
    std::thread       worker_;
};

} // namespace GGTB
