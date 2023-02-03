#include <Windows.h>
#include <string>
#include <Windows/util/Process/ProcessHelper.h>
#include <General/util/Process/Process.h>
#include <Windows/util/Device/InputDevice.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <iostream>


bool HookAddress(uintptr_t target, uintptr_t ourFunc, int hookLen)
{
    
    if (hookLen < 5)
        return false;

    zzj::Process p;
    zzj::Memory m(p);

    zzj::ProcessV2::SuspendPid(p.GetProcessId());
    DWORD oldProtect;
    VirtualProtectEx(GetCurrentProcess(), (LPVOID)target, hookLen, PAGE_GRAPHICS_EXECUTE_READWRITE, &oldProtect);
    std::vector<uint8_t> data(hookLen, 0x90);
    m.Write(target, data);
    
    auto detourAddress = ourFunc - target - 5;
    m.Write(target, {0xE9});
    m.Write(target + 1, &detourAddress, sizeof(detourAddress));
    VirtualProtectEx(GetCurrentProcess(), (LPVOID)target, hookLen, oldProtect, &oldProtect);
    Sleep(3000);
    zzj::ProcessV2::ResumePid(p.GetProcessId());
    return true;
}

bool TrampolionHook(uintptr_t target, uintptr_t ourFunc, int hookLen)
{
    zzj::Process p;
    zzj::Memory m(p);
    auto gateway = m.Alloc(hookLen);
    m.Write(gateway, (void*)target, hookLen);
    m.Write(gateway + hookLen, {0xE9});
    auto relativeAddress = target - gateway - 5;
    m.Write(gateway+hookLen+1,&relativeAddress,sizeof(relativeAddress));

    HookAddress(target, ourFunc, hookLen);
	return true;
}

void  OurFunc()
{
    spdlog::info("In hook");

}
DWORD WINAPI HackThread(LPVOID lpThreadParameter)
{
    bool bHealth = false;
    bool bAmmo   = false;
    std::string processName = "ac_client.exe";
    zzj::Process p;
    zzj::Memory m(p);

    AllocConsole();
    FILE *f;
    freopen_s(&f, "CONOUT$", "w+t", stdout);
    spdlog::flush_on(spdlog::level::level_enum::info);
    auto console = spdlog::stdout_color_mt("console2");
    spdlog::set_level(spdlog::level::level_enum::info);
    spdlog::set_default_logger(console);


    auto baseAddress = p.GetModuleBaseAddress(processName);
    uintptr_t localPlayerPtr = baseAddress+0x0011E20C;

    auto ammoAddress = m.FindMultiPleLevelAddress(localPlayerPtr, {0x374, 0x14, 0});
    auto healthAddr  = m.FindMultiPleLevelAddress(localPlayerPtr, {0xf8});

    spdlog::info("localPlayerPtr: {:#x}", localPlayerPtr);
    spdlog::info("base address: {:#x}", baseAddress);
    spdlog::info("ammo address: {:#x}", ammoAddress);
    spdlog::info("health address: {:#x}", healthAddr);

    auto testHookAddress = baseAddress + 0x62020;
    bool isHook          = false;


    while (p.IsAlive())
    {
        if (zzj::Keyboard::IsKeyUp('1'))
        {
            bHealth = !bHealth;
            spdlog::info("health: {}", bHealth);
        }
        if (zzj::Keyboard::IsKeyUp('2'))
        {
            bAmmo = !bAmmo;
            if (bAmmo)
            {
                spdlog::info("ammo unlimited!");
                m.Write(baseAddress + 0x637e9, {0xFF,0x6});
            }
            else
            {
                spdlog::info("ammo limited!");
                m.Write(baseAddress + 0x637e9, {0xFF, 0xE});
            }
        }

        if (zzj::Keyboard::IsKeyUp('3'))
        {
            isHook = !isHook;
            if (isHook)
            {
                spdlog::info("hook on");
                HookAddress(testHookAddress, (uintptr_t)OurFunc, 6);
            }
            else
            {
                spdlog::info("hook off");
                m.Write(testHookAddress, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8});
            }
        }
        if (zzj::Keyboard::IsKeyUp(VK_END))
        {
            break;
        }

        if (bHealth)
            m.Write(healthAddr, {100});
        Sleep(10);
    }

    fclose(f);
    FreeConsole();
    FreeLibraryAndExitThread((HMODULE)lpThreadParameter, 0);
    return 0;

}
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        CloseHandle(CreateThread(NULL, 0, HackThread, hModule, 0, NULL));
        break;
    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}