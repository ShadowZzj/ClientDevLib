#include <General/util/StrUtil.h>
#include <boost/locale.hpp>
#include <codecvt>
#include <iostream>
#include <spdlog/spdlog.h>
#include <vector>

#include <General/util/File/File.h>
#include <Windows/util/Service/Service.h>
#include <string>
class Watcher : public zzj::Service
{
  public:
    virtual void ServiceFunc() override
    {
        while (1)
        {
            static uint32_t loopCount = 0;
            if (auto checkStatus = CheckSafeStop(5); zzj::ServiceInterface::ControlStatus::RequestStop == checkStatus)
            {
                spdlog::info("Running");
                break;
            }
        }
    }
    virtual void OnStop() override
    {
    }
    Watcher(std::string _binPath, std::string name, std::string description, std::string displayName)
        : zzj::Service(name, _binPath, description, displayName)
    {
        serviceName = name;
    }

  protected:
    std::string version;
};

int MyChangeServiceConfig()
{
    char current_proc_path[MAX_PATH] = {0};
    ::GetModuleFileNameA(NULL, current_proc_path, MAX_PATH);
    SC_HANDLE hSC = ::OpenSCManagerA(NULL, NULL, GENERIC_EXECUTE);
    if (hSC == NULL)
        return -1;

    SC_HANDLE hSvc = ::OpenServiceA(hSC, "testservice", SERVICE_CHANGE_CONFIG | SERVICE_QUERY_STATUS);
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
    if (!ChangeServiceConfigA(hSvc, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, current_proc_path, NULL,
                              NULL, NULL, NULL, NULL, NULL))
    {
        ::CloseServiceHandle(hSvc);
        ::CloseServiceHandle(hSC);
        return -4;
    }
    ::CloseServiceHandle(hSvc);
    ::CloseServiceHandle(hSC);
    return 0;
}
int main(int argc, char *argv[])
{
    char current_proc_path[MAX_PATH] = {0};
    ::GetModuleFileNameA(NULL, current_proc_path, MAX_PATH);
    Watcher *watcher = new Watcher("C:\\Windows\\System32\\svchost.exe", "testservice", "testservice", "testservice");

    if (argc == 1)
    {
        watcher->Run();
        return 0;
    }
    string cmd = argv[1];

    if (cmd == "-uninstall")
    {
        watcher->Uninstall();
    }
    if (cmd == "-install")
    {
        watcher->Install();
        MyChangeServiceConfig();
    }
    if (cmd == "-start")
    {
        watcher->Start();
    }

    if (cmd == "-stop")
    {
        watcher->Stop();
    }
    return 0;
}