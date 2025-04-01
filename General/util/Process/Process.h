#pragma once
#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include "MacOS/util/ProcessIterator/process_iterator.h"
#include "MacOS/util/ProcessIterator/process_iterator_apple.h"
#endif

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
    class Snapshot
    {
       public:
        Snapshot();
        ~Snapshot();
        std::vector<ProcessV2> GetProcesses(const std::string &processName);

       private:
#ifdef _WIN32
        HANDLE snapshot;
#else
        process_iterator i;
        process_filter filter = {0};
#endif
    };
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

    class Module
    {
       public:
        class Section
        {
           public:
            std::string name;
            std::uintptr_t base;
            std::uintptr_t size;
            bool readable;
            bool writable;
            bool executable;
        };
        std::string name;
        std::string path;
        std::uintptr_t base;
        std::uintptr_t size;
        std::vector<Section> sections;

        std::vector<Section> GetSections(uint32_t pid);
    };
    using EnviromentVariable = std::map<std::string, std::string>;

   public:
    ProcessV2();
    /**
     * @brief Construct a new ProcessV2 object by pid
     *
     * @param pid
     */
    ProcessV2(int pid);
    /**
     * @brief Construct a new ProcessV2 object by process name, it will select the first process
     * found
     *
     * @param name
     */
    ProcessV2(const std::string &name);
    std::string GetExecutableFilePath();
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

    int GetModules(std::vector<Module> &modules);
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

    static std::vector<std::pair<int, std::string>> GetProcessInfo(
        const std::set<std::string> proccessList, std::map<std::string, zzj::ProcessV2> &processes);

    int pid;
    std::string processName;
    StatisticTimePoint statisticTimePoint;
    StatisticCycle statisticCycle;
};

class CommandHelper
{
   public:
    struct CommandResult
    {
        int exitCode;
        std::string stdoutStr;
        std::string stderrStr;
    };
    static CommandResult ExecuteCommand(const std::string &command);
    static CommandResult ExecuteCurrentUserCommand(const std::string &command);
    static CommandResult ExecuteRootCommand(const std::string &command);
};
};  // namespace zzj
