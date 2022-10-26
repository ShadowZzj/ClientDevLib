#ifndef PROCESSINFOSTATISTIC_H
#define PROCESSINFOSTATISTIC_H

#include "../Process.h"
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <set>

class ProcessLimitParameters;

class ProcessstatisticParam
{
  public:
    ProcessstatisticParam()
    {
        m_cpu = 0;
        m_memory = 0;
        m_num    = 0;
    }
    ProcessstatisticParam(double m_cpu, int64_t m_memory) : m_cpu(m_cpu), m_memory(m_memory)
    {
    }
    ~ProcessstatisticParam()
    {
    }

  public:
    std::optional<double> m_cpu;
    std::optional<int64_t> m_memory;
    std::optional<int32_t> m_num;
};

class ProcessInfoStatistic
{
  public:
    ~ProcessInfoStatistic();
    static ProcessInfoStatistic *CreateInstance()
    {
        static ProcessInfoStatistic *PISInstance = new ProcessInfoStatistic();
        return PISInstance;
    }

    void SetParam(std::shared_ptr<ProcessLimitParameters> param);
    std::pair<std::int32_t, std::string> Start();
    std::map<std::string, ProcessstatisticParam> GetProcessInfo(std::string resourceName);
    std::set<std::int32_t> GetProcessId(std::string resourceName);

    void Run();
    void Stop();

  protected:
  private:
    ProcessInfoStatistic()
    {
        m_timeSlot     = 0;
        m_statisticSum = 0;
        m_isStop       = false;
        m_thread       = nullptr;
    }
    std::mutex m_mutex;
    std::vector<std::shared_ptr<ProcessLimitParameters>> m_processLimitParametersList;
    std::map<std::string, zzj::ProcessV2> m_processNamePidMap;
    std::map<std::string, zzj::ProcessV2> m_oldProcessNamePidMap;

    std::map<std::string, ProcessstatisticParam> m_processStatisticMap;
    std::map<std::string, std::set<std::int32_t>> m_processIdListMap;

    std::int32_t m_statisticSum;

    std::int32_t m_timeSlot;
    std::atomic<bool> m_isStop;
    std::set<std::string> m_processNameList;
    std::unique_ptr<std::thread> m_thread;
};

#endif