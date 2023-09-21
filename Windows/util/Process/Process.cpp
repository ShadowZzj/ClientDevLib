#include <General/util/BaseUtil.hpp>
#include <General/util/Process/Process.h>
#include <General/util/Process/Thread.h>
#include <General/util/StrUtil.h>
#include <Windows.h>
#include <psapi.h>
#include <set>
#include <tlhelp32.h>

namespace zzj
{
ProcessV2::ProcessV2()
{
}
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
        if (!it->processInfo || it->processInfo->pid != pid)
        {
            it = ret.erase(it);
        }
        else
            ++it;
    }
    return ret;
}

std::vector<zzj::ThreadV2> zzj::ProcessV2::GetProcessThreadsCache(int pid)
{
    static std::mutex m_lck;
    static std::chrono::milliseconds preTime =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
    static auto threads = ThreadV2::EnumAllThreads();
    std::lock_guard<std::mutex> lck(m_lck);

    std::chrono::milliseconds curTime =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
    if (curTime - preTime > std::chrono::milliseconds(1000))
    {
        threads = ThreadV2::EnumAllThreads();
        preTime = curTime;
    }

    std::vector<ThreadV2> tempThreads;

    auto it = threads.begin();
    while (it != threads.end())
    {
        if (it->processInfo && it->processInfo->pid == pid)
        {
            tempThreads.push_back(*it);
        }
        ++it;
    }
    return tempThreads;
}

std::vector<ProcessV2> ProcessV2::GetRunningProcesses()
{
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(PROCESSENTRY32W);
    std::vector<ProcessV2> ret;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

    if (Process32FirstW(snapshot, &entry) == TRUE)
    {
        while (Process32NextW(snapshot, &entry) == TRUE)
        {
            ProcessV2 process;
            process.pid         = entry.th32ProcessID;
            process.processName = str::w2utf8(entry.szExeFile);
            ret.push_back(process);
        }
    }

    CloseHandle(snapshot);
    return ret;
}

BOOL EnableDebugPrivilege()
{
    HANDLE hToken;
    BOOL fOk = FALSE;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken))
    {
        TOKEN_PRIVILEGES tp;
        tp.PrivilegeCount = 1;
        LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &tp.Privileges[0].Luid);

        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);

        fOk = (GetLastError() == ERROR_SUCCESS);
        CloseHandle(hToken);
    }
    return fOk;
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
    EnableDebugPrivilege();
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
    ns100UserTime.LowPart  = userTime.dwLowDateTime;
    ns100UserTime.HighPart = userTime.dwHighDateTime;

    ULARGE_INTEGER ns100KernelTime;
    ns100KernelTime.LowPart  = kernelTime.dwLowDateTime;
    ns100KernelTime.HighPart = kernelTime.dwHighDateTime;

    int64_t totalCpuTimeMs = (ns100UserTime.QuadPart + ns100KernelTime.QuadPart) / 10000;
    statictic.cpuUsed      = std::chrono::milliseconds(totalCpuTimeMs);

    PROCESS_MEMORY_COUNTERS pmc;
    ret = GetProcessMemoryInfo(handle, &pmc, sizeof(pmc));
    if (!ret)
        return -4;
    statictic.memoryUsed = pmc.WorkingSetSize;

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

std::string zzj::ProcessV2::GetProcessNameById(const int &pid)
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
    auto threads = GetProcessThreadsCache(pid);
    for (auto &t : threads)
    {
        if (t.tid == GetCurrentThreadId())
            continue;
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
    auto threads = GetProcessThreadsCache(pid);
    for (auto &t : threads)
    {
        if (t.tid == GetCurrentThreadId())
            continue;
        auto handle = OpenThread(THREAD_SUSPEND_RESUME, false, t.tid);
        if (NULL == handle)
            return false;
        ResumeThread(handle);
        CloseHandle(handle);
    }
    return true;
}
int ProcessV2::GetModules(std::vector<ProcessV2::Module> &modules)
{
    auto handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, false, pid);
    if (NULL == handle)
        return -1;
    DEFER
    {
        CloseHandle(handle);
    };
    DWORD requiredSize = 0;
    EnumProcessModulesEx(handle, NULL, 0, &requiredSize, LIST_MODULES_ALL);

    std::vector<HMODULE> moduleHandles(requiredSize / sizeof(HMODULE));
    auto result = EnumProcessModulesEx(handle, moduleHandles.data(), requiredSize, &requiredSize, LIST_MODULES_ALL);
    if (!result)
        return -2;

    for (auto &h : moduleHandles)
    {
        ProcessV2::Module module;
        std::vector<char> path(MAX_PATH);

        GetModuleFileNameExA(handle, h, path.data(), MAX_PATH);
        module.path = path.data();
        module.name = path.data() + module.path.find_last_of('\\') + 1;
        module.base = (uint64_t)h;

        MODULEINFO info;
        GetModuleInformation(handle, h, &info, sizeof(info));
        module.size = info.SizeOfImage;

        modules.push_back(module);
    }
    return 0;
}
std::vector<std::pair<int, std::string>> ProcessV2::GetProcessInfo(const std::set<std::string> proccessList,
                                                                   std::map<std::string, zzj::ProcessV2> &processes)
{
    std::vector<std::pair<int, std::string>> ret;

    EnableDebugPrivilege();

    PROCESSENTRY32W entry;
    entry.dwSize    = sizeof(PROCESSENTRY32W);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
    if (Process32FirstW(snapshot, &entry) == TRUE)
    {
        while (Process32NextW(snapshot, &entry) == TRUE)
        {
            ProcessV2 process;
            process.pid         = entry.th32ProcessID;
            process.processName = str::w2utf8(entry.szExeFile);
            if (proccessList.find(process.processName) != proccessList.end())
            {
                auto handle =
                    OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, false, process.pid);
                if (NULL == handle)
                {
                    ret.push_back(std::make_pair(-2, "OpenProcess fail,the process name is " + process.processName));
                }
                DEFER
                {
                    CloseHandle(handle);
                };

                FILETIME createTime;
                FILETIME exitTime;
                FILETIME kernelTime;
                FILETIME userTime;

                auto timeRet = GetProcessTimes(handle, &createTime, &exitTime, &kernelTime, &userTime);
                if (!timeRet)
                {
                    ret.push_back(
                        std::make_pair(-3, "GetProcessTimes fail,the process name is " + process.processName));
                }

                ULARGE_INTEGER ns100UserTime;
                ns100UserTime.LowPart  = userTime.dwLowDateTime;
                ns100UserTime.HighPart = userTime.dwHighDateTime;

                ULARGE_INTEGER ns100KernelTime;
                ns100KernelTime.LowPart  = kernelTime.dwLowDateTime;
                ns100KernelTime.HighPart = kernelTime.dwHighDateTime;

                int64_t totalCpuTimeMs             = (ns100UserTime.QuadPart + ns100KernelTime.QuadPart) / 10000;
                process.statisticTimePoint.cpuUsed = std::chrono::milliseconds(totalCpuTimeMs);

                PROCESS_MEMORY_COUNTERS pmc;
                auto memoryRet = GetProcessMemoryInfo(handle, &pmc, sizeof(pmc));
                if (!memoryRet)
                {
                    ret.push_back(
                        std::make_pair(-4, "GetProcessMemoryInfo fail,the process name is " + process.processName));
                }
                process.statisticTimePoint.memoryUsed = pmc.WorkingSetSize;
                processes[process.processName]        = process;
            }
        }
    }

    CloseHandle(snapshot);
    return ret;
}

} // namespace zzj