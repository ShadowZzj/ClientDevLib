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

    void OnRender() override
    {
        if (ImGui::Checkbox(u8"启用##BlockLevelUp", &enabled_))
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

        if (enabled_)
        {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                               u8"已屏蔽 CG_LEVEL_UP_CHECK (412016)");
            ImGui::TextDisabled(
                u8"经验继续累积,但客户端不会请求升级。关闭即恢复。");
        }
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
