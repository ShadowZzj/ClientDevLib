#pragma once
#include <General/util/Service/Service.h>
#include <General/util/System/System.h>
#include <Windows/util/Service/WinService.h>

namespace zzj
{
class Service;
class ServiceInternal : public WinService
{
   public:
    typedef void (Service::*ServiceFuncVoid)();
    typedef BOOL (Service::*ServiceFuncBool)();
    typedef void (Service::*ServiceFuncSession)(Session::SessionMessage message, Session session);

    using WinService::WinService;

    ServiceInternal(ServiceFuncVoid _runningFunc, ServiceFuncVoid _onstopFunc,
                    ServiceFuncVoid _onshutdownFunc, ServiceFuncVoid _onPreShutdownFunc,
                    ServiceFuncBool _oninitFunc, ServiceFuncSession _onsessionChangeFunc,
                    std::string name, std::string displayName, std::string description,
                    Service *_service)
        : WinService(name, description, displayName)
    {
        runningFunc = _runningFunc;
        onstopFunc = _onstopFunc;
        onshutdownFunc = _onshutdownFunc;
        onPreShutdownFunc = _onPreShutdownFunc;
        oninitFunc = _oninitFunc;
        service = _service;
        onsessionChangeFunc = _onsessionChangeFunc;
    }
    virtual BOOL OnInit() override;
    virtual void Run() override;
    virtual void OnStop() override;
    virtual void OnShutdown() override;
    virtual void OnPreShutDown() override;
    virtual void OnSessionChange(zzj::Session::SessionMessage message,
                                 zzj::Session session) override;

   protected:
    Service *service;
    ServiceFuncVoid runningFunc;
    ServiceFuncVoid onstopFunc;
    ServiceFuncVoid onshutdownFunc;
    ServiceFuncVoid onPreShutdownFunc;
    ServiceFuncBool oninitFunc;
    ServiceFuncSession onsessionChangeFunc;
};

class Service : public ServiceInterface
{
   public:
    Service(std::string name, std::string _binPath, std::string _description = "",
            std::string _displayName = "")
        : ServiceInterface(name),
          serviceInternal((ServiceInternal::ServiceFuncVoid)&Service::ServiceFunc,
                          (ServiceInternal::ServiceFuncVoid)&Service::OnStop,
                          (ServiceInternal::ServiceFuncVoid)&Service::OnShutdown,
                          (ServiceInternal::ServiceFuncVoid)&Service::OnPreShutDown,
                          (ServiceInternal::ServiceFuncBool)&Service::OnInit,
                          (ServiceInternal::ServiceFuncSession)&Service::OnSessionChange, name,
                          _displayName, _description, this)
    {
        description = _description;
        displayName = _displayName;
        binPath = _binPath;
    }
    virtual int GetMutex() override;
    virtual int ReleaseMutex() override;
    virtual int CloseMutex() override;
    virtual int Install(ServiceInterface *otherService = nullptr) override;
    virtual int Uninstall() override;
    virtual int Start() override;
    virtual int Stop() override;
    virtual int IsServiceInstalled(bool &isInstlled) override;
    virtual int IsServiceRunning(bool &isRunning) override;
    virtual int IsServiceBinExist(bool &isExist) override;
    virtual int SetServiceStartType(StartUpType startType) override;
    virtual int GetServiceStartType(StartUpType &startType) override;
    virtual std::string GetServiceBinPath() override;
    virtual int SetServiceBinPath(const std::string &binPath) override;
    virtual ControlStatus CheckSafeStop(int seconds) override;

    std::string BinPath() { return binPath; }
    virtual void OnStop() { return; }
    virtual void OnShutdown() { return; }
    virtual void OnPreShutDown() { return; }
    virtual BOOL OnInit() { return true; };
    virtual void ServiceFunc() {}
    virtual void OnSessionChange(Session::SessionMessage message, Session session) { return; }
    virtual void Run() { serviceInternal.Start(); }

   protected:
    ServiceInternal serviceInternal;
    std::string description;
    std::string displayName;
    std::string binPath;
    HANDLE hMutex;
};
};  // namespace zzj