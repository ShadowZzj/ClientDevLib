#include "ChildProcess.h"
#include <mutex>
#include <thread>
#include <system_error>
#ifdef _WIN32
#include <Windows/util/Process/ProcessLaunchSupport.h>
#include <sddl.h>
#else
#include <crt_externs.h>
#include <fcntl.h>
#include <spawn.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace zzj
{
struct ChildProcess::State
{
    uint32_t id = 0;
    bool terminated = false;
    std::mutex mutex;
#ifdef _WIN32
    process_detail::Handle child, job;
#else
    pid_t child = -1;
    bool exited = false;
#endif
};
ChildProcess::ChildProcess(const std::string &executable, const std::vector<std::string> &arguments)
    : state_(std::make_unique<State>())
{
#ifdef _WIN32
    using namespace process_detail;
    auto check = [](BOOL ok) { if (!ok) throw std::system_error(GetLastError(), std::system_category(), "Launch owned child"); };
    const auto path = Wide(executable);
    auto command = Quote(path);
    for (const auto &argument : arguments) command += L" " + Quote(Wide(argument));
    if (command.size() >= 32767) throw std::runtime_error("Child arguments too long");
    state_->job.value = CreateJobObjectW(nullptr, nullptr);
    check(state_->job.value != nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    check(SetInformationJobObject(state_->job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)));
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    check(CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                        nullptr, nullptr, &startup, &process));
    state_->child.value = process.hProcess;
    state_->id = process.dwProcessId;
    Handle thread(process.hThread);
    if (!AssignProcessToJobObject(state_->job.value, state_->child.value))
    {
        const auto error = GetLastError();
        TerminateProcess(state_->child.value, 1);
        WaitForSingleObject(state_->child.value, INFINITE);
        throw std::system_error(error, std::system_category(), "Assign child job");
    }
    check(ResumeThread(thread.value) != static_cast<DWORD>(-1));
#else
    // Match CommandExecution's posix_spawn model: safe in a multithreaded gRPC host, no fork-side C++ work.
    posix_spawnattr_t attributes;
    int error = posix_spawnattr_init(&attributes);
    if (error) throw std::system_error(error, std::generic_category(), "Initialize spawn attributes");
    posix_spawn_file_actions_t actions;
    error = posix_spawn_file_actions_init(&actions);
    if (error) { posix_spawnattr_destroy(&attributes); throw std::system_error(error, std::generic_category(), "Initialize spawn actions"); }
    auto set = [&](int result) { if (!error) error = result; };
    set(posix_spawnattr_setpgroup(&attributes, 0));
    set(posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_CLOEXEC_DEFAULT));
    set(posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0));
    set(posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0));
    set(posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0));
    std::vector<std::string> values{executable};
    values.insert(values.end(), arguments.begin(), arguments.end());
    std::vector<char *> argv;
    for (auto &value : values) argv.push_back(value.data());
    argv.push_back(nullptr);
    if (!error) error = posix_spawn(&state_->child, executable.c_str(), &actions, &attributes, argv.data(), *_NSGetEnviron());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    if (error) throw std::system_error(error, std::generic_category(), "Spawn owned child");
    state_->id = static_cast<uint32_t>(state_->child);
#endif
}
ChildProcess::~ChildProcess() { Terminate(); }
uint32_t ChildProcess::Id() const { return state_->id; }
uint32_t ChildProcess::CurrentId()
{
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<uint32_t>(getpid());
#endif
}
bool ChildProcess::Alive()
{
    std::lock_guard<std::mutex> lock(state_->mutex);
#ifdef _WIN32
    return WaitForSingleObject(state_->child.value, 0) == WAIT_TIMEOUT;
#else
    if (state_->exited) return false;
    int status = 0;
    const auto result = waitpid(state_->child, &status, WNOHANG);
    if (result == state_->child || (result < 0 && errno == ECHILD)) state_->exited = true;
    return !state_->exited;
#endif
}
bool ChildProcess::WaitFor(std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (Alive())
    {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}
void ChildProcess::Terminate()
{
    if (!state_) return;
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->terminated) return;
    state_->terminated = true;
#ifdef _WIN32
    if (state_->job.value) TerminateJobObject(state_->job.value, 1);
    if (state_->child.value) WaitForSingleObject(state_->child.value, INFINITE);
#else
    if (!state_->exited && state_->child > 0)
    {
        kill(-state_->child, SIGKILL);
        while (waitpid(state_->child, nullptr, 0) < 0 && errno == EINTR) {}
        state_->exited = true;
    }
#endif
}
void CreatePrivateDirectory(const std::filesystem::path &path)
{
#ifdef _WIN32
    using namespace process_detail;
    Handle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value))
        throw std::system_error(GetLastError(), std::system_category(), "Read directory owner");
    DWORD size = 0;
    GetTokenInformation(token.value, TokenUser, nullptr, 0, &size);
    std::vector<char> data(size);
    if (!GetTokenInformation(token.value, TokenUser, data.data(), size, &size))
        throw std::system_error(GetLastError(), std::system_category(), "Read directory owner");
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(data.data())->User.Sid, &sid))
        throw std::system_error(GetLastError(), std::system_category(), "Read owner SID");
    const std::wstring sddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
        throw std::system_error(GetLastError(), std::system_category(), "Create private directory ACL");
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
    const auto ok = CreateDirectoryW(path.c_str(), &attributes);
    const auto error = GetLastError();
    LocalFree(descriptor);
    if (!ok) throw std::system_error(error, std::system_category(), "Create private directory");
#else
    if (mkdir(path.c_str(), 0700) != 0) throw std::system_error(errno, std::generic_category(), "Create private directory");
#endif
}
}
