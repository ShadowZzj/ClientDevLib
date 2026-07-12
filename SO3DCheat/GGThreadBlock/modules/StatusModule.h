#pragma once
#include "../IModule.h"
#include "../GGThreadBlock.h"
#include <imgui/imgui.h>

namespace GGTB
{

// Skeleton placeholder. Shows the live blocked-thread counter from the
// CreateThread detour. Replace / extend with real modules per target.
class StatusModule : public IModule
{
  public:
    StatusModule() : IModule(u8"状态") {}

    bool CanAutoPause() const override { return false; }
    bool RenderInHeader() const override { return true; }

    void OnRender() override
    {
        ImGui::Text(u8"已拦截 GameGuardDll.dll 线程: %ld", g_blockedThreadCount);
        ImGui::Separator();
        ImGui::TextDisabled(u8"骨架模块。具体扫描/功能按目标进程接入。");
    }
};

} // namespace GGTB
