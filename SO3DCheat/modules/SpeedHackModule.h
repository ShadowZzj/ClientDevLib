#pragma once
#include "../IModule.h"
#include "../util/SpeedHack.h"
#include <imgui/imgui.h>

class SpeedHackModule : public IModule
{
  public:
    SpeedHackModule() : IModule("SpeedHack") {}

    void OnRender() override
    {
        if (ImGui::Checkbox("Enable##SpeedHack", &enabled_))
        {
            if (enabled_)
            {
                SO3D::SpeedHack::Setup();
                SO3D::SpeedHack::SetSpeed(speed_);
            }
            else
            {
                SO3D::SpeedHack::Detach();
            }
        }

        if (enabled_)
        {
            if (ImGui::SliderFloat("Speed##SH", &speed_, 0.1f, 10.0f, "%.1fx"))
                SO3D::SpeedHack::SetSpeed(speed_);
        }
    }

    void OnShutdown() override
    {
        if (enabled_)
            SO3D::SpeedHack::Detach();
    }

    void OnResume() override
    {
        if (enabled_)
        {
            SO3D::SpeedHack::Setup();
            SO3D::SpeedHack::SetSpeed(speed_);
        }
    }

  private:
    float speed_ = 1.0f;
};
