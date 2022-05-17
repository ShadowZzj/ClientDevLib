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
    /**
     * @brief as name
     *
     */
    std::optional<double> cpuPercentInTaskManager;
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
    std::optional<ProcessLimitParameters> processLimitParameters;
    std::atomic<bool> isStop = false;
    std::atomic<WorkingMode> workingMode = WorkingMode::Limit;
    bool constantly;
};
}; // namespace zzj
