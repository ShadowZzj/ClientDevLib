#include "StartUp.h"
#include <General/util/StrUtil.h>
#include <Windows.h>

namespace zzj
{
namespace
{
// Convert service start type to string
std::string ServiceStartTypeToString(DWORD startType)
{
    switch (startType)
    {
        case SERVICE_BOOT_START:
            return "Boot";
        case SERVICE_SYSTEM_START:
            return "System";
        case SERVICE_AUTO_START:
            return "Auto";
        case SERVICE_DEMAND_START:
            return "Manual";
        case SERVICE_DISABLED:
            return "Disabled";
        default:
            return "Unknown";
    }
}

// Convert service status to string
std::string ServiceStatusToString(DWORD state)
{
    switch (state)
    {
        case SERVICE_STOPPED:
            return "Stopped";
        case SERVICE_START_PENDING:
            return "StartPending";
        case SERVICE_STOP_PENDING:
            return "StopPending";
        case SERVICE_RUNNING:
            return "Running";
        case SERVICE_CONTINUE_PENDING:
            return "ContinuePending";
        case SERVICE_PAUSE_PENDING:
            return "PausePending";
        case SERVICE_PAUSED:
            return "Paused";
        default:
            return "Unknown";
    }
}

}
std::tuple<int, std::vector<ServiceStartUpInfo>> StartUp::GetServiceStartUp()
{
    std::vector<ServiceStartUpInfo> items;

    SC_HANDLE scm = ::OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm)
    {
        return std::make_tuple(-1, std::move(items));
    }

    DWORD bytesNeeded = 0;
    DWORD serviceCount = 0;
    DWORD resumeHandle = 0;
    DWORD serviceType = SERVICE_WIN32;
    DWORD serviceStates = SERVICE_STATE_ALL;

    if (!::EnumServicesStatusExA(scm, SC_ENUM_PROCESS_INFO, serviceType, serviceStates, nullptr, 0,
                                 &bytesNeeded, &serviceCount, &resumeHandle, nullptr) &&
        ::GetLastError() != ERROR_MORE_DATA)
    {
        ::CloseServiceHandle(scm);
        return std::make_tuple(-2, std::move(items));
    }

    std::vector<BYTE> buffer(bytesNeeded);
    auto *services = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSA *>(buffer.data());

    if (!::EnumServicesStatusExA(scm, SC_ENUM_PROCESS_INFO, serviceType, serviceStates,
                                 reinterpret_cast<LPBYTE>(services), bytesNeeded, &bytesNeeded,
                                 &serviceCount, &resumeHandle, nullptr))
    {
        ::CloseServiceHandle(scm);
        return std::make_tuple(-3, std::move(items));
    }

    for (DWORD i = 0; i < serviceCount; ++i)
    {
        const auto &svc = services[i];

        ServiceStartUpInfo info;
        info.name = zzj::str::ansi2utf8(svc.lpServiceName ? svc.lpServiceName : "");
        info.displayName = zzj::str::ansi2utf8(svc.lpDisplayName ? svc.lpDisplayName : "");
        info.status = ServiceStatusToString(svc.ServiceStatusProcess.dwCurrentState);

        SC_HANDLE hService =
            ::OpenServiceA(scm, svc.lpServiceName, SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS);
        if (hService)
        {
            DWORD bytesNeededCfg = 0;
            ::QueryServiceConfigA(hService, nullptr, 0, &bytesNeededCfg);
            if (::GetLastError() == ERROR_INSUFFICIENT_BUFFER && bytesNeededCfg > 0)
            {
                std::vector<BYTE> cfgBuffer(bytesNeededCfg);
                auto *config = reinterpret_cast<QUERY_SERVICE_CONFIGA *>(cfgBuffer.data());
                if (::QueryServiceConfigA(hService, config, bytesNeededCfg, &bytesNeededCfg))
                {
                    info.startType = ServiceStartTypeToString(config->dwStartType);
                    info.binaryPath = zzj::str::ansi2utf8(
                        config->lpBinaryPathName ? config->lpBinaryPathName : "");
                    info.account = zzj::str::ansi2utf8(
                        config->lpServiceStartName ? config->lpServiceStartName : "");
                }
            }
            ::CloseServiceHandle(hService);
        }

        items.emplace_back(std::move(info));
    }

    ::CloseServiceHandle(scm);
    return std::make_tuple(0, std::move(items));
}

}
