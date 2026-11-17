#include <General/util/Process/Process.h>
#include <Windows.h>
#include <Windows/util/Process/ProcessHelper.h>
#include <iostream>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <string>
#include <Detours/build/include/detours.h>

int argument = 2;
void *hookAddress = (void*)0x005A09B7;

void *attackRangeRetAddress;
DWORD attackRange = 1;
int __declspec(naked) HookedMessageBoxA()
{
    __asm
    {
        pushad
    }
    attackRangeRetAddress = (void *)((uintptr_t)hookAddress + 10);
    __asm
    {
        mov eax,attackRange
        mov [ebx + 0x28d4],eax
        popad
        jmp retAddress
    }
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
            DetourAttach(&(PVOID &)hookAddress, &HookedMessageBoxA);
            DetourTransactionCommit();
        }
        if (GetAsyncKeyState(VK_END) & 1)
        {
            spdlog::info("Unhooking");
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourDetach(&(PVOID &)hookAddress, &HookedMessageBoxA);
            DetourTransactionCommit();
            break;
        }

        // add argument
        if (GetAsyncKeyState(VK_NUMPAD1) & 1)
        {
            attackRange++;
            spdlog::info("Argument: {}", attackRange);
        }
        if (GetAsyncKeyState(VK_NUMPAD2) & 1)
        {
            attackRange--;
            spdlog::info("Argument: {}", attackRange);
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