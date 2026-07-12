#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>

namespace GGTB
{

// 普攻移动 — move during normal attack / combo. Patches only the SetAfterAction
// non-skill reject path (loc_753A80, reached when SkillMode 370C∉{3,5,8}) plus
// the shared (refcounted) TraceMove/MoveSync execution gates. Fully independent
// of SkillMove: with only this toggle on, skills still reject at gate 2 and
// cannot move. See CLocalPlayer.{h,cpp} for the rationale.
//
// Pure on/off toggle: no slider, no per-frame re-poke (static byte rewrites).
// NPG can pause/resume via OnShutdown/OnResume.
class AttackMoveModule : public IModule
{
  public:
    AttackMoveModule() : IModule(u8"普攻移动") {}

    bool IsCompact() const override { return true; }

    void OnRender() override
    {
        if (ImGui::Checkbox(u8"普攻移动##AttackMove", &enabled_))
        {
            if (enabled_)
            {
                if (IsPausedByGuard())
                {
                    spdlog::info("GGTB: AttackMove enable deferred (NPG paused)");
                }
                else if (!PatchAttackMove())
                {
                    enabled_ = false;
                    RestoreAttackMove();
                    spdlog::error("GGTB: AttackMove patch failed");
                }
                else
                {
                    spdlog::info("GGTB: AttackMove enabled");
                }
            }
            else
            {
                RestoreAttackMove();
            }
        }

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(IsPausedByGuard()
                                  ? u8"已被 NPG 暂停 (附近有玩家)"
                                  : u8"普通攻击/连击时也能移动");
    }

    void OnShutdown() override
    {
        if (enabled_)
            RestoreAttackMove();
    }

    void OnResume() override
    {
        if (enabled_)
            PatchAttackMove();
    }

    std::string ConfigKey() const override { return "AttackMove"; }
};

} // namespace GGTB
