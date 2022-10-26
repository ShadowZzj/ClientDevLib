#ifndef PROCESSLIMIT
#define PROCESSLIMIT

#include "../Process.h"
#include "ProcessLimitParameter.hpp"
#include "../Sync.hpp"
#include <atomic>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>

class ProcessstatisticParam;
class LimitInerface
{
  public:
    LimitInerface();
    virtual ~LimitInerface();
    virtual void LimitReportEvent(std::map<std::string, ProcessstatisticParam>) = 0;

    void SetParameters(ProcessLimitParameters *param)
    {
        m_percent      = param->GetCpuLimit();
        m_slot         = param->GetTimeSlot();
        m_reportSlot   = param->GetReportTimeSLot();
        m_resourceName = param->GetResourceName();
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
    ProcessLimitAgentInterface();
    void SetParam(std::shared_ptr<ProcessLimitParameters> param);
    void SetInterface(LimitInerface *limitInterface);
    // Sync
    void Run();
    void Stop();

  private:
    int SuspendAll();
    int ResumeAll();

    int DoNoting()
    {
        return 0;
    }
    std::shared_ptr<ProcessLimitParameters> m_params;
    std::atomic<bool> isStop             = false;
    std::atomic<WorkingMode> workingMode = WorkingMode::Monitor;
    LimitInerface *m_interface;
    bool constantly;
    std::set<std::int32_t> m_processIdSet;
};

#endif
