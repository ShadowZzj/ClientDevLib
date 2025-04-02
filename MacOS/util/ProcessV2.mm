#include "ProcessIterator/process_iterator.h"
#include "ProcessIterator/process_iterator_apple.h"
#include <General/util/BaseUtil.hpp>
#include <General/util/Process/Process.h>
#include <General/util/Process/Thread.h>
#include <General/util/User/User.h>
#include <General/util/StrUtil.h>
#include <chrono>
#include <libproc.h>
#include <mach/mach_host.h>
#include <mach/mach_init.h>
#include <mach/mach_port.h>
#include <mach/mach_traps.h>
#include <mach/shared_memory_server.h>
#include <mach/task.h>
#include <mach/task_info.h>
#include <mach/thread_act.h>
#include <mach/thread_info.h>
#include <mach/vm_map.h>
#include <mach/vm_region.h>
#include <signal.h>
#include <spdlog/spdlog.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/vmmeter.h>
#include <boost/filesystem.hpp>
namespace zzj
{
ProcessV2::ProcessV2() { this->pid = getpid(); }
ProcessV2::ProcessV2(int pid) { this->pid = pid; }
ProcessV2::ProcessV2(const std::string &name) { this->processName = name; }
int ProcessV2::GetPid()
{
    if (!processName.empty())
        return GetProcessIdByName(processName);
    else
        return pid;
}

bool ProcessV2::IsProcessAlive()
{
    if (!processName.empty())
        return IsProcessAlive(processName);
    else
        return IsProcessAlive(pid);
}
std::vector<ThreadV2> ProcessV2::GetProcessThreads() { return {}; }
std::vector<ThreadV2> ProcessV2::GetProcessThreads(int pid) { return {}; }
std::vector<ProcessV2> ProcessV2::GetRunningProcesses()
{
    std::vector<ProcessV2> ret;

    struct process_iterator it;
    struct process proc;
    struct process_filter filter;
    filter.pid = 0;
    filter.include_children = 0;
    init_process_iterator(&it, &filter);
    while (get_next_process(&it, &proc) != -1)
    {
        ProcessV2 process;
        process.pid = proc.pid;
        process.processName = str::ansi2utf8(proc.command);
        ret.push_back(process);
    }

    if (close_process_iterator(&it) != 0) return {};
    return ret;
}

int ProcessV2::GetStatistic(StatisticTimePoint &statictic)
{
    int tmpPid = -1;
    if (pid)
        tmpPid = pid;
    else if (!processName.empty())
        tmpPid = GetProcessIdByName(processName);

    if (tmpPid < 0) return -1;

    struct process_iterator it;
    struct process proc;
    struct process_filter filter;
    filter.pid = 0;
    filter.include_children = 0;
    init_process_iterator(&it, &filter);
    while (get_next_process(&it, &proc) != -1)
    {
        if (tmpPid == proc.pid && kill(tmpPid, SIGCONT) == 0)
        {
            statictic.cpuUsed = std::chrono::milliseconds(proc.cputime);
            break;
        }
    }
    if (close_process_iterator(&it) != 0) return -2;

    task_t task;
    kern_return_t error;
    mach_msg_type_number_t count;
    struct task_basic_info ti;
    error = task_for_pid(mach_task_self(), pid, &task);
    if (error != KERN_SUCCESS)
    {
        return -3;
    }
    count = TASK_BASIC_INFO_COUNT;
    error = task_info(task, TASK_BASIC_INFO, (task_info_t)&ti, &count);
    if (error != KERN_SUCCESS) return -4;
    vm_region_basic_info_data_64_t b_info;
    vm_address_t address = GLOBAL_SHARED_TEXT_SEGMENT;
    vm_size_t size;
    mach_port_t object_name;
    count = VM_REGION_BASIC_INFO_COUNT_64;
    error = vm_region_64(task, &address, &size, VM_REGION_BASIC_INFO, (vm_region_info_t)&b_info,
                         &count, &object_name);
    if (error != KERN_SUCCESS)
    {
        mach_port_deallocate(mach_task_self(), task);
        return -5;
    }
    if (b_info.reserved && size == (SHARED_TEXT_REGION_SIZE) &&
        ti.virtual_size > (SHARED_TEXT_REGION_SIZE + SHARED_DATA_REGION_SIZE))
    {
        ti.virtual_size -= (SHARED_TEXT_REGION_SIZE + SHARED_DATA_REGION_SIZE);
    }

    statictic.memoryUsed = ti.resident_size;
    mach_port_deallocate(mach_task_self(), task);
    return 0;
}
int ProcessV2::GetProcessIdByName(const std::string &name)
{
    auto ret = GetRunningProcesses();
    auto utf8Name = str::ansi2utf8(name);
    for (auto &p : ret)
    {
        if (p.processName.find(name) != p.processName.npos) return p.pid;
    }
    return -1;
}

std::string ProcessV2::GetProcessNameById(const int &pid)
{
    char pathBuffer[PROC_PIDPATHINFO_MAXSIZE]{0};
    int result = proc_pidpath(pid, pathBuffer, sizeof(pathBuffer));
    if (result <= 0) return "";
    boost::filesystem::path path(pathBuffer);
    return path.filename().string();
}

bool ProcessV2::IsProcessAlive(const std::string &name)
{
    pid_t pid = -1;
    struct process_iterator it;
    struct process proc;
    struct process_filter filter;
    filter.pid = 0;
    filter.include_children = 0;
    init_process_iterator(&it, &filter);
    while (get_next_process(&it, &proc) != -1)
    {
        if (name == proc.command && kill(pid, 0) == 0)
        {
            pid = proc.pid;
            break;
        }
    }
    if (auto ret = close_process_iterator(&it); ret != 0)
    {
        std::cout << "close_process_iterator error:" << ret << std::endl;
        return false;
    }
    if (pid >= 0)
    {
        return true;
    }
    else
    {
        return false;
    }
}
bool ProcessV2::IsProcessAlive(int pid)
{
    if (pid < 0) return false;

    if (kill(pid, 0) != 0) return false;

    return true;
}
bool ProcessV2::SuspendPid(int pid)
{
    if (kill(pid, SIGSTOP) == 0) return true;
    return false;
}
bool ProcessV2::ResumePid(int pid)
{
    if (kill(pid, SIGCONT) == 0) return true;
    return false;
}

ProcessV2::Snapshot::Snapshot()
{
    int res = init_process_iterator(&i, &filter);
    if (res != 0)
    {
        throw std::runtime_error("init_process_iterator failed");
    }
}
ProcessV2::Snapshot::~Snapshot() { close_process_iterator(&i); }
std::vector<ProcessV2> ProcessV2::Snapshot::GetProcesses(const std::string &processName)
{
    std::vector<ProcessV2> ret;
    try
    {
        struct process proc;
        i.i = 0;
        while (get_next_process(&i, &proc) != -1)
        {
            std::string cmdString = proc.command;
            if (cmdString == processName)
            {
                ProcessV2 process;
                process.pid = proc.pid;
                process.processName = str::ansi2utf8(proc.command);
                process.statisticTimePoint.cpuUsed = std::chrono::milliseconds(proc.cputime);
                struct rusage_info_v1 rusageInfo;
                int rusageRet =
                    proc_pid_rusage(proc.pid, RUSAGE_INFO_V1, (rusage_info_t *)&rusageInfo);
                if (rusageRet != 0) continue;

                process.statisticTimePoint.memoryUsed = rusageInfo.ri_phys_footprint;
                ret.push_back(process);
            }
        }
    }
    catch (const std::exception &e)
    {
        spdlog::error("Exeption in ProcessV2::Snapshot::GetProcesses: {}", e.what());
    }
    catch (...)
    {
        spdlog::error("Exeption in ProcessV2::Snapshot::GetProcesses");
    }
    return ret;
}
std::string ProcessV2::GetExecutableFilePath()
{
    char pathBuffer[PROC_PIDPATHINFO_MAXSIZE]{0};
    int result = proc_pidpath(pid, pathBuffer, sizeof(pathBuffer));
    if (result <= 0) return "";
    boost::filesystem::path path(pathBuffer);
    return path.string();
}

CommandHelper::CommandResult CommandHelper::ExecuteCurrentUserCommand(const std::string &command)
{
    auto userInfoVar = zzj::UserInfo::GetActiveUserInfo();
    if (!userInfoVar.has_value()) throw std::runtime_error("Failed to get active user info.");
    auto uid = userInfoVar->uid;
    std::string launchasUserCommand =
        "launchctl asuser " + uid + " sudo -u " + userInfoVar->userName + " ";
    return ExecuteCommand(launchasUserCommand + command);
}

CommandHelper::CommandResult CommandHelper::ExecuteRootCommand(const std::string &command)
{
    auto userInfoVar = zzj::UserInfo::GetActiveUserInfo();
    if (!userInfoVar.has_value()) throw std::runtime_error("Failed to get active user info.");
    auto uid = userInfoVar->uid;
    std::string launchasUserCommand = "launchctl asuser " + uid + " sudo -u root ";
    return ExecuteCommand(launchasUserCommand + command);
}

}  // namespace zzj
