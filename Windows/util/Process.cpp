#include <General/util/Process.h>
#include <General/util/Thread.h>
#include <General/util/StrUtil.h>
#include <General/util/BaseUtil.hpp>
#include <Windows.h>
#include <tlhelp32.h>
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
    if (!processName.empty())
        return GetProcessThreads(GetProcessIdByName(processName));
    else
        return GetProcessThreads(pid);
}
std::vector<ThreadV2> ProcessV2::GetProcessThreads(int pid)
{
    auto ret = ThreadV2::EnumAllThreads();
    auto it  = ret.begin();
    while (it != ret.end())
    {
        if (it->processInfo || it->processInfo->pid != pid)
        {
            it = ret.erase(it);
        }
        else
            ++it;
    }
    return ret;
}
std::vector<ProcessV2> ProcessV2::GetRunningProcesses()
{
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);
    std::vector<ProcessV2> ret;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

    if (Process32First(snapshot, &entry) == TRUE)
    {
        while (Process32Next(snapshot, &entry) == TRUE)
        {
            ProcessV2 process;
            process.pid         = entry.th32ProcessID;
            process.processName = str::ansi2utf8(entry.szExeFile);
            ret.push_back(process);
        }
    }

    CloseHandle(snapshot);
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

    auto handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, false, pid);
    if (NULL == handle)
        return -2;
    DEFER
    {
        CloseHandle(handle);
    };

    FILETIME createTime;
    FILETIME exitTime;
    FILETIME kernelTime;
    FILETIME userTime;

    bool ret = GetProcessTimes(handle, &createTime, &exitTime, &kernelTime, &userTime);
    if (!ret)
        return -3;

    ULARGE_INTEGER ns100UserTime;
    ns100UserTime.LowPart = userTime.dwLowDateTime;
    ns100UserTime.HighPart = userTime.dwHighDateTime;

    ULARGE_INTEGER ns100KernelTime;
    ns100KernelTime.LowPart = kernelTime.dwLowDateTime;
    ns100KernelTime.HighPart = kernelTime.dwHighDateTime;

    int64_t totalCpuTimeMs = (ns100UserTime.QuadPart + ns100KernelTime.QuadPart)/10000;
    statictic.cpuUsed      = std::chrono::milliseconds(totalCpuTimeMs);
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
bool ProcessV2::IsProcessAlive(const std::string &name)
{
    int pid = GetProcessIdByName(name);
    if (pid > 0)
        return TRUE;
    else
        return false;
}
bool ProcessV2::IsProcessAlive(int pid)
{
    if (pid < 0)
        return false;
    auto processes = GetRunningProcesses();
    for (auto &p : processes)
    {
        if (p.pid == pid)
            return true;
    }
    return false;
}
bool ProcessV2::SuspendPid(int pid)
{
    auto threads = GetProcessThreads(pid);
    for (auto &t : threads)
    {
        auto handle = OpenThread(THREAD_SUSPEND_RESUME, false, t.tid);
        if (NULL == handle)
            return false;
        SuspendThread(handle);
        CloseHandle(handle);
    }
    return true;
}
bool ProcessV2::ResumePid(int pid)
{
    auto threads = GetProcessThreads(pid);
    for (auto &t : threads)
    {
        auto handle = OpenThread(THREAD_SUSPEND_RESUME, false, t.tid);
        if (NULL == handle)
            return false;
        ResumeThread(handle);
        CloseHandle(handle);
    }
    return true;
}

}; // namespace zzj