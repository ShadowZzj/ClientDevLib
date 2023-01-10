#include <Windows.h>
#include "SystemHelper.h"
#include <wtsapi32.h>
#include <sddl.h>
#include <optional>
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
