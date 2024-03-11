#define WIN32_LEAN_AND_MEAN
#include <Windows/util/DirectX/D3D9Hook.h>
#include <General/util/Process/Process.h>
#include <Windows.h>
#include <iostream>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <string>
#include "GameSetting.h"
DWORD WINAPI HackThread(LPVOID lpThreadParameter)
{
    FILE *f;
    std::shared_ptr<zzj::D3D::Setting> setting = std::make_shared<GameSetting>();
    try
    {
        AllocConsole();
        freopen_s(&f, "CONOUT$", "w+t", stdout);
        system("chcp 65001");
        spdlog::flush_on(spdlog::level::level_enum::info);
        auto console = spdlog::stdout_color_mt("console2");
        spdlog::set_level(spdlog::level::level_enum::info);
        spdlog::set_default_logger(console);
        spdlog::info("Start");
        zzj::D3D::D3D9Hook::Setup(setting);
        spdlog::info("SetupDone");
        while (true)
        {
            if (GetAsyncKeyState(VK_END) & 1)
            {
                break;
            }
            Sleep(100);
        }
    }
    catch (const std::exception &e)
    {
        MessageBeep(MB_ICONERROR);
        MessageBoxA(NULL, e.what(), "hack Error", MB_OK | MB_ICONERROR);
    }

    setting->End();
    zzj::D3D::D3D9Hook::Destroy();
    fclose(f);
    FreeConsole();
    setting.reset();
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