#include "ETWHelper.h"
#include <iostream>
namespace zzj
{
ETWHelper::ETWHelper() {}

ETWHelper::~ETWHelper() {}

bool ETWHelper::CreateSession(const std::wstring &sessionName)
{
    try
    {
        if (!event_tracing::is_running_elevated()) return false;
        if (m_sessions.find(sessionName) != m_sessions.end()) return false;

        auto session = std::make_shared<event_tracing::event_trace_session>(sessionName);
        SessionInfo info;
        info.sessionImp = session;
        m_sessions[sessionName] = info;
        return true;
    }
    catch (const event_tracing::event_trace_error &e)
    {
        std::cout << e.what() << std::endl;
        return false;
    }
    catch (const std::exception &e)
    {
        std::cout << e.what() << std::endl;
        return false;
    }
}

bool ETWHelper::RemoveSession(const std::wstring &sessionName)
{
    try
    {
        if (auto sessionInfo = m_sessions.find(sessionName); sessionInfo != m_sessions.end())
        {
            for (auto trace : sessionInfo->second.sessionTraces) trace.second->stop();

            sessionInfo->second.sessionImp->close_trace_session();
            m_sessions.erase(sessionInfo);
            return true;
        }
        return true;
    }
    catch (const event_tracing::event_trace_error &e)
    {
        std::cout << e.what() << std::endl;
        return false;
    }
}

bool ETWHelper::AddConsumer(const std::wstring &sessionName, const std::wstring &consumerName)
{
    if (auto iter = m_sessions.find(sessionName); iter != m_sessions.end())
    {
        if (iter->second.sessionTraces.find(consumerName) != iter->second.sessionTraces.end())
            return false;
        iter->second.sessionTraces[consumerName] =
            std::make_shared<event_tracing::event_trace>(*iter->second.sessionImp);
        return true;
    }
    return false;
}

bool ETWHelper::RemoveConsumer(const std::wstring &sessionName, const std::wstring &consumerName)
{
    if (auto iter = m_sessions.find(sessionName); iter != m_sessions.end())
    {
        if (auto traceIter = iter->second.sessionTraces.find(consumerName);
            traceIter != iter->second.sessionTraces.end())
            iter->second.sessionTraces.erase(traceIter);
        return true;
    }
    return false;
}

bool ETWHelper::AddProvider(const std::wstring &sessionName, const std::wstring &guidName,
                            const std::uint32_t &keyword)
{
    try
    {
        event_tracing::ms_guid msGuid = event_tracing::event_provider_list().get_guid(guidName);
        if (auto iter = m_sessions.find(sessionName); iter != m_sessions.end())
        {
            iter->second.sessionImp->enable_trace(
                msGuid, event_tracing::event_trace_session::trace_level::verbose, keyword);
            return true;
        }
        return false;
    }
    catch (const event_tracing::event_trace_error &e)
    {
        std::cout << e.what() << std::endl;
        return false;
    }
}

bool ETWHelper::RemoveProvider(const std::wstring &sessionName, const std::wstring &guidName)
{
    try
    {
        event_tracing::ms_guid msGuid = event_tracing::event_provider_list().get_guid(guidName);
        if (auto iter = m_sessions.find(sessionName); iter != m_sessions.end())
        {
            iter->second.sessionImp->disable_trace(msGuid);
            return true;
        }
        return false;
    }
    catch (const event_tracing::event_trace_error &e)
    {
        std::cout << e.what() << std::endl;
        return false;
    }
}

bool ETWHelper::SetProviderHandlerForConsumer(const std::wstring &sessionName,
                                              const std::wstring &consumerName,
                                              const std::wstring &guidName, std::uint32_t eventId,
                                              std::function<void(PEVENT_RECORD pRecord)> &&handler)
{
    try
    {
        if (auto iter = m_sessions.find(sessionName); iter != m_sessions.end())
        {
            if (auto traceIter = iter->second.sessionTraces.find(consumerName);
                traceIter != iter->second.sessionTraces.end())
            {
                event_tracing::ms_guid msGuid =
                    event_tracing::event_provider_list().get_guid(guidName);
                if (eventId == UINT_MAX)
                    traceIter->second->on_trace_event(msGuid, handler);
                else
                    traceIter->second->on_trace_event(msGuid, eventId, handler);
                return true;
            }
            return false;
        }
        return false;
    }
    catch (const event_tracing::event_trace_error &e)
    {
        std::cout << e.what() << std::endl;
        return false;
    }
}

bool ETWHelper::SetProviderHandlerForConsumer(const std::wstring &sessionName,
                                              const std::wstring &consumerName,
                                              const std::wstring &providerName,
                                              std::function<void(PEVENT_RECORD pRecord)> &&handler)
{
    return SetProviderHandlerForConsumer(
        sessionName, consumerName, providerName, UINT_MAX,
        std::forward<std::function<void(PEVENT_RECORD pRecord)>>(handler));
}

bool ETWHelper::StartConsumerAsync(const std::wstring &sessionName,
                                   const std::wstring &consumerName)
{
    try
    {
        if (auto iter = m_sessions.find(sessionName); iter != m_sessions.end())
        {
            if (auto traceIter = iter->second.sessionTraces.find(consumerName);
                traceIter != iter->second.sessionTraces.end())
            {
                traceIter->second->open_trace();
                traceIter->second->run_async();
                return true;
            }
            return false;
        }
        return false;
    }
    catch (const event_tracing::event_trace_error &e)
    {
        std::cout << e.what() << std::endl;
        return false;
    }
}

bool ETWHelper::StopConsumer(const std::wstring &sessionName, const std::wstring &consumerName)
{
    try
    {
        if (auto iter = m_sessions.find(sessionName); iter != m_sessions.end())
        {
            if (auto traceIter = iter->second.sessionTraces.find(consumerName);
                traceIter != iter->second.sessionTraces.end())
                traceIter->second->stop();

            return true;
        }
        return false;
    }
    catch (const event_tracing::event_trace_error &e)
    {
        std::cout << e.what() << std::endl;
        return false;
    }
}

}  // namespace zzj