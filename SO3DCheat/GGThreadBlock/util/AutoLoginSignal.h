#pragma once

#include <Windows.h>

#include <string>

namespace GGTB::AutoLoginSignal
{
inline std::wstring CharacterReadyEventName(DWORD pid)
{
    return L"Local\\GGTB_AutoLoginCharacterReady_" + std::to_wstring(pid);
}

inline bool SignalCharacterReady(DWORD pid = GetCurrentProcessId())
{
    std::wstring name = CharacterReadyEventName(pid);
    HANDLE h = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
    if (!h)
        return false;

    const BOOL ok = SetEvent(h);
    CloseHandle(h);
    return ok != FALSE;
}
} // namespace GGTB::AutoLoginSignal
