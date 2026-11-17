#include "ThreadExecute.h"
#include <General/ThirdParty/Detours/build/include/detours.h>
#include <functional>
#include <spdlog/spdlog.h>
bool IsDebuggerAttached(DWORD reserved, LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter)
{
    auto DbgUiRemoteBreakin = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandle(L"ntdll.dll"), "DbgUiRemoteBreakin");
    if (lpStartAddress == DbgUiRemoteBreakin)
        return true;


    return false;
}

#ifdef _WIN64
using BaseThreadInitThunkType = DWORD(__fastcall *)(DWORD reserved, LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter);
BaseThreadInitThunkType BaseThreadInitThunk_original = nullptr;

DWORD __fastcall BaseThreadInitThunk_hook(DWORD reserved, LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter)
{
    IsDebuggerAttached(reserved, lpStartAddress, lpParameter);
    return BaseThreadInitThunk_original(reserved, lpStartAddress, lpParameter);
}

#else
using BaseThreadInitThunkType = DWORD(__stdcall *)(DWORD reserved, LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter);
BaseThreadInitThunkType BaseThreadInitThunk_original = nullptr;
DWORD __stdcall BaseThreadInitThunk_hook(DWORD reserved, LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter)
{
    IsDebuggerAttached(reserved, lpStartAddress, lpParameter);
    return BaseThreadInitThunk_original(reserved, lpStartAddress, lpParameter);
}

#endif

int zzj::AntiDebug::ThreadExecute()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID &)BaseThreadInitThunk_original, BaseThreadInitThunk_hook);
    DetourTransactionCommit();


    return 0;
}