#ifndef _ETWHELPER_H_
#define _ETWHELPER_H_

#include <Windows/util/EventTrace/elevated_check.h>
#include <Windows/util/EventTrace/event_info.h>
#include <Windows/util/EventTrace/event_provider_list.h>
#include <Windows/util/EventTrace/event_trace.h>
#include <Windows/util/EventTrace/event_trace_error.h>
#include <Windows/util/EventTrace/event_trace_session.h>
#include <map>
#include <functional>
class ETWHelper
{
  public:
    ETWHelper();
    ~ETWHelper();
    struct SessionInfo
    {
        std::shared_ptr<event_tracing::event_trace_session> sessionImp;
        std::map<std::wstring, std::shared_ptr<event_tracing::event_trace>> sessionTraces;
    };

    bool CreateSession(const std::wstring &sessionName);
    bool RemoveSession(const std::wstring &sessionName);
    bool AddConsumer(const std::wstring &sessionName, const std::wstring &consumerName);
    bool RemoveConsumer(const std::wstring &sessionName, const std::wstring &consumerName);
    bool AddProvider(const std::wstring& sessionName, const std::wstring &guidName, const std::uint32_t &keyword);
    bool RemoveProvider(const std::wstring& sessionName, const std::wstring &guidName);
    bool SetProviderHandlerForConsumer(const std::wstring& sessionName, const std::wstring& consumerName,const std::wstring &providerName, std::uint32_t eventId, std::function<void(PEVENT_RECORD pRecord)> &&handler);
    bool SetProviderHandlerForConsumer(const std::wstring &sessionName, const std::wstring &consumerName,
                                       const std::wstring &providerName,
                                       std::function<void(PEVENT_RECORD pRecord)> &&handler);
    bool StartConsumerAsync(const std::wstring &sessionName,const std::wstring &consumerName);
    bool StopConsumer(const std::wstring &sessionName,const std::wstring &consumerName);

  private:
    std::map<std::wstring,SessionInfo> m_sessions;
    
};

#endif