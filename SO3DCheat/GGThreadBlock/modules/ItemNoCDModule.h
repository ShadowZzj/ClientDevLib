#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>

namespace GGTB
{

// Item short-cd toggle.
//
// Rewrites 11 bytes inside SkillCdMgr__StartCdForCategory (linear 0x756E98) so
// every item-use arms the per-category cd timer with a small fixed duration
// (slider, default 0.3s) instead of the table-lookup value. The cd MECHANISM
// stays intact — InventoryItem__Use's `GetCooldownRatio > 0` gate still blocks
// re-use within the window — but the wait shrinks from many seconds to ~0.3s.
//
// Why this is preferable to the old "force ratio = 0" patch:
//   - Server may still throttle/reject rapid CG_ITEM_USE (411015), so a tiny
//     real client-side cd reduces desync risk vs. zero cd.
//   - Some downstream code paths in InventoryItem__Use expect a cd to actually
//     elapse; bypassing it entirely caused subtle issues per user feedback.
class ItemNoCDModule : public IModule
{
  public:
    ItemNoCDModule() : IModule(u8"物品短CD") {} // Item short-cd

    void OnRender() override
    {
        if (ImGui::Checkbox(u8"启用##ItemShortCD", &enabled_))
        {
            if (enabled_)
            {
                if (!PatchItemShortCD(cdSeconds_))
                {
                    enabled_ = false;
                    spdlog::error("GGTB: ItemShortCD patch failed");
                }
                else
                {
                    spdlog::info("GGTB: ItemShortCD enabled @ {:.3f}s", cdSeconds_);
                }
            }
            else
            {
                RestoreItemShortCD();
            }
        }

        if (ImGui::SliderFloat(u8"CD 秒数##ItemShortCD", &cdSeconds_, 0.05f, 2.0f, "%.2fs"))
        {
            if (enabled_)
                PatchItemShortCD(cdSeconds_);
        }

        if (enabled_)
            ImGui::TextDisabled(u8"快捷栏物品冷却已缩短 (服务器可能仍限制)");
    }

    void OnShutdown() override
    {
        if (enabled_)
            RestoreItemShortCD();
    }

    void OnResume() override
    {
        if (enabled_)
            PatchItemShortCD(cdSeconds_);
    }

  private:
    float cdSeconds_ = 0.3f;
};

} // namespace GGTB
