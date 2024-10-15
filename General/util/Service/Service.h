#pragma once
#include <iostream>
#include <string>

namespace zzj
{
class ServiceInterface
{
   public:
    ServiceInterface(std::string _serviceName) { serviceName = _serviceName; }
    virtual int GetMutex() { return -1; };
    virtual int ReleaseMutex() { return -1; };
    virtual int CloseMutex() { return -1; };
    virtual ~ServiceInterface() {}
    enum class ControlStatus : int
    {
        RequestStop,
        Timeout,
        Error
    };
    enum class StartUpType : int
    {
        Auto,
        Manual,
        Disabled,
        Unknown
    };
    virtual int Install(ServiceInterface *otherService = nullptr) = 0;
    virtual int Uninstall() = 0;
    virtual int Start() = 0;
    virtual int Stop() = 0;
    virtual void ServiceFunc() = 0;
    virtual void Run() = 0;
    virtual int Protect(ServiceInterface *otherService = nullptr);
    virtual int IsServiceInstalled(bool &isInstlled) = 0;
    virtual int IsServiceRunning(bool &isRunning) = 0;
    virtual int IsServiceBinExist(bool &isExist) = 0;
    virtual int SetServiceStartType(StartUpType startType) = 0;
    virtual int GetServiceStartType(StartUpType &startType) = 0;
    virtual std::string GetServiceBinPath() = 0;
    virtual int SetServiceBinPath(const std::string &binPath) = 0;
    virtual std::string BinPath() = 0;

    virtual ControlStatus CheckSafeStop(int seconds) = 0;
    std::string GetServiceName() { return serviceName; }

   protected:
    std::string serviceName;
};

};  // namespace zzj
