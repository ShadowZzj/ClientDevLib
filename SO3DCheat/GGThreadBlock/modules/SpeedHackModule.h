#pragma once
#include "../IModule.h"
#include "../util/SpeedHack.h"
#include <imgui/imgui.h>

namespace GGTB
{

class SpeedHackModule : public IModule
{
  public:
    SpeedHackModule() : IModule(u8"加速") {}

    Tab GetTab() const override { return Tab::OP; }

    void OnRender() override
    {
        if (ImGui::Checkbox(u8"加速##SpeedHack", &enabled_))
        {
            if (enabled_)
            {
                if (IsPausedByGuard())
                {
                    spdlog::info("GGTB: SpeedHack enable deferred (NPG paused)");
                }
                else
                {
                    SpeedHack::Setup();
                    SpeedHack::SetSpeed(speed_);
                }
            }
            else
            {
                SpeedHack::Detach();
            }
        }

        if (enabled_)
        {
            if (IsPausedByGuard())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                   u8"已被 NPG 暂停 (附近有玩家)");
            }
            if (ImGui::SliderFloat(u8"倍率##SpeedHack", &speed_, 0.1f, 10.0f, "%.1fx"))
            {
                if (!IsPausedByGuard())
                    SpeedHack::SetSpeed(speed_);
            }
        }
    }

    void OnShutdown() override
    {
        if (enabled_)
            SpeedHack::Detach();
    }

    void OnResume() override
    {
        if (enabled_)
        {
            SpeedHack::Setup();
            SpeedHack::SetSpeed(speed_);
        }
    }

    std::string ConfigKey() const override { return "SpeedHack"; }
    void SaveState(nlohmann::json &j) const override
    {
        IModule::SaveState(j);
        j["speed"] = speed_;
    }
    void LoadState(const nlohmann::json &j) override
    {
        speed_ = j.value("speed", speed_);
        IModule::LoadState(j);
    }

  private:
    float speed_ = 1.0f;
};

} // namespace GGTB
