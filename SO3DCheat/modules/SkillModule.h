#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include "../entity/CSkill.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>

class SkillModule : public IModule
{
  public:
    SkillModule() : IModule("SkillSpeed") {}

    void OnRender() override
    {
        if (ImGui::Checkbox("Enable##SkillSpeed", &enabled_))
        {
            if (enabled_)
            {
                SO3D::PatchSkillSpeed();
                SO3D::PatchPreTime();
                auto *lp = SO3D::GetLocalPlayer();
                if (lp) lp->skillSpeed = kSkillSpeedConst;
                spdlog::info("SkillSpeed ON: skillSpeed={} preTime patched", kSkillSpeedConst);
            }
            else
            {
                SO3D::RestoreSkillSpeed();
                SO3D::RestorePreTime();
                spdlog::info("SkillSpeed OFF");
            }
        }

        if (ImGui::CollapsingHeader("Skill List"))
        {
            uint32_t count = SO3D::GetSkillCount();
            for (uint32_t i = 0; i < count; i++)
            {
                auto *sk = SO3D::GetCSkill(i);
                if (!sk || !sk->isValid) continue;

                auto *st = sk->skillTable;
                const char *name = st ? st->skillName : "???";
                float pt = st ? st->preTime : 0.0f;
                ImGui::Text("[%u] id=%u lv=%u cd=%.1f pt=%.3f %s",
                    i, sk->skillId, sk->skillLevel, sk->leftCoolDown, pt, name);
            }
        }
    }

    void OnShutdown() override
    {
        if (enabled_)
        {
            SO3D::RestoreSkillSpeed();
            SO3D::RestorePreTime();
        }
    }

    void OnResume() override
    {
        if (enabled_)
        {
            SO3D::PatchSkillSpeed();
            SO3D::PatchPreTime();
            auto *lp = SO3D::GetLocalPlayer();
            if (lp) lp->skillSpeed = kSkillSpeedConst;
        }
    }

  private:
    // skillSpeed 是动画分母（值越小动画越快），preTime 单独由 PatchPreTime 顶为 0.0001。
    // 用户要求一开就到底，不再暴露滑块。
    static constexpr float kSkillSpeedConst = 0.0001f;
};
