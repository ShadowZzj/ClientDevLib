#pragma once
#include "../IModule.h"
#include "../CheatSetting.h"
#include "../entity/CLocalPlayer.h"
#include "../entity/CUser.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <string>
#include <vector>

class AutoSwitchModule : public IModule
{
  public:
    AutoSwitchModule(CheatSetting *setting) : IModule("AutoSwitch"), setting_(setting) {}

    void OnRender() override
    {
        ImGui::Checkbox("Enable##AutoSwitch", &enabled_);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Detect nearby stranger players and pause all hacks");

        auto now = std::chrono::steady_clock::now();
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCheck_).count();

        if (elapsed >= pollIntervalMs_)
        {
            lastCheck_ = now;
            auto *lp   = SO3D::GetLocalPlayer();
            if (lp)
            {
                std::string myName = lp->GetName();
                aroundPlayers_     = SO3D::GetAroundPlayers(myName);
            }
            else
            {
                aroundPlayers_.clear();
            }
        }

        ImGui::Text("Around Players: %d", static_cast<int>(aroundPlayers_.size()));

        for (auto &p : aroundPlayers_)
            ImGui::BulletText("%s  dist=%.1f", p.name.c_str(), p.dist);

        if (enabled_)
        {
            bool strangersNearby = !aroundPlayers_.empty();

            if (strangersNearby && !paused_)
            {
                spdlog::warn("AutoSwitch: stranger detected, pausing hacks");
                PauseAll();
                paused_ = true;
            }
            else if (!strangersNearby && paused_)
            {
                spdlog::info("AutoSwitch: no strangers, resuming hacks");
                ResumeAll();
                paused_ = false;
            }
        }
    }

    void OnShutdown() override
    {
        if (paused_)
        {
            ResumeAll();
            paused_ = false;
        }
    }

  private:
    void PauseAll()
    {
        if (!setting_)
            return;
        savedStates_.clear();
        for (auto &mod : setting_->GetModules())
        {
            if (mod.get() == this || !mod->CanAutoPause())
                continue;
            savedStates_.push_back({mod->GetName(), mod->IsEnabled()});
            if (mod->IsEnabled())
            {
                mod->OnShutdown();
                mod->SetEnabled(false);
            }
        }
    }

    void ResumeAll()
    {
        if (!setting_)
            return;
        for (auto &saved : savedStates_)
        {
            if (!saved.wasEnabled)
                continue;
            for (auto &mod : setting_->GetModules())
            {
                if (mod->GetName() == saved.name)
                {
                    mod->SetEnabled(true);
                    mod->OnResume();
                    break;
                }
            }
        }
        savedStates_.clear();
    }

    struct SavedState
    {
        std::string name;
        bool wasEnabled;
    };

    CheatSetting *setting_ = nullptr;
    bool paused_            = false;
    int pollIntervalMs_     = 1000;

    std::chrono::steady_clock::time_point lastCheck_{};
    std::vector<SO3D::AroundPlayer> aroundPlayers_;
    std::vector<SavedState> savedStates_;
};
