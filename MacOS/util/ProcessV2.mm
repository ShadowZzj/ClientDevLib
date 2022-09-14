#include "ProcessIterator/process_iterator.h"
#include "ProcessIterator/process_iterator_apple.h"
#include <General/util/BaseUtil.hpp>
#include <General/util/Process.h>
#include <General/util/StrUtil.h>
#include <General/util/Thread.h>
#include <chrono>
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
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/vmmeter.h>


namespace zzj
{
ProcessV2::ProcessV2(int pid)
{
    this->pid = pid;
}
ProcessV2::ProcessV2(const std::string &name)
{
    this->processName = name;
}
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
std::vector<ThreadV2> ProcessV2::GetProcessThreads()
{
    return {};
}
std::vector<ThreadV2> ProcessV2::GetProcessThreads(int pid)
{
    return {};
}
std::vector<ProcessV2> ProcessV2::GetRunningProcesses()
{
    std::vector<ProcessV2> ret;

    struct process_iterator it;
    struct process proc;
    struct process_filter filter;
    filter.pid              = 0;
    filter.include_children = 0;
    init_process_iterator(&it, &filter);
    while (get_next_process(&it, &proc) != -1)
    {
        ProcessV2 process;
        process.pid         = proc.pid;
        process.processName = str::ansi2utf8(proc.command);
        ret.push_back(process);
    }

    if (close_process_iterator(&it) != 0)
        exit(1);
    return ret;
}

int ProcessV2::GetStatistic(StatisticTimePoint &statictic)
{
    int tmpPid = -1;
    if (pid)
        tmpPid = pid;
    else if (!processName.empty())
        tmpPid = GetProcessIdByName(processName);

    if (tmpPid < 0)
        return -1;

    struct process_iterator it;
    struct process proc;
    struct process_filter filter;
    filter.pid              = 0;
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
    if (close_process_iterator(&it) != 0)
        return -2;

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
    if (error != KERN_SUCCESS)
        return -4;
    vm_region_basic_info_data_64_t b_info;
    vm_address_t address = GLOBAL_SHARED_TEXT_SEGMENT;
    vm_size_t size;
    mach_port_t object_name;
    count = VM_REGION_BASIC_INFO_COUNT_64;
    error = vm_region_64(task, &address, &size, VM_REGION_BASIC_INFO, (vm_region_info_t)&b_info, &count, &object_name);
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

    statictic.memoryUsed   = ti.resident_size;
    mach_port_deallocate(mach_task_self(), task);
    return 0;
}
int ProcessV2::GetProcessIdByName(const std::string &name)
{
    auto ret      = GetRunningProcesses();
    auto utf8Name = str::ansi2utf8(name);
    for (auto &p : ret)
    {
        if (p.processName.find(name) != p.processName.npos)
            return p.pid;
    }
    return -1;
}

std::string ProcessV2::GetProcessNameById(const int &pid)
{
    auto ret = GetRunningProcesses();
    for (auto &p : ret)
    {
        if (p.pid == pid)
            return p.processName;
    }
    return "";
}

bool ProcessV2::IsProcessAlive(const std::string &name)
{
    pid_t pid = -1;
    struct process_iterator it;
    struct process proc;
    struct process_filter filter;
    filter.pid              = 0;
    filter.include_children = 0;
    init_process_iterator(&it, &filter);
    while (get_next_process(&it, &proc) != -1)
    {
        if (strncmp(proc.command, name.c_str(), name.length()) == 0 && kill(pid, SIGCONT) == 0)
        {
            pid = proc.pid;
            break;
        }
    }
    if (close_process_iterator(&it) != 0)
        exit(1);
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
    if (pid < 0)
        return false;

    if (kill(pid, 0) != 0)
        return false;

    return true;
}
bool ProcessV2::SuspendPid(int pid)
{
    if (kill(pid, SIGSTOP) == 0)
        return true;
    return false;
}
bool ProcessV2::ResumePid(int pid)
{
    if (kill(pid, SIGCONT) == 0)
        return true;
    return false;
}

}; // namespace zzj
