#include "ProcessInfoStatistic.h"
#include <thread>
#include <General/util/Process/Process.h>
#include "ProcessLimit.h"
#include "ProcessLimitParameter.hpp"

ProcessInfoStatistic::~ProcessInfoStatistic()
{
}

void ProcessInfoStatistic::SetParam(std::shared_ptr<ProcessLimitParameters> param)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_processLimitParametersList.push_back(param);
}

std::pair<std::int32_t, std::string> ProcessInfoStatistic::Start()
{
    if (nullptr != m_thread)
        return std::make_pair(-1, "ProcessInfoStatistic is already running");

    if (m_processLimitParametersList.empty())
        return std::make_pair(-2, "No process limit parameters");

    // get smail  m_timeSlot
    for (auto &param : m_processLimitParametersList)
    {
        if (m_timeSlot == 0)
        {
            m_timeSlot = param->GetTimeSlot();
        }
        else
        {
            if (m_timeSlot > param->GetTimeSlot())
            {
                m_timeSlot = param->GetTimeSlot();
            }
        }
    }
    if (m_timeSlot == 0)
        return std::make_pair(-3, "No time slot");

    // get processNameList
    for (auto &param : m_processLimitParametersList)
    {
        for (auto &processName : param->GetProcessNameList())
        {
            m_processNameList.insert(processName);
        }
    }
    if (m_processNameList.empty())
        return std::make_pair(-4, "No process name list");

    m_isStop = false;
    std::thread tempThread(&ProcessInfoStatistic::Run, this);
    m_thread = std::make_unique<std::thread>(std::move(tempThread));
    if (!m_thread->joinable())
        return std::make_pair(-5, "Thread is not joinable");

    return std::make_pair(0, "success");
}

void ProcessInfoStatistic::Run()
{
    const double alfa = 0.08;

    while (true)
    {
        if (m_isStop.load())
            break;

        m_processNamePidMap.clear();
        auto getRet = zzj::ProcessV2::GetProcessInfo(m_processNameList, m_processNamePidMap);
        for (auto &retIt : getRet)
        {
            if (retIt.first != 0)
            {
                // std::cout << "GetProcessInfo failed, ret = " << retIt.first << ", msg = " << retIt.second <<
                // std::endl;
            }
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        m_processIdListMap.clear();
        for (auto &param : m_processLimitParametersList)
        {
            for (auto &it : param->GetProcessNameList())
            {
                auto oldIt = m_oldProcessNamePidMap.find(it);
                auto newIt = m_processNamePidMap.find(it);

                if (newIt == m_processNamePidMap.end())
                    continue;

                else if (oldIt == m_oldProcessNamePidMap.end() && newIt != m_processNamePidMap.end())
                {
                    m_oldProcessNamePidMap.insert(*newIt);
                    continue;
                }

                else if (oldIt != m_oldProcessNamePidMap.end() && newIt != m_processNamePidMap.end())
                {
                    {
                        m_processIdListMap[param->GetResourceName()].insert(newIt->second.pid);

                        if (newIt->second.pid != oldIt->second.pid)
                        {
                            oldIt->second = newIt->second;
                            continue;
                        }

                        if (!newIt->second.statisticTimePoint.cpuUsed || !oldIt->second.statisticTimePoint.cpuUsed)
                        {
                            oldIt->second = newIt->second;
                            continue;
                        }

#ifdef _WIN32
                        static const auto processor_count = std::thread::hardware_concurrency();
                        double directCpuPercentage        = (double)(newIt->second.statisticTimePoint.cpuUsed.value() -
                                                              oldIt->second.statisticTimePoint.cpuUsed.value())
                                                         .count() /
                                                     (double)(m_timeSlot * processor_count);
#else
                        double directCpuPercentage = (double)(newIt->second.statisticTimePoint.cpuUsed.value() -
                                                              oldIt->second.statisticTimePoint.cpuUsed.value())
                                                         .count() /
                                                     (double)(m_timeSlot);
#endif
                        auto processStatisticIt = m_processStatisticMap.find(it);
                        if (processStatisticIt == m_processStatisticMap.end())
                        {
                            m_processStatisticMap[it] = ProcessstatisticParam();

                            oldIt->second.statisticCycle.cpuPercentage = directCpuPercentage;

                            m_processStatisticMap[it].m_cpu     = oldIt->second.statisticCycle.cpuPercentage;
                            m_processStatisticMap[it].m_memory = newIt->second.statisticTimePoint.memoryUsed;
                            m_processStatisticMap[it].m_num     = 1;
                        }
                        else
                        {
                            oldIt->second.statisticCycle.cpuPercentage =
                                (1.0 - alfa) * oldIt->second.statisticCycle.cpuPercentage.value() +
                                alfa * directCpuPercentage;

                            m_processStatisticMap[it].m_cpu = m_processStatisticMap[it].m_cpu.value() +
                                                               oldIt->second.statisticCycle.cpuPercentage.value();

                            m_processStatisticMap[it].m_memory = m_processStatisticMap[it].m_memory.value() +
                                                                  newIt->second.statisticTimePoint.memoryUsed.value();
                            m_processStatisticMap[it].m_num = m_processStatisticMap[it].m_num.value() + 1;
                        }
                    }
                }
            }
        }

        for (auto &it : m_processNamePidMap)
        {
            auto oldIt = m_oldProcessNamePidMap.find(it.first);
            if (oldIt != m_oldProcessNamePidMap.end())
            {
                oldIt->second.statisticTimePoint = it.second.statisticTimePoint;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(m_timeSlot));
    }
}

void ProcessInfoStatistic::Stop()
{
    m_isStop.store(true);
    m_thread->join();

    m_thread.reset();
    m_processLimitParametersList = {};
    m_processNameList            = {};
    m_processNamePidMap          = {};
    m_oldProcessNamePidMap       = {};
    m_processIdListMap           = {};
    m_processStatisticMap        = {};
}

std::map<std::string, ProcessstatisticParam> ProcessInfoStatistic::GetProcessInfo(std::string resourceName)
{
    if (m_processStatisticMap.empty())
        return {};

    std::map<std::string, ProcessstatisticParam> tempMap;
    tempMap["ALL"] = ProcessstatisticParam();

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &param : m_processLimitParametersList)
    {
        if (param->GetResourceName() == resourceName)
        {
            for (auto &it : param->GetProcessNameList())
            {
                auto processStatisticIt = m_processStatisticMap.find(it);
                if (processStatisticIt != m_processStatisticMap.end())
                {
                    if (!processStatisticIt->second.m_cpu.has_value() ||
                        !processStatisticIt->second.m_memory.has_value())
                        continue;

                    auto tempCpu = processStatisticIt->second.m_cpu.value() / processStatisticIt->second.m_num.value();
                    auto tempMem =
                        processStatisticIt->second.m_memory.value() / processStatisticIt->second.m_num.value();

                    tempMap[processStatisticIt->first] = ProcessstatisticParam(tempCpu, tempMem);

                    tempMap["ALL"].m_cpu    = tempMap["ALL"].m_cpu.value() + tempCpu;
                    tempMap["ALL"].m_memory = tempMap["ALL"].m_memory.value() + tempMem;
                    m_processStatisticMap.erase(processStatisticIt);
                }
            }    
        }
    }
    return tempMap;
}

std::set<std::int32_t> ProcessInfoStatistic::GetProcessId(std::string resourceName)
{
    std::set<std::int32_t> tempSet;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_processIdListMap.find(resourceName);
    if (it != m_processIdListMap.end())
    {
        tempSet = it->second;
    }
    return tempSet;
}
