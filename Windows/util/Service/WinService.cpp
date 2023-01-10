#include "WinService.h"
#include <General/util/BaseUtil.hpp>
#include <functional>


WinService *winService = nullptr;

void __stdcall WinService::ServiceMain(DWORD dwNumServicesArgs, LPSTR *lpServiceArgVectors)
{
    if (!winService)
        return;
    SERVICE_STATUS_HANDLE _statusHandle =
        ::RegisterServiceCtrlHandlerA(winService->name.c_str(), (LPHANDLER_FUNCTION)ServiceHandler);
    if (NULL == _statusHandle)
    {
        return;
    }

    winService->statusHandle = _statusHandle;
    winService->ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 0, 0, 3000);

    winService->eventStop = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (!winService->OnInit())
    {
        winService->ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0, 0, 0);
        return;
    }

    winService->ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0, 0, 0);
    winService->Run();
    winService->ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0, 0, 0);

    return;
}

void __stdcall WinService::ServiceHandler(DWORD dwControl)
{
    switch (dwControl)
    {
    case SERVICE_CONTROL_INTERROGATE:
        return;
    case SERVICE_CONTROL_STOP:
        winService->InternalOnStop();
        return;
    case SERVICE_CONTROL_SHUTDOWN:
        winService->InternalOnShutdown();
        return;
    case SERVICE_CONTROL_PRESHUTDOWN:
        winService->InternalOnPreShutdown();
        return;
    default:
        return;
    }
}

BOOL WinService::ReportSvcStatus(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwSvcExitCode, DWORD dwCheckPoint,
                                 DWORD dwWaitHint)
{
    SERVICE_STATUS svcStatus            = {0};
    svcStatus.dwServiceType             = serviceType;
    svcStatus.dwCurrentState            = dwCurrentState;
    svcStatus.dwWin32ExitCode           = dwWin32ExitCode;
    svcStatus.dwServiceSpecificExitCode = dwSvcExitCode;
    svcStatus.dwCheckPoint              = dwCheckPoint;
    svcStatus.dwWaitHint                = dwWaitHint;
    switch (dwCurrentState)
    {
    case SERVICE_START_PENDING:
        svcStatus.dwControlsAccepted = 0;
        break;
    case SERVICE_STOP_PENDING:
        svcStatus.dwControlsAccepted = 0;
        break;
    case SERVICE_STOPPED:
        svcStatus.dwControlsAccepted = 0;
        break;
    case SERVICE_RUNNING:
        svcStatus.dwControlsAccepted =
            SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_PRESHUTDOWN | SERVICE_ACCEPT_POWEREVENT;
        break;
    default:
        break;
    }

    if (NULL == statusHandle)
        return FALSE;

    return ::SetServiceStatus(statusHandle, &svcStatus);
}

void WinService::Start()
{
    winService = this;

    entry[0].lpServiceName = (LPSTR)name.c_str();
    entry[0].lpServiceProc = (LPSERVICE_MAIN_FUNCTIONA)ServiceMain;

    entry[1].lpServiceName = NULL;
    entry[1].lpServiceProc = NULL;

    StartServiceCtrlDispatcherA(entry);
}

void WinService::InternalOnStop()
{
    OnStop();
    ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0, 0, 5000);
    ::SetEvent(eventStop);
}

void WinService::InternalOnShutdown()
{
    OnShutdown();
    ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0, 0, 5000);
    ::SetEvent(eventStop);
}

void WinService::InternalOnPreShutdown()
{
    OnPreShutDown();
    ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0, 0, 5000);
    ::SetEvent(eventStop);
}

WinService::WinService(std::string name, std::string description, std::string displayName)
{
    this->name        = name;
    this->description = description;
    this->displayName = displayName;
    eventStop         = NULL;
}

bool WinService::InstallService()
{

    char DirBuf[1024]       = {0};
    bool ret                = FALSE;
    SC_HANDLE sch           = NULL;
    SC_HANDLE schNewSrv     = NULL;
    const char *serviceName = name.c_str();
    const char *display     = displayName.c_str();

    GetModuleFileNameA(NULL, DirBuf, sizeof(DirBuf));
    sch = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!sch)
    {
        ret = FALSE;
        goto exit;
    }
    schNewSrv = CreateServiceA(sch, serviceName, display, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                               SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, DirBuf, NULL, NULL, NULL, NULL, NULL);

    if (!schNewSrv)
    {
        ret = false;
        goto exit;
    }

    SERVICE_DESCRIPTIONA sd;
    sd.lpDescription = (LPSTR)description.c_str();

    ChangeServiceConfig2A(schNewSrv, SERVICE_CONFIG_DESCRIPTION, &sd);
    ret = TRUE;

exit:
    if (schNewSrv)
        CloseServiceHandle(schNewSrv);
    if (sch)
        CloseServiceHandle(sch);

    return ret;
}

bool WinService::UninstallService()
{
    return UninstallService(name.c_str());
}

int WinService::CheckSafeStop(int sleepSeconds)
{
    DWORD res;
    if (NULL == eventStop)
    {
        Sleep(sleepSeconds * 1000);
        res = WAIT_TIMEOUT;
    }
    else
    {
        res = WaitForSingleObject(eventStop, sleepSeconds * 1000);
    }
    if (WAIT_TIMEOUT == res)
        return 0;
    else if (WAIT_OBJECT_0 == res)
        return 1;
    else
        return -1;
}

int WinService::InstallService(const char *serviceName, const char *displayName, const char *description,
                               const char *binPath)
{
    int ret             = 0;
    SC_HANDLE sch       = NULL;
    SC_HANDLE schNewSrv = NULL;

    sch = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!sch)
    {
        ret = -1;
        goto exit;
    }
    schNewSrv = CreateServiceA(sch, serviceName, displayName, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                               SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, binPath, NULL, NULL, NULL, NULL, NULL);

    if (!schNewSrv)
    {
        ret = -2;
        goto exit;
    }

    SERVICE_DESCRIPTIONA sd;
    sd.lpDescription = (LPSTR)description;

    ChangeServiceConfig2A(schNewSrv, SERVICE_CONFIG_DESCRIPTION, &sd);
    ret = 0;

exit:
    if (schNewSrv)
        CloseServiceHandle(schNewSrv);
    if (sch)
        CloseServiceHandle(sch);

    return ret;
}

bool WinService::InstallKernelService(const char *binaryPath, const char *serviceName, const char *display,
                                      const char *description)
{

    SC_HANDLE sch = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!sch)
    {

        return FALSE;
    }
    SC_HANDLE schNewSrv =
        CreateServiceA(sch, serviceName, display, SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER, SERVICE_BOOT_START,
                       SERVICE_ERROR_NORMAL, binaryPath, NULL, NULL, NULL, NULL, NULL);

    if (!schNewSrv)
    {

        return FALSE;
    }

    SERVICE_DESCRIPTIONA sd;
    sd.lpDescription = (LPSTR)description;

    ChangeServiceConfig2A(schNewSrv, SERVICE_CONFIG_DESCRIPTION, &sd);
    CloseServiceHandle(schNewSrv);
    CloseServiceHandle(sch);

    return TRUE;
}

bool WinService::MyStartService(const char *serviceName)
{

    SC_HANDLE hSC = ::OpenSCManagerA(NULL, NULL, GENERIC_EXECUTE);
    if (hSC == NULL)
        return false;

    SC_HANDLE hSvc = ::OpenServiceA(hSC, serviceName, SERVICE_START | SERVICE_QUERY_STATUS);
    if (hSvc == NULL)
    {
        ::CloseServiceHandle(hSC);
        return false;
    }

    SERVICE_STATUS status;
    if (::QueryServiceStatus(hSvc, &status) == FALSE)
    {
        ::CloseServiceHandle(hSvc);
        ::CloseServiceHandle(hSC);
        return false;
    }

    if (status.dwCurrentState == SERVICE_STOPPED)
    {

        if (::StartService(hSvc, NULL, NULL) == FALSE)
        {
            ::CloseServiceHandle(hSvc);
            ::CloseServiceHandle(hSC);
            return false;
        }

        int waitCount = 0;
        while (::QueryServiceStatus(hSvc, &status) == TRUE)
        {
            ::Sleep(1000);
            if (status.dwCurrentState == SERVICE_RUNNING)
            {
                ::CloseServiceHandle(hSvc);
                ::CloseServiceHandle(hSC);
                return true;
            }
            waitCount++;
            if (waitCount >= 5)
            {
                ::CloseServiceHandle(hSvc);
                ::CloseServiceHandle(hSC);
                return false;
            }
        }
    }

    ::CloseServiceHandle(hSvc);
    ::CloseServiceHandle(hSC);
    return true;
}
bool WinService::StopService(const char *serviceName, uint16_t waitSecond)
{

    SC_HANDLE hSC = ::OpenSCManagerA(NULL, NULL, GENERIC_EXECUTE);
    if (hSC == NULL)
        return false;

    SC_HANDLE hSvc = ::OpenServiceA(hSC, serviceName, SERVICE_START | SERVICE_QUERY_STATUS | SERVICE_STOP);
    if (hSvc == NULL)
    {
        ::CloseServiceHandle(hSC);
        return false;
    }

    SERVICE_STATUS status;
    if (::QueryServiceStatus(hSvc, &status) == FALSE)
    {
        ::CloseServiceHandle(hSvc);
        ::CloseServiceHandle(hSC);
        return false;
    }

    if (status.dwCurrentState == SERVICE_RUNNING)
    {

        if (::ControlService(hSvc, SERVICE_CONTROL_STOP, &status) == FALSE)
        {
            ::CloseServiceHandle(hSvc);
            ::CloseServiceHandle(hSC);
            return false;
        }

        int waitCount = 0;
        while (::QueryServiceStatus(hSvc, &status) == TRUE)
        {
            ::Sleep(1000);
            if (status.dwCurrentState == SERVICE_STOPPED)
            {
                ::CloseServiceHandle(hSvc);
                ::CloseServiceHandle(hSC);
                return true;
            }
            waitCount++;
            if (waitCount >= waitSecond)
            {
                ::CloseServiceHandle(hSvc);
                ::CloseServiceHandle(hSC);
                return false;
            }
        }
    }
    ::CloseServiceHandle(hSvc);
    ::CloseServiceHandle(hSC);
    return true;
}

bool WinService::UninstallService(const char *serviceName)
{

    SC_HANDLE scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm)
        return FALSE;

    SC_HANDLE scml = OpenServiceA(scm, serviceName, SC_MANAGER_ALL_ACCESS);
    if (!scml)
    {
        CloseServiceHandle(scm);
        return FALSE;
    }

    if (!StopService(serviceName))
    {
        CloseServiceHandle(scml);
        CloseServiceHandle(scm);
        return FALSE;
    }

    if (!DeleteService(scml))
    {

        CloseServiceHandle(scml);
        CloseServiceHandle(scm);
        return FALSE;
    }

    CloseServiceHandle(scml);
    CloseServiceHandle(scm);
    return TRUE;
}
int WinService::SetServiceStartUpType(const char *serviceName, int startupType)
{
    SC_HANDLE hSC = ::OpenSCManagerA(NULL, NULL, GENERIC_EXECUTE);
    if (hSC == NULL)
        return -1;

    SC_HANDLE hSvc = ::OpenServiceA(hSC, serviceName, SERVICE_CHANGE_CONFIG | SERVICE_QUERY_STATUS);
    if (hSvc == NULL)
    {
        ::CloseServiceHandle(hSC);
        return -2;
    }

    SERVICE_STATUS status;
    if (::QueryServiceStatus(hSvc, &status) == FALSE)
    {
        ::CloseServiceHandle(hSvc);
        ::CloseServiceHandle(hSC);
        return -3;
    }

    // change service to auto start
    if (!ChangeServiceConfigA(hSvc, SERVICE_NO_CHANGE, startupType, SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL,
                              NULL, NULL))
    {
        ::CloseServiceHandle(hSvc);
        ::CloseServiceHandle(hSC);
        return -4;
    }
    ::CloseServiceHandle(hSvc);
    ::CloseServiceHandle(hSC);
    return 0;
}

int WinService::IsServiceInstalled(const char *serviceName, bool &installed)
{
    int result = 0;
    int errCode;
    SC_HANDLE scml = NULL;
    SC_HANDLE scm  = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm)
    {
        errCode = GetLastError();
        result  = -1;
        goto exit;
    }

    scml = OpenServiceA(scm, serviceName, SC_MANAGER_ALL_ACCESS);
    if (!scml)
    {
        if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST)
        {
            result    = 0;
            installed = false;
        }
        else
            result = -2;
        goto exit;
    }

    installed = true;
    result    = 0;
exit:
    if (scm)
        CloseServiceHandle(scm);
    if (scml)
        CloseServiceHandle(scml);
    return result;
}

int WinService::IsServiceRunning(const char *serviceName, bool &running)
{
    int result = 0;
    int errCode;
    SC_HANDLE scml = NULL;
    SC_HANDLE scm  = NULL;
    SERVICE_STATUS ss;

    scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm)
    {
        errCode = GetLastError();
        result  = -1;
        goto exit;
    }

    scml = OpenServiceA(scm, serviceName, SC_MANAGER_ALL_ACCESS);
    if (!scml)
    {
        result = -2;
        goto exit;
    }

    if (!QueryServiceStatus(scml, &ss))
    {
        result = -3;
        goto exit;
    }

    if (ss.dwCurrentState == SERVICE_RUNNING)
    {
        running = true;
        result  = 0;
        goto exit;
    }

    running = false;
    result  = 0;
    goto exit;

exit:
    if (scm)
        CloseServiceHandle(scm);
    if (scml)
        CloseServiceHandle(scml);
    return result;
}
