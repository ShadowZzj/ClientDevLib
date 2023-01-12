#include <iostream>
#include <Windows/util/Process/ProcessHelper.h>
#include <General/util/Process/Process.h>
#include <Windows/util/Device/InputDevice.h>
#include <spdlog/spdlog.h>
#include <General/ThirdParty/GHInjector/Injection.h>
#include <filesystem>

int main()
{
    zzj::GHInJector injector;
    auto processes      = zzj::ProcessV2::GetRunningProcesses();
    DWORD pid = 0;
    for(auto& p : processes)
    {
        if(p.processName == "ac_client.exe")
        {
            pid = p.pid;
        }
    }
    if (pid == 0)
        return -1;
    char buf[MAX_PATH]{0};
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    std::filesystem::path dllPath = buf;
    dllPath.remove_filename();
    dllPath /= "ClientDevLib_testdll.dll";
    INJECTIONDATAA data = {"",
                           pid,
                           INJECTION_MODE::IM_LoadLibraryExW,
                           LAUNCH_METHOD::LM_NtCreateThreadEx,
                           NULL,
                           5000,
                           NULL,
                           NULL,
                           true};
    strcpy(data.szDllPath, dllPath.string().c_str());
    auto ret = injector.InjectA(&data);
    spdlog::info("ret : {}", ret);
    getchar();
    return 0;
}