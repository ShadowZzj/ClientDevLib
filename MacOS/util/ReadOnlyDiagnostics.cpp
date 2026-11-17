#include <General/util/System/ReadOnlyDiagnostics.h>
#include <General/util/Process/CommandExecution.h>
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sstream>

namespace zzj
{
namespace
{
void Check(const DiagnosticCheck &check) { if (check) check(); }
[[noreturn]] void Fail(const char *operation)
{
    throw DiagnosticError("UNAVAILABLE", std::string(operation) + " failed (errno " + std::to_string(errno) + ")");
}
template<class T> T Sysctl(const char *name)
{
    T value{}; size_t length = sizeof(value);
    if (sysctlbyname(name, &value, &length, nullptr, 0)) Fail(name);
    return value;
}
struct Host
{
    mach_port_t value = mach_host_self();
    ~Host() { mach_port_deallocate(mach_task_self(), value); }
};
}
std::vector<ProcessSummary> ReadOnlyDiagnostics::Processes(const DiagnosticCheck &check)
{
    // Existing ProcessV2 iterator collects extra statistics and skips system processes.
    // This reusable inventory includes visible system PIDs and handles libproc byte counts.
    Check(check);
    const int bytes = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (bytes <= 0 || bytes > 4 * 1024 * 1024) Fail("Process count");
    std::vector<pid_t> pids(static_cast<size_t>(bytes) / sizeof(pid_t) + 1024);
    const int used = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
    if (used <= 0) Fail("Process list");
    if (static_cast<size_t>(used) == pids.size() * sizeof(pid_t))
        throw DiagnosticError("RESOURCE_EXHAUSTED", "Process inventory grew during collection; refresh");
    pids.resize(static_cast<size_t>(used) / sizeof(pid_t));
    std::sort(pids.begin(), pids.end()); pids.erase(std::unique(pids.begin(), pids.end()), pids.end());
    std::vector<ProcessSummary> result;
    for (const auto pid : pids)
    {
        Check(check);
        if (pid <= 0) continue;
        proc_bsdinfo info{};
        if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info)) continue;
        result.push_back({static_cast<uint32_t>(pid), info.pbi_ppid, 0,
            info.pbi_name[0] ? std::string(info.pbi_name, strnlen(info.pbi_name, sizeof(info.pbi_name))) :
                               std::string(info.pbi_comm, strnlen(info.pbi_comm, sizeof(info.pbi_comm)))});
    }
    return result;
}
ProcessDetails ReadOnlyDiagnostics::Process(uint32_t pid, const DiagnosticCheck &check)
{
    Check(check);
    proc_bsdinfo info{};
    if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info))
        throw DiagnosticError(errno == EPERM || errno == EACCES ? "PERMISSION_DENIED" : "NOT_FOUND", "Process unavailable or exited");
    ProcessDetails result;
    result.process = {pid, info.pbi_ppid, 0, info.pbi_name[0] ?
        std::string(info.pbi_name, strnlen(info.pbi_name, sizeof(info.pbi_name))) :
        std::string(info.pbi_comm, strnlen(info.pbi_comm, sizeof(info.pbi_comm)))};
    result.startedAtMs = uint64_t(info.pbi_start_tvsec) * 1000 + info.pbi_start_tvusec / 1000;
    char path[PROC_PIDPATHINFO_MAXSIZE]{};
    if (proc_pidpath(pid, path, sizeof(path)) > 0) result.executable = path;
    else result.partial = true;
    proc_taskinfo task{};
    if (proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &task, sizeof(task)) == sizeof(task))
    {
        result.process.threads = task.pti_threadnum;
        result.residentBytes = task.pti_resident_size;
        // XNU fill_taskprocinfo exposes Mach absolute ticks, not nanoseconds.
        // Rosetta can expose a translated timebase. Leave this optional value
        // unavailable rather than report a falsely scaled CPU duration.
        int translated = 0; size_t translatedSize = sizeof(translated);
        sysctlbyname("sysctl.proc_translated", &translated, &translatedSize, nullptr, 0);
        mach_timebase_info_data_t timebase{};
        if (!translated && mach_timebase_info(&timebase) == KERN_SUCCESS && timebase.denom)
            result.cpuTimeMs = static_cast<uint64_t>((static_cast<long double>(task.pti_total_user) + task.pti_total_system) *
                timebase.numer / timebase.denom / 1000000);
        else result.partial = true;
    }
    else result.partial = true;
    return result;
}
MachineInfo ReadOnlyDiagnostics::Machine()
{
    utsname info{}; if (uname(&info)) Fail("uname");
    char version[128]{}; size_t length = sizeof(version);
    if (sysctlbyname("kern.osproductversion", version, &length, nullptr, 0)) Fail("macOS version");
    const auto boot = Sysctl<timeval>("kern.boottime");
    timeval now{}; gettimeofday(&now, nullptr);
    const auto uptime = std::max<int64_t>(0, (int64_t(now.tv_sec) - boot.tv_sec) * 1000 + (now.tv_usec - boot.tv_usec) / 1000);
    return {"macos", version, info.machine, info.nodename, Sysctl<uint32_t>("hw.logicalcpu"), static_cast<uint64_t>(uptime)};
}
MemoryInfoSnapshot ReadOnlyDiagnostics::Memory()
{
    Host host; vm_statistics64_data_t vm{}; mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(host.value, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &count) != KERN_SUCCESS)
        throw DiagnosticError("UNAVAILABLE", "Memory statistics unavailable");
    const auto total = Sysctl<uint64_t>("hw.memsize");
    const auto available = (uint64_t(vm.free_count) + vm.inactive_count) * static_cast<uint64_t>(getpagesize());
    return {total, std::min(total, available), true};
}
CpuCounters ReadOnlyDiagnostics::Cpu()
{
    Host host; host_cpu_load_info_data_t cpu{}; mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(host.value, HOST_CPU_LOAD_INFO, reinterpret_cast<host_info_t>(&cpu), &count) != KERN_SUCCESS)
        throw DiagnosticError("UNAVAILABLE", "CPU statistics unavailable");
    return {cpu.cpu_ticks[CPU_STATE_USER], cpu.cpu_ticks[CPU_STATE_SYSTEM], cpu.cpu_ticks[CPU_STATE_IDLE], cpu.cpu_ticks[CPU_STATE_NICE], true};
}
std::vector<DiskInfoSnapshot> ReadOnlyDiagnostics::Disks(const DiagnosticCheck &check)
{
    Check(check);
    const int count = getfsstat(nullptr, 0, MNT_NOWAIT);
    if (count < 0 || count > 4096) Fail("Mount count");
    std::vector<struct statfs> mounts(static_cast<size_t>(count) + 32);
    const int used = getfsstat(mounts.data(), static_cast<int>(mounts.size() * sizeof(struct statfs)), MNT_NOWAIT);
    if (used < 0 || static_cast<size_t>(used) >= mounts.size()) Fail("Mount list");
    std::vector<DiskInfoSnapshot> result;
    for (int i = 0; i < used; ++i)
    {
        Check(check); const auto &fs = mounts[i];
        if (!(fs.f_flags & MNT_LOCAL)) continue;
        result.push_back({fs.f_mntonname, fs.f_fstypename, uint64_t(fs.f_blocks) * fs.f_bsize,
            uint64_t(fs.f_bfree) * fs.f_bsize, uint64_t(fs.f_bavail) * fs.f_bsize});
    }
    return result;
}
std::vector<ServiceInfoSnapshot> ReadOnlyDiagnostics::Services(const DiagnosticCheck &check)
{
    // Reuse the library's bounded execution for this single fixed read-only command.
    // No model/user text is passed to an executable or shell.
    CommandOptions options; options.executable = "/bin/launchctl"; options.arguments = {"list"};
    options.timeout = std::chrono::milliseconds(3000); options.maxOutputBytes = 1024 * 1024;
    options.cancelled = [&] { Check(check); return false; };
    Check(check);
    const auto output = CommandHelper::ExecuteBounded(options);
    if (output.exitCode || output.timedOut || output.stdoutTruncated)
        throw DiagnosticError("UNAVAILABLE", "launchctl list failed or exceeded collection bounds");
    std::vector<ServiceInfoSnapshot> result;
    std::istringstream stream(output.stdoutStr); std::string line;
    std::getline(stream, line); // PID / Status / Label header
    while (std::getline(stream, line))
    {
        Check(check); std::istringstream row(line); std::string pid, status, label;
        if (!(row >> pid >> status >> label)) continue;
        result.push_back({label, label, pid == "-" ? "NotRunning" : "Running", "Unknown"});
    }
    return result;
}
}
