#define WIN32_LEAN_AND_MEAN
#include "GameSetting.h"
#include <General/util/File/File.h>
#include <General/util/Process/Process.h>
#include <Windows.h>
#include <Windows/util/DirectX/D3D9Hook.h>
#include <boost/filesystem.hpp>
#include <iostream>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <string>
#include "GameManager.h"
#include "GameSetting.h"
extern "C" __declspec(dllexport) void test()
{
    spdlog::info("test called");
    return;
}

DWORD WINAPI HackThread(LPVOID lpThreadParameter)
{
    std::shared_ptr<zzj::D3D::Setting> setting = std::make_shared<GameSetting>();
    try
    {
        zzj::D3D::D3D9Hook::Setup(setting);
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
    setting.reset();
    FreeLibraryAndExitThread((HMODULE)lpThreadParameter, 0);
    return 0;
}
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        InitLog("default");
        gameManager.HookMachineCode();
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