#ifndef _G_PROCESS_H_
#define _G_PROCESS_H_
#include <chrono>
#include <optional>
#include <string>
int IsProcessWithIdRunning(const char *id);
int AddCrashHandler();

namespace zzj
{

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
};
}; // namespace zzj
#endif