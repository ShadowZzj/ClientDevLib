#pragma once
#include <General/util/Lua/LuaExport.hpp>
#include <string>
#include <vector>

namespace zzj
{
class SoftInfo
{
  public:
    // The software name
    std::string m_strSoftName;
    // Software version number
    std::string m_strSoftVersion;
    // Software installation directory
    std::string m_strInstallLocation;
    // Software vendor
    std::string m_strPublisher;
    // The full path of the main program
    std::string m_strMainProPath;
    // Uninstall exe on the full path
    std::string m_strUninstallPth;

    DECLARE_LUA_EXPORT(SoftInfo)
};

class SoftInfoManager
{
  public:
    std::tuple<int, std::vector<SoftInfo>> GetInstalledSoftware();
    SoftInfoManager()
    {
    }
    ~SoftInfoManager()
    {
    }
    DECLARE_LUA_EXPORT(SoftInfoManager)
};
}; // namespace zzj