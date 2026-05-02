#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Detours/build/include/detours.h>
#include <spdlog/spdlog.h>
#include <intrin.h>
#include <memory>

#pragma intrinsic(_ReturnAddress)

#include "GGThreadBlock.h"
#include "Setting.h"
#include "util/PatternResolver.h"
#include "util/SpeedHack.h"
#include "util/UserConfig.h"
#include "modules/StatusModule.h"
#include "modules/MoveSpeedModule.h"
#include "modules/AttackSpeedModule.h"
#include "modules/SkillSpeedModule.h"
#include "modules/SpeedHackModule.h"
#include "modules/ItemNoCDModule.h"
#include "modules/AttackRangeModule.h"
#include "modules/ActionMoveModule.h"
#include "modules/AutoPickupModule.h"
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

// ============================================================
//  SetWindowsHookExA/W detour — fake-success the host exe's keyboard hook
//
//  Host exe installs a WH_KEYBOARD_LL (or legacy WH_KEYBOARD on old NT) hook
//  in WinMain that swallows VK_LWIN/VK_RWIN (and Ctrl+Esc on the legacy path)
//  for "fullscreen anti-mistake" — see RE notes on KeyboardHookProc_BlockWinKey
//  at unpackd_so3d.exe!0x978c50. We want Win key + task switching back, so we
//  return a sentinel HHOOK that pretends the install succeeded; UnhookWindowsHookEx
//  recognises the sentinel and reports success too.
//
//  Filter: only the host EXE's keyboard hooks are swallowed. Other DLLs (input
//  overlays, IME, accessibility tools, even GameGuard if it ever installed one)
//  pass through unmodified — _ReturnAddress() gives us the real caller.
// ============================================================

using fnSetWindowsHookExA   = HHOOK(WINAPI *)(int, HOOKPROC, HINSTANCE, DWORD);
using fnSetWindowsHookExW   = HHOOK(WINAPI *)(int, HOOKPROC, HINSTANCE, DWORD);
using fnUnhookWindowsHookEx = BOOL(WINAPI *)(HHOOK);

static fnSetWindowsHookExA   g_oSetWindowsHookExA   = SetWindowsHookExA;
static fnSetWindowsHookExW   g_oSetWindowsHookExW   = SetWindowsHookExW;
static fnUnhookWindowsHookEx g_oUnhookWindowsHookEx = UnhookWindowsHookEx;

// Non-NULL sentinel ('GGHB' little-endian). Distinct enough that
// UnhookWindowsHookEx can tell it apart from a real OS-allocated HHOOK.
static HHOOK const kFakeKeyboardHook = reinterpret_cast<HHOOK>(static_cast<uintptr_t>(0x42484747));

static bool IsAddressInHostExe(LPCVOID addr)
{
    if (!addr)
        return false;
    HMODULE hMod = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(addr), &hMod) ||
        !hMod)
        return false;
    return hMod == GetModuleHandleW(nullptr);
}

static bool ShouldFakeKeyboardHook(int idHook, LPCVOID caller)
{
    if (idHook != WH_KEYBOARD && idHook != WH_KEYBOARD_LL)
        return false;
    return IsAddressInHostExe(caller);
}

static HHOOK WINAPI HookSetWindowsHookExA(int idHook, HOOKPROC lpfn,
                                          HINSTANCE hMod, DWORD dwThreadId)
{
    PVOID caller = _ReturnAddress();
    if (ShouldFakeKeyboardHook(idHook, caller))
    {
        OutputDebugStringA("[GGThreadBlock] swallowed host SetWindowsHookExA(KEYBOARD)\n");
        return kFakeKeyboardHook;
    }
    return g_oSetWindowsHookExA(idHook, lpfn, hMod, dwThreadId);
}

static HHOOK WINAPI HookSetWindowsHookExW(int idHook, HOOKPROC lpfn,
                                          HINSTANCE hMod, DWORD dwThreadId)
{
    PVOID caller = _ReturnAddress();
    if (ShouldFakeKeyboardHook(idHook, caller))
    {
        OutputDebugStringA("[GGThreadBlock] swallowed host SetWindowsHookExW(KEYBOARD)\n");
        return kFakeKeyboardHook;
    }
    return g_oSetWindowsHookExW(idHook, lpfn, hMod, dwThreadId);
}

static BOOL WINAPI HookUnhookWindowsHookEx(HHOOK hhk)
{
    if (hhk == kFakeKeyboardHook)
        return TRUE;
    return g_oUnhookWindowsHookEx(hhk);
}

static void InstallDetour()
{
    DetourRestoreAfterWith();
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(reinterpret_cast<PVOID *>(&g_oCreateThread),         HookCreateThread);
    DetourAttach(reinterpret_cast<PVOID *>(&g_oSetWindowsHookExA),    HookSetWindowsHookExA);
    DetourAttach(reinterpret_cast<PVOID *>(&g_oSetWindowsHookExW),    HookSetWindowsHookExW);
    DetourAttach(reinterpret_cast<PVOID *>(&g_oUnhookWindowsHookEx),  HookUnhookWindowsHookEx);
    DetourTransactionCommit();
}

static void UninstallDetour()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(reinterpret_cast<PVOID *>(&g_oCreateThread),         HookCreateThread);
    DetourDetach(reinterpret_cast<PVOID *>(&g_oSetWindowsHookExA),    HookSetWindowsHookExA);
    DetourDetach(reinterpret_cast<PVOID *>(&g_oSetWindowsHookExW),    HookSetWindowsHookExW);
    DetourDetach(reinterpret_cast<PVOID *>(&g_oUnhookWindowsHookEx),  HookUnhookWindowsHookEx);
    DetourTransactionCommit();
}

// ============================================================
//  Hack thread
// ============================================================

static DWORD WINAPI HackThread(LPVOID lpParam)
{
    auto hModule = static_cast<HMODULE>(lpParam);

    // Bootstrap logger to <dll-dir>/GGConfig/_bootstrap/bootstrap.log first —
    // PatternResolver::Init logs to the default sink, so it must exist by then.
    GGTB::UserConfig::Bootstrap(hModule);
    spdlog::info("GGThreadBlock DLL Attached");

    GGTB::PatternResolver::Init(hModule);

    auto setting = std::make_shared<GGTB::Setting>();
    setting->RegisterModule(std::make_shared<GGTB::StatusModule>());
    setting->RegisterModule(std::make_shared<GGTB::MoveSpeedModule>());
    setting->RegisterModule(std::make_shared<GGTB::AttackSpeedModule>());
    setting->RegisterModule(std::make_shared<GGTB::SkillSpeedModule>());
    setting->RegisterModule(std::make_shared<GGTB::SpeedHackModule>());
    setting->RegisterModule(std::make_shared<GGTB::ItemNoCDModule>());
    setting->RegisterModule(std::make_shared<GGTB::AttackRangeModule>());
    setting->RegisterModule(std::make_shared<GGTB::ActionMoveModule>());
    setting->RegisterModule(std::make_shared<GGTB::AutoPickupModule>());
    // Guard registered LAST so it sees every other module via Setting::GetModules().
    setting->RegisterModule(std::make_shared<GGTB::NearbyPlayerGuardModule>(setting.get()));

    try
    {
        zzj::D3D::D3D9Hook::Setup(setting);

        while (true)
        {
            if (GetAsyncKeyState(VK_END) & 1)
                break;
            GGTB::UserConfig::Tick(setting.get());
            Sleep(100);
        }
    }
    catch (const std::exception &e)
    {
        spdlog::error("GGTB HackThread exception: {}", e.what());
        MessageBoxA(nullptr, e.what(), "GGThreadBlock Error", MB_OK | MB_ICONERROR);
    }

    // Persist current state BEFORE setting->End() — OnShutdown may reset
    // enabled_/slider values and we want to save what the user actually had.
    GGTB::UserConfig::SaveModuleStates(setting.get());

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
        // Logger init moved into HackThread — file IO + spdlog registry mutex
        // under loader-lock can deadlock.
        CloseHandle(CreateThread(nullptr, 0, HackThread, hModule, 0, nullptr));
        break;
    case DLL_PROCESS_DETACH:
        UninstallDetour();
        break;
    }
    return TRUE;
}
