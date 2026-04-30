#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Detours/build/include/detours.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <memory>

#include "GGThreadBlock.h"
#include "Setting.h"
#include "util/PatternResolver.h"
#include "util/SpeedHack.h"
#include "modules/StatusModule.h"
#include "modules/MoveSpeedModule.h"
#include "modules/AttackSpeedModule.h"
#include "modules/SkillSpeedModule.h"
#include "modules/SpeedHackModule.h"
#include "modules/ItemNoCDModule.h"
#include "modules/AttackRangeModule.h"
#include "modules/NearbyPlayerGuardModule.h"

namespace GGTB
{
volatile LONG g_blockedThreadCount = 0;
} // namespace GGTB

// ============================================================
//  CreateThread detour — swallow GameGuardDll.dll worker threads
// ============================================================

using fnCreateThread = HANDLE(WINAPI *)(LPSECURITY_ATTRIBUTES, SIZE_T,
                                        LPTHREAD_START_ROUTINE, LPVOID,
                                        DWORD, LPDWORD);

static fnCreateThread g_oCreateThread = CreateThread;

static DWORD WINAPI DummyLoopThread(LPVOID)
{
    for (;;)
        Sleep(INFINITE);
    return 0;
}

static bool IsAddressInGameGuard(LPCVOID addr)
{
    if (!addr)
        return false;

    HMODULE hMod = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(addr), &hMod) ||
        !hMod)
        return false;

    char  path[MAX_PATH] = {};
    DWORD n = GetModuleFileNameA(hMod, path, MAX_PATH);
    if (n == 0)
        return false;

    const char *base = path;
    for (DWORD i = 0; i < n; ++i)
        if (path[i] == '\\' || path[i] == '/')
            base = path + i + 1;

    return _stricmp(base, "GameGuardDll.dll") == 0;
}

static HANDLE WINAPI HookCreateThread(LPSECURITY_ATTRIBUTES sec, SIZE_T stack,
                                      LPTHREAD_START_ROUTINE start, LPVOID param,
                                      DWORD flags, LPDWORD outTid)
{
    if (IsAddressInGameGuard(reinterpret_cast<LPCVOID>(start)))
    {
        InterlockedIncrement(&GGTB::g_blockedThreadCount);
        OutputDebugStringA("[GGThreadBlock] blocked GameGuard thread\n");
        return g_oCreateThread(sec, stack, DummyLoopThread, nullptr, flags, outTid);
    }
    return g_oCreateThread(sec, stack, start, param, flags, outTid);
}

static void InstallDetour()
{
    DetourRestoreAfterWith();
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(reinterpret_cast<PVOID *>(&g_oCreateThread), HookCreateThread);
    DetourTransactionCommit();
}

static void UninstallDetour()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(reinterpret_cast<PVOID *>(&g_oCreateThread), HookCreateThread);
    DetourTransactionCommit();
}

// ============================================================
//  Logger / Hack thread
// ============================================================

static std::string GetDllDirectory(HMODULE hModule)
{
    char path[MAX_PATH]{};
    GetModuleFileNameA(hModule, path, MAX_PATH);
    return std::filesystem::path(path).parent_path().string();
}

static void InitLogger(HMODULE hModule)
{
    auto logPath = GetDllDirectory(hModule) + "\\ggtb.log";
    auto logger  = spdlog::basic_logger_mt("ggtb", logPath, true);
    logger->set_level(spdlog::level::info);
    logger->flush_on(spdlog::level::info);
    spdlog::set_default_logger(logger);
}

static DWORD WINAPI HackThread(LPVOID lpParam)
{
    auto hModule = static_cast<HMODULE>(lpParam);

    GGTB::PatternResolver::Init(hModule);

    auto setting = std::make_shared<GGTB::Setting>();
    setting->RegisterModule(std::make_shared<GGTB::StatusModule>());
    setting->RegisterModule(std::make_shared<GGTB::MoveSpeedModule>());
    setting->RegisterModule(std::make_shared<GGTB::AttackSpeedModule>());
    setting->RegisterModule(std::make_shared<GGTB::SkillSpeedModule>());
    setting->RegisterModule(std::make_shared<GGTB::SpeedHackModule>());
    setting->RegisterModule(std::make_shared<GGTB::ItemNoCDModule>());
    setting->RegisterModule(std::make_shared<GGTB::AttackRangeModule>());
    // Guard registered LAST so it sees every other module via Setting::GetModules().
    setting->RegisterModule(std::make_shared<GGTB::NearbyPlayerGuardModule>(setting.get()));

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
        spdlog::error("GGTB HackThread exception: {}", e.what());
        MessageBoxA(nullptr, e.what(), "GGThreadBlock Error", MB_OK | MB_ICONERROR);
    }

    setting->End();
    GGTB::SpeedHack::Shutdown();
    zzj::D3D::D3D9Hook::Destroy();
    setting.reset();

    spdlog::info("GGThreadBlock unloaded");
    spdlog::shutdown();

    FreeLibraryAndExitThread(hModule, 0);
    return 0;
}

// ============================================================
//  Exports
// ============================================================

extern "C" __declspec(dllexport) LONG GGThreadBlock_GetBlockedCount()
{
    return GGTB::g_blockedThreadCount;
}

extern "C" __declspec(dllexport) int GGThreadBlock_Test()
{
    MessageBoxA(nullptr, "GGThreadBlock loaded", "GGThreadBlock",
                MB_OK | MB_ICONINFORMATION);
    return 0x1337;
}

// ============================================================
//  DllMain
// ============================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (DetourIsHelperProcess())
        return TRUE;

    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        InstallDetour();
        InitLogger(hModule);
        spdlog::info("GGThreadBlock DLL Attached");
        CloseHandle(CreateThread(nullptr, 0, HackThread, hModule, 0, nullptr));
        break;
    case DLL_PROCESS_DETACH:
        UninstallDetour();
        break;
    }
    return TRUE;
}
