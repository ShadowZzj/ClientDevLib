#include <iostream>
#include <Windows/util/Process/ProcessHelper.h>
#include <General/util/Process/Process.h>
int main()
{
    std::string processName = "ac_client.exe";
    auto runningProcesses  = zzj::ProcessV2::GetRunningProcesses();
    int pid = 0;
    for(auto &p : runningProcesses)
    {
        if (p.processName == processName)
            pid = p.pid;
    }
    if(pid == 0)
        return 0;
    
    
    zzj::Process p(pid,PROCESS_ALL_ACCESS);
    auto baseAddress = p.GetModuleBaseAddress(processName);
    std::cout << "base address: " << std::hex << baseAddress << std::endl;
    zzj::Memory m(p);
    uintptr_t dynamicBase = baseAddress+0x0011E20C;
    std::cout << "dynamic base: " << std::hex << dynamicBase << std::endl;
    std::vector<unsigned int> ammoOffsets= {0x374,0x14,0};

    auto ammoAddress = m.FindMultiPleLevelAddress(dynamicBase,ammoOffsets);
    std::cout << "ammo address: " << std::hex << ammoAddress << std::endl;
    int ammo = 0;
    m.Read(ammoAddress,&ammo,sizeof(int));
    std::cout << "ammo: " << std::dec << ammo << std::endl;
    ammo = 999;
    m.Write(ammoAddress,&ammo,sizeof(int));
    m.Read(ammoAddress,&ammo,sizeof(int));
    std::cout << "ammo: " << std::dec << ammo << std::endl;
    system("pause");
    return 0;
}