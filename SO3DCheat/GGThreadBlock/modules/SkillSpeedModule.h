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
class SkillSpeedModule : public IModule
{
  public:
    SkillSpeedModule() : IModule(u8"技能速度") {} // 技能速度

    void OnRender() override
    {
        if (ImGui::Checkbox(u8"启用##SkillSpeed", &enabled_))
        {
            if (enabled_)
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
                    if (auto *p = GetSkillSpeedPtr()) *p = kSkillSpeedConst;
                    spdlog::info("GGTB: SkillSpeed enabled, skillSpeed={} preTime patched", kSkillSpeedConst);
                }
            }
            else
            {
                RestorePreTime();
                RestoreSkillSpeed();
            }
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
            if (auto *p = GetSkillSpeedPtr())
                *p = kSkillSpeedConst;
        }
    }

  private:
    // 用户要求一开就到底，不再暴露滑块。与 SO3DCheat::SkillModule 保持一致。
    static constexpr float kSkillSpeedConst = 0.0001f;
};

} // namespace GGTB
