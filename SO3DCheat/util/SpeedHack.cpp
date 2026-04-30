#include "SpeedHack.h"
#include <Detours/build/include/detours.h>
#include <spdlog/spdlog.h>

namespace SO3D::SpeedHack
{

static double s_speed = 1.0;
static bool   s_active = false;

using fnGetTickCount   = DWORD(WINAPI *)();
using fnGetTickCount64 = ULONGLONG(WINAPI *)();
using fnQPC            = BOOL(WINAPI *)(LARGE_INTEGER *);

static fnGetTickCount   s_oGTC   = nullptr;
static DWORD            s_gtcBase = 0, s_gtcOffset = 0;

static fnGetTickCount64 s_oGTC64 = nullptr;
static ULONGLONG        s_gtc64Base = 0, s_gtc64Offset = 0;

static fnQPC            s_oQPC   = nullptr;
static LARGE_INTEGER    s_qpcBase{}, s_qpcOffset{};

static DWORD WINAPI hGetTickCount()
{
    return s_gtcOffset + static_cast<DWORD>((s_oGTC() - s_gtcBase) * s_speed);
}

static ULONGLONG WINAPI hGetTickCount64()
{
    return s_gtc64Offset + static_cast<ULONGLONG>((s_oGTC64() - s_gtc64Base) * s_speed);
}

static BOOL WINAPI hQueryPerformanceCounter(LARGE_INTEGER *lp)
{
    LARGE_INTEGER now;
    s_oQPC(&now);
    lp->QuadPart = s_qpcOffset.QuadPart +
                   static_cast<LONGLONG>((now.QuadPart - s_qpcBase.QuadPart) * s_speed);
    return TRUE;
}

void Setup()
{
    if (s_active) return;

    s_oGTC   = &GetTickCount;
    s_gtcBase   = s_oGTC();
    s_gtcOffset = s_gtcBase;

    s_oGTC64    = &GetTickCount64;
    s_gtc64Base   = s_oGTC64();
    s_gtc64Offset = s_gtc64Base;

    s_oQPC = &QueryPerformanceCounter;
    s_oQPC(&s_qpcBase);
    s_qpcOffset.QuadPart = s_qpcBase.QuadPart;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(reinterpret_cast<PVOID *>(&s_oGTC),   hGetTickCount);
    DetourAttach(reinterpret_cast<PVOID *>(&s_oGTC64),  hGetTickCount64);
    DetourAttach(reinterpret_cast<PVOID *>(&s_oQPC),    hQueryPerformanceCounter);
    DetourTransactionCommit();

    s_active = true;
    spdlog::info("SpeedHack: attached (speed={:.2f})", s_speed);
}

void Detach()
{
    if (!s_active) return;
    SetSpeed(1.0);
    spdlog::info("SpeedHack: disabled (hooks kept, speed=1.0)");
}

void Shutdown()
{
    if (!s_active) return;
    SetSpeed(1.0);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(reinterpret_cast<PVOID *>(&s_oGTC),   hGetTickCount);
    DetourDetach(reinterpret_cast<PVOID *>(&s_oGTC64),  hGetTickCount64);
    DetourDetach(reinterpret_cast<PVOID *>(&s_oQPC),    hQueryPerformanceCounter);
    DetourTransactionCommit();

    s_active = false;
    spdlog::info("SpeedHack: detached");
}

void SetSpeed(double relSpeed)
{
    if (s_active)
    {
        s_gtcOffset = hGetTickCount();
        s_gtcBase   = s_oGTC();

        s_gtc64Offset = hGetTickCount64();
        s_gtc64Base   = s_oGTC64();

        hQueryPerformanceCounter(&s_qpcOffset);
        s_oQPC(&s_qpcBase);
    }
    s_speed = relSpeed;
    spdlog::info("SpeedHack: speed={:.2f}", s_speed);
}

double GetSpeed() { return s_speed; }
bool   IsActive() { return s_active; }

} // namespace SO3D::SpeedHack
