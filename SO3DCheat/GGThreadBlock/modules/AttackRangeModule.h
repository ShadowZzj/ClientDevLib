#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>

namespace GGTB
{

// Attack range (CUser+0x35AC, int "attackRangeTiles") + skill cast range
// (SkillTable+0x170, tiles).
//
// AttackRange (CUser field) — same shape as MoveSpeed:
//   - CalcStatus rewrites the field every frame in 3 sites (baseline -> weapon
//     clamp -> buff accumulate). NOP all 3 so the field can be poked freely.
//   - Combat__GetAttackRangeSquared then squares the field with a hard cap of
//     225 (=15^2). Raise the cap imm32 in both branches so values > 15 tiles
//     actually take effect.
//
// SkillRange (per-skill table field) — rewrites the SkillTable__GetSkillRange
// prologue (0x939890) to `mov eax, imm32; ret` (6B). That one patch covers
// the 13 callers: Net__SendSkillPacket's 9 distance gates, the 2 GetRangeSq
// wrappers, and sub_A89AD0 (walk animation). Updating the slider just
// rewrites the imm32.
//
// Per-frame re-poke: constructor / respawn init paths still fire occasionally
// (CUser ctor at 0x73F297 sets the field to 1), so we re-write the attack
// range field from OnRender every UI tick — same defensive pattern MoveSpeed
// uses. SkillRange patch is stable (function prologue isn't rewritten by
// per-frame paths) so it only needs a re-patch when the slider moves.
class AttackRangeModule : public IModule
{
  public:
    AttackRangeModule() : IModule(u8"攻击距离") {} // Attack range

    Tab GetTab() const override { return Tab::OP; }

    void OnRender() override
    {
        if (ImGui::Checkbox(u8"攻击距离##AttackRange", &enabled_))
        {
            if (enabled_)
            {
                // NPG held us down: keep user intent, let OnResume apply the
                // patch when the area clears. Applying it now would make a
                // nearby player see the cheat effect.
                if (IsPausedByGuard())
                {
                    spdlog::info("GGTB: AttackRange enable deferred (NPG paused)");
                }
                else
                {
                    bool ok = PatchAttackRange()
                              && PatchAttackRangeCap(kAttackRangeCapDefault)
                              && PatchSkillRange(static_cast<uint32_t>(skillTiles_));
                    if (!ok)
                    {
                        enabled_ = false;
                        RestoreAttackRange();
                        RestoreAttackRangeCap();
                        RestoreSkillRange();
                        spdlog::error("GGTB: AttackRange/SkillRange patch failed");
                    }
                    else
                    {
                        spdlog::info("GGTB: AttackRange={} tiles, SkillRange={} tiles",
                                     tiles_, skillTiles_);
                    }
                }
            }
            else
            {
                RestoreAttackRange();
                RestoreAttackRangeCap();
                RestoreSkillRange();
            }
        }

        if (enabled_)
        {
            if (IsPausedByGuard())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                   u8"已被 NPG 暂停 (附近有玩家)");
            }
            if (ImGui::SliderInt(u8"攻击距离(格)##AttackRange", &tiles_, 1, 15))
            {
                if (!IsPausedByGuard())
                {
                    if (auto *p = GetAttackRangePtr())
                        *p = tiles_;
                }
            }
            if (auto *p = GetAttackRangePtr())
            {
                if (!IsPausedByGuard())
                    *p = tiles_; // re-poke each frame in case ctor/respawn reset it
                ImGui::SameLine();
                ImGui::TextDisabled(u8"当前=%d", *p);
            }
            else
            {
                ImGui::SameLine();
                ImGui::TextDisabled(u8"(本地玩家未加载)");
            }

            if (ImGui::SliderInt(u8"技能距离(格)##SkillRange", &skillTiles_, 1, 15))
            {
                if (!IsPausedByGuard())
                    PatchSkillRange(static_cast<uint32_t>(skillTiles_));
            }
        }
    }

    void OnShutdown() override
    {
        if (enabled_)
        {
            RestoreAttackRange();
            RestoreAttackRangeCap();
            RestoreSkillRange();
        }
    }

    void OnResume() override
    {
        if (enabled_)
        {
            PatchAttackRange();
            PatchAttackRangeCap(kAttackRangeCapDefault);
            PatchSkillRange(static_cast<uint32_t>(skillTiles_));
            if (auto *p = GetAttackRangePtr())
                *p = tiles_;
        }
    }

    std::string ConfigKey() const override { return "AttackRange"; }
    void SaveState(nlohmann::json &j) const override
    {
        IModule::SaveState(j);
        j["tiles"]       = tiles_;
        j["skill_tiles"] = skillTiles_;
    }
    void LoadState(const nlohmann::json &j) override
    {
        tiles_      = j.value("tiles", tiles_);
        skillTiles_ = j.value("skill_tiles", skillTiles_);
        IModule::LoadState(j);
    }

  private:
    int tiles_      = 10;
    int skillTiles_ = 10;
};

} // namespace GGTB
