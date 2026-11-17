#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>
#include <Windows/util/DirectX/DWMHook.h>
#include <Windows.h>
#include <iostream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
DWORD WINAPI DoMagic(LPVOID lpParam)
{
     
    AllocConsole();
    spdlog::flush_on(spdlog::level::level_enum::info);
    spdlog::info("start1");
    auto console = spdlog::stdout_color_mt("console2");
    spdlog::set_level(spdlog::level::level_enum::info);
    spdlog::set_default_logger(console);
    spdlog::info("Start");
    
    std::shared_ptr<zzj::D3D::Setting> setting = std::make_shared<zzj::D3D::Setting>();
    zzj::D3D::DWMHook::Setup(setting);
    while (true)
    {
        if (GetAsyncKeyState(VK_END) & 1)
        {
            break;
        }
        Sleep(100);
    }

    setting->End();
    zzj::D3D::DWMHook::Destroy();

    
    FreeConsole();
    setting.reset();
    FreeLibraryAndExitThread((HMODULE)lpParam, 0);
	return 0;
}
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    HANDLE threadHandle;

    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        // Create a thread and close the handle as we do not want to use it to wait for it
        threadHandle = CreateThread(NULL, 0, DoMagic, hModule, 0, NULL);
        CloseHandle(threadHandle);

    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
