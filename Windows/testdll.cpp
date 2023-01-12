#include <Windows.h>
#include <string>
#include <Windows/util/Process/ProcessHelper.h>
#include <General/util/Process/Process.h>
#include <Windows/util/Device/InputDevice.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <iostream>
DWORD WINAPI HackThread(LPVOID lpThreadParameter)
{
    MessageBoxA(NULL, "start", "gg", MB_OK);
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

        if (zzj::Keyboard::IsKeyUp(VK_ESCAPE))
        {
            return 0;
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