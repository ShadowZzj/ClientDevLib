#pragma once

#include <Windows/util/EventTrace/elevated_check.h>
#include <Windows/util/EventTrace/event_info.h>
#include <Windows/util/EventTrace/event_provider_list.h>
#include <Windows/util/EventTrace/event_trace.h>
#include <Windows/util/EventTrace/event_trace_error.h>
#include <Windows/util/EventTrace/event_trace_session.h>
#include <map>
#include <functional>
namespace zzj
{
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
    bool AddProvider(const std::wstring &sessionName, const std::wstring &guidName,
                     const std::uint32_t &keyword);
    bool RemoveProvider(const std::wstring &sessionName, const std::wstring &guidName);
    bool SetProviderHandlerForConsumer(const std::wstring &sessionName,
                                       const std::wstring &consumerName,
                                       const std::wstring &providerName, std::uint32_t eventId,
                                       std::function<void(PEVENT_RECORD pRecord)> &&handler);
    bool SetProviderHandlerForConsumer(const std::wstring &sessionName,
                                       const std::wstring &consumerName,
                                       const std::wstring &providerName,
                                       std::function<void(PEVENT_RECORD pRecord)> &&handler);
    bool StartConsumerAsync(const std::wstring &sessionName, const std::wstring &consumerName);
    bool StopConsumer(const std::wstring &sessionName, const std::wstring &consumerName);
    template <typename T>
    static T *GetPropertyValue(PEVENT_RECORD pEvent, LPWSTR propertyName)
    {
        // Prevent using this template for LPWSTR type, which should use GetStringPropertyValue
        // instead
        static_assert(!std::is_same<T, LPWSTR>::value,
                      "LPWSTR is not supported, use GetStringPropertyValue instead");

        DWORD propertySize = 0;
        PROPERTY_DATA_DESCRIPTOR descriptor;
        ZeroMemory(&descriptor, sizeof(PROPERTY_DATA_DESCRIPTOR));
        descriptor.PropertyName = reinterpret_cast<ULONGLONG>(propertyName);
        descriptor.ArrayIndex = ULONG_MAX;

        ULONG status = TdhGetPropertySize(pEvent, 0, NULL, 1, &descriptor, &propertySize);
        if (status != ERROR_SUCCESS)
        {
            return nullptr;
        }

        T *data = reinterpret_cast<T *>(malloc(propertySize));
        if (!data)
        {
            return nullptr;
        }

        status = TdhGetProperty(pEvent, 0, NULL, 1, &descriptor, propertySize,
                                reinterpret_cast<PBYTE>(data));
        if (status != ERROR_SUCCESS)
        {
            free(data);
            return nullptr;
        }

        return data;
    }

    static LPWSTR GetStringPropertyValue(PEVENT_RECORD pEvent, LPWSTR propertyName)
    {
        DWORD propertySize = 0;
        PROPERTY_DATA_DESCRIPTOR descriptor;
        ZeroMemory(&descriptor, sizeof(PROPERTY_DATA_DESCRIPTOR));
        descriptor.PropertyName = reinterpret_cast<ULONGLONG>(propertyName);
        descriptor.ArrayIndex = ULONG_MAX;

        ULONG status = TdhGetPropertySize(pEvent, 0, NULL, 1, &descriptor, &propertySize);
        if (status != ERROR_SUCCESS)
        {
            return nullptr;
        }

        BYTE *buffer = reinterpret_cast<BYTE *>(malloc(propertySize));
        if (!buffer)
        {
            return nullptr;
        }

        status = TdhGetProperty(pEvent, 0, NULL, 1, &descriptor, propertySize, buffer);
        if (status != ERROR_SUCCESS)
        {
            free(buffer);
            return nullptr;
        }

        return reinterpret_cast<LPWSTR>(buffer);
    }

   private:
    std::map<std::wstring, SessionInfo> m_sessions;
};

}  // namespace zzj