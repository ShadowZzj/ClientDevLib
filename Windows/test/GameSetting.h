#pragma once
#include <Windows/util/DirectX/D3D9Hook.h>
#include <spdlog/spdlog.h>
class GameSetting : public zzj::D3D::Setting
{
  public:
    virtual void InitImguiConfig() override;
    virtual void UninitImguiConfig() override;
    virtual void Render(bool &open);
    virtual DWORD GetToggleMenuKey()
    {
        return VK_INSERT;
    }

    virtual void End() override;
};