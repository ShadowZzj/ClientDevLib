#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>

namespace GGTB
{

// skillSpeed (+0x2E20) is the animation divisor PlayAnim funcs use to scale
// skill cast time — lower = faster. SkillTable.preTime (+0x174 in this build)
// is *also* still read by Skill__GetPreTime as the cast startup delay; without
// the PreTime patch the startup pause stays full-length even with skillSpeed=0,
// so we apply both here. Same enable/disable lifecycle for both.
//
// Sliders span 0.001~1.0 with log scale: defaults sit at the fast end (matches
// the prior hardcoded 0.0001-ish behavior; clamped to the slider min for UI
// sanity). Slide toward 1.0 to dial back toward natural cast speed if the
// instant-cast burst trips server validation.
class SkillSpeedModule : public IModule
{
  public:
    SkillSpeedModule() : IModule(u8"技能速度") {}

    Tab GetTab() const override { return Tab::OP; }

    void OnRender() override
    {
        if (ImGui::Checkbox(u8"技能速度##SkillSpeed", &enabled_))
        {
            if (enabled_)
            {
                if (IsPausedByGuard())
                {
                    spdlog::info("GGTB: SkillSpeed enable deferred (NPG paused)");
                }
                else
                {
                    bool ok = PatchSkillSpeed() && PatchPreTime();
                    if (!ok)
                    {
                        enabled_ = false;
                        RestorePreTime();
                        RestoreSkillSpeed();
                        spdlog::error("GGTB: SkillSpeed/PreTime patch failed");
                    }
                    else
                    {
                        ApplyLive();
                        spdlog::info("GGTB: SkillSpeed enabled, skillSpeed={} preTime={}",
                                     skillSpeed_, preTime_);
                    }
                }
            }
            else
            {
                RestorePreTime();
                RestoreSkillSpeed();
            }
        }

        // Sliders are always interactive — write-through is harmless when the
        // patches aren't active (engine just overwrites +0x2E20 next frame, and
        // s_preTimeConst isn't read by anything when the movss isn't patched).
        constexpr ImGuiSliderFlags kFlags = ImGuiSliderFlags_Logarithmic;
        if (ImGui::SliderFloat(u8"人物 skillSpeed##SkillSpeed",
                               &skillSpeed_, kSliderMin, kSliderMax, "%.4f", kFlags))
        {
            if (enabled_ && !IsPausedByGuard())
                if (auto *p = GetSkillSpeedPtr()) *p = skillSpeed_;
        }
        if (ImGui::SliderFloat(u8"PreTime##SkillSpeed",
                               &preTime_, kSliderMin, kSliderMax, "%.4f", kFlags))
        {
            SetPreTimeConst(preTime_);
        }

        if (enabled_ && IsPausedByGuard())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               u8"已被 NPG 暂停 (附近有玩家)");
        }
    }

    void OnShutdown() override
    {
        if (enabled_)
        {
            RestorePreTime();
            RestoreSkillSpeed();
        }
    }

    void OnResume() override
    {
        if (enabled_)
        {
            PatchSkillSpeed();
            PatchPreTime();
            ApplyLive();
        }
    }

    std::string ConfigKey() const override { return "SkillSpeed"; }

    void SaveState(nlohmann::json &j) const override
    {
        IModule::SaveState(j);
        j["skillSpeed"] = skillSpeed_;
        j["preTime"]    = preTime_;
    }

    void LoadState(const nlohmann::json &j) override
    {
        skillSpeed_ = j.value("skillSpeed", skillSpeed_);
        preTime_    = j.value("preTime",    preTime_);
        // Clamp restored values into the slider band so a torn config can't
        // resurrect a value the slider can't represent.
        if (skillSpeed_ < kSliderMin) skillSpeed_ = kSliderMin;
        if (skillSpeed_ > kSliderMax) skillSpeed_ = kSliderMax;
        if (preTime_    < kSliderMin) preTime_    = kSliderMin;
        if (preTime_    > kSliderMax) preTime_    = kSliderMax;
        SetPreTimeConst(preTime_);
        IModule::LoadState(j);
    }

  private:
    void ApplyLive()
    {
        if (auto *p = GetSkillSpeedPtr()) *p = skillSpeed_;
        SetPreTimeConst(preTime_);
    }

    static constexpr float kSliderMin = 0.001f;
    static constexpr float kSliderMax = 1.0f;

    // 滑块默认贴近 min,延续旧版 0.0001 常量的"瞬发"手感。
    float skillSpeed_ = 0.001f;
    float preTime_    = 0.001f;
};

} // namespace GGTB
