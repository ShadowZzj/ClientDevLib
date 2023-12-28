#pragma once
#include <imgui/imgui.h>
#include <windows.h>
#include <string>
namespace zzj
{
namespace D3D
{
class Setting
{
  public:
    virtual void Render(bool& open)
    {
        ImGui::Begin("d3d9test", &open);
        ImGui::End();
    }
    virtual DWORD GetToggleMenuKey()
    {
        return VK_INSERT;
    }
};
} // namespace D3D
} // namespace zzj