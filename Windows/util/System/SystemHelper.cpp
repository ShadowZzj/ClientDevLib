#include "SystemHelper.h"
#include <Windows.h>
#include <optional>
#include <sddl.h>
#include <wtsapi32.h>
#pragma comment(lib, "Wtsapi32.lib")

std::optional<zzj::SystemInfo::VersionInfo> zzj::SystemInfo::GetWindowsVersion()
{
    std::string vname;
    typedef void(__stdcall * NTPROC)(DWORD *, DWORD *, DWORD *);
    HINSTANCE hinst = LoadLibraryA("ntdll.dll");
    DWORD dwMajor, dwMinor, dwBuildNumber;
    NTPROC proc = (NTPROC)GetProcAddress(hinst, "RtlGetNtVersionNumbers");
    if (!proc)
        return {};
    proc(&dwMajor, &dwMinor, &dwBuildNumber);
    return VersionInfo{(int)dwMajor, (int)dwMinor, (int)dwBuildNumber};
}

std::optional<std::string> zzj::SystemInfo::GetComputerName()
{
    char szComputerName[1024];
    DWORD dwLen  = 1024;
    BOOL bStatus = GetComputerNameA(szComputerName, &dwLen);
    if (bStatus)
        return szComputerName;
    return {};
}
