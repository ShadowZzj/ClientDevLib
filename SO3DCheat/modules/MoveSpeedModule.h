#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>

class MoveSpeedModule : public IModule
{
  public:
    MoveSpeedModule() : IModule("MoveSpeed") {}

    void OnRender() override
    {
        if (ImGui::Checkbox("Enable##MoveSpeed", &enabled_))
        {
            if (enabled_)
            {
                SO3D::PatchMoveSpeed();
                spdlog::info("MoveSpeed enabled, value={:.1f}", speed_);
            }
            else
            {
                SO3D::RestoreMoveSpeed();
                spdlog::info("MoveSpeed disabled");
            }
        }

        if (enabled_)
        {
            if (ImGui::SliderFloat("Speed##MoveSpeed", &speed_, 1.0f, 14.0f, "%.1f"))
            {
                auto *lp = SO3D::GetLocalPlayer();
                if (lp) lp->moveSpeed = speed_;
            }
        }
    }

    void OnShutdown() override
    {
        if (enabled_)
            SO3D::RestoreMoveSpeed();
    }

    void OnResume() override
    {
        if (enabled_)
            SO3D::PatchMoveSpeed();
    }

  private:
    float speed_ = 7.0f;
};
