#include "ProcessInfoStatistic.h"
#include <General/util/Process/Process.h>
#include <iostream>
#include <spdlog/spdlog.h>
#include <thread>
namespace zzj
{

void ProcessInfoStatistic::SetMonitorProcessSet(std::set<std::string> processNames)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_monitorProcessSet = processNames;
}

int ProcessInfoStatistic::Start()
{
    if (nullptr != m_thread)
        return -1;

    m_isStop = false;
    std::thread tempThread(&ProcessInfoStatistic::Run, this);
    m_thread = std::make_unique<std::thread>(std::move(tempThread));
    return 0;
}

void ProcessInfoStatistic::CalculateNotExistProcessCpuPercentage(std::vector<ProcessV2 *> &notExistBeforeProcessObjects,
                                                                 std::chrono::steady_clock::time_point preTime)
{
    if (notExistBeforeProcessObjects.empty())
        return;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    auto nowTime = std::chrono::steady_clock::now();

    auto nowProcessObjects = GetCurrentProcessObjects(m_monitorProcessSet);
    nowProcessObjects.erase(
        std::remove_if(nowProcessObjects.begin(), nowProcessObjects.end(),
                       [&notExistBeforeProcessObjects](const ProcessV2 &processObject) {
                           return std::find_if(notExistBeforeProcessObjects.begin(), notExistBeforeProcessObjects.end(),
                                               [&processObject](const ProcessV2 *processObject2) {
                                                   return processObject.pid == processObject2->pid &&
                                                          processObject.processName == processObject2->processName;
                                               }) == notExistBeforeProcessObjects.end();
                       }),
        nowProcessObjects.end());

    for (auto &nowProcessObject : nowProcessObjects)
    {
        auto preProcessObjectIter =
            std::find_if(notExistBeforeProcessObjects.begin(), notExistBeforeProcessObjects.end(),
                         [&nowProcessObject](const ProcessV2 *processObject) {
                             return processObject->pid == nowProcessObject.pid &&
                                    processObject->processName == nowProcessObject.processName;
                         });
        auto deltaMilliSeconds = std::chrono::duration_cast<std::chrono::milliseconds>(nowTime - preTime).count();
        CalculateCpuPercentage(**preProcessObjectIter, nowProcessObject, std::chrono::milliseconds(deltaMilliSeconds));
        **preProcessObjectIter = nowProcessObject;
    }
}
void ProcessInfoStatistic::CalculateCpuPercentage(const ProcessV2 &lastProcess, ProcessV2 &nowProcess,
                                                  std::chrono::milliseconds deltaTimeMilliSeconds)
{
    const double alfa       = 0.08;
    auto lastProcessCpuUsed = lastProcess.statisticTimePoint.cpuUsed.value();
    auto nowProcessCpuUsed  = nowProcess.statisticTimePoint.cpuUsed.value();
#ifdef _WIN32
    const auto processor_count = std::thread::hardware_concurrency();
    double directCpuPercentage = (double)(nowProcessCpuUsed - lastProcessCpuUsed).count() /
                                 (double)(deltaTimeMilliSeconds.count() * processor_count);
#else
    double directCpuPercentage =
        (double)(nowProcessCpuUsed - lastProcessCpuUsed).count() / (double)(deltaTimeMilliSeconds.count());
    std::cout<<"now - last cpuUsed: "<<(nowProcessCpuUsed - lastProcessCpuUsed).count()<<std::endl;
    std::cout<<"deltaTimeCount: "<<deltaTimeMilliSeconds.count()<<std::endl;
#endif

    if (lastProcess.statisticCycle.cpuPercentage.has_value())
    {
        nowProcess.statisticCycle.cpuPercentage =
            alfa * directCpuPercentage + (1 - alfa) * lastProcess.statisticCycle.cpuPercentage.value();
    }
    else
    {
        nowProcess.statisticCycle.cpuPercentage = directCpuPercentage;
    }
}
std::vector<ProcessV2> ProcessInfoStatistic::GetCurrentProcessObjects(const std::set<std::string> &processNames)
{
    std::vector<ProcessV2> ret;
    try
    {
        zzj::ProcessV2::Snapshot snapshot;
        for (auto &processName : processNames)
            for (auto &processObject : snapshot.GetProcesses(processName))
                ret.push_back(processObject);
    }
    catch (const std::exception &e)
    {
        spdlog::error("Exeption in ProcessInfoStatistic: {}", e.what());
    }
    catch (...)
    {
        spdlog::error("Exeption in ProcessInfoStatistic");
    }
    return ret;
}
std::vector<ProcessV2> ProcessInfoStatistic::GetProcessStatistics()
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_lastProcessObjects;
}
std::optional<ProcessV2> ProcessInfoStatistic::GetProcessStatistic(const int pid, const std::string &processName)
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    auto iter = std::find_if(m_lastProcessObjects.begin(), m_lastProcessObjects.end(),
                             [&pid, &processName](const ProcessV2 &processObject) {
                                 return processObject.pid == pid && processObject.processName == processName;
                             });
    if (iter == m_lastProcessObjects.end())
        return std::nullopt;
    return *iter;
}

void ProcessInfoStatistic::Run()
{
    const double alfa = 0.08;

    do
    {
        if (m_isStop.load())
            break;

        // 进行写锁定
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        std::vector<ProcessV2> nowProcessObjects = GetCurrentProcessObjects(m_monitorProcessSet);
        auto nowTime                             = std::chrono::steady_clock::now();
        auto deltaTimeMilliSeconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(nowTime - m_lastTime).count();
        std::vector<ProcessV2 *> notExistBeforeProcessObjects;
        for (auto &nowProcessObject : nowProcessObjects)
        {
            auto lastProcessObjectIter =
                std::find_if(m_lastProcessObjects.begin(), m_lastProcessObjects.end(),
                             [&nowProcessObject](const ProcessV2 &processObject) {
                                 return processObject.pid == nowProcessObject.pid &&
                                        processObject.processName == nowProcessObject.processName;
                             });
            // 对于之前已经存在的进程，结合之前的cpu使用率进行线性计算cpu使用率
            if (lastProcessObjectIter != m_lastProcessObjects.end())
                CalculateCpuPercentage(*lastProcessObjectIter, nowProcessObject,
                                       std::chrono::milliseconds(deltaTimeMilliSeconds));
            else
                notExistBeforeProcessObjects.push_back(&nowProcessObject);
        }

        // 对于之前不存在的进程，直接计算cpu使用率
        if (0 != notExistBeforeProcessObjects.size())
            CalculateNotExistProcessCpuPercentage(notExistBeforeProcessObjects, nowTime);
        for (auto &nowProcessObject : nowProcessObjects)
        {
            std::cout << nowProcessObject.processName << " " << nowProcessObject.pid << " "
                      << nowProcessObject.statisticCycle.cpuPercentage.value() << " "
                      << nowProcessObject.statisticTimePoint.memoryUsed.value() / 1024 << std::endl;
        }
        m_lastProcessObjects = nowProcessObjects;
        m_lastTime           = nowTime;

    } while (std::this_thread::sleep_for(std::chrono::milliseconds(250)), true);
}
void ProcessInfoStatistic::Stop()
{
    m_isStop.store(true);
    if (m_thread)
    {
        m_thread->join();
        m_thread.reset();
    }

    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_lastProcessObjects.clear();
    m_lastTime = std::chrono::steady_clock::now();
}

} // namespace zzj
