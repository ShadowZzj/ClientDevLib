#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>

namespace GGTB
{

// 技能移动 — move while a skill is active. Patches only the SetAfterAction
// skill-mode reject path (loc_753A54, reached when SkillMode 370C∈{3,5,8}) plus
// the shared (refcounted) TraceMove/MoveSync execution gates. Does NOT skip the
// cast windup, so a skill's natural per-cast animation lock still throttles rapid
// auto-combo (123.dll) — skipping it spiked combo rate with no frame gate. Fully
// independent of AttackMove: with only this toggle on, a normal attack still
// rejects at gates 3-7 and cannot move. See CLocalPlayer.{h,cpp} for the rationale.
//
// Pure on/off toggle: no slider, no per-frame re-poke (static byte rewrites).
// NPG can pause/resume via OnShutdown/OnResume.
class SkillMoveModule : public IModule
{
  public:
    SkillMoveModule() : IModule(u8"技能移动") {}

    bool IsCompact() const override { return true; }

    void OnRender() override
    {
        if (ImGui::Checkbox(u8"技能移动##SkillMove", &enabled_))
        {
            if (enabled_)
            {
                if (IsPausedByGuard())
                {
                    spdlog::info("GGTB: SkillMove enable deferred (NPG paused)");
                }
                else if (!PatchSkillMove())
                {
                    enabled_ = false;
                    RestoreSkillMove();
                    spdlog::error("GGTB: SkillMove patch failed");
                }
                else
                {
                    spdlog::info("GGTB: SkillMove enabled");
                }
            }
            else
            {
                RestoreSkillMove();
            }
        }

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(IsPausedByGuard()
                                  ? u8"已被 NPG 暂停 (附近有玩家)"
                                  : u8"施放技能时也能移动");
    }

    void OnShutdown() override
    {
        if (enabled_)
            RestoreSkillMove();
    }

    void OnResume() override
    {
        if (enabled_)
            PatchSkillMove();
    }

    std::string ConfigKey() const override { return "SkillMove"; }
};

} // namespace GGTB
