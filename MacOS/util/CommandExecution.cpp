#include <General/util/Process/CommandExecutionInternal.h>
#include <General/util/User/User.h>
#include <crt_externs.h>
#include <spawn.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <grp.h>
#include <pwd.h>
#include <membership.h>
#include <uuid/uuid.h>
#include <signal.h>
#include <thread>

namespace zzj
{
namespace
{
[[noreturn]] void Fail(const char *operation, int error = errno)
{
    throw CommandExecutionError("UNAVAILABLE", std::string(operation) + " failed (errno " + std::to_string(error) + ")");
}
struct Descriptor
{
    int value = -1;
    ~Descriptor() { if (value >= 0) close(value); }
    void Close() { if (value >= 0) close(value); value = -1; }
};
struct Actions
{
    posix_spawn_file_actions_t value;
    Actions() { const auto e = posix_spawn_file_actions_init(&value); if (e) Fail("spawn file actions", e); }
    ~Actions() { posix_spawn_file_actions_destroy(&value); }
};
struct Attributes
{
    posix_spawnattr_t value;
    Attributes() { const auto e = posix_spawnattr_init(&value); if (e) Fail("spawn attributes", e); }
    ~Attributes() { posix_spawnattr_destroy(&value); }
};
struct Child
{
    pid_t pid = -1;
    ~Child()
    {
        if (pid > 0) { kill(-pid, SIGKILL); while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {} }
    }
};
UserInfo ActiveUser()
{
    const auto user = UserInfo::GetActiveUserInfo();
    if (!user || user->uid.empty() || user->uid == "0" || user->userName == "loginwindow")
        throw CommandExecutionError("FAILED_PRECONDITION", "No active desktop user");
    return *user;
}
bool AdminMember(const UserInfo &user)
{
    uuid_t userId, groupId;
    int member = 0;
    return mbr_uid_to_uuid(static_cast<uid_t>(std::stoul(user.uid)), userId) == 0 &&
           mbr_gid_to_uuid(80, groupId) == 0 && mbr_check_membership(userId, groupId, &member) == 0 && member;
}
void Pipe(Descriptor &read, Descriptor &write)
{
    int fds[2]; if (pipe(fds)) Fail("pipe");
    read.value = fds[0]; write.value = fds[1];
    // Daemons may have closed standard descriptors. Keep pipe endpoints away from
    // 0/1/2 so spawn's stdin/dup2 actions cannot accidentally overwrite a source.
    for (auto *descriptor : {&read, &write})
    {
        if (descriptor->value >= 3) continue;
        const auto duplicate = fcntl(descriptor->value, F_DUPFD_CLOEXEC, 3);
        if (duplicate < 0) Fail("duplicate pipe");
        descriptor->Close(); descriptor->value = duplicate;
    }
    if (fcntl(read.value, F_SETFD, FD_CLOEXEC) < 0 || fcntl(write.value, F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(read.value, F_SETFL, O_NONBLOCK) < 0) Fail("pipe flags");
}
void Drain(int fd, std::string &output, size_t limit, bool &truncated)
{
    for (int i = 0; i < 16; ++i)
    {
        char buffer[4096]; const auto n = read(fd, buffer, sizeof(buffer));
        if (n <= 0) return;
        command_detail::Append(output, buffer, static_cast<size_t>(n), limit, truncated);
    }
}
}

std::string CommandHelper::PowerShellPath()
{
    for (const auto *path : {"/opt/homebrew/bin/pwsh", "/usr/local/bin/pwsh"})
        if (access(path, X_OK) == 0) return path;
    return {};
}
std::vector<CommandEnvironment> CommandHelper::GetExecutionEnvironments()
{
    std::vector<CommandEnvironment> result{{"service", std::to_string(geteuid()), "service", "", true, geteuid() == 0}};
    for (const auto identity : {CommandIdentity::CurrentUser, CommandIdentity::CurrentUserAdmin})
    {
        try
        {
            const auto user = ActiveUser();
            const bool admin = identity == CommandIdentity::CurrentUserAdmin;
            const bool available = geteuid() == 0 && (!admin || AdminMember(user));
            result.push_back({command_detail::Name(identity), admin ? "0" : user.uid, "gui/" + user.uid,
                available ? "" : "Requires a root daemon and, for elevation, an admin desktop user", available, admin});
        }
        catch (const CommandExecutionError &e) { result.push_back({command_detail::Name(identity), "", "", e.what(), false, false}); }
    }
    return result;
}

CommandHelper::CommandResult CommandHelper::ExecuteBounded(const CommandOptions &o)
{
    command_detail::Validate(o);
    if (o.cancelled && o.cancelled()) throw CommandExecutionError("CANCELLED", "Cancelled before process creation");
    const auto deadline = std::chrono::steady_clock::now() + o.timeout;
    std::vector<std::string> args;
    if (o.kind == CommandKind::Program)
    {
        args.push_back(o.executable); args.insert(args.end(), o.arguments.begin(), o.arguments.end());
    }
    else if (o.kind == CommandKind::Shell)
    {
        const auto shell = o.shell.empty() ? "bash" : o.shell;
        if (shell != "bash" && shell != "zsh") throw CommandExecutionError("INVALID_ARGUMENT", "macOS shell must be bash or zsh");
        args = {"/bin/" + shell, shell == "bash" ? "--noprofile" : "-f"};
        if (shell == "bash") args.push_back("--norc");
        args.insert(args.end(), {"-c", o.command});
    }
    else
    {
        const auto path = PowerShellPath();
        if (path.empty()) throw CommandExecutionError("UNAVAILABLE", "PowerShell 7 (pwsh) is not installed");
        args = {path, "-NoLogo", "-NoProfile", "-NonInteractive", "-Command", o.command};
    }
    CommandResult result;
    result.user = std::to_string(geteuid()); result.elevated = geteuid() == 0; result.session = "service";
    auto cwd = o.workingDirectory.empty() ? "/" : o.workingDirectory;
    if (o.identity != CommandIdentity::Service)
    {
        const auto user = ActiveUser();
        const bool admin = o.identity == CommandIdentity::CurrentUserAdmin;
        if (geteuid() != 0 || (admin && !AdminMember(user)))
            throw CommandExecutionError("PERMISSION_DENIED", "Requested desktop user elevation is unavailable");
        // Reuse ExecuteCurrentUserCommand/ExecuteRootCommand's launchctl + sudo model,
        // now using argv (no shell interpolation), noninteractive sudo and bounded IO.
        std::vector<std::string> wrapped{"/bin/launchctl", "asuser", user.uid,
            "/usr/bin/sudo", "-n", "-H", "-u", admin ? "#0" : "#" + user.uid, "--"};
        wrapped.insert(wrapped.end(), args.begin(), args.end()); args = std::move(wrapped);
        result.user = admin ? "0" : user.uid; result.elevated = admin; result.session = "gui/" + user.uid;
        if (o.workingDirectory.empty()) cwd = user.homeDirectory;
    }
    Descriptor outRead, outWrite, errRead, errWrite;
    Pipe(outRead, outWrite); Pipe(errRead, errWrite);
    Actions actions; Attributes attributes;
    auto check = [](int e) { if (e) Fail("configure spawn", e); };
    check(posix_spawn_file_actions_addopen(&actions.value, STDIN_FILENO, "/dev/null", O_RDONLY, 0));
    check(posix_spawn_file_actions_adddup2(&actions.value, outWrite.value, STDOUT_FILENO));
    check(posix_spawn_file_actions_adddup2(&actions.value, errWrite.value, STDERR_FILENO));
    check(posix_spawn_file_actions_addchdir_np(&actions.value, cwd.c_str()));
    check(posix_spawnattr_setpgroup(&attributes.value, 0));
    check(posix_spawnattr_setflags(&attributes.value, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_CLOEXEC_DEFAULT));
    std::vector<char *> argv; for (auto &arg : args) argv.push_back(arg.data()); argv.push_back(nullptr);
    Child child;
    if (o.cancelled && o.cancelled()) throw CommandExecutionError("CANCELLED", "Cancelled before process creation");
    if (std::chrono::steady_clock::now() >= deadline)
        throw CommandExecutionError("DEADLINE_EXCEEDED", "Execution setup exceeded timeout");
    const int error = posix_spawn(&child.pid, args[0].c_str(), &actions.value, &attributes.value, argv.data(), *_NSGetEnviron());
    if (error) { child.pid = -1; Fail("spawn command", error); }
    result.processId = static_cast<unsigned long>(child.pid);
    outWrite.Close(); errWrite.Close();
    int status = 0;
    while (true)
    {
        Drain(outRead.value, result.stdoutStr, o.maxOutputBytes, result.stdoutTruncated);
        Drain(errRead.value, result.stderrStr, o.maxOutputBytes, result.stderrTruncated);
        const auto waited = waitpid(child.pid, &status, WNOHANG);
        if (waited == child.pid) break;
        if (waited < 0 && errno != EINTR) Fail("waitpid");
        result.cancelled = o.cancelled && o.cancelled();
        result.timedOut = std::chrono::steady_clock::now() >= deadline;
        if (result.cancelled || result.timedOut)
        {
            kill(-child.pid, SIGKILL);
            while (waitpid(child.pid, &status, 0) < 0 && errno == EINTR) {}
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    kill(-child.pid, SIGKILL); // Reap/stop remaining members of this invocation's process group.
    child.pid = -1;
    Drain(outRead.value, result.stdoutStr, o.maxOutputBytes, result.stdoutTruncated);
    Drain(errRead.value, result.stderrStr, o.maxOutputBytes, result.stderrTruncated);
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    return result;
}
}
