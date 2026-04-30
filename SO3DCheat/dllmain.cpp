#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <filesystem>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include "CheatSetting.h"
#include "modules/GGBypassModule.h"
#include "modules/AttackSpeedModule.h"
#include "modules/DemoModule.h"
#include "modules/MoveSpeedModule.h"
#include "modules/SkillModule.h"
#include "modules/SpeedHackModule.h"
#include "modules/AutoSwitchModule.h"
#include "modules/AutoReloginModule.h"
#include "modules/NetworkMonitorModule.h"
#include "util/PatternResolver.h"
#include "util/SpeedHack.h"

extern "C" __declspec(dllexport) void SO3DCheatEntry() {}

static void EnableGameLog()
{
    auto base = reinterpret_cast<uintptr_t>(GetModuleHandleA("SO3D.exe"));
    if (!base)
        return;
    *reinterpret_cast<BYTE *>(base + 0xC6ACB5) = 1;
    spdlog::info("Game internal file log enabled");
}

static std::string GetDllDirectory(HMODULE hModule)
{
    char path[MAX_PATH]{};
    GetModuleFileNameA(hModule, path, MAX_PATH);
    return std::filesystem::path(path).parent_path().string();
}

static void InitLogger(HMODULE hModule)
{
    auto logPath = GetDllDirectory(hModule) + "\\cheat.log";
    auto logger  = spdlog::basic_logger_mt("cheat", logPath, true);
    logger->set_level(spdlog::level::info);
    logger->flush_on(spdlog::level::info);
    spdlog::set_default_logger(logger);
}

DWORD WINAPI HackThread(LPVOID lpParam)
{
    SO3D::PatternResolver::Init((HMODULE)lpParam);
    EnableGameLog();

    auto setting     = std::make_shared<CheatSetting>();
    auto ggBypass    = std::make_shared<GGBypassModule>();
    auto netMon      = std::make_shared<NetworkMonitorModule>();
    auto autoRelogin = std::make_shared<AutoReloginModule>();

    ggBypass->AdoptOrApply();
    netMon->InstallHooks();

    setting->RegisterModule(ggBypass);
    setting->RegisterModule(std::make_shared<MoveSpeedModule>());
    setting->RegisterModule(std::make_shared<AttackSpeedModule>());
    setting->RegisterModule(std::make_shared<SkillModule>());
    setting->RegisterModule(std::make_shared<SpeedHackModule>());
    setting->RegisterModule(std::make_shared<AutoSwitchModule>(setting.get()));
    setting->RegisterModule(std::make_shared<DemoModule>());
    setting->RegisterModule(netMon);
    setting->RegisterModule(autoRelogin);

    try
    {
        zzj::D3D::D3D9Hook::Setup(setting);

        while (true)
        {
            if (GetAsyncKeyState(VK_END) & 1)
                break;
            Sleep(100);
        }
    }
    catch (const std::exception &e)
    {
        spdlog::error("HackThread exception: {}", e.what());
        MessageBoxA(NULL, e.what(), "SO3DCheat Error", MB_OK | MB_ICONERROR);
    }

    setting->End();
    SO3D::SpeedHack::Shutdown();
    zzj::D3D::D3D9Hook::Destroy();
    setting.reset();

    spdlog::info("SO3DCheat unloaded");
    spdlog::shutdown();

    FreeLibraryAndExitThread((HMODULE)lpParam, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        GGBypassModule::EarlyPatch();
        InitLogger(hModule);
        spdlog::info("SO3DCheat DLL Attached!");
        CloseHandle(CreateThread(NULL, 0, HackThread, hModule, 0, NULL));
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
