#include "CommonApi.h"
#include <Windows/util/ApiLoader/Apis.h>
#include <Windows/util/Common.h>
#include <spdlog/spdlog.h>
#include <windows.h>
#include <winternl.h>
#define STATUS_PORT_NOT_SET ((NTSTATUS)0xC0000353L)

namespace zzj::AntiDebug
{
using namespace zzj::ApiLoader;
BOOL NtQueryInformationProcessCheck()
{
    auto apiLoader = API::GetInstance();
    auto NtQueryInfoProcess =
        static_cast<pNtQueryInformationProcess>(apiLoader->GetAPI(API_IDENTIFIER::API_NtQueryInformationProcess));
    if (NtQueryInfoProcess == nullptr)
    {
        spdlog::error("Failed to get NtQueryInformationProcess");
        return FALSE;
    }
    // ProcessDebugPort
    const int ProcessDbgPort = 7;

    // Other Vars
    NTSTATUS Status;
    DWORD NoDebugInherit = 0;
#if defined(ENV64BIT)
    DWORD dProcessInformationLength = sizeof(ULONG) * 2;
    DWORD64 IsRemotePresent         = 0;

#elif defined(ENV32BIT)
    DWORD dProcessInformationLength = sizeof(ULONG);
    DWORD32 IsRemotePresent         = 0;
#endif

    Status = NtQueryInfoProcess(GetCurrentProcess(), ProcessDbgPort, &IsRemotePresent, dProcessInformationLength, NULL);
    if (Status == 0x00000000 && IsRemotePresent != 0)
    {
        spdlog::info("NTQIP1");
        return TRUE;
    }

    // ProcessDebugFlags
    const int ProcessDebugFlags = 0x1f;
    Status = NtQueryInfoProcess(GetCurrentProcess(), ProcessDebugFlags, &NoDebugInherit, sizeof(DWORD), NULL);
    if (Status == 0x00000000 && NoDebugInherit == 0)
    {
        spdlog::info("NTQIP2");
        return TRUE;
    }

    // ProcessDebugObjectHandle
    HANDLE hDebugObject                = NULL;
    const int ProcessDebugObjectHandle = 0x1e;
    Status = NtQueryInfoProcess(GetCurrentProcess(), ProcessDebugObjectHandle, &hDebugObject, dProcessInformationLength,
                                NULL);

    if (Status != STATUS_PORT_NOT_SET)
    {
        spdlog::info("NTQIP3");
        return TRUE;
    }

    // Check with overlapping return length and debug object handle buffers to find anti-anti-debuggers
    Status = NtQueryInfoProcess(GetCurrentProcess(), ProcessDebugObjectHandle, &hDebugObject, dProcessInformationLength,
                                (PULONG)&hDebugObject);
    if (Status != STATUS_PORT_NOT_SET)
    {
        spdlog::info("NTQIP4");
        return TRUE;
    }
    if (hDebugObject == NULL)
    {
        spdlog::info("NTQIP5");
        return TRUE; // Handle incorrectly zeroed
    }
    if ((ULONG)(ULONG_PTR)hDebugObject != dProcessInformationLength)
    {
        spdlog::info("NTQIP6");
        return TRUE; // Return length incorrectly overwritten
    }

    return FALSE;
}
BOOL IsDebuggerPresentPEB(VOID)
/*++

Routine Description:

    Checks if the BeingDebugged flag is set in the Process Environment Block (PEB).
    This is effectively the same code that IsDebuggerPresent() executes internally.
    The PEB pointer is fetched from DWORD FS:[0x30] on x86_32 and QWORD GS:[0x60] on x86_64.

Arguments:

    None

Return Value:

    TRUE - if debugger was detected
    FALSE - otherwise
--*/
{
#if defined(ENV64BIT)
    PPEB pPeb = (PPEB)__readgsqword(0x60);

#elif defined(ENV32BIT)
    PPEB pPeb = (PPEB)__readfsdword(0x30);

#endif

    return pPeb->BeingDebugged == 1;
}
BOOL NtCloseCheck()
{
    // Let's try first with user mode API: CloseHandle
    __try
    {
        CloseHandle(reinterpret_cast<HANDLE>(0x99999999ULL));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return TRUE;
    }

    auto apiLoader = API::GetInstance();
    // Direct call to NtClose to bypass user mode hooks
    auto NtClose_ = static_cast<pNtClose>(apiLoader->GetAPI(API_IDENTIFIER::API_NtClose));
    if (NtClose_ == nullptr)
    {
        spdlog::error("Failed to get NtClose");
        return FALSE;
    }
    __try
    {
        NtClose_(reinterpret_cast<HANDLE>(0x99999999ULL));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return TRUE;
    }

    return FALSE;
}
int CommonAntiDebugCheck()
{
    BOOL result = IsDebuggerPresent();
    if (result)
    {
        return 1;
    }

    result = IsDebuggerPresentPEB();
    if (result)
    {
        return 2;
    }

    CheckRemoteDebuggerPresent(GetCurrentProcess(), &result);
    if (result)
    {
        return 3;
    }

    result = NtQueryInformationProcessCheck();
    if (result)
    {
        return 4;
    }

    result = NtCloseCheck();
    if (result)
    {
        return 5;
    }

    return 0;
}
}; // namespace zzj::AntiDebug