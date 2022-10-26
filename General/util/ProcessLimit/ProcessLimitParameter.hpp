#ifndef PROCESSLIMITPARAM
#define PROCESSLIMITPARAM

#include <atomic>
#include <chrono>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <tuple>

class ProcessLimitParameters
{
  public:
    ProcessLimitParameters()
    {
        m_cpuLimit       = 0;
        m_workingMode    = 0;
        m_timeSlot       = 0;
        m_reportTimeSlot = 0;
    }
    ~ProcessLimitParameters()
    {
    }

    bool IsWorkTime()
    {
        auto tt                   = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm *ptm              = localtime(&tt);
        std::int32_t curruntTTime = ptm->tm_hour * 60 + ptm->tm_min;

        for (auto it : m_worktimeVct)
        {
            if (it.first < curruntTTime && it.second > curruntTTime)
                return true;
        }
        return false;
    }

    void SetWorkTime(std::set<std::pair<std::string, std::string>> worktimeVct)
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

            m_worktimeVct.emplace(std::make_pair(startTime, stopTime));
        }
    }

    void SetProcessList(std::set<std::string> processList)
    {
        m_processList = processList;
    }

    void SetCpuLimit(double cpuLimit)
    {
        m_cpuLimit = cpuLimit;
    }

    void SetWorkingMode(std::int32_t workingMode)
    {
        m_workingMode = workingMode;
    }

    void SetTimeSlot(std::int32_t time)
    {
        m_timeSlot = time;
    }

    void SetReportTimeSLot(std::int32_t time)
    {
        m_reportTimeSlot = time;
    }

    void SetResourceName(std::string name)
    {
        m_resourceName = name;
    }

    std::set<std::pair<std::int32_t, std::int32_t>> GetWorkTime()
    {
        return m_worktimeVct;
    }
    std::set<std::string> GetProcessNameList()
    {
        return m_processList;
    }

    double GetCpuLimit()
    {
        return m_cpuLimit;
    }

    std::int32_t GetWorkingMode()
    {
        return m_workingMode;
    }
    std::int32_t GetTimeSlot()
    {
        return m_timeSlot;
    }
    std::int32_t GetReportTimeSLot()
    {
        return m_reportTimeSlot;
    }
    std::string GetResourceName()
    {
        return m_resourceName;
    }

  private:
    double m_cpuLimit;

    std::set<std::pair<std::int32_t, std::int32_t>> m_worktimeVct;
    std::set<std::string> m_processList;
    std::int32_t m_workingMode;
    std::int32_t m_timeSlot;
    std::int32_t m_reportTimeSlot;
    std::string m_resourceName;
};

#endif