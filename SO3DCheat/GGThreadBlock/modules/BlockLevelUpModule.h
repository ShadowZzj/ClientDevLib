#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>

namespace GGTB
{

// 禁止升级 — flips a single jge in CLocalPlayer::UpdateExp so the
// CG_LEVEL_UP_CHECK request packet is never sent. Character pins at current
// level while EXP keeps accumulating server-side. Re-enabled by restoring
// the original byte (RestoreBlockLevelUp).
//
// CanAutoPause = false: the patch has no client-visible side effect to a
// nearby player (no animation, no visual, no broadcast — just a missing
// outbound packet on our own socket), so NPG has no reason to gate it. Same
// rationale as StatusModule / AutoMailModule.
class BlockLevelUpModule : public IModule
{
  public:
    BlockLevelUpModule() : IModule(u8"禁止升级") {}

    bool CanAutoPause() const override { return false; }
    bool IsCompact() const override { return true; }

    void OnRender() override
    {
        if (ImGui::Checkbox(u8"禁止升级##BlockLevelUp", &enabled_))
        {
            if (enabled_)
            {
                if (!PatchBlockLevelUp())
                {
                    enabled_ = false;
                    spdlog::error("GGTB: BlockLevelUp patch failed");
                }
                else
                {
                    spdlog::info("GGTB: BlockLevelUp enabled");
                }
            }
            else
            {
                RestoreBlockLevelUp();
            }
        }

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(
                enabled_
                    ? u8"已屏蔽 CG_LEVEL_UP_CHECK (412016) — 经验继续累积,客户端不请求升级"
                    : u8"勾选后客户端不再请求升级,经验继续累积,关闭即恢复");
    }

    void OnShutdown() override
    {
        if (enabled_)
            RestoreBlockLevelUp();
    }

    void OnResume() override
    {
        if (enabled_)
            PatchBlockLevelUp();
    }

    std::string ConfigKey() const override { return "BlockLevelUp"; }
};

} // namespace GGTB
