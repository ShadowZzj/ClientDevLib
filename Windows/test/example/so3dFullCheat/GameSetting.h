#pragma once
#include <Windows/util/DirectX/D3D9Hook.h>
#include <spdlog/spdlog.h>
#include <json.hpp>
void InitLog(const std::string &name);
class GameSetting : public zzj::D3D::Setting
{
  public:
    virtual void Init() override;
    virtual void UninitImguiConfig() override;
    virtual void Render(bool &open);
    void LoadRoleConfig(const std::string& name);
    void SaveRoleConfig(const std::string &name);
    void MoveSpeedHandler();
    void AttackSpeed();
    virtual DWORD GetToggleMenuKey()
    {
        return VK_INSERT;
    }
    
    virtual void End() override;
    inline static std::mutex sellerGuarderMutex;
    nlohmann::json roleConfig;
};