#include "Sync.hpp"
#include <atomic>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <map>
#include "Process.h"
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
    std::optional<double> singleCoreCpuPercent;
};
class ProcessLimitAgentInterface
{
  public:
    ProcessLimitAgentInterface(int pid);
    ProcessLimitAgentInterface(const std::string &processName, bool constantly = false);
    ProcessLimitAgentInterface(const std::set<int> &pids);
    ProcessLimitAgentInterface(const std::set<std::string> processNames, bool constantly = false);

    void SetLimit(const ProcessLimitParameters &params);
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


    std::set<int> pids;
    std::set<std::string> processNames;
    std::optional<ProcessLimitParameters> processLimitParameters;
    std::atomic<bool> isStop = false;
    bool constantly;
};
}; // namespace zzj