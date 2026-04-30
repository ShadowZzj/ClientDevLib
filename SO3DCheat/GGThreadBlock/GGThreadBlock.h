#pragma once
#include <Windows.h>

namespace GGTB
{
// Live counter of CreateThread calls that targeted GameGuardDll.dll and were
// swallowed by the detour in dllmain.cpp.
extern volatile LONG g_blockedThreadCount;
} // namespace GGTB
