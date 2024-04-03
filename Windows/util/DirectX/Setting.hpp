#pragma once
#include <imgui/imgui.h>
#include <string>
#include <windows.h>
namespace zzj
{
namespace D3D
{
class Setting
{
  public:
    virtual void InitImguiConfig()
    {
        ImGui::StyleColorsDark();
    }
    virtual void UninitImguiConfig()
    {
        
    }
    virtual void Render(bool &open)
    {
        ImGui::Begin("Demo imgui", &open);
        ImGui::Text("Hello, world!");
        ImGui::Button("Demo");
        ImGui::End();
    }
    virtual DWORD GetToggleMenuKey()
    {
        return VK_INSERT;
    }

    virtual void End()
    {
        return;
    }
};
} // namespace D3D
} // namespace zzj