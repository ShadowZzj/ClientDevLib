#pragma once
#include <string>
#include <optional>
namespace zzj
{
class SystemInfo
{
  public:
    class VersionInfo
    {
      public:
        int major;
        int minor;
        int buildnumber;
    };
    static std::optional<VersionInfo> GetWindowsVersion();

};

} // namespace zzj