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
    
}