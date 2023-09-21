#include "SystemHelper.h"
#include "WinApiFamily.h"
#include <Windows.h>
#include <optional>
#include <sddl.h>
#include <wtsapi32.h>

#pragma comment(lib, "Wtsapi32.lib")

bool zzj::SystemInfo::IsWindowsVersionGreaterOrEqual(WORD wMajorVersion, WORD wMinorVersion, WORD wServicePackMajor)
{
    OSVERSIONINFOEXW osvi = {sizeof(osvi), 0, 0, 0, 0, {0}, 0, 0};
    DWORDLONG const dwlConditionMask =
        VerSetConditionMask(VerSetConditionMask(VerSetConditionMask(0, VER_MAJORVERSION, VER_GREATER_EQUAL),
                                                VER_MINORVERSION, VER_GREATER_EQUAL),
                            VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL);

    osvi.dwMajorVersion    = wMajorVersion;
    osvi.dwMinorVersion    = wMinorVersion;
    osvi.wServicePackMajor = wServicePackMajor;

    return VerifyVersionInfoW(&osvi, VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR, dwlConditionMask) !=
           FALSE;
}
bool zzj::SystemInfo::IsWindowsVersionLessOrEqual(WORD wMajorVersion, WORD wMinorVersion, WORD wServicePackMajor)
{
    OSVERSIONINFOEXW osvi = {sizeof(osvi), 0, 0, 0, 0, {0}, 0, 0};
    DWORDLONG const dwlConditionMask =
        VerSetConditionMask(VerSetConditionMask(0, VER_MAJORVERSION, VER_EQUAL), VER_MINORVERSION, VER_LESS_EQUAL);

    osvi.dwMajorVersion    = wMajorVersion;
    osvi.dwMinorVersion    = wMinorVersion;
    osvi.wServicePackMajor = wServicePackMajor;

    return VerifyVersionInfoW(&osvi, VER_MAJORVERSION | VER_MINORVERSION, dwlConditionMask) != FALSE;
}
bool zzj::SystemInfo::IsWindowsXPOrGreater()
{
    return IsWindowsVersionGreaterOrEqual(HIBYTE(_WIN32_WINNT_WINXP), LOBYTE(_WIN32_WINNT_WINXP), 0);
}
bool zzj::SystemInfo::IsWindowsXPOr2k()
{
    return IsWindowsVersionLessOrEqual(HIBYTE(_WIN32_WINNT_WINXP), LOBYTE(_WIN32_WINNT_WINXP), 0);
}
bool zzj::SystemInfo::IsWindowsXPSP1OrGreater()
{
    return IsWindowsVersionGreaterOrEqual(HIBYTE(_WIN32_WINNT_WINXP), LOBYTE(_WIN32_WINNT_WINXP), 1);
}
bool zzj::SystemInfo::IsWindowsXPSP2OrGreater()
{
    return IsWindowsVersionGreaterOrEqual(HIBYTE(_WIN32_WINNT_WINXP), LOBYTE(_WIN32_WINNT_WINXP), 2);
}
bool zzj::SystemInfo::IsWindowsXPSP3OrGreater()
{
    return IsWindowsVersionGreaterOrEqual(HIBYTE(_WIN32_WINNT_WINXP), LOBYTE(_WIN32_WINNT_WINXP), 3);
}
bool zzj::SystemInfo::IsWindowsVistaOrGreater()
{
    return IsWindowsVersionGreaterOrEqual(HIBYTE(_WIN32_WINNT_VISTA), LOBYTE(_WIN32_WINNT_VISTA), 0);
}
bool zzj::SystemInfo::IsWindowsVistaSP1OrGreater()
{
    return IsWindowsVersionGreaterOrEqual(HIBYTE(_WIN32_WINNT_VISTA), LOBYTE(_WIN32_WINNT_VISTA), 1);
}
bool zzj::SystemInfo::IsWindowsVistaSP2OrGreater()
{
    return IsWindowsVersionGreaterOrEqual(HIBYTE(_WIN32_WINNT_VISTA), LOBYTE(_WIN32_WINNT_VISTA), 2);
}
bool zzj::SystemInfo::IsWindows7OrGreater()
{
    return IsWindowsVersionGreaterOrEqual(HIBYTE(_WIN32_WINNT_WIN7), LOBYTE(_WIN32_WINNT_WIN7), 0);
}
bool zzj::SystemInfo::IsWindows7SP1OrGreater()
{
    return IsWindowsVersionGreaterOrEqual(HIBYTE(_WIN32_WINNT_WIN7), LOBYTE(_WIN32_WINNT_WIN7), 1);
}
bool zzj::SystemInfo::IsWindows8OrGreater()
{
    return IsWindowsVersionGreaterOrEqual(HIBYTE(_WIN32_WINNT_WIN8), LOBYTE(_WIN32_WINNT_WIN8), 0);
}
bool zzj::SystemInfo::IsWindows8Point1OrGreater()
{
    return IsWindowsVersionGreaterOrEqual(HIBYTE(_WIN32_WINNT_WINBLUE), LOBYTE(_WIN32_WINNT_WINBLUE), 0);
}
bool zzj::SystemInfo::IsWindows10OrGreater()
{
    return IsWindowsVersionGreaterOrEqual(HIBYTE(_WIN32_WINNT_WIN10), LOBYTE(_WIN32_WINNT_WIN10), 0);
}
bool zzj::SystemInfo::IsWindowsServer()
{
    OSVERSIONINFOEXW osvi            = {sizeof(osvi), 0, 0, 0, 0, {0}, 0, 0, 0, VER_NT_WORKSTATION};
    DWORDLONG const dwlConditionMask = VerSetConditionMask(0, VER_PRODUCT_TYPE, VER_EQUAL);

    return !VerifyVersionInfoW(&osvi, VER_PRODUCT_TYPE, dwlConditionMask);
}
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

std::optional<std::string> zzj::SystemInfo::GetComputerNameStr()
{
    char szComputerName[1024];
    DWORD dwLen  = 1024;
    BOOL bStatus = GetComputerNameA(szComputerName, &dwLen);
    if (bStatus)
        return szComputerName;
    return {};
}
