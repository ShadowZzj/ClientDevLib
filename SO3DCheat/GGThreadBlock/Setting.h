#pragma once
#include <Windows/util/DirectX/D3D9Hook.h>
#include <atomic>
#include <json.hpp>
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
    DWORD GetToggleMenuKey() const { return VK_OEM_4; } // '['

    const std::vector<std::shared_ptr<IModule>> &GetModules() const { return modules_; }

    // True after Init() has run (the external menu thread calls Init during setup).
    // UserConfig::Tick gates on this — modules' OnInit must run before LoadState.
    bool IsInitialized() const { return initialized_.load(); }

  private:
    std::vector<std::shared_ptr<IModule>> modules_;
    std::atomic<bool>                     initialized_{false};
    nlohmann::json                        lastSnapshot_;
};

} // namespace GGTB
