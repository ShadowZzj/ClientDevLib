#pragma once
#include <Windows/util/DirectX/D3D9Hook.h>
#include <spdlog/spdlog.h>
#include <memory>
#include <vector>
#include "IModule.h"

class CheatSetting : public zzj::D3D::Setting
{
  public:
    void RegisterModule(std::shared_ptr<IModule> mod);

    virtual void Init() override;
    virtual void Render(bool &open) override;
    virtual void End() override;
    virtual DWORD GetToggleMenuKey() override { return VK_INSERT; }

    const std::vector<std::shared_ptr<IModule>> &GetModules() const { return modules_; }

  private:
    std::vector<std::shared_ptr<IModule>> modules_;
};
