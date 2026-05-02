#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>

namespace GGTB
{

// Action-time movement bypass — removes the "can't move while attacking /
// casting" lock. See CLocalPlayer.{h,cpp} for the 4-site patch rationale.
//
// Pure on/off toggle: no slider, no per-frame re-poke (the patches are static
// byte rewrites, not field writes). NPG can pause/resume via OnShutdown/OnResume.
class ActionMoveModule : public IModule
{
  public:
    ActionMoveModule() : IModule(u8"攻击/技能时移动") {}

    void OnRender() override
    {
        if (ImGui::Checkbox(u8"启用##ActionMove", &enabled_))
        {
            if (enabled_)
            {
                if (!PatchActionMove())
                {
                    enabled_ = false;
                    RestoreActionMove();
                    spdlog::error("GGTB: ActionMove patch failed");
                }
                else
                {
                    spdlog::info("GGTB: ActionMove enabled");
                }
            }
            else
            {
                RestoreActionMove();
            }
        }
    }

    void OnShutdown() override
    {
        if (enabled_)
            RestoreActionMove();
    }

    void OnResume() override
    {
        if (enabled_)
            PatchActionMove();
    }

    std::string ConfigKey() const override { return "ActionMove"; }
};

} // namespace GGTB
