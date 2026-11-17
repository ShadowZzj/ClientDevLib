#pragma once
#include <Windows.h>
#include <cstdint>

namespace zzj::AntiDebug
{
int ExceptionCheck();
BOOL IsSofewareBreakPointInRange(std::uintptr_t start, std::uintptr_t end);
}; // namespace zzj::AntiDebug