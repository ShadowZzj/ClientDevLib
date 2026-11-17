#include "NamedPipeSecurity.h"

#include <aclapi.h>
#include <sddl.h>
#include <exception>
#include <algorithm>
#include <utility>
#include <vector>

namespace zzj::ipc
{
NamedPipeSecurity::~NamedPipeSecurity()
{
    if (descriptor_)
        LocalFree(descriptor_);
}
NamedPipeSecurity::NamedPipeSecurity(NamedPipeSecurity&& other) noexcept
    : descriptor_(std::exchange(other.descriptor_, nullptr)), error_(other.error_)
{
}
NamedPipeSecurity& NamedPipeSecurity::operator=(NamedPipeSecurity&& other) noexcept
{
    if (this != &other)
    {
        if (descriptor_)
            LocalFree(descriptor_);
        descriptor_ = std::exchange(other.descriptor_, nullptr);
        error_ = other.error_;
    }
    return *this;
}
NamedPipeSecurity NamedPipeSecurity::FromSddl(const std::wstring& sddl)
{
    NamedPipeSecurity result;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                              &result.descriptor_, nullptr))
        result.error_ = GetLastError();
    return result;
}
NamedPipeSecurity NamedPipeSecurity::ForAdministrators()
{
    return FromSddl(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
}
NamedPipeSecurity NamedPipeSecurity::ForLocalUsers()
{
    return FromSddl(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x0012019b;;;AU)");
}
SECURITY_ATTRIBUTES* NamedPipeSecurity::Attributes() noexcept
{
    attributes_ = {sizeof(SECURITY_ATTRIBUTES), descriptor_, FALSE};
    return descriptor_ ? &attributes_ : nullptr;
}

DWORD ApplyNamedPipeSecurity(HANDLE pipe, const NamedPipeSecurity& security) noexcept
{
    if (!security)
        return security.Error() ? security.Error() : ERROR_INVALID_SECURITY_DESCR;
    BOOL present = FALSE, defaulted = FALSE;
    PACL dacl = nullptr;
    if (!GetSecurityDescriptorDacl(security.Descriptor(), &present, &dacl, &defaulted))
        return GetLastError();
    if (!present || !dacl)
        return ERROR_INVALID_SECURITY_DESCR; // null DACL grants universal access
    return SetSecurityInfo(pipe, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                           nullptr, nullptr, dacl, nullptr);
}

bool EnsureLocalClient(HANDLE pipe, DWORD* error) noexcept
{
    ULONG processId = 0;
    const BOOL success = GetNamedPipeClientProcessId(pipe, &processId);
    DWORD result = success && processId != 0 ? ERROR_SUCCESS : (success ? ERROR_ACCESS_DENIED : GetLastError());
    if (result == ERROR_SUCCESS)
    {
        wchar_t computerName[256]{};
        // NPFS returns ERROR_PIPE_LOCAL for a local client. A remote client
        // can have a process ID too, so a readable PID alone is insufficient.
        if (GetNamedPipeClientComputerNameW(pipe, computerName, 256))
            result = ERROR_ACCESS_DENIED;
        else if (GetLastError() != ERROR_PIPE_LOCAL)
            result = GetLastError();
    }
    if (error)
        *error = result;
    return result == ERROR_SUCCESS;
}

bool IsNamedPipeClientAdministrator(HANDLE pipe, DWORD* error) noexcept
{
    const auto fail = [error](DWORD result) { if (error) *error = result; return false; };
    if (error)
        *error = ERROR_SUCCESS;
    HANDLE existingToken = nullptr;
    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &existingToken))
    {
        CloseHandle(existingToken);
        return fail(ERROR_ALREADY_ASSIGNED);
    }
    if (GetLastError() != ERROR_NO_TOKEN)
        return fail(GetLastError());
    if (!ImpersonateNamedPipeClient(pipe))
        return fail(GetLastError());

    DWORD result = ERROR_SUCCESS;
    BOOL member = FALSE;
    HANDLE token = nullptr;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token))
        result = GetLastError();
    else
    {
        BYTE sid[SECURITY_MAX_SID_SIZE];
        DWORD sidSize = sizeof(sid);
        if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, sid, &sidSize) ||
            !CheckTokenMembership(token, sid, &member))
            result = GetLastError();
        CloseHandle(token);
    }
    // Returning while still impersonating changes the authority of later
    // service operations. Microsoft's failure guidance requires termination.
    if (!RevertToSelf())
        std::terminate();
    if (result != ERROR_SUCCESS)
        return fail(result);
    if (!member)
        return fail(ERROR_ACCESS_DENIED);
    return true;
}

HANDLE OpenNamedPipeClient(const std::wstring& name, DWORD timeoutMilliseconds, DWORD* error) noexcept
{
    const auto fail = [error](DWORD result) { if (error) *error = result; return INVALID_HANDLE_VALUE; };
    if (error)
        *error = ERROR_SUCCESS;
    const std::wstring prefix = L"\\\\.\\pipe\\";
    std::wstring path;
    if (name.compare(0, prefix.size(), prefix) == 0)
        path = name;
    else if (!name.empty() && name.find_first_of(L"\\/:\0", 0, 4) == std::wstring::npos)
        path = prefix + name;
    else
        return fail(ERROR_INVALID_NAME);
    if (path.size() == prefix.size() || path.find(L'\0') != std::wstring::npos ||
        path.find_first_of(L"\\/", prefix.size()) != std::wstring::npos)
        return fail(ERROR_INVALID_NAME);
    const ULONGLONG started = GetTickCount64();
    while (true)
    {
        HANDLE pipe = CreateFileW(path.c_str(), 0x0012019b, 0, nullptr, OPEN_EXISTING,
                                   FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe != INVALID_HANDLE_VALUE)
            return pipe;
        const DWORD openError = GetLastError();
        if (openError != ERROR_PIPE_BUSY && openError != ERROR_FILE_NOT_FOUND)
            return fail(openError);
        const ULONGLONG elapsed = GetTickCount64() - started;
        if (elapsed >= timeoutMilliseconds)
            return fail(ERROR_SEM_TIMEOUT);
        const DWORD remaining = timeoutMilliseconds - static_cast<DWORD>(elapsed);
        if (openError == ERROR_PIPE_BUSY)
        {
            if (!WaitNamedPipeW(path.c_str(), remaining))
            {
                const DWORD waitError = GetLastError();
                if (waitError != ERROR_FILE_NOT_FOUND)
                    return fail(waitError);
            }
        }
        else
            Sleep((std::min)(remaining, static_cast<DWORD>(25)));
    }
}

bool IsNamedPipeServerPrivileged(HANDLE pipe, DWORD* error) noexcept
{
    const auto fail = [error](DWORD result) { if (error) *error = result; return false; };
    if (error)
        *error = ERROR_SUCCESS;
    ULONG processId = 0;
    if (!GetNamedPipeServerProcessId(pipe, &processId) || processId == 0)
        return fail(GetLastError());
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process)
        return fail(GetLastError());
    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token))
    {
        const DWORD result = GetLastError();
        CloseHandle(process);
        return fail(result);
    }
    CloseHandle(process);
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> userData(size);
    if (size == 0 || !GetTokenInformation(token, TokenUser, userData.data(), size, &size))
    {
        const DWORD result = GetLastError();
        CloseHandle(token);
        return fail(result);
    }
    BYTE systemSid[SECURITY_MAX_SID_SIZE], adminSid[SECURITY_MAX_SID_SIZE];
    DWORD systemSize = sizeof(systemSid), adminSize = sizeof(adminSid);
    if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, systemSid, &systemSize) ||
        !CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, adminSid, &adminSize))
    {
        const DWORD result = GetLastError();
        CloseHandle(token);
        return fail(result);
    }
    bool privileged = EqualSid(reinterpret_cast<TOKEN_USER*>(userData.data())->User.Sid, systemSid) != FALSE;
    if (!privileged)
    {
        size = 0;
        GetTokenInformation(token, TokenGroups, nullptr, 0, &size);
        std::vector<BYTE> groupData(size);
        if (size == 0 || !GetTokenInformation(token, TokenGroups, groupData.data(), size, &size))
        {
            const DWORD result = GetLastError();
            CloseHandle(token);
            return fail(result);
        }
        const auto* groups = reinterpret_cast<TOKEN_GROUPS*>(groupData.data());
        for (DWORD i = 0; i < groups->GroupCount; ++i)
            if ((groups->Groups[i].Attributes & SE_GROUP_ENABLED) &&
                !(groups->Groups[i].Attributes & SE_GROUP_USE_FOR_DENY_ONLY) &&
                EqualSid(groups->Groups[i].Sid, adminSid))
                privileged = true;
    }
    CloseHandle(token);
    return privileged ? true : fail(ERROR_ACCESS_DENIED);
}

bool IsEffectiveAdministrator(DWORD* error) noexcept
{
    if (error) *error = ERROR_SUCCESS;
    BYTE sid[SECURITY_MAX_SID_SIZE];
    DWORD sidSize = sizeof(sid);
    BOOL member = FALSE;
    if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, sid, &sidSize) ||
        !CheckTokenMembership(nullptr, sid, &member))
    {
        if (error) *error = GetLastError();
        return false;
    }
    if (!member && error) *error = ERROR_ACCESS_DENIED;
    return member != FALSE;
}
} // namespace zzj::ipc
