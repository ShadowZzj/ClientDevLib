#pragma once
#include <General/util/Process/Process.h>
#include <chrono>
#include <map>
#include <memory>
#include <shared_mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace zzj
{
class ProcessInfoStatistic
{
  public:
    void ProcessInfoStatistic::SetMonitorProcessSet(std::set<std::string> processNames);

    int Start();
    void Stop();
    void Run();
    std::vector<ProcessV2> GetProcessStatistics();
    std::optional<ProcessV2> GetProcessStatistic(const int pid, const std::string &processName);
  private:
    void CalculateNotExistProcessCpuPercentage(std::vector<ProcessV2*> &notExistBeforeProcessObjects,std::chrono::steady_clock::time_point preTime);
    void CalculateCpuPercentage(const ProcessV2 &lastProcess, ProcessV2 &nowProcess,
                                std::chrono::milliseconds deltaTimeMilliSeconds);
    std::vector<ProcessV2> GetCurrentProcessObjects(const std::set<std::string> &processNames);
    std::shared_mutex m_mutex;
    std::set<std::string> m_monitorProcessSet;
    std::unique_ptr<std::thread> m_thread;
    std::atomic<bool> m_isStop;

    std::vector<zzj::ProcessV2> m_lastProcessObjects;
    std::chrono::steady_clock::time_point m_lastTime;
};

} // namespace zzj