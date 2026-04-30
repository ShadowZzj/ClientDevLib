#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>

namespace GGTB
{

class MoveSpeedModule : public IModule
{
  public:
    MoveSpeedModule() : IModule(u8"移动速度") {}

    void OnRender() override
    {
        if (ImGui::Checkbox(u8"启用##MoveSpeed", &enabled_))
        {
            if (enabled_)
            {
                if (!PatchMoveSpeed())
                {
                    enabled_ = false;
                    spdlog::error("GGTB: MoveSpeed patch failed");
                }
                else
                {
                    spdlog::info("GGTB: MoveSpeed enabled, value={:.1f}", speed_);
                }
            }
            else
            {
                RestoreMoveSpeed();
            }
        }

        if (enabled_)
        {
            if (ImGui::SliderFloat(u8"速度##MoveSpeed", &speed_, 1.0f, 14.0f, "%.1f"))
            {
                if (auto *p = GetMoveSpeedPtr())
                    *p = speed_;
            }
            if (auto *p = GetMoveSpeedPtr())
            {
                ImGui::SameLine();
                ImGui::TextDisabled(u8"当前=%.2f", *p);
            }
            else
            {
                ImGui::SameLine();
                ImGui::TextDisabled(u8"(本地玩家未加载)");
            }
        }
    }

    void OnShutdown() override
    {
        if (enabled_)
            RestoreMoveSpeed();
    }

    void OnResume() override
    {
        if (enabled_)
        {
            PatchMoveSpeed();
            // Re-apply slider value: while paused, the game's CalcStatus
            // overwrote [user+0x1A4] back to the base speed.
            if (auto *p = GetMoveSpeedPtr())
                *p = speed_;
        }
    }

  private:
    float speed_ = 7.0f;
};

} // namespace GGTB
