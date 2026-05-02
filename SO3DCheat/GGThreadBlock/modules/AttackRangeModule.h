#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>

namespace GGTB
{

// Attack range (CUser+0x35AC, int "attackRangeTiles").
//
// Same shape as MoveSpeed:
//   - CalcStatus rewrites the field every frame in 3 sites (baseline -> weapon
//     clamp -> buff accumulate). NOP all 3 so the field can be poked freely.
//   - Combat__GetAttackRangeSquared then squares the field with a hard cap of
//     225 (=15^2). Raise the cap imm32 in both branches so values > 15 tiles
//     actually take effect.
//
// Per-frame re-poke: constructor / respawn init paths still fire occasionally
// (CUser ctor at 0x73F297 sets the field to 1), so we re-write it from
// OnRender every UI tick — same defensive pattern MoveSpeed uses.
class AttackRangeModule : public IModule
{
  public:
    AttackRangeModule() : IModule(u8"攻击距离") {} // Attack range

    void OnRender() override
    {
        if (ImGui::Checkbox(u8"启用##AttackRange", &enabled_))
        {
            if (enabled_)
            {
                if (!PatchAttackRange() || !PatchAttackRangeCap(kAttackRangeCapDefault))
                {
                    enabled_ = false;
                    RestoreAttackRange();
                    RestoreAttackRangeCap();
                    spdlog::error("GGTB: AttackRange patch failed");
                }
                else
                {
                    spdlog::info("GGTB: AttackRange enabled, value={} tiles", tiles_);
                }
            }
            else
            {
                RestoreAttackRange();
                RestoreAttackRangeCap();
            }
        }

        if (enabled_)
        {
            if (ImGui::SliderInt(u8"距离(格)##AttackRange", &tiles_, 1, 30))
            {
                if (auto *p = GetAttackRangePtr())
                    *p = tiles_;
            }
            if (auto *p = GetAttackRangePtr())
            {
                *p = tiles_; // re-poke each frame in case ctor/respawn reset it
                ImGui::SameLine();
                ImGui::TextDisabled(u8"当前=%d", *p);
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
        {
            RestoreAttackRange();
            RestoreAttackRangeCap();
        }
    }

    void OnResume() override
    {
        if (enabled_)
        {
            PatchAttackRange();
            PatchAttackRangeCap(kAttackRangeCapDefault);
            if (auto *p = GetAttackRangePtr())
                *p = tiles_;
        }
    }

    std::string ConfigKey() const override { return "AttackRange"; }
    void SaveState(nlohmann::json &j) const override
    {
        IModule::SaveState(j);
        j["tiles"] = tiles_;
    }
    void LoadState(const nlohmann::json &j) override
    {
        tiles_ = j.value("tiles", tiles_);
        IModule::LoadState(j);
    }

  private:
    int tiles_ = 10;
};

} // namespace GGTB
