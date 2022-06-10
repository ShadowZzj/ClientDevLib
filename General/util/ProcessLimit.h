#ifndef PROCESSLIMIT
#define PROCESSLIMIT

#include "Process.h"
#include "Sync.hpp"
#include <atomic>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>

namespace zzj
{
/**
 * @brief processlimit parameters, it is used with ProcessLimitAgentInterface
 *
 *
 */

class ProcessLimitParameters
{
  public:
    bool IsWorkTime();
    void SetWorkTime(std::set<std::pair<std::string, std::string>> worktimeVct);
    void SetTimeSlot(std::int32_t time);
    void SetReportTimeSLot(std::int32_t time);
    void SetResourceName(std::string name);
  public:
    /**
     * @brief as name
     *
     */
    std::optional<double> cpuPercentInTaskManager; 
    std::set<std::pair<std::int32_t, std::int32_t>> worktime;
    std::int32_t timeSlot;
    std::int32_t reportTimeSlot;
    std::string resourceName;

};

class LimitInerface
{
  public:
    LimitInerface();
    ~LimitInerface();
    virtual void LimitReportEvent(double workingRate, std::map<std::string, double>) = 0;

    void SetParameters(ProcessLimitParameters* param)
    {
        m_percent      = param->cpuPercentInTaskManager.value();
        m_slot = param->timeSlot;
        m_reportSlot   = param->reportTimeSlot;
        m_resourceName = param->resourceName;
    }


  public:
    double m_percent;
    std::int32_t m_slot;
    std::int32_t m_reportSlot;
    std::string m_resourceName;
};


class ProcessLimitAgentInterface
{
  public:
    enum class WorkingMode
    {
        Monitor,
        Limit
    };
    ProcessLimitAgentInterface(int pid);
    ProcessLimitAgentInterface(const std::string &processName, bool constantly = false);
    ProcessLimitAgentInterface(const std::set<int> &pids);
    ProcessLimitAgentInterface(const std::set<std::string> processNames, bool constantly = false);
    
    void SetLimit(const ProcessLimitParameters &params);
    void SetWorkingMode(const WorkingMode &workingMode);
    void SetInterface(LimitInerface *limitInterface);
    // Sync
    void Run();
    void Stop();

  private:
    void RefreshStatistic(std::chrono::milliseconds &preTimePoint,
                          std::map<int, std::tuple<ProcessV2::StatisticTimePoint, ProcessV2::StatisticCycle>> &arg);
    void RefreshPids();
    int SuspendAll();
    int ResumeAll();
    void RemoveDeadPid();
    
    int DoNoting()
    {
        return 0;
    };
    std::set<int> pids;
    std::set<std::string> processNames;
    std::map<int, std::string> processMap;
    std::optional<ProcessLimitParameters> processLimitParameters;
    std::atomic<bool> isStop = false;
    std::atomic<WorkingMode> workingMode = WorkingMode::Limit;
    LimitInerface *m_interface; 
    bool constantly;
};
}; // namespace zzj
#endif
