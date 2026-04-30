#pragma once
#include <Windows/util/DirectX/D3D9Hook.h>
#include <memory>
#include <vector>
#include "IModule.h"

namespace GGTB
{

class Setting : public zzj::D3D::Setting
{
  public:
    void RegisterModule(std::shared_ptr<IModule> mod);

    void Init() override;
    void Render(bool &open) override;
    void End() override;
    DWORD GetToggleMenuKey() override { return VK_INSERT; }

    const std::vector<std::shared_ptr<IModule>> &GetModules() const { return modules_; }

  private:
    std::vector<std::shared_ptr<IModule>> modules_;
};

} // namespace GGTB
