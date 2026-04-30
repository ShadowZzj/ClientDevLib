#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>

class AttackSpeedModule : public IModule
{
  public:
    AttackSpeedModule() : IModule("AttackSpeed") {}

    void OnRender() override
    {
        if (ImGui::Checkbox("Enable##AttackSpeed", &enabled_))
        {
            if (enabled_)
            {
                SO3D::PatchAttackSpeed();
                spdlog::info("AttackSpeed enabled, value={:.3f}", speed_);
            }
            else
            {
                SO3D::RestoreAttackSpeed();
                spdlog::info("AttackSpeed disabled");
            }
        }

        if (enabled_)
        {
            if (ImGui::SliderFloat("Speed##AttackSpeed", &speed_, 0.0f, 1.0f, "%.3f"))
            {
                auto *lp = SO3D::GetLocalPlayer();
                if (lp) lp->attackSpeed = speed_;
            }
        }
    }

    void OnShutdown() override
    {
        if (enabled_)
            SO3D::RestoreAttackSpeed();
    }

    void OnResume() override
    {
        if (enabled_)
            SO3D::PatchAttackSpeed();
    }

  private:
    float speed_ = 0.5f;
};
