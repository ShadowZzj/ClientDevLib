#define WIN32_LEAN_AND_MEAN
#include "HwFpSpoof.h"
#include "PatternResolver.h"
#include "Stage1Trigger.h"
#include "UserConfig.h"

#include <Detours/build/include/detours.h>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <share.h>

// HwFp_FillBuffer (sub_BCCBF0 @ RVA 0x7CCBF0) is __thiscall. Same __fastcall
// trick as NetLog — on x86 MSVC, __fastcall(ecx, edx, stack...) matches
// __thiscall(ecx, stack...) as long as EDX is a free scratch, which it is at
// every call site (thiscall callers don't set EDX).

namespace GGTB::HwFpSpoof
{
namespace fs = std::filesystem;

namespace
{
using fnHwFpFillBuffer = int(__fastcall *)(void *ecx, void *edx, uint8_t *outPkt);

fnHwFpFillBuffer g_oHwFpFillBuffer = nullptr;

std::atomic<bool> s_attached{false};
std::once_flag    s_seedOnce;

// 16 bytes of persistent randomness, re-used across restarts. Everything we
// spoof (MAC, computer name, user name, registry/CPUID-like hex dumps) is
// derived from this seed, so the server sees ONE stable fake machine rather
// than a different identity every launch.
uint8_t s_seed[16] = {0};

// Layout captured from sub_BCCBF0 decompile + callers. Out-buffer inside
// login packet, hardware-fingerprint region starts at +0x20.
constexpr uint32_t kOffOsCpuGpu1   = 0x020; // 128B strcpy (sub_BDC3B0)
constexpr uint32_t kOffOsCpuGpu2   = 0x0A0; // 128B strcpy (second copy)
constexpr uint32_t kOffMemDisk     = 0x120; //  64B strcpy (sub_BDB4C0)
constexpr uint32_t kOffLocale      = 0x160; //  32B strcpy (sub_BDAE70)
constexpr uint32_t kOffComputer    = 0x180; //  32B strcpy (sub_BDAA60 part 1)
constexpr uint32_t kOffUser        = 0x1A0; //  32B strcpy (sub_BDAA60 part 2)
constexpr uint32_t kOffDiskMac     = 0x1C0; //  ~36B DeviceIoControl MAC
constexpr uint32_t kOffCpuidHashes = 0x1E0; //  sub_57D930 sprintf target

// ---- Seed management ----

fs::path SeedPath()
{
    // UserDir() returns <dll-dir>/GGConfig/_bootstrap/ until UserConfig
    // upgrades to a character folder. We want the seed global to the
    // install, not per-character, so hard-route to the bootstrap folder
    // regardless of UserConfig state. UserConfig::Bootstrap creates this
    // directory during HackThread startup — well before we install here.
    auto dir = GGTB::UserConfig::UserDir();
    // If UserConfig hasn't bootstrapped yet (should not happen: Install is
    // called after NetLog::Install, which is called after Bootstrap), fall
    // back to CWD so we don't crash. Seed may then be non-persistent, but
    // nothing else breaks.

    // GGThreadBlockStarter sets GGTB_HWFP_GROUP=<idx> so every 3 accounts
    // share one fake hardware identity. Each group gets its own seed file;
    // absence of the env var (running standalone / non-launcher flow) keeps
    // the original single-seed behaviour.
    wchar_t groupBuf[16] = {0};
    DWORD got = GetEnvironmentVariableW(L"GGTB_HWFP_GROUP", groupBuf,
                                        static_cast<DWORD>(std::size(groupBuf)));
    std::wstring fileName = L"hwfp_seed.bin";
    if (got > 0 && got < std::size(groupBuf))
    {
        // Validate digits-only to avoid weird filenames if env is poisoned.
        bool ok = true;
        for (DWORD i = 0; i < got; ++i)
            if (groupBuf[i] < L'0' || groupBuf[i] > L'9') { ok = false; break; }
        if (ok)
            fileName = std::wstring(L"hwfp_seed_group") + groupBuf + L".bin";
    }

    if (dir.empty()) return fs::path{fileName};
    return dir / fileName;
}

void EnsureSeed()
{
    std::call_once(s_seedOnce, []() {
        auto path = SeedPath();

        FILE *fp = ::_wfsopen(path.c_str(), L"rb", _SH_DENYNO);
        if (fp)
        {
            size_t n = std::fread(s_seed, 1, sizeof(s_seed), fp);
            std::fclose(fp);
            if (n == sizeof(s_seed))
            {
                spdlog::info("GGTB::HwFpSpoof: loaded seed from {}", path.string());
                return;
            }
        }

        // Generate with RtlGenRandom via advapi32 to avoid CryptAcquireContext.
        // Fallback to a clock/PID mix if anything fails — still gives distinct
        // values across installs.
        HMODULE h = LoadLibraryA("advapi32.dll");
        using fnRtlGenRandom = BOOLEAN(APIENTRY *)(PVOID, ULONG);
        fnRtlGenRandom rtl = nullptr;
        if (h)
            rtl = reinterpret_cast<fnRtlGenRandom>(GetProcAddress(h, "SystemFunction036"));

        bool ok = false;
        if (rtl && rtl(s_seed, sizeof(s_seed)))
            ok = true;

        if (!ok)
        {
            // Mix the group index so that even on the RtlGenRandom-failure
            // path two different groups can't accidentally collide on the
            // same (tick, pid) state. PID already differs per process, but
            // the group number is what guarantees cross-group separation.
            wchar_t groupBuf[16] = {0};
            DWORD   got          = GetEnvironmentVariableW(
                L"GGTB_HWFP_GROUP", groupBuf,
                static_cast<DWORD>(std::size(groupBuf)));
            uint32_t groupMix = 0;
            for (DWORD i = 0; i < got && i < std::size(groupBuf); ++i)
                groupMix = groupMix * 31 + static_cast<uint32_t>(groupBuf[i]);

            uint64_t t   = GetTickCount64();
            uint32_t pid = GetCurrentProcessId();
            for (size_t i = 0; i < sizeof(s_seed); ++i)
                s_seed[i] = static_cast<uint8_t>((t >> (i * 4)) ^ (pid >> (i & 3)) ^
                                                 (groupMix >> (i & 3)) ^ (i * 0x9Bu));
        }

        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        fp = ::_wfsopen(path.c_str(), L"wb", _SH_DENYNO);
        if (fp)
        {
            std::fwrite(s_seed, 1, sizeof(s_seed), fp);
            std::fclose(fp);
            spdlog::info("GGTB::HwFpSpoof: generated new seed at {}", path.string());
        }
        else
        {
            spdlog::warn("GGTB::HwFpSpoof: failed to write seed at {}", path.string());
        }
    });
}

// ---- Fake field formatting ----

void FormatMac(char *dst, uint32_t cap)
{
    // GetAdaptersInfo returns "XX-XX-XX-XX-XX-XX" style strings; sub_BCEFD0
    // hashes whatever we write so format matters less than stability. We use
    // the canonical AA-BB-CC-DD-EE-FF form with a locally-administered OUI
    // so it can't collide with real hardware.
    if (cap < 18) { if (cap) dst[0] = '\0'; return; }
    uint8_t  oui0 = 0x02; // locally-administered, unicast
    uint8_t  mac[6] = { oui0,
                        s_seed[0], s_seed[1],
                        s_seed[2], s_seed[3], s_seed[4] };
    std::snprintf(dst, cap, "%02X-%02X-%02X-%02X-%02X-%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void FormatComputerName(char *dst, uint32_t cap)
{
    // 15 chars max matches real GetComputerNameA behaviour (MAX_COMPUTERNAME_LENGTH).
    if (cap == 0) return;
    std::snprintf(dst, cap, "DESKTOP-%02X%02X%02X",
                  s_seed[5], s_seed[6], s_seed[7]);
}

void FormatUserName(char *dst, uint32_t cap)
{
    if (cap == 0) return;
    std::snprintf(dst, cap, "User%02X%02X",
                  s_seed[8], s_seed[9]);
}

void FormatLocale(char *dst, uint32_t cap)
{
    if (cap == 0) return;
    // GetLocaleInfoA for LCID 0x800 / types 5,1,4098,4097 — concatenated
    // result is "en-US|English|.|," on an English Windows. We ship a stable
    // zh-TW-like response (this is the Taiwan build) — server doesn't hash
    // per-locale as a fingerprint signal, it's there for region checks only.
    std::snprintf(dst, cap, "zh-TW");
}

void FormatOsCpuGpu(char *dst, uint32_t cap)
{
    // Server feeds a 128B strcpy. We need a terminator in the first byte we
    // control (kOffOsCpuGpu1/2) to defeat the `!a2[32]` sentinel, but also
    // want plausible content for anti-cheat telemetry that logs the field.
    if (cap == 0) return;
    std::snprintf(dst, cap,
                  "Windows 10 Pro|Intel(R) Core(TM) i5-%02X%02X CPU|NVIDIA GeForce GTX %02X%02X",
                  s_seed[10], s_seed[11], s_seed[12], s_seed[13]);
}

void FormatMemDisk(char *dst, uint32_t cap)
{
    if (cap == 0) return;
    std::snprintf(dst, cap, "RAM:16GB DISK:%02X%02X GB",
                  s_seed[14], s_seed[15]);
}

// ---- The detour ----

// Writes `src` (NUL-terminated) into outPkt[off..off+cap-1], never overflowing
// cap and always leaving a NUL terminator at off+min(strlen(src), cap-1).
void WriteField(uint8_t *outPkt, uint32_t off, const char *src, uint32_t cap)
{
    if (cap == 0) return;
    uint32_t n = static_cast<uint32_t>(std::strlen(src));
    if (n + 1 > cap) n = cap - 1;
    std::memcpy(outPkt + off, src, n);
    outPkt[off + n] = '\0';
}

int __fastcall HookHwFpFillBuffer(void *ecx, void *edx, uint8_t *outPkt)
{
    // Always invoke the original FIRST, then overwrite. Two reasons:
    //
    // 1) sub_BC5A70 at the tail writes a2+0x1C0 (MAC) and a2+0x1E0 (CPUID/
    //    disk/SMART/MAC hash sprintf) UNCONDITIONALLY — no sentinel gate —
    //    so any pre-fill at those offsets would be clobbered. Post-fill
    //    wins unconditionally.
    //
    // 2) 方案2 (hook-exit) explicitly accepts that the internal collection
    //    functions (GetAdaptersInfo, GetComputerNameA, GetLocaleInfoA,
    //    DeviceIoControl disk SMART) still run. We only care that the bytes
    //    sent on the wire are fake — which is exactly what overwriting the
    //    packet's fingerprint region after the original finishes achieves.
    int rv = g_oHwFpFillBuffer(ecx, edx, outPkt);

    if (!outPkt)
        return rv;

    EnsureSeed();

    __try
    {
        char buf[128];

        FormatOsCpuGpu(buf, sizeof(buf));
        WriteField(outPkt, kOffOsCpuGpu1, buf, 128);
        WriteField(outPkt, kOffOsCpuGpu2, buf, 128);

        FormatMemDisk(buf, sizeof(buf));
        WriteField(outPkt, kOffMemDisk, buf, 64);

        FormatLocale(buf, sizeof(buf));
        WriteField(outPkt, kOffLocale, buf, 32);

        FormatComputerName(buf, sizeof(buf));
        WriteField(outPkt, kOffComputer, buf, 32);

        FormatUserName(buf, sizeof(buf));
        WriteField(outPkt, kOffUser, buf, 32);

        FormatMac(buf, sizeof(buf));
        WriteField(outPkt, kOffDiskMac, buf, 36);

        // CPUID/disk/SMART/MAC hashes — originally the 4-int sprintf target
        // of sub_57D930 (a2+0x1E0). The packet field is ~64 bytes of ASCII
        // hex; we keep it inside that bound.
        uint32_t h0 = *reinterpret_cast<uint32_t *>(s_seed +  0);
        uint32_t h1 = *reinterpret_cast<uint32_t *>(s_seed +  4);
        uint32_t h2 = *reinterpret_cast<uint32_t *>(s_seed +  8);
        uint32_t h3 = *reinterpret_cast<uint32_t *>(s_seed + 12);
        std::snprintf(buf, sizeof(buf),
                      "%08X%08X%08X%08X", h0, h1, h2, h3);
        WriteField(outPkt, kOffCpuidHashes, buf, 64);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // Swallow — swallowing keeps login going even if the packet buffer
        // somehow isn't the shape we expect (e.g. different game build).
    }

    return rv;
}

// ---- Stage-1 / stage-2 wiring ----

void AttachRealHook()
{
    if (s_attached.load()) return;

    uintptr_t addr = GGTB::PatternResolver::Get("HwFpFillBuffer");
    if (!addr)
    {
        spdlog::error("GGTB::HwFpSpoof: missing pattern address for HwFpFillBuffer");
        return;
    }

    auto dumpBytes = [](uintptr_t a) {
        const uint8_t *p = reinterpret_cast<const uint8_t *>(a);
        return fmt::format("{:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}",
                           p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
    };

    spdlog::info("GGTB::HwFpSpoof: stage-2 pre-attach target[{:x}]={}", addr, dumpBytes(addr));

    g_oHwFpFillBuffer = reinterpret_cast<fnHwFpFillBuffer>(addr);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    LONG e   = DetourAttach(reinterpret_cast<PVOID *>(&g_oHwFpFillBuffer), HookHwFpFillBuffer);
    LONG err = DetourTransactionCommit();
    if (err != NO_ERROR || e != NO_ERROR)
    {
        spdlog::error("GGTB::HwFpSpoof: stage-2 Detour failed: commit={} attach={}",
                      err, e);
        return;
    }

    spdlog::info("GGTB::HwFpSpoof: stage-2 post-attach target[{:x}]={} (expect E9)",
                 addr, dumpBytes(addr));

    s_attached.store(true);

    // Seed the PRNG-derived fields now so the first hook fire doesn't pay
    // the file-read cost. EnsureSeed inside the hook would also work, but
    // doing it up front keeps the hook path lean.
    EnsureSeed();

    spdlog::info("GGTB::HwFpSpoof: hook attached @ {:x}", addr);
}

} // anonymous

void Install()
{
    if (s_attached.load()) return;

    // Stage1Trigger owns the shared DeleteFileA shim; we just subscribe.
    // AttachRealHook runs when WinMain fires DeleteFileA("error.txt"), by
    // which point the Winlicense unpacker has resolved sub_BCCBF0.
    GGTB::Stage1Trigger::Register(&AttachRealHook);
    spdlog::info("GGTB::HwFpSpoof: subscribed to Stage1Trigger");
}

void Uninstall()
{
    if (!s_attached.load())
        return;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(reinterpret_cast<PVOID *>(&g_oHwFpFillBuffer), HookHwFpFillBuffer);
    DetourTransactionCommit();
    s_attached.store(false);

    spdlog::info("GGTB::HwFpSpoof: hooks detached");
}

bool IsActive() { return s_attached.load(); }

} // namespace GGTB::HwFpSpoof
