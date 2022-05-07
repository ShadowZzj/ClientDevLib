#include "ProcessLimit.h"
#include "Process.h"
#include <map>
#include <tuple>
#include <iostream>
using namespace zzj;

ProcessLimitAgentInterface::ProcessLimitAgentInterface(int pid) : ProcessLimitAgentInterface(std::set<int>{pid})
{
}
ProcessLimitAgentInterface::ProcessLimitAgentInterface(const std::string &processName, bool constantly)
    : ProcessLimitAgentInterface(std::set<std::string>{processName})
{
}
ProcessLimitAgentInterface::ProcessLimitAgentInterface(const std::set<int> &pids)
{
    this->pids = pids;
    constantly = false;
}
ProcessLimitAgentInterface::ProcessLimitAgentInterface(const std::set<std::string> processNames,
                                                       bool constantly)
{
    this->processNames = processNames;
    constantly         = constantly;
}

void ProcessLimitAgentInterface::SetLimit(const ProcessLimitParameters &params)
{
    processLimitParameters = params;
}
void ProcessLimitAgentInterface::RemoveDeadPid()
{
    auto tmp = pids;
    for (auto &pid : tmp)
        if (!ProcessV2::IsProcessAlive(pid))
            pids.erase(pid);
}

void ProcessLimitAgentInterface::RefreshPids()
{
    if (!processNames.empty())
    {
        for (auto &processName : processNames)
        {
            int pid = ProcessV2::GetProcessIdByName(processName);
            if (pid > 0)
                pids.insert(pid);
        }
    }
    RemoveDeadPid();
}
void ProcessLimitAgentInterface::RefreshStatistic(
    std::chrono::milliseconds &preTimePoint,
    std::map<int, std::tuple<ProcessV2::StatisticTimePoint, ProcessV2::StatisticCycle>> &arg)
{
    static const auto processor_count = std::thread::hardware_concurrency();
    auto nowTimePoint =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
    auto deltaTime = nowTimePoint - preTimePoint;
    if (deltaTime < std::chrono::milliseconds(20))
        return;

    const double alfa = 0.08;
    for (auto &pid : pids)
    {
        if (arg.find(pid) == arg.end())
            arg[pid] = {};

        auto &preStatistic          = arg[pid];
        auto &preStatisticTimePoint = std::get<0>(preStatistic);
        auto &preStatisticCycle     = std::get<1>(preStatistic);

        if (!preStatisticTimePoint.cpuUsed)
        {
            ProcessV2 process(pid);
            process.GetStatistic(preStatisticTimePoint);
        }
        else
        {
            ProcessV2::StatisticTimePoint nowStatisticTimePoint;
            ProcessV2 process(pid);
            process.GetStatistic(nowStatisticTimePoint);
            if (!preStatisticTimePoint.cpuUsed || !nowStatisticTimePoint.cpuUsed)
                continue;
            
            #ifdef __WIN32
            double directCpuPercentage =
                (double)(nowStatisticTimePoint.cpuUsed.value() - preStatisticTimePoint.cpuUsed.value()).count() /
                (deltaTime.count() * processor_count);
            #else
            double directCpuPercentage =
                (double)(nowStatisticTimePoint.cpuUsed.value() - preStatisticTimePoint.cpuUsed.value()).count() /
                (deltaTime.count());
            #endif
            if (!preStatisticCycle.cpuPercentage)
                preStatisticCycle.cpuPercentage = directCpuPercentage;
            else
                preStatisticCycle.cpuPercentage =
                    (1.0 - alfa) * preStatisticCycle.cpuPercentage.value() + alfa * directCpuPercentage;

            preStatisticTimePoint = nowStatisticTimePoint;
        }
    }
    preTimePoint = nowTimePoint;
    return;
}

void ProcessLimitAgentInterface::Run()
{
    if (!processLimitParameters)
        return;

    auto timePointPre =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
    RefreshPids();
    std::map<int, std::tuple<ProcessV2::StatisticTimePoint, ProcessV2::StatisticCycle>> pidToStatictic;
    const std::chrono::milliseconds timeSlot(100);
    double workingRate = -1;

    while (1)
    {
        if (isStop.load())
        {
            ResumeAll();
            break;
        }

        auto tmp = pids;
        if (constantly)
            RefreshPids();
        else
        {
            RemoveDeadPid();
            if (pids.empty())
                break;
        }
        if (pids != tmp)
        {
            timePointPre = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch());
            pidToStatictic.clear();
        }

        RefreshStatistic(timePointPre, pidToStatictic);

        double pcpu        = -1;
        for (auto &[pid, tup] : pidToStatictic)
        {
            ProcessV2::StatisticCycle &staticCycle = std::get<1>(tup);
            if (!staticCycle.cpuPercentage)
                continue;
            if (pcpu < 0)
                pcpu = 0;
            pcpu += staticCycle.cpuPercentage.value();
        }

        if (!processLimitParameters->cpuPercentInTaskManager)
            break;

        double limit = processLimitParameters->cpuPercentInTaskManager.value();
        std::chrono::milliseconds workTime;
        std::chrono::milliseconds sleepTime;
        if (pcpu < 0)
        {
            // it's the 1st cycle, initialize workingrate
            pcpu        = limit;
            workingRate = limit;
            workTime    = std::chrono::milliseconds(int64_t(timeSlot.count() * limit));
        }
        else
        {
            // adjust workingrate
            auto tmp    = workingRate * (limit / pcpu);
            workingRate = tmp < 1 ? tmp : 1;
            workTime    = std::chrono::milliseconds(int64_t(timeSlot.count() * workingRate));
        }
        sleepTime = timeSlot - workTime;

        std::cout << "worktime: " << workTime.count() << std::endl;
        std::cout << "sleeptime: " << sleepTime.count() << std::endl;
        std::cout << "working rate: " << workingRate<< std::endl;
        ResumeAll();
        std::this_thread::sleep_for(workTime);
        SuspendAll();
        std::this_thread::sleep_for(sleepTime);
    }
}
void ProcessLimitAgentInterface::Stop()
{
    isStop.store(true);
}

int ProcessLimitAgentInterface::SuspendAll()
{
    for (auto &pid : pids)
        ProcessV2::SuspendPid(pid);
    return 0;
}
int ProcessLimitAgentInterface::ResumeAll()
{
    for (auto &pid : pids)
        ProcessV2::ResumePid(pid);
    return 0;
}