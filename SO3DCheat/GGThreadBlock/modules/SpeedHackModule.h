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

    void OnRender() override
    {
        if (ImGui::Checkbox(u8"启用##SpeedHack", &enabled_))
        {
            if (enabled_)
            {
                SpeedHack::Setup();
                SpeedHack::SetSpeed(speed_);
            }
            else
            {
                SpeedHack::Detach();
            }
        }

        if (enabled_)
        {
            if (ImGui::SliderFloat(u8"倍率##SpeedHack", &speed_, 0.1f, 10.0f, "%.1fx"))
                SpeedHack::SetSpeed(speed_);
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

  private:
    float speed_ = 1.0f;
};

} // namespace GGTB
