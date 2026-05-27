#pragma once
#include <Windows.h>

namespace GGTB::HwFpSpoof
{
// Detour sub_BCCBF0 (HwFp_FillBuffer) inside unpackd_so3d.exe so the
// login-packet hardware-fingerprint region is overwritten with a seeded,
// persistent fake before the caller forwards it to the server.
//
// Layout of the `a2` output buffer (observed in IDA):
//   a2+0x020 (128B) OS/CPU/GPU info #1 — sub_BDC3B0 (RegOpenKeyExW + GetVersionExA)
//   a2+0x0A0 (128B) OS/CPU/GPU info #2 — same source, second copy
//   a2+0x120 ( 64B) memory/disk info   — sub_BDB4C0
//   a2+0x160 ( 32B) locale             — sub_BDAE70 (GetLocaleInfoA ×4)
//   a2+0x180 ( 32B) ComputerName       — sub_BDAA60 (GetComputerNameA)
//   a2+0x1A0 ( 32B) UserName           — sub_BDAA60 (GetUserNameA)
//   a2+0x1C0 ( 36B) MAC string         — sub_BC5A70 (GetAdaptersInfo / NCB_ASTAT)
//   a2+0x1E0 ( 64B) CPUID/disk/SMART/MAC hash — sub_BC5A70 sprintf
//
// TWO-STAGE INSTALL — same shape as NetLog. Binary is Winlicense-packed; the
// real function body at 0xBCCBF0 doesn't exist yet at DllMain. We hook
// kernel32!DeleteFileA first; when WinMain's `DeleteFileA("error.txt")` fires
// we know the unpacker is finished and the .text we want to detour is in
// place. At that point we attach the real hook and leave the DeleteFileA
// shim in the chain (harmless, and lets Uninstall reverse cleanly).
//
// A persistent 16B seed at <dll-dir>/GGConfig/_bootstrap/hwfp_seed.bin keeps
// the spoofed identity stable across restarts — the server sees one fake
// machine, not a new one every launch. Delete the file to rotate identities.
void Install();
void Uninstall();
bool IsActive();
} // namespace GGTB::HwFpSpoof
