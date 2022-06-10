#include "ProcessLimit.h"
#include "Process.h"
#include <functional>
#include <iostream>
#include <map>
#include <tuple>
#include <thread>
#include <iostream> 
#include <sstream> 
#include <iomanip>   
#include <ctime>  

using namespace zzj;

ProcessLimitAgentInterface::ProcessLimitAgentInterface(int pid) : ProcessLimitAgentInterface(std::set<int>{pid})
{
}
ProcessLimitAgentInterface::ProcessLimitAgentInterface(const std::string &processName, bool constantly)
    : ProcessLimitAgentInterface(std::set<std::string>{processName}, constantly)
{
}
ProcessLimitAgentInterface::ProcessLimitAgentInterface(const std::set<int> &pids)
{
    this->pids = pids;
    constantly = false;
}
ProcessLimitAgentInterface::ProcessLimitAgentInterface(const std::set<std::string> processNames, bool constantly)
{
    this->processNames = processNames;
    this->constantly   = constantly;
}

void ProcessLimitAgentInterface::SetLimit(const ProcessLimitParameters &params)
{
    processLimitParameters = params;
}
void zzj::ProcessLimitAgentInterface::SetWorkingMode(const WorkingMode &workingMode)
{
    this->workingMode.store(workingMode);
}

void ProcessLimitAgentInterface::SetInterface(LimitInerface *limitInterface)
{
    m_interface = limitInterface;
}

void ProcessLimitAgentInterface::RemoveDeadPid()
{
    auto tmp = pids;
    for (auto &pid : tmp)
        if (!ProcessV2::IsProcessAlive(pid))
        {
            pids.erase(pid);
            processMap.erase(pid);
        }
}

void ProcessLimitAgentInterface::RefreshPids()
{
    if (!processNames.empty())
    {
        for (auto &processName : processNames)
        {
            int pid = ProcessV2::GetProcessIdByName(processName);
            if (pid > 0)
            {
                pids.insert(pid);
                processMap.emplace(pid, processName);
            }   
        }
    }
    else if (!pids.empty())
    {
        for (auto &pid : pids)
        {
            auto name = ProcessV2::GetProcessNameById(pid);
            if (name != "")
            {
                pids.insert(pid);
                processMap.emplace(pid, name);
            }
        }
    }

    RemoveDeadPid();
}
void ProcessLimitAgentInterface::RefreshStatistic(
    std::chrono::milliseconds &preTimePoint,
    std::map<int, std::tuple<ProcessV2::StatisticTimePoint, ProcessV2::StatisticCycle>> &arg)
{
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

#ifdef _WIN32
            static const auto processor_count = std::thread::hardware_concurrency();
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
    double workingRate = -1;
    auto timeSlot      = std::chrono::milliseconds(processLimitParameters->timeSlot);

    std::function<int(void)> resumeFunc;
    std::function<int(void)> suspendFunc;

    if (workingMode.load() == WorkingMode::Limit)
    {
        resumeFunc  = [this]() { return this->ResumeAll(); };
        suspendFunc = [this]() { return this->SuspendAll(); };
    }
    else
    {
        resumeFunc = suspendFunc = [this]() { return this->DoNoting(); };
    }

    while (1)
    { 
        if (isStop.load())
        {
            if (m_interface)
                delete m_interface;

            resumeFunc();
            break;
        }

        if (!processLimitParameters->IsWorkTime())
        {
            std::this_thread::sleep_for(timeSlot);
            continue;
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

        double pcpu = -1;
        std::map<std::string, double> processCPUMap;
        for (auto &[pid, tup] : pidToStatictic)
        {
            ProcessV2::StatisticCycle &staticCycle = std::get<1>(tup);
            if (!staticCycle.cpuPercentage)
                continue;

            if (pcpu < 0)
                pcpu = 0;
            pcpu += staticCycle.cpuPercentage.value();

            if (processMap.find(pid) != processMap.end())
            {
                auto processName = processMap[pid];
                processCPUMap.emplace(processName, staticCycle.cpuPercentage.value());
            }

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

        if (m_interface)
            m_interface->LimitReportEvent(pcpu, processCPUMap);
        //std::cout << "worktime: " << workTime.count() << std::endl;
        //std::cout << "sleeptime: " << sleepTime.count() << std::endl;
        //std::cout << "working rate: " << workingRate << std::endl;

        resumeFunc();
        std::this_thread::sleep_for(workTime);
        suspendFunc();
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

bool ProcessLimitParameters::IsWorkTime()
{
    auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm *ptm = localtime(&tt);
    std::int32_t curruntTTime = ptm->tm_hour * 60 + ptm->tm_min;
    
    for (auto it : worktime)
    {
        if (it.first < curruntTTime && it.second > curruntTTime)
            return true;
    }
    return false;
}

void ProcessLimitParameters::SetWorkTime(std::set<std::pair<std::string, std::string>> worktimeVct)
{
    for (auto it : worktimeVct)
    {
        std::tm tm = {};
        std::stringstream ss(it.first);
        ss >> std::get_time(&tm, "%H:%M:%S");
        std::int32_t startTime = tm.tm_hour * 60 + tm.tm_min;

        std::tm tm1 = {};
        std::stringstream ss1(it.second);
        ss1 >> std::get_time(&tm1, "%H:%M:%S");
        std::int32_t stopTime = tm1.tm_hour * 60 + tm1.tm_min;


        worktime.emplace(std::make_pair(startTime, stopTime));
    }
}



void ProcessLimitParameters::SetTimeSlot(std::int32_t time)
{
    timeSlot = time;
}

void ProcessLimitParameters::SetResourceName(std::string name)
{
    resourceName = name;
}

void ProcessLimitParameters::SetReportTimeSLot(std::int32_t time)
{
    reportTimeSlot = time;
}

LimitInerface::LimitInerface()
{
    m_slot = 0;
    m_reportSlot = 0;
}

LimitInerface::~LimitInerface()
{
}
