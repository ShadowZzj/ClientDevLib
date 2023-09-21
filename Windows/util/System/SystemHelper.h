#pragma once
#include <optional>
#include <string>
#include <Windows.h>
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
    static bool IsWindowsVersionGreaterOrEqual(WORD wMajorVersion, WORD wMinorVersion, WORD wServicePackMajor);
    static bool IsWindowsVersionLessOrEqual(WORD wMajorVersion, WORD wMinorVersion, WORD wServicePackMajor);
    static bool IsWindowsXPOrGreater();
    static bool IsWindowsXPOr2k();
    static bool IsWindowsXPSP1OrGreater();
    static bool IsWindowsXPSP2OrGreater();
    static bool IsWindowsXPSP3OrGreater();
    static bool IsWindowsVistaOrGreater();
    static bool IsWindowsVistaSP1OrGreater();
    static bool IsWindowsVistaSP2OrGreater();
    static bool IsWindows7OrGreater();
    static bool IsWindows7SP1OrGreater();
    static bool IsWindows8OrGreater();
    static bool IsWindows8Point1OrGreater();
    static bool IsWindows10OrGreater();
    static bool IsWindowsServer();

    static std::optional<VersionInfo> GetWindowsVersion();
    static std::optional<std::string> GetComputerNameStr();
};

} // namespace zzj