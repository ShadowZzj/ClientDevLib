#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "util/LoginBridge.h"

static DWORD WINAPI BridgeThread(LPVOID)
{
    GGTB::LoginBridge::Install();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        if (HANDLE hThread = CreateThread(nullptr, 0, BridgeThread, nullptr, 0, nullptr))
            CloseHandle(hThread);
        break;
    case DLL_PROCESS_DETACH:
        GGTB::LoginBridge::Uninstall();
        break;
    }
    return TRUE;
}
