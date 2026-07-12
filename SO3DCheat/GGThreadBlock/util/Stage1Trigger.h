#pragma once
#include <Windows.h>
#include <functional>

namespace GGTB::Stage1Trigger
{
// Single DeleteFileA("error.txt") shim shared by every module that needs to
// attach AFTER the Winlicense unpacker has finished but BEFORE game code
// starts running. See NetLog.cpp / HwFpSpoof.cpp for the original context —
// both used to install their own DeleteFileA detour independently; this
// consolidates them so we only pay the kernel32 trampoline cost once.
//
// Usage:
//   Stage1Trigger::Register([]{ /* stage-2 */ }); // queue in required order
//   Stage1Trigger::Install();                     // arm after the queue is ready
//   ...
//   Stage1Trigger::Uninstall();                 // on HackThread exit
//
// Callbacks fire on the thread that ran WinMain's first DeleteFileA, in
// registration order. If Register is called AFTER the trigger has already
// fired, the callback runs inline on the caller's thread — so registration
// order vs. WinMain timing doesn't matter.
bool Install();
void Register(std::function<void()> callback);

// Atomically cancel callbacks only if the WinMain signal has not started
// firing them yet. Returns false once FireAll owns the callback batch.
bool CancelPending();

void Uninstall();
} // namespace GGTB::Stage1Trigger
