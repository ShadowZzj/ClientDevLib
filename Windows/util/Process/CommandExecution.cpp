#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <General/util/Process/CommandExecutionInternal.h>
#include <General/util/Crypto/Base64.hpp>
#include "ProcessHelper.h"
#include <UserEnv.h>
#include <sddl.h>
#include <array>
#include <thread>
#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "advapi32.lib")

namespace zzj
{
namespace
{
struct Handle
{
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE value) : value(value) {}
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    ~Handle() { Close(); }
    void Close() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); value = nullptr; }
};
[[noreturn]] void Fail(const char *operation)
{
    throw CommandExecutionError("UNAVAILABLE", std::string(operation) + " failed (Win32 " + std::to_string(GetLastError()) + ")");
}
std::wstring Wide(const std::string &s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n) throw CommandExecutionError("INVALID_ARGUMENT", "Command text must be UTF-8");
    std::wstring r(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), r.data(), n);
    return r;
}
std::string Utf8(const std::wstring &s)
{
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string r(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), r.data(), n, nullptr, nullptr);
    return r;
}
std::wstring Quote(const std::wstring &arg)
{
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const auto c : arg)
    {
        if (c == L'\\') { ++slashes; continue; }
        result.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        if (c == L'"') result += L'\\';
        result += c; slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
std::string SystemPath(const wchar_t *suffix)
{
    std::array<wchar_t, MAX_PATH + 1> dir{};
    if (!GetSystemDirectoryW(dir.data(), static_cast<UINT>(dir.size()))) Fail("GetSystemDirectory");
    return Utf8(std::wstring(dir.data()) + suffix);
}
bool Elevated(HANDLE token)
{
    TOKEN_ELEVATION elevation{}; DWORD size = 0;
    if (!GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) Fail("TokenElevation");
    return elevation.TokenIsElevated != 0;
}
CommandEnvironment TokenInfo(HANDLE token, CommandIdentity identity)
{
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<char> user(size);
    if (!GetTokenInformation(token, TokenUser, user.data(), size, &size)) Fail("TokenUser");
    wchar_t *sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid, &sid)) Fail("Token SID");
    const auto name = Utf8(sid); LocalFree(sid);
    DWORD session = 0;
    if (!GetTokenInformation(token, TokenSessionId, &session, sizeof(session), &size)) Fail("TokenSessionId");
    return {command_detail::Name(identity), name, std::to_string(session), "", true, Elevated(token)};
}
void GetToken(CommandIdentity identity, Handle &token)
{
    if (identity == CommandIdentity::Service)
    {
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &token.value)) Fail("OpenProcessToken");
        return;
    }
    // Reuse the library's active desktop/explorer discovery and UAC token model.
    ActiveExplorerInfo active{};
    if (!Process::GetActiveExplorerInfo(&active))
        throw CommandExecutionError("FAILED_PRECONDITION", "No accessible active desktop user");
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, active.ProcessId)), original, linked;
    if (!process.value || !OpenProcessToken(process.value, TOKEN_QUERY | TOKEN_DUPLICATE, &original.value)) Fail("Open desktop token");
    HANDLE selected = original.value;
    const bool admin = identity == CommandIdentity::CurrentUserAdmin;
    if (Elevated(selected) != admin)
    {
        TOKEN_LINKED_TOKEN info{}; DWORD size = 0;
        if (GetTokenInformation(selected, TokenLinkedToken, &info, sizeof(info), &size))
        {
            linked.value = info.LinkedToken;
            selected = linked.value;
        }
        else if (admin) throw CommandExecutionError("PERMISSION_DENIED", "Active user has no elevated token");
    }
    if (admin && !Elevated(selected))
        throw CommandExecutionError("PERMISSION_DENIED", "Active user is not an administrator");
    if (!DuplicateTokenEx(selected, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation, TokenPrimary, &token.value))
        Fail("DuplicateTokenEx");
}
std::wstring Home(HANDLE token)
{
    DWORD size = 0; GetUserProfileDirectoryW(token, nullptr, &size);
    if (!size) Fail("GetUserProfileDirectory");
    std::vector<wchar_t> buffer(size);
    if (!GetUserProfileDirectoryW(token, buffer.data(), &size)) Fail("GetUserProfileDirectory");
    return buffer.data();
}
struct Environment
{
    void *value = nullptr;
    ~Environment() { if (value) DestroyEnvironmentBlock(value); }
};
struct Attributes
{
    std::vector<char> memory;
    LPPROC_THREAD_ATTRIBUTE_LIST value = nullptr;
    ~Attributes() { if (value) DeleteProcThreadAttributeList(value); }
};
void Drain(HANDLE pipe, std::string &output, size_t limit, bool &truncated)
{
    // A per-tick budget prevents a noisy child from starving cancellation/timeout checks.
    for (int i = 0; i < 16; ++i)
    {
        DWORD available = 0, count = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || !available) return;
        char buffer[4096];
        if (!ReadFile(pipe, buffer, std::min<DWORD>(available, sizeof(buffer)), &count, nullptr) || !count) return;
        command_detail::Append(output, buffer, count, limit, truncated);
    }
}
}

std::string CommandHelper::PowerShellPath()
{
    const auto path = SystemPath(L"\\WindowsPowerShell\\v1.0\\powershell.exe");
    return std::filesystem::is_regular_file(std::filesystem::u8path(path)) ? path : "";
}
std::vector<CommandEnvironment> CommandHelper::GetExecutionEnvironments()
{
    std::vector<CommandEnvironment> environments;
    for (const auto identity : {CommandIdentity::Service, CommandIdentity::CurrentUser, CommandIdentity::CurrentUserAdmin})
    {
        try { Handle token; GetToken(identity, token); environments.push_back(TokenInfo(token.value, identity)); }
        catch (const CommandExecutionError &e) { environments.push_back({command_detail::Name(identity), "", "", e.what(), false, false}); }
    }
    return environments;
}

CommandHelper::CommandResult CommandHelper::ExecuteBounded(const CommandOptions &o)
{
    command_detail::Validate(o);
    if (o.cancelled && o.cancelled()) throw CommandExecutionError("CANCELLED", "Cancelled before process creation");
    const auto deadline = std::chrono::steady_clock::now() + o.timeout;
    Handle token; GetToken(o.identity, token);
    const auto identity = TokenInfo(token.value, o.identity);
    CommandResult result;
    result.user = identity.user; result.elevated = identity.elevated;
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) Fail("Caller session");
    result.session = std::to_string(session);
    // Pipe inheritance cannot cross TS sessions. Headless commands keep the caller's
    // session but use the selected user's token/profile, never the service identity.
    if (o.identity != CommandIdentity::Service && result.session != identity.session &&
        !SetTokenInformation(token.value, TokenSessionId, &session, sizeof(session))) Fail("Set headless token session");
    Environment environment;
    if (!CreateEnvironmentBlock(&environment.value, token.value, FALSE)) Fail("CreateEnvironmentBlock");
    auto executable = Wide(o.executable);
    std::wstring commandLine;
    if (o.kind == CommandKind::Shell)
    {
        if (!o.shell.empty() && o.shell != "cmd") throw CommandExecutionError("INVALID_ARGUMENT", "Windows shell must be cmd");
        executable = Wide(SystemPath(L"\\cmd.exe"));
        commandLine = Quote(executable) + L" /d /s /c \"" + Wide(o.command) + L"\"";
    }
    else if (o.kind == CommandKind::PowerShell)
    {
        const auto path = PowerShellPath();
        if (path.empty()) throw CommandExecutionError("UNAVAILABLE", "PowerShell is not installed");
        executable = Wide(path);
        const auto script = L"[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false); " + Wide(o.command);
        commandLine = Quote(executable) + L" -NoLogo -NoProfile -NonInteractive -EncodedCommand " +
            Wide(Base64Help::Encode(reinterpret_cast<const char *>(script.data()), script.size() * sizeof(wchar_t)));
    }
    else
    {
        commandLine = Quote(executable);
        for (const auto &arg : o.arguments) commandLine += L" " + Quote(Wide(arg));
    }
    if (commandLine.size() >= 32767) throw CommandExecutionError("INVALID_ARGUMENT", "Windows command line is too long");
    const auto cwd = o.workingDirectory.empty() ? Home(token.value) : Wide(o.workingDirectory);
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle outRead, outWrite, errRead, errWrite;
    if (!CreatePipe(&outRead.value, &outWrite.value, &security, 0) ||
        !CreatePipe(&errRead.value, &errWrite.value, &security, 0)) Fail("CreatePipe");
    if (!SetHandleInformation(outRead.value, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(errRead.value, HANDLE_FLAG_INHERIT, 0)) Fail("Pipe inheritance");
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr));
    if (input.value == INVALID_HANDLE_VALUE) Fail("Open NUL");
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow = SW_HIDE;
    startup.StartupInfo.hStdInput = input.value;
    startup.StartupInfo.hStdOutput = outWrite.value;
    startup.StartupInfo.hStdError = errWrite.value;
    Attributes attributes; SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes); attributes.memory.resize(bytes);
    auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.memory.data());
    if (!InitializeProcThreadAttributeList(list, 1, 0, &bytes)) Fail("InitializeProcThreadAttributeList");
    attributes.value = startup.lpAttributeList = list;
    HANDLE inherited[] = {input.value, outWrite.value, errWrite.value};
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr))
        Fail("Set explicit inherited handles");
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) Fail("Create process job");
    PROCESS_INFORMATION pi{};
    if (o.cancelled && o.cancelled()) throw CommandExecutionError("CANCELLED", "Cancelled before process creation");
    if (std::chrono::steady_clock::now() >= deadline)
        throw CommandExecutionError("DEADLINE_EXCEEDED", "Execution setup exceeded timeout");
    const DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT;
    const auto ok = o.identity == CommandIdentity::Service
        ? CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, TRUE, flags, environment.value, cwd.c_str(), &startup.StartupInfo, &pi)
        : CreateProcessAsUserW(token.value, executable.c_str(), commandLine.data(), nullptr, nullptr, TRUE, flags,
                              environment.value, cwd.c_str(), &startup.StartupInfo, &pi);
    if (!ok) Fail("Create command process");
    Handle process(pi.hProcess), thread(pi.hThread);
    result.processId = pi.dwProcessId;
    if (!AssignProcessToJobObject(job.value, process.value))
    {
        const auto error = GetLastError(); TerminateProcess(process.value, 1);
        SetLastError(error); Fail("Assign command job");
    }
    if (ResumeThread(thread.value) == static_cast<DWORD>(-1)) Fail("Resume command");
    outWrite.Close(); errWrite.Close(); input.Close();
    while (true)
    {
        Drain(outRead.value, result.stdoutStr, o.maxOutputBytes, result.stdoutTruncated);
        Drain(errRead.value, result.stderrStr, o.maxOutputBytes, result.stderrTruncated);
        if (WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0) break;
        result.cancelled = o.cancelled && o.cancelled();
        result.timedOut = std::chrono::steady_clock::now() >= deadline;
        if (result.cancelled || result.timedOut) { TerminateJobObject(job.value, 1); break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // No background processes survive a synchronous invocation, even on normal parent exit.
    TerminateJobObject(job.value, 1);
    WaitForSingleObject(process.value, 2000);
    Drain(outRead.value, result.stdoutStr, o.maxOutputBytes, result.stdoutTruncated);
    Drain(errRead.value, result.stderrStr, o.maxOutputBytes, result.stderrTruncated);
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(process.value, &exitCode)) Fail("Get command exit code");
    result.exitCode = static_cast<int>(exitCode);
    return result;
}
}
