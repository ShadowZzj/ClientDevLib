#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>

namespace GGTB
{

// Redirects the game's DIK_LSHIFT hook site to the predicate's original
// Shift-success block. It does not synthesize or hold any keyboard state.
class NoShiftAttackModule : public IModule
{
  public:
    NoShiftAttackModule() : IModule(u8"免 Shift") {}

    Tab  GetTab() const override { return Tab::PK; }
    bool CanAutoPause() const override { return false; }
    bool IsCompact() const override { return true; }

    void OnRender() override
    {
        if (ImGui::Checkbox(u8"免 Shift##NoShiftAttack", &enabled_))
        {
            if (enabled_)
            {
                if (!PatchNoShiftAttack())
                {
                    enabled_ = false;
                    RestoreNoShiftAttack();
                    spdlog::error("GGTB: NoShiftAttack patch failed");
                }
                else
                {
                    spdlog::info("GGTB: NoShiftAttack enabled");
                }
            }
            else
            {
                RestoreNoShiftAttack();
                spdlog::info("GGTB: NoShiftAttack disabled");
            }
        }

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(u8"勾选后直接点击玩家即可攻击，无需按住 Shift；不受 NPG 影响");
    }

    void OnShutdown() override
    {
        RestoreNoShiftAttack();
    }

    void OnResume() override
    {
        if (enabled_ && !PatchNoShiftAttack())
        {
            enabled_ = false;
            spdlog::error("GGTB: NoShiftAttack resume failed");
        }
    }

    std::string ConfigKey() const override { return "NoShiftAttack"; }
};

} // namespace GGTB
