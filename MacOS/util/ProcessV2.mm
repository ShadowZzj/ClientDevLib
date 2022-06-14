#include <signal.h>
#include <chrono>

#include <General/util/Process.h>
#include <General/util/Thread.h>
#include <General/util/StrUtil.h>
#include <General/util/BaseUtil.hpp>
#include "ProcessIterator/process_iterator_apple.h"
#include "ProcessIterator/process_iterator.h"

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
	filter.pid = 0;
	filter.include_children = 0;
	init_process_iterator(&it, &filter);
	while (get_next_process(&it, &proc) != -1)
	{
        ProcessV2 process;
        process.pid         = proc.pid;
        process.processName = str::ansi2utf8(proc.command);
        ret.push_back(process);
            
	}

	if (close_process_iterator(&it) != 0) exit(1);
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
	filter.pid = 0;
	filter.include_children = 0;
	init_process_iterator(&it, &filter);
	while (get_next_process(&it, &proc) != -1)
	{
		if (tmpPid == proc.pid  && kill(tmpPid,SIGCONT)==0) {
            statictic.cpuUsed = std::chrono::milliseconds(proc.cputime);
			break;
		}
	}
	if (close_process_iterator(&it) != 0) exit(1);
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
    auto ret      = GetRunningProcesses();
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
	filter.pid = 0;
	filter.include_children = 0;
	init_process_iterator(&it, &filter);
	while (get_next_process(&it, &proc) != -1)
	{
		if (strncmp(proc.command, name.c_str(), name.length())==0 && kill(pid,SIGCONT)==0) {
			pid = proc.pid;
			break;
		}
	}
	if (close_process_iterator(&it) != 0) exit(1);
	if (pid >= 0) {
		return true;
	}
	else {
		return false;
	}
}
bool ProcessV2::IsProcessAlive(int pid)
{
    if (pid < 0)
        return false;

    if (kill(pid,0) != 0) 
        return false;

    return true;
}
bool ProcessV2::SuspendPid(int pid)
{
    if(kill(pid, SIGSTOP) == 0)
        return true;
    return false;
}
bool ProcessV2::ResumePid(int pid)
{
    if(kill(pid, SIGCONT) == 0)
        return true;
    return false;
}

}; // namespace zzj
