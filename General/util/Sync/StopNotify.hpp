#pragma once
#include <mutex>
namespace zzj
{
class StopNotify
{
  public:
    struct SyncParam
    {
        std::mutex mutex;
        std::condition_variable threadCondition;
        bool requestStop = false;
    };
    enum class ControlStatus : int
    {
        RequestStop,
        Timeout,
        Error
    };

    StopNotify(SyncParam& param) : m_param(param)
    {
    }

    void NotifyStop()
    {
        std::unique_lock<std::mutex> lck(m_param.mutex);
        m_param.requestStop = true;
        m_param.threadCondition.notify_all();
    }
    ControlStatus CheckForStop(int seconds)
    {
        return CheckForStop(std::chrono::milliseconds(1000*seconds));
    }
    ControlStatus CheckForStop(std::chrono::milliseconds ms)
    {
        std::unique_lock<std::mutex> lck(m_param.mutex);
        bool res = m_param.threadCondition.wait_for(lck, std::chrono::milliseconds(ms),
                                                    [this]() { return this->m_param.requestStop; });
        return res == false ? ControlStatus::Timeout : ControlStatus::RequestStop;
    }

  protected:
    SyncParam& m_param;
};
}; // namespace zzj

