#ifndef _G_PROCESS_H_
#define _G_PROCESS_H_
#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <set>
int IsProcessWithIdRunning(const char *id);
int AddCrashHandler();

namespace zzj
{
class ThreadV2;
/**
 * @brief new process class with object oriented
 *
 */
class ProcessV2
{

  public:
    /**
     * @brief cycle statistic such as cpu precentage
     *
     */
    class StatisticCycle
    {
      public:
        /**
         * @brief cpu used cycle. percentage
         *
         */
        std::optional<double> cpuPercentage;
    };
    /**
     * @brief timepoint statistic  like cputime since the process started
     *
     */
    class StatisticTimePoint
    {
      public:
        /**
         * @brief cpu used since start. milliseconds
         *
         */
        std::optional<std::chrono::milliseconds> cpuUsed;

        /**
         * @brief memory used since start. Byte
         *
         */
        std::optional<int64_t> memoryUsed;
    };

  public:
    ProcessV2();
    /**
     * @brief Construct a new ProcessV2 object by pid
     *
     * @param pid
     */
    ProcessV2(int pid);
    /**
     * @brief Construct a new ProcessV2 object by process name, it will select the first process found
     *
     * @param name
     */
    ProcessV2(const std::string &name);

    /**
     * @brief Get Pid
     *
     * @return int
     */
    int GetPid();
    /**
     * @brief if process alive
     *
     * @return true
     * @return false
     */

    bool IsProcessAlive();
    /**
     * @brief Get the Statictic object
     *
     * @param statictic
     * @return 0 if succeed
     */
    int GetStatistic(StatisticTimePoint &statictic);
    /**
     * @brief Get the Process Id By Name object
     *
     * @param name
     * @return -1 if not found
     */
    static int GetProcessIdByName(const std::string &name);
    /**
     * @brief Get the Process Threads
     *
     * @return std::vector<ThreadV2>
     */
    static std::string GetProcessNameById(const int &pid);

    std::vector<ThreadV2> GetProcessThreads();
    /**
     * @brief Get the Process Threads object
     *
     * @param pid
     * @return std::vector<ThreadV2>
     */
    static std::vector<ThreadV2> GetProcessThreadsCache(int pid);

    static std::vector<ThreadV2> GetProcessThreads(int pid);
    /**
     * @brief Get the Running Processes object
     *
     * @return std::vector<ProcessV2>
     */
    static std::vector<ProcessV2> GetRunningProcesses();
    /**
     * @brief if process alive
     *
     * @param processName
     * @return true
     * @return false
     */
    static bool IsProcessAlive(const std::string &name);
    /**
     * @brief if process alive
     *
     * @param pid
     * @return true
     * @return false
     */
    static bool IsProcessAlive(int pid);
    /**
     * @brief Suspend process by pid
     *
     * @param pid
     * @return true
     * @return false
     */
    static bool SuspendPid(int pid);
    /**
     * @brief Resume process by pid
     *
     * @param pid
     * @return true
     * @return false
     */
    static bool ResumePid(int pid);

    static std::vector<std::pair<int, std::string>> GetProcessInfo(const std::set<std::string> proccessList,
                                                                   std::map<std::string, zzj::ProcessV2> &processes);

    int pid;
    std::string processName;
    StatisticTimePoint statisticTimePoint;
    StatisticCycle statisticCycle;


};
}; // namespace zzj
#endif