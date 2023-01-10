#include "ProcessLimit.h"
#include "../SPDLogHelper.h"
#include <General/util/Process/Process.h>
#include "ProcessInfoStatistic.h"
#include "ProcessLimitParameter.hpp"
#include <ctime>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>
#include <tuple>

using namespace zzj;

ProcessLimitAgentInterface::ProcessLimitAgentInterface()
{
}

void ProcessLimitAgentInterface::SetInterface(LimitInerface *limitInterface)
{
    m_interface = limitInterface;
}

void ProcessLimitAgentInterface::SetParam(std::shared_ptr<ProcessLimitParameters> param)
{
    m_params = param;
}

void ProcessLimitAgentInterface::Run()
{
    if (!m_params)
        return;

    auto timePointPre =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
    std::map<int, std::tuple<ProcessV2::StatisticTimePoint, ProcessV2::StatisticCycle>> pidToStatictic;
    double workingRate = -1;
    auto timeSlot      = std::chrono::milliseconds(m_params->GetTimeSlot());

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

        if (!m_params->IsWorkTime())
        {
            std::this_thread::sleep_for(timeSlot);
            continue;
        }

        auto processInfoMap = ProcessInfoStatistic::CreateInstance()->GetProcessInfo(m_params->GetResourceName());
        auto allInfo        = processInfoMap.find("ALL");
        if (allInfo == processInfoMap.end())
            continue;

        double limit = m_params->GetCpuLimit();
        std::chrono::milliseconds workTime;
        std::chrono::milliseconds sleepTime;

        if (!allInfo->second.m_cpu.has_value() || !allInfo->second.m_memory.has_value())
            continue;

        double pcpu    = allInfo->second.m_cpu.value();
        int64_t memory = allInfo->second.m_memory.value();

        if (m_interface)
        {
            if (int(pcpu * 10000) == int(limit * 10000) || pcpu > 12)
                m_interface->LimitReportEvent({});
            else
                m_interface->LimitReportEvent(processInfoMap);
        }
        // std::cout << "worktime: " << workTime.count() << std::endl;
        // std::cout << "sleeptime: " << sleepTime.count() << std::endl;
        // std::cout << "working rate: " << workingRate << std::endl;

        if (pcpu < 0)
        {
            // it's the 1st cycle, initialize workingrate
            pcpu        = limit;
            workingRate = limit;
            workTime    = std::chrono::milliseconds(int64_t(timeSlot.count() * limit));
        }
        else
        {
            if ((std::int32_t)workingRate== -1)
                workingRate = limit;

            auto tmp    = workingRate * (limit / pcpu);
            workingRate = tmp < 1 ? tmp : 1;
            workTime    = std::chrono::milliseconds(int64_t(timeSlot.count() * workingRate));
        }
        sleepTime = timeSlot - workTime;

        m_processIdSet = ProcessInfoStatistic::CreateInstance()->GetProcessId(m_params->GetResourceName());
        if (m_processIdSet.empty())
        {
            std::this_thread::sleep_for(timeSlot);
            continue;
        }
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
    for (auto &pid : m_processIdSet)
        ProcessV2::SuspendPid(pid);
    return 0;
}
int ProcessLimitAgentInterface::ResumeAll()
{
    for (auto &pid : m_processIdSet)
        ProcessV2::ResumePid(pid);
    return 0;
}

LimitInerface::LimitInerface()
{
    m_slot       = 0;
    m_reportSlot = 0;
}

LimitInerface::~LimitInerface()
{
}
