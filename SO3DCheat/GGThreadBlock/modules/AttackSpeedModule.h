#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>

namespace GGTB
{

// attackSpeed is an animation divisor: lower = faster normal attacks.
// SO3DCheat's reference UI uses 0.0 ~ 1.0 with default 0.5.
class AttackSpeedModule : public IModule
{
  public:
    AttackSpeedModule() : IModule(u8"攻击速度") {} // 攻击速度

    Tab GetTab() const override { return Tab::OP; }

    void OnRender() override
    {
        if (ImGui::Checkbox(u8"攻击速度##AttackSpeed", &enabled_))
        {
            if (enabled_)
            {
                if (IsPausedByGuard())
                {
                    spdlog::info("GGTB: AttackSpeed enable deferred (NPG paused)");
                }
                else if (!PatchAttackSpeed())
                {
                    enabled_ = false;
                    spdlog::error("GGTB: AttackSpeed patch failed");
                }
                else
                {
                    spdlog::info("GGTB: AttackSpeed enabled, value={:.3f}", speed_);
                }
            }
            else
            {
                RestoreAttackSpeed();
            }
        }

        if (enabled_)
        {
            if (IsPausedByGuard())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                   u8"已被 NPG 暂停 (附近有玩家)");
            }
            if (ImGui::SliderFloat(u8"倍率##AttackSpeed", &speed_, 0.0f, 1.0f, "%.3f"))
            {
                if (!IsPausedByGuard())
                {
                    if (auto *p = GetAttackSpeedPtr())
                        *p = speed_;
                }
            }
            if (auto *p = GetAttackSpeedPtr())
            {
                ImGui::SameLine();
                ImGui::TextDisabled(u8"当前=%.3f", *p);
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
            RestoreAttackSpeed();
    }

    void OnResume() override
    {
        if (enabled_)
        {
            PatchAttackSpeed();
            if (auto *p = GetAttackSpeedPtr())
                *p = speed_;
        }
    }

    std::string ConfigKey() const override { return "AttackSpeed"; }
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
    float speed_ = 0.5f;
};

} // namespace GGTB
