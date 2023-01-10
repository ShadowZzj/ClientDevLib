#pragma once
#include <General/util/Service/Service.h>
#include <Windows/util/Service/WinService.h>
namespace zzj
{
class Service;
class ServiceInternal : public WinService
{
  public:
    typedef void (Service::*ServiceFuncVoid)();
    typedef BOOL (Service::*ServiceFuncBool)();

    using WinService::WinService;

    ServiceInternal(ServiceFuncVoid _runningFunc, ServiceFuncVoid _onstopFunc, ServiceFuncVoid _onshutdownFunc,
                    ServiceFuncVoid _onPreShutdownFunc, ServiceFuncBool _oninitFunc, std::string name,
                    std::string displayName, std::string description, Service *_service)
        : WinService(name, description, displayName)
    {
        runningFunc       = _runningFunc;
        onstopFunc        = _onstopFunc;
        onshutdownFunc    = _onshutdownFunc;
        onPreShutdownFunc = _onPreShutdownFunc;
        oninitFunc        = _oninitFunc;
        service           = _service;
    }
    virtual BOOL OnInit() override;
    virtual void Run() override;
    virtual void OnStop() override;
    virtual void OnShutdown() override;
    virtual void OnPreShutDown() override;

  protected:
    Service *service;
    ServiceFuncVoid runningFunc;
    ServiceFuncVoid onstopFunc;
    ServiceFuncVoid onshutdownFunc;
    ServiceFuncVoid onPreShutdownFunc;
    ServiceFuncBool oninitFunc;
};

class Service : public ServiceInterface
{
  public:
    Service(std::string name, std::string _binPath, std::string _description = "", std::string _displayName = "")
        : ServiceInterface(name),
          serviceInternal((ServiceInternal::ServiceFuncVoid)&Service::ServiceFunc,
                          (ServiceInternal::ServiceFuncVoid)&Service::OnStop,
                          (ServiceInternal::ServiceFuncVoid)&Service::OnShutdown,
                          (ServiceInternal::ServiceFuncVoid)&Service::OnPreShutDown,
                          (ServiceInternal::ServiceFuncBool)&Service::OnInit, name, _displayName, _description, this)
    {
        description = _description;
        displayName = _displayName;
        binPath     = _binPath;
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
    virtual ControlStatus CheckSafeStop(int seconds) override;

    virtual void OnStop()
    {
        return;
    }
    virtual void OnShutdown()
    {
        return;
    }
    virtual void OnPreShutDown()
    {
        return;
    }
    virtual BOOL OnInit()
    {
        return true;
    };
    virtual void ServiceFunc()
    {
    }
    virtual void Run()
    {
        serviceInternal.Start();
    }

  protected:
    ServiceInternal serviceInternal;
    std::string description;
    std::string displayName;
    std::string binPath;
    HANDLE hMutex;
};
}; // namespace zzj