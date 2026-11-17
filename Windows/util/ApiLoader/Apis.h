#pragma once
#include "ApiTypeDefs.h"
#include <mutex>
#include <Windows/util/System/SystemHelper.h>
namespace zzj::ApiLoader
{
enum API_IDENTIFIER
{
    API_CsrGetProcessId,
    API_EnumSystemFirmwareTables,
    API_GetActiveProcessorCount,
    API_GetSystemFirmwareTable,
    API_GetNativeSystemInfo,
    API_GetProductInfo,
    API_EnumProcessModulesEx_Kernel,
    API_EnumProcessModulesEx_PSAPI,
    API_IsWow64Process,
    API_LdrEnumerateLoadedModules,
    API_NtClose,
    API_NtSystemDebugControl,
    API_NtCreateDebugObject,
    API_NtDelayExecution,
    API_NtOpenDirectoryObject,
    API_NtQueryInformationThread,
    API_NtQueryInformationProcess,
    API_NtQueryLicenseValue,
    API_NtQueryDirectoryObject,
    API_NtQueryObject,
    API_NtQuerySystemInformation,
    API_NtSetInformationThread,
    API_NtWow64QueryInformationProcess64,
    API_NtWow64QueryVirtualMemory64,
    API_NtWow64ReadVirtualMemory64,
    API_NtYieldExecution,
    API_RtlGetVersion,
    API_RtlInitUnicodeString,
    API_WudfIsAnyDebuggerPresent,
    API_WudfIsKernelDebuggerPresent,
    API_WudfIsUserDebuggerPresent,
};

enum API_OS_VERSION
{
    NONE,
    WIN_XP,
    WIN_XP_SP1,
    WIN_XP_SP2,
    WIN_XP_SP3,
    WIN_VISTA,
    WIN_VISTA_SP1,
    WIN_VISTA_SP2,
    WIN_7,
    WIN_7_SP1,
    WIN_80,
    WIN_81,
    WIN_10,
    VERSION_MAX
};

enum API_OS_BITS
{
    ANY,
    X86_ONLY,
    X64_ONLY,
};

struct VERSION_FUNCTION_MAP
{
    API_OS_VERSION Version;
    bool (*Function)();

    VERSION_FUNCTION_MAP(API_OS_VERSION version, bool (*function)())
    {
        Version  = version;
        Function = function;
    }

    VERSION_FUNCTION_MAP()
    {
    }
};

struct API_DATA
{
    API_IDENTIFIER Identifier;
    const char *Library;
    const char *EntryName;
    API_OS_BITS PlatformBits;
    API_OS_VERSION MinVersion;
    API_OS_VERSION RemovedInVersion;
    bool Available;
    bool ExpectedAvailable;
    void *Pointer;

    API_DATA(API_IDENTIFIER identifier, const char *lib, const char *name, API_OS_BITS bits, API_OS_VERSION minVersion,
             API_OS_VERSION removedInVersion)
    {
        Identifier        = identifier;
        Library           = lib;
        EntryName         = name;
        PlatformBits      = bits;
        MinVersion        = minVersion;
        RemovedInVersion  = removedInVersion;
        Available         = false;
        ExpectedAvailable = false;
        Pointer           = nullptr;
    }
};

const VERSION_FUNCTION_MAP VersionFunctionMap[] = {
    {API_OS_VERSION::NONE, nullptr},
    {API_OS_VERSION::WIN_XP, zzj::SystemInfo::IsWindowsXPOrGreater},
    {API_OS_VERSION::WIN_XP_SP1, zzj::SystemInfo::IsWindowsXPSP1OrGreater},
    {API_OS_VERSION::WIN_XP_SP2, zzj::SystemInfo::IsWindowsXPSP2OrGreater},
    {API_OS_VERSION::WIN_XP_SP3, zzj::SystemInfo::IsWindowsXPSP3OrGreater},
    {API_OS_VERSION::WIN_VISTA, zzj::SystemInfo::IsWindowsVistaOrGreater},
    {API_OS_VERSION::WIN_VISTA_SP1, zzj::SystemInfo::IsWindowsVistaSP1OrGreater},
    {API_OS_VERSION::WIN_VISTA_SP2, zzj::SystemInfo::IsWindowsVistaSP2OrGreater},
    {API_OS_VERSION::WIN_7, zzj::SystemInfo::IsWindows7OrGreater},
    {API_OS_VERSION::WIN_7_SP1, zzj::SystemInfo::IsWindows7SP1OrGreater},
    {API_OS_VERSION::WIN_80, zzj::SystemInfo::IsWindows8OrGreater},
    {API_OS_VERSION::WIN_81, zzj::SystemInfo::IsWindows8Point1OrGreater},
    {API_OS_VERSION::WIN_10, zzj::SystemInfo::IsWindows10OrGreater},
};

class API
{
  public:
    void Init();
    void PrintAvailabilityReport();
    bool IsAvailable(API_IDENTIFIER api);
    void *GetAPI(API_IDENTIFIER api);
    static API *GetInstance();

  private:
    bool ShouldFunctionExistOnCurrentPlatform(API_OS_BITS bits, API_OS_VERSION minVersion,
                                              API_OS_VERSION removedInVersion);
    API()                       = default;
    inline static API *instance = nullptr;
    static inline std::mutex m_mutex;
};
}; // namespace zzj::ApiLoader