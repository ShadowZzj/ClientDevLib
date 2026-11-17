#pragma once

#include <winsock2.h>
#include <windows.h>
#include <string>

namespace zzj::ipc
{
// Self-relative security descriptor, owned with LocalFree. No inheritance.
class NamedPipeSecurity
{
  public:
    NamedPipeSecurity() = default;
    ~NamedPipeSecurity();
    NamedPipeSecurity(NamedPipeSecurity&& other) noexcept;
    NamedPipeSecurity& operator=(NamedPipeSecurity&& other) noexcept;
    NamedPipeSecurity(const NamedPipeSecurity&) = delete;
    NamedPipeSecurity& operator=(const NamedPipeSecurity&) = delete;

    static NamedPipeSecurity FromSddl(const std::wstring& sddl);
    static NamedPipeSecurity ForAdministrators();
    // Authenticated users can communicate, but cannot create server instances.
    // Clients must request explicit rights (0x0012019b), not GENERIC_WRITE,
    // which includes FILE_CREATE_PIPE_INSTANCE for named-pipe objects.
    static NamedPipeSecurity ForLocalUsers();
    SECURITY_ATTRIBUTES* Attributes() noexcept;
    PSECURITY_DESCRIPTOR Descriptor() const noexcept { return descriptor_; }
    DWORD Error() const noexcept { return error_; }
    explicit operator bool() const noexcept { return descriptor_ != nullptr; }

  private:
    PSECURITY_DESCRIPTOR descriptor_ = nullptr;
    SECURITY_ATTRIBUTES attributes_{};
    DWORD error_ = ERROR_SUCCESS;
};

DWORD ApplyNamedPipeSecurity(HANDLE pipe, const NamedPipeSecurity& security) noexcept;
// Call only after reading a client command; the token represents that last
// message. An administrator account with a UAC-filtered token is denied.
// Always returns to the original unimpersonated thread before returning.
bool IsNamedPipeClientAdministrator(HANDLE pipe, DWORD* error = nullptr) noexcept;
bool EnsureLocalClient(HANDLE pipe, DWORD* error = nullptr) noexcept;
// Returned handle is overlapped and caller-owned. A bare pipe name or an
// explicit \\.\pipe\name is accepted; remote pipe paths are rejected.
HANDLE OpenNamedPipeClient(const std::wstring& name, DWORD timeoutMilliseconds = 3000,
                           DWORD* error = nullptr) noexcept;
// Defense against an unprivileged process pre-creating the pipe endpoint.
bool IsNamedPipeServerPrivileged(HANDLE pipe, DWORD* error = nullptr) noexcept;
// Checks the current effective token, including UAC filtering.
bool IsEffectiveAdministrator(DWORD* error = nullptr) noexcept;
} // namespace zzj::ipc
