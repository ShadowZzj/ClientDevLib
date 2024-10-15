#pragma once
#include <General/util/Service/Service.h>
#include <mutex>

namespace zzj
{
class Service : public ServiceInterface
{
   public:
    Service(std::string _binPath, std::string serviceName) : ServiceInterface(serviceName)
    {
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
    virtual void ServiceFunc() override {}
    virtual int SetServiceStartType(StartUpType startType) override;
    virtual int GetServiceStartType(StartUpType &startType) override;
    virtual std::string GetServiceBinPath() override;
    virtual int SetServiceBinPath(const std::string &binPath) override;
    virtual std::string BinPath() override { return binPath; }
    virtual void Run() override;
    int EnableService();
    int DisableService();
    static void TermHandler();
    virtual zzj::ServiceInterface::ControlStatus CheckSafeStop(int seconds) override;

   protected:
    std::string GetPlistFileFullName()
    {
        return std::string("/Library/LaunchDaemons/") + GetPlistFileName();
    }
    std::string GetPlistFileName() { return serviceName + ".plist"; }
    std::string binPath;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool requestStop = false;
};

};  // namespace zzj
