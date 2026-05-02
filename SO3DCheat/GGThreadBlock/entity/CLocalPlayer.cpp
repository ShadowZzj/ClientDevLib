#include "CLocalPlayer.h"
#include "../util/PatternResolver.h"
#include "../util/UserConfig.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace GGTB
{

static const char *kMoveSpeedKeys[kMoveSpeedWriteCount] = {
    "MoveSpeedWrite1", "MoveSpeedWrite2", "MoveSpeedWrite3", "MoveSpeedWrite4",
    "MoveSpeedWrite5", "MoveSpeedWrite6", "MoveSpeedWrite7", "MoveSpeedWrite8",
    // TraceMove per-frame clamps — without these, NOPping CalcStatus alone lets
    // TraceMove pull moveSpeed back down to 7.0/10.0 each frame.
    "MoveSpeedClampMounted", "MoveSpeedClampNormal",
};

static BYTE s_moveSpeedOrig[kMoveSpeedWriteCount][kMoveSpeedPatchSize] = {};
static bool s_moveSpeedOrigCaptured[kMoveSpeedWriteCount] = {};
static bool s_moveSpeedPatched = false;

uintptr_t GetLocalUserPtr()
{
    auto addr = PatternResolver::Get("LocalUserPtr");
    if (!addr)
        return 0;
    auto p = *reinterpret_cast<uintptr_t *>(addr);
    if (p == 0 || p == 0xFFFFFFFF)
        return 0;
    return p;
}

float *GetMoveSpeedPtr()
{
    auto p = GetLocalUserPtr();
    return p ? reinterpret_cast<float *>(p + kMoveSpeedOffset) : nullptr;
}

float *GetAttackSpeedPtr()
{
    auto p = GetLocalUserPtr();
    return p ? reinterpret_cast<float *>(p + kAttackSpeedOffset) : nullptr;
}

float *GetSkillSpeedPtr()
{
    auto p = GetLocalUserPtr();
    return p ? reinterpret_cast<float *>(p + kSkillSpeedOffset) : nullptr;
}

static bool NopBytes(uintptr_t addr, size_t len, BYTE *backup, bool &captured)
{
    DWORD oldProt = 0;
    if (!VirtualProtect(reinterpret_cast<void *>(addr), len, PAGE_EXECUTE_READWRITE, &oldProt))
    {
        spdlog::error("GGTB: VirtualProtect failed at {:x}", addr);
        return false;
    }
    if (!captured)
    {
        memcpy(backup, reinterpret_cast<void *>(addr), len);
        captured = true;
    }
    memset(reinterpret_cast<void *>(addr), 0x90, len);
    VirtualProtect(reinterpret_cast<void *>(addr), len, oldProt, &oldProt);
    return true;
}

static bool RestoreBytes(uintptr_t addr, size_t len, const BYTE *backup, bool captured)
{
    if (!captured)
        return true;
    DWORD oldProt = 0;
    if (!VirtualProtect(reinterpret_cast<void *>(addr), len, PAGE_EXECUTE_READWRITE, &oldProt))
        return false;
    memcpy(reinterpret_cast<void *>(addr), backup, len);
    VirtualProtect(reinterpret_cast<void *>(addr), len, oldProt, &oldProt);
    return true;
}

static bool WriteBytes(uintptr_t addr, const BYTE *src, size_t len, BYTE *backup,
                       bool &captured)
{
    DWORD oldProt = 0;
    if (!VirtualProtect(reinterpret_cast<void *>(addr), len, PAGE_EXECUTE_READWRITE, &oldProt))
    {
        spdlog::error("GGTB: VirtualProtect failed at {:x}", addr);
        return false;
    }
    if (!captured)
    {
        memcpy(backup, reinterpret_cast<void *>(addr), len);
        captured = true;
    }
    memcpy(reinterpret_cast<void *>(addr), src, len);
    VirtualProtect(reinterpret_cast<void *>(addr), len, oldProt, &oldProt);
    return true;
}

bool PatchMoveSpeed()
{
    if (s_moveSpeedPatched)
        return true;

    bool allOk = true;
    for (size_t i = 0; i < kMoveSpeedWriteCount; ++i)
    {
        auto addr = PatternResolver::Get(kMoveSpeedKeys[i]);
        if (!addr)
        {
            spdlog::error("GGTB: {} unresolved", kMoveSpeedKeys[i]);
            allOk = false;
            continue;
        }
        if (NopBytes(addr, kMoveSpeedPatchSize, s_moveSpeedOrig[i],
                     s_moveSpeedOrigCaptured[i]))
            spdlog::info("GGTB: NOP {} @ {:x}", kMoveSpeedKeys[i], addr);
        else
            allOk = false;
    }
    s_moveSpeedPatched = allOk;
    return allOk;
}

bool RestoreMoveSpeed()
{
    if (!s_moveSpeedPatched)
        return true;

    bool allOk = true;
    for (size_t i = 0; i < kMoveSpeedWriteCount; ++i)
    {
        auto addr = PatternResolver::Get(kMoveSpeedKeys[i]);
        if (!addr)
        {
            allOk = false;
            continue;
        }
        if (!RestoreBytes(addr, kMoveSpeedPatchSize, s_moveSpeedOrig[i],
                          s_moveSpeedOrigCaptured[i]))
            allOk = false;
    }
    if (allOk)
    {
        s_moveSpeedPatched = false;
        spdlog::info("GGTB: MoveSpeed restored");
    }
    return allOk;
}

// ============================================================
//  AttackSpeed / SkillSpeed (single NOP each in CalcStatus)
// ============================================================

static BYTE s_attackSpeedOrig[kAttackSpeedPatchSize] = {};
static bool s_attackSpeedOrigCaptured = false;
static bool s_attackSpeedPatched      = false;

static BYTE s_skillSpeedOrig[kSkillSpeedPatchSize] = {};
static bool s_skillSpeedOrigCaptured = false;
static bool s_skillSpeedPatched      = false;

static bool PatchSingleNop(const char *name, size_t len, BYTE *backup,
                           bool &captured, bool &patched)
{
    if (patched)
        return true;
    auto addr = PatternResolver::Get(name);
    if (!addr)
    {
        spdlog::error("GGTB: {} unresolved", name);
        return false;
    }
    if (!NopBytes(addr, len, backup, captured))
        return false;
    spdlog::info("GGTB: NOP {} @ {:x}", name, addr);
    patched = true;
    return true;
}

static bool RestoreSingleNop(const char *name, size_t len, const BYTE *backup,
                             bool captured, bool &patched)
{
    if (!patched)
        return true;
    auto addr = PatternResolver::Get(name);
    if (!addr)
        return false;
    if (!RestoreBytes(addr, len, backup, captured))
        return false;
    patched = false;
    spdlog::info("GGTB: restored {}", name);
    return true;
}

bool PatchAttackSpeed()
{
    return PatchSingleNop("AttackSpeedWrite", kAttackSpeedPatchSize,
                          s_attackSpeedOrig, s_attackSpeedOrigCaptured,
                          s_attackSpeedPatched);
}

bool RestoreAttackSpeed()
{
    return RestoreSingleNop("AttackSpeedWrite", kAttackSpeedPatchSize,
                            s_attackSpeedOrig, s_attackSpeedOrigCaptured,
                            s_attackSpeedPatched);
}

bool PatchSkillSpeed()
{
    return PatchSingleNop("SkillSpeedWrite", kSkillSpeedPatchSize,
                          s_skillSpeedOrig, s_skillSpeedOrigCaptured,
                          s_skillSpeedPatched);
}

bool RestoreSkillSpeed()
{
    return RestoreSingleNop("SkillSpeedWrite", kSkillSpeedPatchSize,
                            s_skillSpeedOrig, s_skillSpeedOrigCaptured,
                            s_skillSpeedPatched);
}

// ============================================================
//  PreTime — rewrite Skill__GetPreTime's two `movss xmm0,[reg+0x174]`
//  instructions to read from &s_preTimeConst instead. Lets us drop cast
//  startup to ~0.0001s without touching SkillTable contents.
// ============================================================

// `static` keeps the constant at a fixed .data address so the disp32 we splice
// into the patched movss stays valid for the process lifetime.
static float s_preTimeConst = 0.0001f;
static BYTE  s_preTimeOrig1[kPreTimePatchSize] = {};
static BYTE  s_preTimeOrig2[kPreTimePatchSize] = {};
static bool  s_preTimeOrig1Captured            = false;
static bool  s_preTimeOrig2Captured            = false;
static bool  s_preTimePatched                  = false;

static bool PatchOnePreTime(uintptr_t addr, BYTE *backup, bool &captured)
{
    // ModR/M 0x05 = mod=00, reg=000(xmm0), r/m=101(disp32 absolute).
    // Original sites both target xmm0 via [edx+0x174] / [ecx+0x174]; we keep
    // xmm0 as the destination so downstream FPU code is unchanged.
    BYTE patch[kPreTimePatchSize] = { 0xF3, 0x0F, 0x10, 0x05, 0, 0, 0, 0 };
    auto constAddr                = reinterpret_cast<uintptr_t>(&s_preTimeConst);
    memcpy(&patch[4], &constAddr, 4);
    return WriteBytes(addr, patch, kPreTimePatchSize, backup, captured);
}

bool PatchPreTime()
{
    if (s_preTimePatched)
        return true;
    auto a1 = PatternResolver::Get("PreTimeRead1");
    auto a2 = PatternResolver::Get("PreTimeRead2");
    if (!a1 || !a2)
    {
        spdlog::error("GGTB: PreTimeRead1/2 unresolved");
        return false;
    }

    bool ok1 = PatchOnePreTime(a1, s_preTimeOrig1, s_preTimeOrig1Captured);
    bool ok2 = PatchOnePreTime(a2, s_preTimeOrig2, s_preTimeOrig2Captured);
    s_preTimePatched = ok1 && ok2;

    if (s_preTimePatched)
        spdlog::info("GGTB: PreTime patched: {:x}, {:x} -> movss xmm0,[{:x}] ({})",
                     a1, a2, reinterpret_cast<uintptr_t>(&s_preTimeConst),
                     s_preTimeConst);
    return s_preTimePatched;
}

bool RestorePreTime()
{
    if (!s_preTimePatched)
        return true;
    auto a1 = PatternResolver::Get("PreTimeRead1");
    auto a2 = PatternResolver::Get("PreTimeRead2");
    if (!a1 || !a2)
        return false;

    bool ok1 = RestoreBytes(a1, kPreTimePatchSize, s_preTimeOrig1, s_preTimeOrig1Captured);
    bool ok2 = RestoreBytes(a2, kPreTimePatchSize, s_preTimeOrig2, s_preTimeOrig2Captured);
    if (ok1 && ok2)
    {
        s_preTimePatched = false;
        spdlog::info("GGTB: PreTime restored");
    }
    return ok1 && ok2;
}

// ============================================================
//  Attack range (3 writes in CalcStatus + 2 imm32 caps in Combat__GetAttackRangeSquared)
// ============================================================

int *GetAttackRangePtr()
{
    auto p = GetLocalUserPtr();
    return p ? reinterpret_cast<int *>(p + kAttackRangeOffset) : nullptr;
}

namespace
{
struct AttackRangeWriteSlot
{
    const char *key;
    size_t      size;
    BYTE        backup[10]; // sized for the largest write (10 bytes)
    bool        captured;
};
} // anonymous

static AttackRangeWriteSlot s_attackRangeWrites[3] = {
    {"AttackRangeWrite1", kAttackRangeWrite1PatchSize, {}, false},
    {"AttackRangeWrite2", kAttackRangeWrite2PatchSize, {}, false},
    {"AttackRangeWrite3", kAttackRangeWrite3PatchSize, {}, false},
};
static bool s_attackRangePatched = false;

bool PatchAttackRange()
{
    if (s_attackRangePatched)
        return true;

    bool allOk = true;
    for (auto &slot : s_attackRangeWrites)
    {
        auto addr = PatternResolver::Get(slot.key);
        if (!addr)
        {
            spdlog::error("GGTB: {} unresolved", slot.key);
            allOk = false;
            continue;
        }
        if (NopBytes(addr, slot.size, slot.backup, slot.captured))
            spdlog::info("GGTB: NOP {} @ {:x} ({}B)", slot.key, addr, slot.size);
        else
            allOk = false;
    }
    s_attackRangePatched = allOk;
    return allOk;
}

bool RestoreAttackRange()
{
    if (!s_attackRangePatched)
        return true;

    bool allOk = true;
    for (auto &slot : s_attackRangeWrites)
    {
        auto addr = PatternResolver::Get(slot.key);
        if (!addr)
        {
            allOk = false;
            continue;
        }
        if (!RestoreBytes(addr, slot.size, slot.backup, slot.captured))
            allOk = false;
    }
    if (allOk)
    {
        s_attackRangePatched = false;
        spdlog::info("GGTB: AttackRange restored");
    }
    return allOk;
}

static BYTE s_attackRangeCapOrig[2][kAttackRangeCapImmSize] = {};
static bool s_attackRangeCapOrigCaptured[2]                 = {};
static bool s_attackRangeCapPatched                          = false;

bool PatchAttackRangeCap(uint32_t newCap)
{
    static const char *kKeys[2] = {"AttackRangeCapImm1", "AttackRangeCapImm2"};

    BYTE imm[kAttackRangeCapImmSize] = {
        static_cast<BYTE>(newCap & 0xFF),
        static_cast<BYTE>((newCap >> 8) & 0xFF),
        static_cast<BYTE>((newCap >> 16) & 0xFF),
        static_cast<BYTE>((newCap >> 24) & 0xFF),
    };

    bool allOk = true;
    for (size_t i = 0; i < 2; ++i)
    {
        auto addr = PatternResolver::Get(kKeys[i]);
        if (!addr)
        {
            spdlog::error("GGTB: {} unresolved", kKeys[i]);
            allOk = false;
            continue;
        }
        if (!WriteBytes(addr, imm, kAttackRangeCapImmSize, s_attackRangeCapOrig[i],
                        s_attackRangeCapOrigCaptured[i]))
            allOk = false;
        else
            spdlog::info("GGTB: AttackRangeCap {} @ {:x} = {}", kKeys[i], addr, newCap);
    }
    if (allOk)
        s_attackRangeCapPatched = true;
    return allOk;
}

bool RestoreAttackRangeCap()
{
    if (!s_attackRangeCapPatched)
        return true;
    static const char *kKeys[2] = {"AttackRangeCapImm1", "AttackRangeCapImm2"};

    bool allOk = true;
    for (size_t i = 0; i < 2; ++i)
    {
        auto addr = PatternResolver::Get(kKeys[i]);
        if (!addr)
        {
            allOk = false;
            continue;
        }
        if (!RestoreBytes(addr, kAttackRangeCapImmSize, s_attackRangeCapOrig[i],
                          s_attackRangeCapOrigCaptured[i]))
            allOk = false;
    }
    if (allOk)
    {
        s_attackRangeCapPatched = false;
        spdlog::info("GGTB: AttackRangeCap restored");
    }
    return allOk;
}

// ============================================================
//  Item short-cd (14-byte rewrite inside StatTable__SetItemCdRemaining)
// ============================================================

static BYTE s_itemShortCdOrig[kItemShortCdPatchSize] = {};
static bool s_itemShortCdOrigCaptured = false;
static bool s_itemShortCdPatched      = false;

bool PatchItemShortCD(float seconds)
{
    auto addr = PatternResolver::Get("ItemCdSetter");
    if (!addr)
    {
        spdlog::error("GGTB: ItemCdSetter unresolved");
        return false;
    }

    // C7 84 01 E0 0E 00 00 <imm32 float>  =  mov dword [ecx+eax+0EE0h], imm32   (11B)
    // 90 90 90                                                                 (3B padding)
    BYTE patch[kItemShortCdPatchSize] = {
        0xC7, 0x84, 0x01, 0xE0, 0x0E, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x90, 0x90, 0x90,
    };
    uint32_t bits = 0;
    memcpy(&bits, &seconds, sizeof(bits));
    patch[7]  = static_cast<BYTE>(bits & 0xFF);
    patch[8]  = static_cast<BYTE>((bits >> 8) & 0xFF);
    patch[9]  = static_cast<BYTE>((bits >> 16) & 0xFF);
    patch[10] = static_cast<BYTE>((bits >> 24) & 0xFF);

    if (!WriteBytes(addr, patch, kItemShortCdPatchSize, s_itemShortCdOrig,
                    s_itemShortCdOrigCaptured))
        return false;

    spdlog::info("GGTB: ItemShortCD patched @ {:x} duration={:.3f}s", addr, seconds);
    s_itemShortCdPatched = true;
    return true;
}

bool RestoreItemShortCD()
{
    if (!s_itemShortCdPatched)
        return true;
    auto addr = PatternResolver::Get("ItemCdSetter");
    if (!addr)
        return false;
    if (!RestoreBytes(addr, kItemShortCdPatchSize, s_itemShortCdOrig,
                      s_itemShortCdOrigCaptured))
        return false;
    s_itemShortCdPatched = false;
    spdlog::info("GGTB: ItemShortCD restored");
    return true;
}

// ============================================================
//  Action-time movement bypass
//
//  4 in-place rewrites inside CLocalUser::SetAfterAction (0x7539E0) and
//  CLocalUser::TraceMove (0x756F10). See CLocalPlayer.h header for the full
//  rationale; in short:
//
//    1. SetAfterActionGate @ 0x7539FE — overwrite `7E 22` (jle short) with
//       `E9 C1 02 00 00` (jmp 0x753CC4 success path). One patch bypasses every
//       reject inside SetAfterAction (skill mode, stun, m_bCanMove, animation
//       whitelist, etc.) so any move-while-acting request is queued.
//
//    2. TraceMoveGate1 @ 0x756FAA — flip `76 -> EB` so the per-frame stun-time
//       check (xmm vs g_kReadyFactorZero, +0x3468) is always taken as "no stun".
//
//    3. TraceMoveOrChain @ 0x756FCD — overwrite the start of the long
//       `cmp [eax+194h], <id>` OR-chain with `E9 19 01 00 00` (jmp 0x7570EB).
//       Skips every animation/skill ID equality test; falls through to the
//       second per-frame timer gate.
//
//    4. TraceMoveGate2 @ 0x7570FD — flip `76 -> EB` so the +0x2BCC per-frame
//       timer gate is always skipped. After this, TraceMove always advances the
//       position toward the queued target.
// ============================================================

static BYTE s_actionMoveSetAfterActionGateOrig[kActionMoveSetAfterActionGateSize] = {};
static BYTE s_actionMoveTraceMoveGate1Orig    [kActionMoveTraceMoveGate1Size]     = {};
static BYTE s_actionMoveTraceMoveOrChainOrig  [kActionMoveTraceMoveOrChainSize]   = {};
static BYTE s_actionMoveTraceMoveGate2Orig    [kActionMoveTraceMoveGate2Size]     = {};
static bool s_actionMoveSetAfterActionGateOrigCaptured = false;
static bool s_actionMoveTraceMoveGate1OrigCaptured     = false;
static bool s_actionMoveTraceMoveOrChainOrigCaptured   = false;
static bool s_actionMoveTraceMoveGate2OrigCaptured     = false;
static bool s_actionMovePatched = false;

bool PatchActionMove()
{
    if (s_actionMovePatched)
        return true;

    auto addrSetAfter   = PatternResolver::Get("ActionMoveSetAfterActionGate");
    auto addrTraceGate1 = PatternResolver::Get("ActionMoveTraceMoveGate1");
    auto addrTraceOr    = PatternResolver::Get("ActionMoveTraceMoveOrChain");
    auto addrTraceGate2 = PatternResolver::Get("ActionMoveTraceMoveGate2");
    if (!addrSetAfter || !addrTraceGate1 || !addrTraceOr || !addrTraceGate2)
    {
        spdlog::error("GGTB: ActionMove pattern(s) unresolved (sa={:x} g1={:x} or={:x} g2={:x})",
                      addrSetAfter, addrTraceGate1, addrTraceOr, addrTraceGate2);
        return false;
    }

    // E9 C1 02 00 00 = jmp +0x2C1 (0x7539FE -> 0x753CC4).
    BYTE patchSetAfter[kActionMoveSetAfterActionGateSize] = {0xE9, 0xC1, 0x02, 0x00, 0x00};
    // EB = jmp short (preserves the 0x1E displacement of the original jbe).
    BYTE patchTraceGate1[kActionMoveTraceMoveGate1Size]   = {0xEB};
    // E9 19 01 00 00 = jmp +0x119 (0x756FCD -> 0x7570EB).
    BYTE patchTraceOr[kActionMoveTraceMoveOrChainSize]    = {0xE9, 0x19, 0x01, 0x00, 0x00};
    BYTE patchTraceGate2[kActionMoveTraceMoveGate2Size]   = {0xEB};

    bool allOk = true;
    if (!WriteBytes(addrSetAfter, patchSetAfter, kActionMoveSetAfterActionGateSize,
                    s_actionMoveSetAfterActionGateOrig,
                    s_actionMoveSetAfterActionGateOrigCaptured))
        allOk = false;
    if (!WriteBytes(addrTraceGate1, patchTraceGate1, kActionMoveTraceMoveGate1Size,
                    s_actionMoveTraceMoveGate1Orig,
                    s_actionMoveTraceMoveGate1OrigCaptured))
        allOk = false;
    if (!WriteBytes(addrTraceOr, patchTraceOr, kActionMoveTraceMoveOrChainSize,
                    s_actionMoveTraceMoveOrChainOrig,
                    s_actionMoveTraceMoveOrChainOrigCaptured))
        allOk = false;
    if (!WriteBytes(addrTraceGate2, patchTraceGate2, kActionMoveTraceMoveGate2Size,
                    s_actionMoveTraceMoveGate2Orig,
                    s_actionMoveTraceMoveGate2OrigCaptured))
        allOk = false;

    if (allOk)
    {
        s_actionMovePatched = true;
        spdlog::info("GGTB: ActionMove patched (sa={:x} g1={:x} or={:x} g2={:x})",
                     addrSetAfter, addrTraceGate1, addrTraceOr, addrTraceGate2);
    }
    return allOk;
}

bool RestoreActionMove()
{
    if (!s_actionMovePatched)
        return true;

    auto addrSetAfter   = PatternResolver::Get("ActionMoveSetAfterActionGate");
    auto addrTraceGate1 = PatternResolver::Get("ActionMoveTraceMoveGate1");
    auto addrTraceOr    = PatternResolver::Get("ActionMoveTraceMoveOrChain");
    auto addrTraceGate2 = PatternResolver::Get("ActionMoveTraceMoveGate2");

    bool allOk = true;
    if (!RestoreBytes(addrSetAfter, kActionMoveSetAfterActionGateSize,
                      s_actionMoveSetAfterActionGateOrig,
                      s_actionMoveSetAfterActionGateOrigCaptured))
        allOk = false;
    if (!RestoreBytes(addrTraceGate1, kActionMoveTraceMoveGate1Size,
                      s_actionMoveTraceMoveGate1Orig,
                      s_actionMoveTraceMoveGate1OrigCaptured))
        allOk = false;
    if (!RestoreBytes(addrTraceOr, kActionMoveTraceMoveOrChainSize,
                      s_actionMoveTraceMoveOrChainOrig,
                      s_actionMoveTraceMoveOrChainOrigCaptured))
        allOk = false;
    if (!RestoreBytes(addrTraceGate2, kActionMoveTraceMoveGate2Size,
                      s_actionMoveTraceMoveGate2Orig,
                      s_actionMoveTraceMoveGate2OrigCaptured))
        allOk = false;

    if (allOk)
    {
        s_actionMovePatched = false;
        spdlog::info("GGTB: ActionMove restored");
    }
    return allOk;
}

// ============================================================
//  Entity iteration — EntityManager around-player linked list
// ============================================================

namespace
{

bool SafeReadDword(uintptr_t addr, uint32_t &out)
{
    __try
    {
        out = *reinterpret_cast<volatile uint32_t *>(addr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SafeReadByte(uintptr_t addr, uint8_t &out)
{
    __try
    {
        out = *reinterpret_cast<volatile uint8_t *>(addr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SafeReadFloat(uintptr_t addr, float &out)
{
    __try
    {
        out = *reinterpret_cast<volatile float *>(addr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SafeReadString(uintptr_t addr, char *buf, size_t cap)
{
    __try
    {
        for (size_t i = 0; i < cap - 1; ++i)
        {
            char c = *reinterpret_cast<volatile char *>(addr + i);
            buf[i] = c;
            if (c == 0)
                return true;
        }
        buf[cap - 1] = 0;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Player names in this build are stored as Big5 (CP 950) raw bytes.
// Convert to UTF-8 so ImGui (with simhei.ttf + ChineseFull range) and spdlog
// can render them without mojibake. Falls back to the raw bytes if conversion
// fails — better to show garbage than to drop the name entirely.
std::string Big5ToUtf8(const char *src)
{
    if (!src || !*src)
        return {};
    int wlen = MultiByteToWideChar(950, 0, src, -1, nullptr, 0);
    if (wlen <= 0)
        return std::string(src);
    std::wstring w(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(950, 0, src, -1, w.data(), wlen);
    int u8len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (u8len <= 0)
        return std::string(src);
    std::string u(static_cast<size_t>(u8len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, u.data(), u8len, nullptr, nullptr);
    if (!u.empty() && u.back() == '\0')
        u.pop_back();
    return u;
}

// Returns 0 = accepted, otherwise reason code for diagnostics.
//   1 = read kind failed
//   2 = read name failed
//   3 = empty name
//   4 = local user (skipped by pointer match)
//   5 = read position failed
//   6 = out of range
//   7 = whitelisted (multi-boxed alt account)
int VisitUser(uintptr_t user, uintptr_t localUser,
              float lx, float ly, float lz, float maxDist,
              std::vector<NearbyPlayer> &out, uint32_t &outKind)
{
    if (!user)
        return 1;

    // Read kind for diagnostics — but DON'T filter on it yet. We're not 100%
    // sure of its semantics in this build (sub_753840's `==5` could mean
    // "remote player visible" OR "local user" OR a per-state flag); a failed
    // theory stays a wrong filter until we see real values.
    if (!SafeReadDword(user + kUserKindOffset, outKind))
        return 1;

    // Skip self by pointer — the local user is in this same linked list, and
    // pointer compare is more reliable than name match (which depends on Big5
    // round-trip).
    if (user == localUser)
        return 4;

    char nameBuf[kNameMaxLen + 1] = {};
    if (!SafeReadString(user + kNameOffset, nameBuf, sizeof(nameBuf)))
        return 2;
    if (!nameBuf[0])
        return 3;

    auto utf8 = Big5ToUtf8(nameBuf);
    if (UserConfig::IsWhitelisted(utf8))
        return 7;

    float x = 0, y = 0, z = 0;
    if (!SafeReadFloat(user + kPositionXOffset, x) ||
        !SafeReadFloat(user + kPositionYOffset, y) ||
        !SafeReadFloat(user + kPositionZOffset, z))
        return 5;

    float dx = x - lx, dy = y - ly, dz = z - lz;
    float d  = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (maxDist > 0 && d > maxDist)
        return 6;

    out.push_back({std::move(utf8), d, x, y, z});
    return 0;
}

} // anonymous

bool GetLocalPosition(float &x, float &y, float &z)
{
    auto p = GetLocalUserPtr();
    if (!p)
        return false;
    return SafeReadFloat(p + kPositionXOffset, x) &&
           SafeReadFloat(p + kPositionYOffset, y) &&
           SafeReadFloat(p + kPositionZOffset, z);
}

std::string GetLocalPlayerName()
{
    auto p = GetLocalUserPtr();
    if (!p)
        return {};
    char buf[kNameMaxLen + 1] = {};
    if (!SafeReadString(p + kNameOffset, buf, sizeof(buf)))
        return {};
    return Big5ToUtf8(buf);
}

std::vector<NearbyPlayer> GetAroundPlayers(const std::string &localName,
                                           float              maxDistance)
{
    (void)localName; // skip-self uses pointer compare now, not name
    std::vector<NearbyPlayer> result;

    auto mgrAddrPtr = PatternResolver::Get("EntityManagerPtr");
    if (!mgrAddrPtr)
        return result;

    uint32_t mgr = 0;
    if (!SafeReadDword(mgrAddrPtr, mgr) || !mgr)
        return result;

    uintptr_t localUser = GetLocalUserPtr();

    float lx = 0, ly = 0, lz = 0;
    if (!GetLocalPosition(lx, ly, lz))
        return result;

    uint32_t node = 0;
    if (!SafeReadDword(mgr + kUserMgrListHeadOffset, node))
        return result;

    // Diagnostic counters — printed at most once every 5s so they don't spam
    // the log during the normal half-second poll loop. Grouped by reject reason
    // so we can tell whether nodes are being filtered (kind/range/local) or
    // not even being reached (read failures).
    static DWORD s_lastDiagMs    = 0;
    int          rejected[8]     = {};
    uint32_t     firstNonzeroKind = 0;

    constexpr int kMaxNodes = 1024;
    int           visited   = 0;
    for (; visited < kMaxNodes && node; ++visited)
    {
        uint32_t kind = 0;
        int      r    = VisitUser(node, localUser, lx, ly, lz, maxDistance, result, kind);
        if (r >= 0 && r < 8)
            rejected[r]++;
        if (kind && !firstNonzeroKind)
            firstNonzeroKind = kind;
        if (!SafeReadDword(node + kUserNextOffset, node))
            break;
    }

    DWORD now = GetTickCount();
    if (now - s_lastDiagMs > 5000)
    {
        s_lastDiagMs = now;
        spdlog::info(
            "GGTB::AroundPlayers: mgr={:x} head_node_first_kind={} visited={} "
            "accepted={} rej_kind={} rej_name={} rej_empty={} rej_self={} "
            "rej_pos={} rej_range={} rej_white={}",
            static_cast<uintptr_t>(mgr), firstNonzeroKind, visited,
            rejected[0], rejected[1], rejected[2], rejected[3], rejected[4],
            rejected[5], rejected[6], rejected[7]);
    }
    return result;
}

// ============================================================
//  Drop-item iteration + auto-pickup helper
// ============================================================

std::vector<DropItemInfo> GetNearbyDropItems(float maxDistance,
                                             bool  includeUnpickable)
{
    std::vector<DropItemInfo> result;

    auto containerPtrAddr = PatternResolver::Get("ItemContainerPtr");
    if (!containerPtrAddr)
        return result;

    uint32_t container = 0;
    if (!SafeReadDword(containerPtrAddr, container) || !container)
        return result;

    float lx = 0, ly = 0, lz = 0;
    bool  haveLocal = GetLocalPosition(lx, ly, lz);

    uint32_t node = 0;
    if (!SafeReadDword(container + kDropContainerHeadOffset, node))
        return result;

    constexpr int kMaxNodes = 2048;
    int           visited   = 0;
    for (; visited < kMaxNodes && node; ++visited)
    {
        uint32_t dropId = 0, itemId = 0;
        float    x = 0, y = 0, z = 0;
        uint8_t  canPickByte = 0;

        bool ok = SafeReadDword(node + kDropIdOffset, dropId) &&
                  SafeReadDword(node + kDropItemIdOffset, itemId) &&
                  SafeReadFloat(node + kDropPosXOffset, x) &&
                  SafeReadFloat(node + kDropPosYOffset, y) &&
                  SafeReadFloat(node + kDropPosZOffset, z) &&
                  SafeReadByte (node + kDropCanPickOffset, canPickByte);

        if (ok)
        {
            bool canPick = canPickByte != 0;
            if (includeUnpickable || canPick)
            {
                float d = 0.0f;
                if (haveLocal)
                {
                    float dx = x - lx, dy = y - ly, dz = z - lz;
                    d = std::sqrt(dx * dx + dy * dy + dz * dz);
                }
                if (maxDistance <= 0.0f || !haveLocal || d <= maxDistance)
                    result.push_back({dropId, itemId, d, x, y, z, canPick});
            }
        }

        if (!SafeReadDword(node + kDropNextOffset, node))
            break;
    }

    std::sort(result.begin(), result.end(),
              [](const DropItemInfo &a, const DropItemInfo &b) {
                  return a.distance < b.distance;
              });
    return result;
}

namespace
{
// SEH-only helper. Must contain no C++ objects requiring unwinding —
// MSVC's C2712 forbids __try inside functions whose stack frame holds
// destructible objects.
using PickItemFn = int(__stdcall *)(int);

static int CallPickItemSEH(PickItemFn fn, int dropId)
{
    __try
    {
        return fn(dropId);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}
} // namespace

int SendPickItem(uint32_t dropId)
{
    auto fnAddr = PatternResolver::Get("SendPickItemPacketFn");
    if (!fnAddr)
        return 0;

    auto fn = reinterpret_cast<PickItemFn>(fnAddr);
    int  rv = CallPickItemSEH(fn, static_cast<int>(dropId));
    if (rv < 0)
    {
        spdlog::warn("GGTB::SendPickItem: SEH caught for dropId={}", dropId);
        return 0;
    }
    return rv;
}

} // namespace GGTB
