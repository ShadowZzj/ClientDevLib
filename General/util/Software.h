#pragma once
#include <string>
#include <vector>
#include "LuaExport.hpp"
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

class SoftInfoManagerInterface
{
  public:
    virtual std::tuple <int, std::vector<SoftInfo>> GetInstalledSoftware() = 0;
    SoftInfoManagerInterface()
    {
    }
    ~SoftInfoManagerInterface()
    {
    }
    DECLARE_LUA_EXPORT(SoftInfoManagerInterface)
};
}; // namespace zzj