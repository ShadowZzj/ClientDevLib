#ifndef _G_SYNC_H_
#define _G_SYNC_H_
#include <mutex>
namespace zzj
{
class StopSyncWrapper
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

    StopSyncWrapper()
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
        std::unique_lock<std::mutex> lck(m_param.mutex);
        bool res = m_param.threadCondition.wait_for(lck, std::chrono::seconds(seconds),
                                                    [this]() { return this->m_param.requestStop; });
        return res == false ? ControlStatus::Timeout : ControlStatus::RequestStop;
    }

  protected:
    SyncParam m_param;
};
}; // namespace zzj

#endif
