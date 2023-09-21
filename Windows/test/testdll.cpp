#include <Windows.h>
#include <string>
#include <Windows/util/Process/ProcessHelper.h>
#include <General/util/Process/Process.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <iostream>
#include <Detours/build/include/detours.h>

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

static std::uintptr_t moduleBase                                                   = NULL;
static void(__fastcall *RecoilCalculationFunction)(void *thisptr, int edx, float a, float b) = nullptr;

static HHOOK(WINAPI* SetWindowsHookExAFunction)(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId) = nullptr;
static HHOOK(WINAPI* SetWindowsHookExWFunction)(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId) = nullptr;

void __fastcall RecoilCalculationFunctionHook(void *thisptr, int edx, float a, float b)
{
    spdlog::info("Hooked RecoilCalculationFunction");
}

DWORD WINAPI HackThread(LPVOID lpThreadParameter)
{
    AllocConsole();
    FILE *f;
    freopen_s(&f, "CONOUT$", "w+t", stdout);
    spdlog::flush_on(spdlog::level::level_enum::info);
    auto console = spdlog::stdout_color_mt("console2");
    spdlog::set_level(spdlog::level::level_enum::info);
    spdlog::set_default_logger(console);
    

    while (true)
    {
        if (GetAsyncKeyState(VK_HOME) & 1)
        {
			spdlog::info("Hooking");
			DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourAttach(&(PVOID &)RecoilCalculationFunction, RecoilCalculationFunctionHook);
            DetourTransactionCommit();
		}
        if (GetAsyncKeyState(VK_END) & 1)
        {
            spdlog::info("Unhooking");
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourDetach(&(PVOID &)RecoilCalculationFunction, RecoilCalculationFunctionHook);
            DetourTransactionCommit();
		}
		Sleep(100);
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