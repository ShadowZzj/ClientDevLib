#include "Service.h"
#include <General/util/File/File.h>
#include <Windows/util/Process/ThreadHelper.h>
BOOL zzj::ServiceInternal::OnInit()
{
    return (service->*oninitFunc)();
}
void zzj::ServiceInternal::Run()
{
    return (service->*runningFunc)();
}
void zzj::ServiceInternal::OnStop()
{
    return (service->*onstopFunc)();
}
void zzj::ServiceInternal::OnShutdown()
{
    return (service->*onshutdownFunc)();
}
void zzj::ServiceInternal::OnPreShutDown()
{
    return (service->*onPreShutdownFunc)();
}

int zzj::Service::GetMutex()
{
    std::string uniqueName = serviceName + "_zzj_unique";
    hMutex                 = zzj::Thread::WaitForMutex(uniqueName.c_str());
    return 0;
}

int zzj::Service::ReleaseMutex()
{
    if (hMutex)
        ::ReleaseMutex(hMutex);
    return 0;
}

int zzj::Service::CloseMutex()
{
    if (hMutex)
        ::CloseHandle(hMutex);
    return 0;
}
int zzj::Service::Install(ServiceInterface *otherService)
{
    int result = 0;
    if (otherService != nullptr)
        return otherService->Install();
    result = WinService::InstallService(serviceName.c_str(), displayName.c_str(), description.c_str(), binPath.c_str());
    return result;
}

int zzj::Service::Uninstall()
{
    bool res   = serviceInternal.UninstallService();
    int result = !res;
    return result;
}
int zzj::Service::Start()
{
    bool res   = WinService::MyStartService(serviceName.c_str());
    int result = !res;
    return result;
}
int zzj::Service::Stop()
{
    bool res   = WinService::StopService(serviceName.c_str(), 15);
    int result = !res;
    return result;
}

int zzj::Service::IsServiceInstalled(bool &isInstlled)
{
    return WinService::IsServiceInstalled(serviceName.c_str(), isInstlled);
}
int zzj::Service::IsServiceRunning(bool &isRunning)
{
    return WinService::IsServiceRunning(serviceName.c_str(), isRunning);
}

int zzj::Service::IsServiceBinExist(bool &isExist)
{
    isExist = IsFileExist(binPath.c_str());
    return 0;
}
int zzj::Service::SetServiceStartType(StartUpType startType)
{
    int result = 0;
    switch (startType)
    {
    case StartUpType::Auto:
        result = WinService::SetServiceStartUpType(serviceName.c_str(), SERVICE_AUTO_START);
        break;
    case StartUpType::Manual:
        result = WinService::SetServiceStartUpType(serviceName.c_str(), SERVICE_DEMAND_START);
        break;
    case StartUpType::Disabled:
        result = WinService::SetServiceStartUpType(serviceName.c_str(), SERVICE_DISABLED);
        break;
    default:
        result = -100;
        break;
    }
    return result;
}
int zzj::Service::GetServiceStartType(StartUpType &startType)
{
    int startupType = -1;
    int result      = WinService::GetServiceStartUpType(serviceName.c_str(), startupType);
    switch (startupType)
    {
    case SERVICE_AUTO_START:
        startType = StartUpType::Auto;
        break;
    case SERVICE_DEMAND_START:

        startType = StartUpType::Manual;
        break;
    case SERVICE_DISABLED:
        startType = StartUpType::Disabled;
        break;
    default:
        startType = StartUpType::Unknown;
        break;
    }
    return result;
}

zzj::ServiceInterface::ControlStatus zzj::Service::CheckSafeStop(int seconds)
{
    int res = serviceInternal.CheckSafeStop(seconds);
    zzj::ServiceInterface::ControlStatus ret;
    switch (res)
    {
    case 0:
        ret = zzj::ServiceInterface::ControlStatus::Timeout;
        break;
    case 1:
        ret = zzj::ServiceInterface::ControlStatus::RequestStop;
        break;
    default:
        ret = zzj::ServiceInterface::ControlStatus::Error;
        break;
    }
    return ret;
}
