#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>

namespace GGTB
{

// 无视控制 — 眩晕(状态 18)/沉默(状态 19) bypass。打开后即使中了眩晕/沉默也能
// 施法和吃药/用道具。靠 CLocalPlayer 里的 4 处分支翻转(item gate + 技能键的
// stun/silence/stunTime 三门),见 CLocalPlayer.{h,cpp}。服务端仍校验,所以这是
// 客户端不再自我拦截、把请求照常发出去。
//
// 纯 on/off:静态字节补丁,没有滑块、不用每帧 re-poke。NPG 可经 OnShutdown/
// OnResume 暂停/恢复 —— 附近有真人时撤掉补丁,避免在人前无视控制被看出来。
class IgnoreCCModule : public IModule
{
  public:
    IgnoreCCModule() : IModule(u8"无视眩晕/沉默") {}

    Tab  GetTab() const override { return Tab::OP; }
    bool IsCompact() const override { return true; }

    void OnRender() override
    {
        if (ImGui::Checkbox(u8"无视眩晕/沉默##IgnoreCC", &enabled_))
        {
            if (enabled_)
            {
                if (IsPausedByGuard())
                {
                    spdlog::info("GGTB: IgnoreCC enable deferred (NPG paused)");
                }
                else if (!PatchIgnoreCC())
                {
                    enabled_ = false;
                    RestoreIgnoreCC();
                    spdlog::error("GGTB: IgnoreCC patch failed");
                }
                else
                {
                    spdlog::info("GGTB: IgnoreCC enabled");
                }
            }
            else
            {
                RestoreIgnoreCC();
            }
        }

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(IsPausedByGuard()
                                  ? u8"已被 NPG 暂停 (附近有玩家)"
                                  : u8"勾选后中眩晕/沉默仍可施法 + 吃药 (服务端仍校验)");
    }

    void OnShutdown() override
    {
        if (enabled_)
            RestoreIgnoreCC();
    }

    void OnResume() override
    {
        if (enabled_)
            PatchIgnoreCC();
    }

    std::string ConfigKey() const override { return "IgnoreCC"; }
};

} // namespace GGTB
