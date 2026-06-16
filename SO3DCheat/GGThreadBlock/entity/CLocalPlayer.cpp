#include "CLocalPlayer.h"
#include "../util/PatternResolver.h"
#include "../util/UserConfig.h"
#include "../util/NetLog.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <cmath>
#include <ctime>
#include <unordered_set>

namespace GGTB
{

static const char *kMoveSpeedKeys[kMoveSpeedWriteCount] = {
    "MoveSpeedWrite1", "MoveSpeedWrite2", "MoveSpeedWrite3", "MoveSpeedWrite4",
    "MoveSpeedWrite5", "MoveSpeedWrite6", "MoveSpeedWrite7", "MoveSpeedWrite8",
    // TraceMove per-frame clamps — without these, NOPping CalcStatus alone lets
    // TraceMove pull moveSpeed back down to 7.0/10.0 each frame.
    "MoveSpeedClampMounted", "MoveSpeedClampNormal",
    // Mount / state refresh paths. Mounting can otherwise rewrite +0x1A4 back
    // to the vehicle/default speed after the slider value has been applied.
    "MoveSpeedMountStateBoost", "MoveSpeedMountStateMax",
    "MoveSpeedMountStateDefault", "MoveSpeedMountStatusSync",
    "MoveSpeedMountPathRefresh",
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

void SetPreTimeConst(float v)
{
    s_preTimeConst = v;
}

float GetPreTimeConst()
{
    return s_preTimeConst;
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
//  Skill cast range — rewrite SkillTable__GetSkillRange (0x939890)
//  prologue to `mov eax, imm32; ret`. Covers all 13 distance-gate
//  callers (9 in Net__SendSkillPacket, 2 in GetSkillRangeSq wrapper,
//  1 in sub_A89AD0 animation normaliser) with one patch.
// ============================================================

static BYTE s_skillRangeOrig[kSkillRangePatchSize] = {};
static bool s_skillRangeOrigCaptured               = false;
static bool s_skillRangePatched                    = false;

bool PatchSkillRange(uint32_t tiles)
{
    auto addr = PatternResolver::Get("SkillRangeGetterEntry");
    if (!addr)
    {
        spdlog::error("GGTB: SkillRangeGetterEntry unresolved");
        return false;
    }

    // B8 <imm32> C3  =  mov eax, imm32 ; ret
    BYTE patch[kSkillRangePatchSize] = {0xB8, 0x00, 0x00, 0x00, 0x00, 0xC3};
    patch[1] = static_cast<BYTE>(tiles & 0xFF);
    patch[2] = static_cast<BYTE>((tiles >> 8) & 0xFF);
    patch[3] = static_cast<BYTE>((tiles >> 16) & 0xFF);
    patch[4] = static_cast<BYTE>((tiles >> 24) & 0xFF);

    if (!WriteBytes(addr, patch, kSkillRangePatchSize, s_skillRangeOrig,
                    s_skillRangeOrigCaptured))
        return false;

    s_skillRangePatched = true;
    spdlog::info("GGTB: SkillRange patched @ {:x} = {} tiles", addr, tiles);
    return true;
}

bool RestoreSkillRange()
{
    if (!s_skillRangePatched)
        return true;
    auto addr = PatternResolver::Get("SkillRangeGetterEntry");
    if (!addr)
        return false;
    if (!RestoreBytes(addr, kSkillRangePatchSize, s_skillRangeOrig,
                      s_skillRangeOrigCaptured))
        return false;
    s_skillRangePatched = false;
    spdlog::info("GGTB: SkillRange restored");
    return true;
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
//    2. InstantCastStartup @ 0x756331 — overwrite `0F 86 C0 00 00 00`
//       (jbe loc_7563F7) with `E9 C1 00 00 00 90`. SendSkillCast always takes
//       the immediate-send path and skips the preTime mode=3 raise-hand branch.
//
//    3. TraceMoveGate1 @ 0x756FAA — flip `76 -> EB` so the per-frame stun-time
//       check (xmm vs g_kReadyFactorZero, +0x3468) is always taken as "no stun".
//
//    4. TraceMoveOrChain @ 0x756FCD — overwrite the start of the long
//       `cmp [eax+194h], <id>` OR-chain with `E9 19 01 00 00` (jmp 0x7570EB).
//       Skips every animation/skill ID equality test; falls through to the
//       second per-frame timer gate.
//
//    5. TraceMoveGate2 @ 0x7570FD — flip `76 -> EB` so the +0x2BCC per-frame
//       timer gate is always skipped. After this, TraceMove always advances the
//       position toward the queued target.
// ============================================================

static BYTE s_actionMoveSetAfterActionGateOrig[kActionMoveSetAfterActionGateSize] = {};
static BYTE s_actionMoveInstantCastStartupOrig [kActionMoveInstantCastStartupSize]  = {};
static BYTE s_actionMoveTraceMoveGate1Orig    [kActionMoveTraceMoveGate1Size]     = {};
static BYTE s_actionMoveTraceMoveOrChainOrig  [kActionMoveTraceMoveOrChainSize]   = {};
static BYTE s_actionMoveTraceMoveGate2Orig    [kActionMoveTraceMoveGate2Size]     = {};
static bool s_actionMoveSetAfterActionGateOrigCaptured = false;
static bool s_actionMoveInstantCastStartupOrigCaptured  = false;
static bool s_actionMoveTraceMoveGate1OrigCaptured     = false;
static bool s_actionMoveTraceMoveOrChainOrigCaptured   = false;
static bool s_actionMoveTraceMoveGate2OrigCaptured     = false;
static bool s_actionMovePatched = false;

bool PatchActionMove()
{
    if (s_actionMovePatched)
        return true;

    auto addrSetAfter   = PatternResolver::Get("ActionMoveSetAfterActionGate");
    auto addrInstant    = PatternResolver::Get("ActionMoveInstantCastStartup");
    auto addrTraceGate1 = PatternResolver::Get("ActionMoveTraceMoveGate1");
    auto addrTraceOr    = PatternResolver::Get("ActionMoveTraceMoveOrChain");
    auto addrTraceGate2 = PatternResolver::Get("ActionMoveTraceMoveGate2");
    if (!addrSetAfter || !addrInstant || !addrTraceGate1 || !addrTraceOr || !addrTraceGate2)
    {
        spdlog::error("GGTB: ActionMove pattern(s) unresolved (sa={:x} inst={:x} g1={:x} or={:x} g2={:x})",
                      addrSetAfter, addrInstant, addrTraceGate1, addrTraceOr, addrTraceGate2);
        return false;
    }

    // E9 C1 02 00 00 = jmp +0x2C1 (0x7539FE -> 0x753CC4).
    BYTE patchSetAfter[kActionMoveSetAfterActionGateSize] = {0xE9, 0xC1, 0x02, 0x00, 0x00};
    // E9 C1 00 00 00 90 = jmp +0xC1 (0x756331 -> 0x7563F7) + nop.
    BYTE patchInstant[kActionMoveInstantCastStartupSize]  = {0xE9, 0xC1, 0x00, 0x00, 0x00, 0x90};
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
    if (!WriteBytes(addrInstant, patchInstant, kActionMoveInstantCastStartupSize,
                    s_actionMoveInstantCastStartupOrig,
                    s_actionMoveInstantCastStartupOrigCaptured))
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
        spdlog::info("GGTB: ActionMove patched (sa={:x} inst={:x} g1={:x} or={:x} g2={:x})",
                     addrSetAfter, addrInstant, addrTraceGate1, addrTraceOr, addrTraceGate2);
    }
    return allOk;
}

bool RestoreActionMove()
{
    if (!s_actionMovePatched)
        return true;

    auto addrSetAfter   = PatternResolver::Get("ActionMoveSetAfterActionGate");
    auto addrInstant    = PatternResolver::Get("ActionMoveInstantCastStartup");
    auto addrTraceGate1 = PatternResolver::Get("ActionMoveTraceMoveGate1");
    auto addrTraceOr    = PatternResolver::Get("ActionMoveTraceMoveOrChain");
    auto addrTraceGate2 = PatternResolver::Get("ActionMoveTraceMoveGate2");

    bool allOk = true;
    if (!RestoreBytes(addrSetAfter, kActionMoveSetAfterActionGateSize,
                      s_actionMoveSetAfterActionGateOrig,
                      s_actionMoveSetAfterActionGateOrigCaptured))
        allOk = false;
    if (!RestoreBytes(addrInstant, kActionMoveInstantCastStartupSize,
                      s_actionMoveInstantCastStartupOrig,
                      s_actionMoveInstantCastStartupOrigCaptured))
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
//  Block level-up — flip `jge short` to `jmp short` so the
//  CG_LEVEL_UP_CHECK send block at the tail of UpdateExp is always
//  skipped. Server keeps accumulating EXP but never receives the
//  client's level-up request, so the character pins at current level.
// ============================================================

static BYTE s_blockLevelUpOrig[kBlockLevelUpPatchSize] = {};
static bool s_blockLevelUpOrigCaptured                 = false;
static bool s_blockLevelUpPatched                      = false;

bool PatchBlockLevelUp()
{
    if (s_blockLevelUpPatched)
        return true;
    auto addr = PatternResolver::Get("BlockLevelUpGate");
    if (!addr)
    {
        spdlog::error("GGTB: BlockLevelUpGate unresolved");
        return false;
    }
    BYTE patch[kBlockLevelUpPatchSize] = {0xEB}; // jge short -> jmp short
    if (!WriteBytes(addr, patch, kBlockLevelUpPatchSize, s_blockLevelUpOrig,
                    s_blockLevelUpOrigCaptured))
        return false;
    s_blockLevelUpPatched = true;
    spdlog::info("GGTB: BlockLevelUp patched @ {:x} (jge -> jmp)", addr);
    return true;
}

bool RestoreBlockLevelUp()
{
    if (!s_blockLevelUpPatched)
        return true;
    auto addr = PatternResolver::Get("BlockLevelUpGate");
    if (!addr)
        return false;
    if (!RestoreBytes(addr, kBlockLevelUpPatchSize, s_blockLevelUpOrig,
                      s_blockLevelUpOrigCaptured))
        return false;
    s_blockLevelUpPatched = false;
    spdlog::info("GGTB: BlockLevelUp restored");
    return true;
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

uint32_t ProfessionStringIndex(uint32_t profession)
{
    // Mirrors client sub_978880: raw profession id -> job text/icon index.
    if (profession <= 8)
        return profession;

    uint32_t tier = profession / 10;
    uint32_t base = profession % 10;
    uint32_t idx  = 0;
    if (base <= 8)
        idx = tier + 2 * base + 6;
    if (base == 9)
        idx = tier + 21;

    if (profession == 31)
        idx = 24;
    else if (profession == 131)
        idx = 25;
    else if (profession == 231)
        idx = 26;
    return idx;
}

using StringTableCopyFn = char *(__cdecl *)(uint32_t id, char *dst, size_t cap);

bool CopyStringTableSEH(StringTableCopyFn fn, uint32_t id, char *dst, size_t cap)
{
    __try
    {
        fn(id, dst, cap);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
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

    uint32_t profession = 0;
    SafeReadDword(user + kUserProfessionOffset, profession);
    auto professionName = GetProfessionName(profession);

    // 远程玩家 HP 明文存 +0x1B00(CUser__GetCurrentHP),不需要 XOR。
    int32_t  hp = -1;
    uint32_t rawHp = 0;
    if (SafeReadDword(user + kLocalUserHpOffset, rawHp))
        hp = static_cast<int32_t>(rawHp);

    out.push_back({std::move(utf8), profession, std::move(professionName),
                   d, x, y, z, hp});
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

std::string GetProfessionName(uint32_t profession)
{
    auto stringTable = PatternResolver::Get("StringTableCopy");
    if (stringTable)
    {
        char raw[128] = {};
        auto fn = reinterpret_cast<StringTableCopyFn>(stringTable);
        uint32_t textId = ProfessionStringIndex(profession) + 0x198;
        if (CopyStringTableSEH(fn, textId, raw, sizeof(raw)) && raw[0])
            return Big5ToUtf8(raw);
    }

    switch (profession)
    {
    case 0:  return u8"初心者";
    case 1:  return u8"剑士";
    case 2:  return u8"骑士";
    case 3:  return u8"小丑";
    case 4:  return u8"魔法师";
    case 5:  return u8"祭司";
    case 6:  return u8"铁匠";
    case 7:
    case 10: return "GM";
    case 9:  return u8"猎人";
    case 16: return u8"爆破";
    case 31: return u8"食神";
    default:
        char buf[32] = {};
        snprintf(buf, sizeof(buf), "职业%u", profession);
        return buf;
    }
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

// 公屏聊天 sender 反查:走 EntityManager 单链表(AOI 范围内的 CUser),按 +0x70(userId)
// 匹配。比 rb-tree FindUserById 稳得多(后者跟 insert/delete 抢锁会爆)。AOI 之外的
// 玩家(野外大区)拿不到名字 — 调用方 fallback 显示 User#<id>。返回 UTF-8。
std::string LookupAroundPlayerNameById(uint32_t userId)
{
    NearbyPlayer info;
    return LookupAroundPlayerById(userId, info) ? info.name : std::string{};
}

bool LookupAroundPlayerById(uint32_t userId, NearbyPlayer &out)
{
    if (!userId)
        return false;

    auto p = GetLocalUserPtr();
    if (p)
    {
        uint32_t selfId = 0;
        if (SafeReadDword(p + kUserSelfIdOffset, selfId) && selfId == userId)
        {
            char nameBuf[kNameMaxLen + 1] = {};
            if (!SafeReadString(p + kNameOffset, nameBuf, sizeof(nameBuf)) || !nameBuf[0])
                return false;

            uint32_t profession = 0;
            SafeReadDword(p + kUserProfessionOffset, profession);
            float x = 0, y = 0, z = 0;
            GetLocalPosition(x, y, z);
            out = {Big5ToUtf8(nameBuf), profession, GetProfessionName(profession),
                   0.0f, x, y, z, static_cast<int32_t>(GetLocalHp())};
            return true;
        }
    }

    auto mgrAddrPtr = PatternResolver::Get("EntityManagerPtr");
    if (!mgrAddrPtr)
        return false;
    uint32_t mgr = 0;
    if (!SafeReadDword(mgrAddrPtr, mgr) || !mgr)
        return false;

    uint32_t node = 0;
    if (!SafeReadDword(mgr + kUserMgrListHeadOffset, node))
        return false;

    float lx = 0, ly = 0, lz = 0;
    bool haveLocalPos = GetLocalPosition(lx, ly, lz);

    constexpr int kMaxNodes = 1024;
    for (int visited = 0; visited < kMaxNodes && node; ++visited)
    {
        uint32_t nodeId = 0;
        if (SafeReadDword(node + kUserSelfIdOffset, nodeId) && nodeId == userId)
        {
            char nameBuf[kNameMaxLen + 1] = {};
            if (!SafeReadString(node + kNameOffset, nameBuf, sizeof(nameBuf)) || !nameBuf[0])
                return false;

            uint32_t profession = 0;
            SafeReadDword(node + kUserProfessionOffset, profession);

            float x = 0, y = 0, z = 0;
            SafeReadFloat(node + kPositionXOffset, x);
            SafeReadFloat(node + kPositionYOffset, y);
            SafeReadFloat(node + kPositionZOffset, z);

            float distance = 0.0f;
            if (haveLocalPos)
            {
                float dx = x - lx, dy = y - ly, dz = z - lz;
                distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            }

            // 远程玩家 HP 明文存 +0x1B00(CUser__GetCurrentHP),不需要 XOR。
            int32_t  hp = -1;
            uint32_t rawHp = 0;
            if (SafeReadDword(node + kLocalUserHpOffset, rawHp))
                hp = static_cast<int32_t>(rawHp);

            out = {Big5ToUtf8(nameBuf), profession, GetProfessionName(profession),
                   distance, x, y, z, hp};
            return true;
        }
        if (!SafeReadDword(node + kUserNextOffset, node))
            break;
    }
    return false;
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

// ============================================================
//  Fire-full-power — learned-skill snapshot, monster scan, packet send
// ============================================================

namespace
{

bool SafeReadSkill(uintptr_t addr, CSkill &out)
{
    __try
    {
        // memcpy instead of struct assignment — `volatile CSkill` has no
        // implicit operator=(volatile CSkill&), MSVC C2679.
        memcpy(&out, reinterpret_cast<const void *>(addr), sizeof(CSkill));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SafeReadInt64(uintptr_t addr, int64_t &out)
{
    __try
    {
        out = *reinterpret_cast<volatile int64_t *>(addr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Pure-C SEH wrapper — copies up to kSkillNameMaxLen-1 Big5 bytes from
// SkillTable+0x0C into the caller's stack buffer. Kept POD-only (no std types,
// no destructors) so the surrounding function can use __try/__except, which
// MSVC C2712-bans in any function that requires C++ object unwinding.
bool SafeCopySkillNameBytes(const SkillTable *table,
                            char (&out)[kSkillNameMaxLen])
{
    out[0] = '\0';
    if (!table)
        return false;
    __try
    {
        const char *src = reinterpret_cast<const char *>(
            reinterpret_cast<uintptr_t>(table) + kSkillTableNameOffset);
        for (size_t i = 0; i < kSkillNameMaxLen - 1; ++i)
        {
            char c = src[i];
            out[i] = c;
            if (c == '\0')
                break;
        }
        out[kSkillNameMaxLen - 1] = '\0';
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        out[0] = '\0';
        return false;
    }
}

// Reads SkillTable+0x0C as a Big5 char[] and returns UTF-8.
// SEH-protected raw read is delegated to SafeCopySkillNameBytes so this
// function can use std::wstring/std::string (C++ unwinding) without the
// __try/__except + object-destructor conflict (MSVC C2712).
// Stops at first NUL or kSkillNameMaxLen-1 to match wsprintfA's "%s" behaviour
// at the engine-side reference site (sub_92F550 @ 0x9328A8).
std::string SafeReadBig5SkillName(const SkillTable *table)
{
    char raw[kSkillNameMaxLen] = {};
    if (!SafeCopySkillNameBytes(table, raw) || raw[0] == '\0')
        return {};

    // CP 950 = Big5. Two-pass: bytes -> UTF-16 -> UTF-8.
    int wideLen = MultiByteToWideChar(950, 0, raw, -1, nullptr, 0);
    if (wideLen <= 1)
        return {};
    std::wstring wide(static_cast<size_t>(wideLen - 1), L'\0');
    if (MultiByteToWideChar(950, 0, raw, -1, wide.data(), wideLen) <= 0)
        return {};

    int u8Len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
                                    nullptr, 0, nullptr, nullptr);
    if (u8Len <= 1)
        return {};
    std::string out(static_cast<size_t>(u8Len - 1), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), u8Len,
                            nullptr, nullptr) <= 0)
        return {};
    return out;
}

} // anonymous

std::vector<LearnedSkillInfo> GetLearnedSkills()
{
    std::vector<LearnedSkillInfo> result;

    auto mgrPtrAddr = PatternResolver::Get("SkillManagerPtr");
    if (!mgrPtrAddr)
        return result;

    uint32_t mgr = 0;
    if (!SafeReadDword(mgrPtrAddr, mgr) || !mgr)
        return result;

    uint32_t arrayBase = 0;
    uint32_t count     = 0;
    if (!SafeReadDword(mgr + kSkillArrayOffset, arrayBase) ||
        !SafeReadDword(mgr + kSkillCountOffset, count))
        return result;

    // count 是按 skillId 索引的稀疏数组长度(= 最大 skillId + 1),不是已学习
    // 技能数量 — 见到的真实 skillId 有 119/255/348+,所以 count 通常上千。
    // 之前 `count > 256` 守卫导致 GetLearnedSkills 一直返回空。这里换成宽
    // 松上限,只用来防御被撕裂的随机值(0xCCCC..)。
    if (!arrayBase || count == 0 || count > kSkillMaxArrayLen)
        return result;

    result.reserve(64); // 真实学习技能数量上限
    for (uint32_t i = 0; i < count; ++i)
    {
        CSkill s{};
        if (!SafeReadSkill(arrayBase + i * kSkillStructSize, s))
            continue;
        // 用 engine 自己的"已学习"门(SkillManager__CanCastSkill @ 0x938840
        // 第一行就是 cmp [CSkill+0x1C], 0)。skillId/skillLevel 在空槽里也
        // 可能是非零垃圾(取决于 init);learnedFlag 是最权威的信号。
        if (s.learnedFlag == 0)
            continue;
        LearnedSkillInfo info;
        info.skill = s;
        info.name  = SafeReadBig5SkillName(s.skillTable);
        result.push_back(std::move(info));
    }
    return result;
}

namespace
{

// Returns 0=accepted, otherwise reject reason for diagnostics.
int VisitCreature(uintptr_t cre, float lx, float ly, float lz, float maxDist,
                  std::vector<NearbyMonster> &out)
{
    if (!cre)
        return 1;

    // statTable null = entry not yet fully initialised; the engine itself
    // skips these in AutoTarget (it dereferences statTable before reading hp).
    uint32_t statTable = 0;
    if (!SafeReadDword(cre + kCreatureStatTableOffset, statTable) || !statTable)
        return 2;

    // kind lives inside StatTable, NOT on CCreature. cre+0xDC is something else
    // entirely — reading it gave 0 for every entry, silently passing the
    // kind!=NPC && kind!=Pet filter for all entries. Read via the indirection
    // exactly like Creature__MatchesTargetSpec does: *(statTable + 0xDC).
    uint32_t kind = 0;
    if (!SafeReadDword(statTable + kStatTableKindOffset, kind))
        return 3;
    if (kind == kMonsterKindNpc || kind == kMonsterKindPet)
        return 4;

    uint32_t state = 0;
    if (!SafeReadDword(cre + kCreatureStateOffset, state))
        return 5;
    if (state == kCreatureStateDead)
        return 6;

    int64_t hp = 0;
    if (!SafeReadInt64(cre + kCreatureHpOffset, hp))
        return 7;
    if (hp <= 0)
        return 8;

    uint32_t monsterId = 0;
    if (!SafeReadDword(cre + kCreatureIdOffset, monsterId) || !monsterId)
        return 9;

    float x = 0, y = 0, z = 0;
    if (!SafeReadFloat(cre + kCreaturePosXOffset, x) ||
        !SafeReadFloat(cre + kCreaturePosYOffset, y) ||
        !SafeReadFloat(cre + kCreaturePosZOffset, z))
        return 10;

    float dx = x - lx, dy = y - ly, dz = z - lz;
    float d  = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (maxDist > 0 && d > maxDist)
        return 11;

    uint32_t level = 0;
    SafeReadDword(cre + kMonsterLevelOffset, level); // best-effort

    out.push_back({monsterId, kind, level, d, x, y, z, hp, cre});
    return 0;
}

// Re-reads state/hp/id at the snapshot's recorded CCreature pointer and
// returns true iff the entry is still the same alive monster. Callers that
// plan to send a packet targeting this creature MUST run this first — the
// snapshot walk happens once per tick but we then cast N skills at the same
// target; earlier casts (ours or someone else's) may have killed it, and a
// scene transition can swap the list out from under us. Wrapped in SEH
// because we're dereferencing a raw engine pointer that may have been freed.
bool VerifyMonsterStillAlive(const NearbyMonster &m)
{
    if (!m.addr)
        return false;

    bool ok = false;
    __try
    {
        uint32_t state = 0, id = 0, statTable = 0;
        int64_t  hp = 0;

        if (!SafeReadDword(m.addr + kCreatureStatTableOffset, statTable) || !statTable)
            return false;
        if (!SafeReadDword(m.addr + kCreatureIdOffset, id) || id != m.monsterId)
            return false; // slot was recycled into a different creature
        if (!SafeReadDword(m.addr + kCreatureStateOffset, state) ||
            state == kCreatureStateDead)
            return false;
        if (!SafeReadInt64(m.addr + kCreatureHpOffset, hp) || hp <= 0)
            return false;

        ok = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = false;
    }
    return ok;
}

} // anonymous

namespace
{

// Silent walk shared by GetNearbyMonsters and GetMonstersAround. Logs nothing
// itself — diag throttling lives in the public callers so it isn't spammed
// when the AOE scorer calls GetMonstersAround once per candidate target.
bool WalkCreatures(float cx, float cy, float cz, float maxDistance,
                   std::vector<NearbyMonster> &out,
                   uint32_t                   &outMgr,
                   int                        &outVisited,
                   int                         outRejected[16])
{
    auto mgrPtrAddr = PatternResolver::Get("CreatureMgrPtr");
    if (!mgrPtrAddr)
        return false;

    uint32_t mgr = 0;
    if (!SafeReadDword(mgrPtrAddr, mgr) || !mgr)
        return false;
    outMgr = mgr;

    uint32_t node = 0;
    if (!SafeReadDword(mgr + kCreatureMgrListHeadOffset, node))
        return false;

    constexpr int kMaxNodes = 2048;
    int           visited   = 0;
    for (; visited < kMaxNodes && node; ++visited)
    {
        int r = VisitCreature(node, cx, cy, cz, maxDistance, out);
        if (r >= 0 && r < 16 && outRejected)
            outRejected[r]++;
        if (!SafeReadDword(node + kCreatureNextOffset, node))
            break;
    }
    outVisited = visited;

    std::sort(out.begin(), out.end(),
              [](const NearbyMonster &a, const NearbyMonster &b) {
                  return a.distance < b.distance;
              });
    return true;
}

} // anonymous

std::vector<NearbyMonster> GetNearbyMonsters(float maxDistance)
{
    std::vector<NearbyMonster> result;

    float lx = 0, ly = 0, lz = 0;
    if (!GetLocalPosition(lx, ly, lz))
        return result;

    uint32_t mgr = 0;
    int      visited = 0;
    int      rejected[16] = {};
    if (!WalkCreatures(lx, ly, lz, maxDistance, result, mgr, visited, rejected))
        return result;

    static DWORD s_lastDiagMs = 0;
    DWORD        now           = GetTickCount();
    if (now - s_lastDiagMs > 5000)
    {
        s_lastDiagMs = now;
        spdlog::info(
            "GGTB::NearbyMonsters: mgr={:x} visited={} accepted={} "
            "rej_null={} rej_stat={} rej_kind_read={} rej_npc_or_pet={} "
            "rej_state_read={} rej_dead={} rej_hp_read={} rej_zero_hp={} "
            "rej_id={} rej_pos={} rej_range={}",
            static_cast<uintptr_t>(mgr), visited, rejected[0],
            rejected[1], rejected[2], rejected[3], rejected[4],
            rejected[5], rejected[6], rejected[7], rejected[8],
            rejected[9], rejected[10], rejected[11]);
    }

    return result;
}

std::vector<NearbyMonster> GetMonstersAround(float cx, float cy, float cz,
                                             float maxDistance)
{
    std::vector<NearbyMonster> result;
    uint32_t mgr = 0;
    int      visited = 0;
    int      rejected[16] = {};
    WalkCreatures(cx, cy, cz, maxDistance, result, mgr, visited, rejected);
    return result;
}

namespace
{
// Net__BeginSend  — __cdecl, no args, returns the global packet buffer (eax).
// Net__SendSkillPacket — __thiscall(this=netBuf, skillId, targetId).
//
// Verified against the canonical call site in CLocalUser__SendSkillCast at
// 0x7565E0:
//   push edx                ; targetId = v34[28]
//   push ecx                ; skillId  = v34[3522]
//   call Net__BeginSend     ; eax = buf
//   mov  ecx, eax           ; this = buf
//   call Net__SendSkillPacket  ; thiscall, callee cleans 8 bytes
//
// SEH-only helper — must hold no destructible C++ objects (MSVC C2712).
using NetBeginSendFn       = void *(__cdecl *)();
using NetSendSkillPacketFn = int  (__thiscall *)(void *, int, int);

static int CallSendSkillSEH(NetBeginSendFn pBegin, NetSendSkillPacketFn pSend,
                            int skillId, int targetId)
{
    __try
    {
        void *buf = pBegin();
        if (!buf)
            return -1;
        return pSend(buf, skillId, targetId);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}
} // anonymous

bool CastSkillOnMonster(uint32_t skillId, uint32_t monsterId)
{
    auto beginAddr = PatternResolver::Get("NetBeginSend");
    auto sendAddr  = PatternResolver::Get("NetSendSkillPacket");
    if (!beginAddr || !sendAddr)
    {
        spdlog::error("GGTB::CastSkill: pattern unresolved (begin={:x} send={:x})",
                      beginAddr, sendAddr);
        return false;
    }

    auto pBegin = reinterpret_cast<NetBeginSendFn>(beginAddr);
    auto pSend  = reinterpret_cast<NetSendSkillPacketFn>(sendAddr);

    int rv = CallSendSkillSEH(pBegin, pSend, static_cast<int>(skillId),
                              static_cast<int>(monsterId));
    if (rv < 0)
    {
        spdlog::warn("GGTB::CastSkill: SEH caught skillId={} targetId={}",
                     skillId, monsterId);
        return false;
    }
    return true;
}

namespace
{
// Combat__TryUseSkill @ 0x601CA0. Declared __fastcall but ctx/edx are dead
// (see PatternResolver note + IDA comment) — all state comes from globals, so
// we call it as a plain stdcall-style (skillId, targetId) with two dummy regs.
// Modeled here as __fastcall(ecx, edx, skillId, targetId): the compiler loads
// ecx/edx with our dummies and pushes skillId/targetId; callee does `retn 8`.
using CombatTryUseSkillFn = int(__fastcall *)(int ecxDummy, int edxDummy,
                                              int skillId, int targetId);

static int CallTryUseSkillSEH(CombatTryUseSkillFn fn, int skillId, int targetId)
{
    __try
    {
        return fn(0, 0, skillId, targetId);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return INT_MIN;
    }
}

// Reads CSkill[skillId] out of the SkillManager sparse array and reports
// whether the skill is learned and off cooldown. Mirrors the first two gates
// of SkillManager__CanCastSkill (@0x938840) without calling into the engine,
// so a web-driven poll doesn't spam TryUseSkill while a skill is on cooldown.
// Returns: 0 = ok to cast, 1 = not learned / not found, 2 = on cooldown.
static int CheckSkillCastable(uint32_t skillId, float &cooldownOut)
{
    cooldownOut = 0.0f;
    auto mgrPtrAddr = PatternResolver::Get("SkillManagerPtr");
    if (!mgrPtrAddr)
        return 1;
    uint32_t mgr = 0;
    if (!SafeReadDword(mgrPtrAddr, mgr) || !mgr)
        return 1;
    uint32_t arrayBase = 0, count = 0;
    if (!SafeReadDword(mgr + kSkillArrayOffset, arrayBase) ||
        !SafeReadDword(mgr + kSkillCountOffset, count))
        return 1;
    if (!arrayBase || count == 0 || count > kSkillMaxArrayLen || skillId >= count)
        return 1;

    uintptr_t entry = arrayBase + static_cast<uintptr_t>(skillId) * kSkillStructSize;
    uint32_t learned = 0;
    uint32_t cdBits  = 0;
    if (!SafeReadDword(entry + 0x1C, learned) ||
        !SafeReadDword(entry + 0x18, cdBits))
        return 1;
    if (learned == 0)
        return 1;
    float cd;
    std::memcpy(&cd, &cdBits, sizeof(cd));
    cooldownOut = cd;
    if (cd > 0.0f)
        return 2;
    return 0;
}
} // anonymous

bool CastSkillById(uint32_t skillId, uint32_t targetId, bool checkCanCast)
{
    if (skillId == 0)
    {
        spdlog::debug("GGTB::CastSkillById skip skillId=0");
        return false;
    }

    if (checkCanCast)
    {
        float cd = 0.0f;
        int gate = CheckSkillCastable(skillId, cd);
        if (gate == 1)
        {
            spdlog::info("GGTB::CastSkillById skillId={} reject=not_learned", skillId);
            return false;
        }
        if (gate == 2)
        {
            spdlog::debug("GGTB::CastSkillById skillId={} reject=on_cooldown cd={:.2f}",
                          skillId, cd);
            return false;
        }
    }

    auto fnAddr = PatternResolver::Get("CombatTryUseSkill");
    if (!fnAddr)
    {
        spdlog::error("GGTB::CastSkillById: CombatTryUseSkill pattern unresolved");
        return false;
    }

    auto fn = reinterpret_cast<CombatTryUseSkillFn>(fnAddr);
    int rv = CallTryUseSkillSEH(fn, static_cast<int>(skillId),
                                static_cast<int>(targetId));
    if (rv == INT_MIN)
    {
        spdlog::warn("GGTB::CastSkillById: SEH caught skillId={} targetId={}",
                     skillId, targetId);
        return false;
    }

    // TryUseSkill returns 1 only on the "instant cast accepted" path; the
    // self-buff / pretime arms fall through to `return 0` after queuing the
    // cast. So a 0 return does NOT mean failure here — the packets still went
    // out via SendSkillCast. Treat anything that didn't throw as accepted.
    spdlog::info("GGTB::CastSkillById skillId={} targetId={} rv={}",
                 skillId, targetId, rv);
    return true;
}

int CastSkill(const LearnedSkillInfo &info, const std::vector<NearbyMonster> &allMonsters,
              bool dryRun)
{
    const CSkill &skill = info.skill;
    const char   *nm    = info.name.empty() ? "?" : info.name.c_str();

    if (!skill.skillTable || skill.skillId == 0)
    {
        spdlog::debug("GGTB::CastSkill skip skillId={} name={} reason=null_table_or_id",
                      skill.skillId, nm);
        return 0;
    }
    if (allMonsters.empty())
    {
        spdlog::debug("GGTB::CastSkill skip skillId={} name={} reason=no_monsters",
                      skill.skillId, nm);
        return 0;
    }

    auto tableAddr = reinterpret_cast<uintptr_t>(skill.skillTable);

    uint32_t typeVal = 0, variantVal = 0, rangeTiles = 0, coverTiles = 0;
    if (!SafeReadDword(tableAddr + kSkillTableTypeOffset,         typeVal))
    {
        spdlog::warn("GGTB::CastSkill skillId={} name={} table={:x} type read failed",
                     skill.skillId, nm, tableAddr);
        return 0;
    }
    SafeReadDword(tableAddr + kSkillTablePacketVariantOffset, variantVal);
    SafeReadDword(tableAddr + kSkillTableRangeOffset,         rangeTiles);
    SafeReadDword(tableAddr + kSkillTableCoverRangeOffset,    coverTiles);

    auto type = static_cast<SkillType>(typeVal);

    // Only type==Attack (1) is a castable combat skill. type==2 is Interact
    // (trade/duel-request — engine puts local player into target-pick mode,
    // never a cast packet). type==0/4/8/... are passive/mount/summon/movement.
    // All real damage-dealing skills report type==1, regardless of whether
    // they're single-target or AOE — that split is in packetVariant below.
    if (type != SkillType::Attack)
    {
        spdlog::info("GGTB::CastSkill REJECT skillId={} name={} type={} variant={} "
                     "reason=non_attack_type",
                     skill.skillId, nm, typeVal, variantVal);
        return 0;
    }

    // Packet variant whitelist — this is also the single/AOE splitter.
    //   variant 2 = single-target packet shape
    //   variant 3 = AOE list packet shape
    // Variants 1/5/6/7 contain hardcoded skillId branches inside
    // Net__SendSkillPacket that send unexpected shapes (pet / world-pos /
    // item-skill / couple-skill) the server disconnects on.
    if (variantVal != 2 && variantVal != 3)
    {
        spdlog::info("GGTB::CastSkill REJECT skillId={} name={} type={} variant={} "
                     "reason=variant_not_whitelisted",
                     skill.skillId, nm, typeVal, variantVal);
        return 0;
    }

    // Filter by the skill's own cast range. Engine uses the SAME unit scale
    // for skillRange and for (targetPos - localPos) — verified by staring at
    // CLocalUser__UseSkill @ 0x7556c4: it does `(cx-lx)^2 + (cy-ly)^2 > range^2`
    // where coords come straight from CUser/CCreature +0x19C/+0x1A0 (int tile)
    // and range comes from SkillTable+0x170. No multiplier. Same story in
    // Combat__GetAttackRangeSquared @ 0x7493C1 which squares the raw tile
    // count and compares to the raw squared tile distance (cap 225 = 15^2).
    //
    // Our `m.distance` is sqrt(dx^2+dy^2+dz^2) over CCreature/CUser +0x3C
    // (float world), which lives in the same numeric space — a creature a
    // few tiles away reports distance≈10, not distance≈500. The earlier
    // `* 50` multiplier was a pure guess that effectively disabled the
    // gate (rangeWorld=350 > almost every in-game distance), which is why
    // we were firing at targets ~10 tiles away with rangeTiles=7 skills.
    //
    // If the field reads 0 (table not yet populated) fall back to the
    // caller's snapshot — better to fire at suboptimal distance than skip.
    float rangeLimit = static_cast<float>(rangeTiles);

    std::vector<NearbyMonster> targets;
    if (rangeLimit > 0.0f)
    {
        targets.reserve(allMonsters.size());
        for (auto &m : allMonsters)
            if (m.distance <= rangeLimit)
                targets.push_back(m);
    }
    else
    {
        targets = allMonsters;
    }
    if (targets.empty())
    {
        spdlog::info("GGTB::CastSkill REJECT skillId={} name={} type={} variant={} "
                     "rangeTiles={} reason=no_target_in_range (closest={:.1f})",
                     skill.skillId, nm, typeVal, variantVal, rangeTiles,
                     allMonsters.front().distance);
        return 0;
    }

    // Pick the primary target.
    size_t pickIdx = 0;
    if (variantVal == 3 && coverTiles > 0)
    {
        // AOE packet shape (variant 3) — server resolves AOE from the primary
        // target. Pick the monster with the most neighbours inside
        // skillCoverRange so a single packet covers as many enemies as
        // possible.
        // coverTiles is in the same unit space as m.distance/position deltas
        // — see the rangeLimit comment above. No tile->world multiplier.
        float  coverR = static_cast<float>(coverTiles);
        float  r2     = coverR * coverR;
        size_t bestCnt    = 0;
        for (size_t i = 0; i < targets.size(); ++i)
        {
            const auto &p   = targets[i];
            size_t      cnt = 0;
            for (size_t j = 0; j < targets.size(); ++j)
            {
                float dx = targets[j].x - p.x;
                float dy = targets[j].y - p.y;
                float dz = targets[j].z - p.z;
                if (dx * dx + dy * dy + dz * dz <= r2)
                    ++cnt;
            }
            if (cnt > bestCnt)
            {
                bestCnt = cnt;
                pickIdx = i;
            }
        }
    }
    // variant==2 (single-target) or variant==3 with no cover data:
    //   targets[0] is the closest in-range monster.

    const auto &chosen = targets[pickIdx];

    // Final liveness recheck against the live engine pointer. The monster
    // snapshot is from tick-start; by the time we actually send, earlier
    // casts this same tick (or anyone else's damage) may have killed it.
    // Sending a skill packet at a corpse is both wasted bandwidth and — for
    // variant 3 AOE where the server fans out from this primary — a hint
    // the server logs as abnormal (dead-target cast attempts).
    if (!VerifyMonsterStillAlive(chosen))
    {
        spdlog::info("GGTB::CastSkill REJECT skillId={} name={} type={} variant={} "
                     "target={} reason=target_dead_or_gone",
                     skill.skillId, nm, typeVal, variantVal, chosen.monsterId);
        return 0;
    }

    spdlog::info("GGTB::CastSkill {} skillId={} name={} type={} variant={} "
                 "rangeTiles={} coverTiles={} target={} dist={:.1f}",
                 dryRun ? "DRY" : "FIRE", skill.skillId, nm, typeVal, variantVal,
                 rangeTiles, coverTiles, chosen.monsterId, chosen.distance);

    // Return value = "would have sent (or did send)". Caller uses this to count
    // its per-tick cap, so dryRun must return 1 on a successful filter pass —
    // otherwise rejected skills wrongly consume cap slots and the user can't
    // see what the worker would actually fire.
    if (dryRun)
        return 1;
    return CastSkillOnMonster(skill.skillId, chosen.monsterId) ? 1 : 0;
}

// ============================================================================
// Bomber-class bomb throwing (爆破师火力全开)
// ============================================================================

uint32_t GetProfession()
{
    auto user = GetLocalUserPtr();
    if (!user)
        return 0;
    uint32_t prof = 0;
    __try
    {
        prof = *reinterpret_cast<uint32_t *>(user + kUserProfessionOffset);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
    return prof;
}

bool SetSelectedItemId(uint32_t itemId)
{
    auto user = GetLocalUserPtr();
    if (!user)
        return false;
    __try
    {
        *reinterpret_cast<uint32_t *>(user + kUserSelectedItemIdOffset) = itemId;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return true;
}

// Item__GetItemClass(int itemId) — __stdcall, returns 3 for throwable bombs,
// 1/2 for other consumable classes, 0 otherwise. Resolved via PatternResolver
// so this file doesn't hard-code the RVA twice.
using ItemGetItemClassFn = int (__stdcall *)(int);

static int CallItemGetItemClassSEH(ItemGetItemClassFn pFn, int itemId)
{
    __try
    {
        return pFn(itemId);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

int GetItemClass(uint32_t itemId)
{
    auto addr = PatternResolver::Get("ItemGetItemClass");
    if (!addr)
        return 0;
    auto pFn = reinterpret_cast<ItemGetItemClassFn>(addr);
    int  rv  = CallItemGetItemClassSEH(pFn, static_cast<int>(itemId));
    return rv < 0 ? 0 : rv;
}

namespace
{
// SEH-only helpers for bomber-class bag scanning. Kept in a POD-free anonymous
// namespace so MSVC's C2712 (can't use __try in a function requiring object
// unwinding) never triggers.
struct BagSlotRaw
{
    uint32_t  bagId;
    uint32_t  itemId;
    uint32_t  count;
    uintptr_t itemTable;
};

static bool ReadContainerPtrSEH(uintptr_t ptrAddr, uintptr_t *out)
{
    __try
    {
        *out = *reinterpret_cast<uintptr_t *>(ptrAddr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool ReadBagSlotSEH(uintptr_t slot, BagSlotRaw *out)
{
    __try
    {
        out->bagId     = *reinterpret_cast<uint32_t *>(slot + kItemBagIdOffset);
        out->itemId    = *reinterpret_cast<uint32_t *>(slot + kItemItemIdOffset);
        out->count     = *reinterpret_cast<uint32_t *>(slot + kItemCountOffset);
        out->itemTable = *reinterpret_cast<uintptr_t *>(slot + kItemItemTableOffset);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool ReadItemNameSEH(uintptr_t itemTable, char *buf, size_t bufSize)
{
    __try
    {
        const char *src = reinterpret_cast<const char *>(itemTable + kItemTableNameOffset);
        size_t i = 0;
        for (; i < bufSize - 1 && src[i] != '\0'; ++i)
            buf[i] = src[i];
        buf[i] = '\0';
        return i > 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        buf[0] = '\0';
        return false;
    }
}
} // anonymous

bool FindBombInBag(BagBombInfo &out_info)
{
    out_info = {};
    auto containerPtrAddr = PatternResolver::Get("ItemContainerPtr");
    if (!containerPtrAddr)
        return false;

    uintptr_t container = 0;
    if (!ReadContainerPtrSEH(containerPtrAddr, &container) || !container)
        return false;

    // Full-capacity walk — ItemContainer__GetBagSize returns the USED count
    // (first empty slot terminator), so occupied slots past that count are
    // still valid entries we want to scan. Old so3dFullCheat used 192 as
    // the hard cap and the new build's layout hasn't grown the array.
    for (size_t i = 0; i < kItemFullSlotCount; ++i)
    {
        uintptr_t  slot = container + kItemArrayBaseOffset + i * kItemStride;
        BagSlotRaw raw{};
        if (!ReadBagSlotSEH(slot, &raw))
            continue;
        if (!raw.itemTable || raw.itemId == 0)
            continue;

        // Item__GetItemClass returns 3 for the bomb class — same classification
        // the engine's own AutoHunt uses when deciding "can I feed this into
        // skill 83?". Matching by class instead of item name sidesteps the
        // client-side string encryption and still picks up both 原子彈 and
        // 黑色自製炸藥 (they share class 3).
        if (GetItemClass(raw.itemId) != 3)
            continue;

        out_info.bagId     = raw.bagId;
        out_info.slotIndex = static_cast<uint32_t>(i); // 老项目里就是这个 i,wire 用 i+0xD
        out_info.itemId    = raw.itemId;
        out_info.count     = raw.count;
        return true;
    }
    return false;
}

uint32_t GetTotalBombCount()
{
    auto containerPtrAddr = PatternResolver::Get("ItemContainerPtr");
    if (!containerPtrAddr)
        return 0;

    uintptr_t container = 0;
    if (!ReadContainerPtrSEH(containerPtrAddr, &container) || !container)
        return 0;

    uint32_t total = 0;
    for (size_t i = 0; i < kItemFullSlotCount; ++i)
    {
        uintptr_t  slot = container + kItemArrayBaseOffset + i * kItemStride;
        BagSlotRaw raw{};
        if (!ReadBagSlotSEH(slot, &raw))
            continue;
        if (!raw.itemTable || raw.itemId == 0)
            continue;
        if (GetItemClass(raw.itemId) != 3)
            continue;
        total += raw.count;
    }
    return total;
}

bool CastMakeBomb(bool dryRun)
{
    if (dryRun)
    {
        spdlog::info("GGTB::CastBomb DRY skillId={} (MakeBomb)", kSkillIdMakeBomb);
        return false;
    }
    spdlog::info("GGTB::CastBomb FIRE skillId={} (MakeBomb, no target)",
                 kSkillIdMakeBomb);
    // targetId = 0 — MakeBomb's SendSkillCast branch reads v34[3264]==0 and
    // falls through the target-validation gates because v30[68] (variant)==6
    // takes the "else" branch that doesn't require a creature.
    return CastSkillOnMonster(kSkillIdMakeBomb, 0);
}

int CastBombOnMonster(const NearbyMonster &target, bool dryRun)
{
    // Liveness recheck against the live engine pointer — same gate CastSkill
    // runs at line ~1521. Tick-start snapshot is stale by the time we reach
    // the second/third bomb of a per-tick burst, and the prior throws may
    // have killed this very target.
    if (!VerifyMonsterStillAlive(target))
    {
        spdlog::info("GGTB::CastBomb REJECT skillId={} target={} reason=target_dead_or_gone",
                     kSkillIdThrowBomb, target.monsterId);
        return 0;
    }

    BagBombInfo bomb{};
    bool        haveBomb = FindBombInBag(bomb);

    if (!haveBomb)
    {
        // No bomb → craft one. Mirrors AutoHunt__ChooseNextAction: when a
        // bomber has skill 325 learned but no bomb in bag, it returns 325
        // (MakeBomb) instead of 83 (ThrowBomb).
        return CastMakeBomb(dryRun) ? 1 : 0;
    }

    // Pin the selected bomb bagId so SendSkillCast's StatTable lookup
    // succeeds. Net__SendSkillPacket reads *(localUser+0x3578) and passes it
    // to StatTable__GetItemTblEntryById which matches against item+0x00 (the
    // bag-slot unique id), NOT item+0x04 (the game template itemId). Writing
    // the wrong field causes the lookup to return NULL → SetSkillMode(0) abort.
    if (!SetSelectedItemId(bomb.bagId))
    {
        spdlog::warn("GGTB::CastBomb failed to write selectedBagId={} — aborting",
                     bomb.bagId);
        return 0;
    }

    spdlog::info("GGTB::CastBomb {} skillId={} target={} bagId={} itemId={} count={}",
                 dryRun ? "DRY" : "FIRE", kSkillIdThrowBomb, target.monsterId,
                 bomb.bagId, bomb.itemId, bomb.count);

    if (dryRun)
        return 1;
    return CastSkillOnMonster(kSkillIdThrowBomb, target.monsterId) ? 1 : 0;
}

// ---------- Raw-packet bomber (mirrors old so3dFullCheat ThrowBomb) ----------
namespace
{
// Net__SkillSendPackage — __thiscall(this=netBuf, routingTag, buf, size).
// Verified at 0xB2EA90; called by every variant branch inside
// Net__SendSkillPacket with routingTag=411022. Old code reached this same
// function via `mov ecx,gameClient; push size; push buf; push 0x6458E; call X`.
using NetSkillSendPackageFn = int (__thiscall *)(void *, int, const void *, size_t);

constexpr int kRoutingTagSkill = 411022; // 0x6458E

static int CallSkillSendPackageSEH(NetBeginSendFn pBegin,
                                   NetSkillSendPackageFn pSend,
                                   const void *buf, size_t size,
                                   int routingTag = kRoutingTagSkill)
{
    __try
    {
        void *netBuf = pBegin();
        if (!netBuf)
            return -1;
        return pSend(netBuf, routingTag, buf, size);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}
} // anonymous

int CastBombOnMonsterRaw(const NearbyMonster              &target,
                         const std::vector<NearbyMonster> &allMonsters,
                         bool                              dryRun)
{
    // Liveness recheck — same gate engine path uses. Tick-start snapshot can
    // be stale on the second/third bomb of a per-tick burst.
    if (!VerifyMonsterStillAlive(target))
    {
        spdlog::info("GGTB::CastBombRaw REJECT skillId={} target={} reason=target_dead_or_gone",
                     kSkillIdThrowBomb, target.monsterId);
        return 0;
    }

    BagBombInfo bomb{};
    if (!FindBombInBag(bomb))
    {
        // No bomb → craft one. Same fallback as the engine path.
        return CastMakeBomb(dryRun) ? 1 : 0;
    }

    // Pin the selected bomb bagId. Even though raw mode embeds bombItemId in
    // the buffer itself, we still write 0x3578 to keep parity with the
    // engine's invariants — some downstream client-side state machines (e.g.
    // animation arm) read that field independently of the wire packet.
    if (!SetSelectedItemId(bomb.bagId))
    {
        spdlog::warn("GGTB::CastBombRaw failed to write selectedBagId={} — aborting",
                     bomb.bagId);
        return 0;
    }

    // Collect alive neighbors within kBomberNeighborTileRadius of the primary.
    // Old project hardcoded this to 5 tiles regardless of the skill's
    // SkillTable+0x16C — keep the same constant for behavioral parity.
    std::vector<uint32_t> neighborIds;
    neighborIds.reserve(allMonsters.size());
    const float r2 = kBomberNeighborTileRadius * kBomberNeighborTileRadius;
    for (auto &m : allMonsters)
    {
        if (m.monsterId == target.monsterId)
            continue;
        if (m.hp <= 0)
            continue;
        float dx = m.x - target.x, dy = m.y - target.y, dz = m.z - target.z;
        if (dx * dx + dy * dy + dz * dz <= r2)
            neighborIds.push_back(m.monsterId);
    }

    // Build the wire buffer.
    //   [0] skillId, [1] level, [2] bombItemId, [3] isLocalInRange,
    //   [4] count = 1 + N, [5] primary, [6..] neighbors.
    // isLocalInRange: old project used `neighbors > 0 ? 0 : 1`. Engine path
    // computes the actual local-vs-target distance check; we keep the old
    // project's heuristic since it preserved the exact wire format that
    // worked there.
    const size_t targetCount = 1 + neighborIds.size();
    std::vector<uint32_t> buf(5 + targetCount);
    buf[0] = kSkillIdThrowBomb;
    buf[1] = 1;                                          // skill level
    buf[2] = bomb.bagId;                                 // raw bagId (验证可用,勿改)
    buf[3] = neighborIds.empty() ? 1u : 0u;
    buf[4] = static_cast<uint32_t>(targetCount);
    buf[5] = target.monsterId;
    for (size_t i = 0; i < neighborIds.size(); ++i)
        buf[6 + i] = neighborIds[i];

    spdlog::info("GGTB::CastBombRaw {} skillId={} target={} bagId={} itemId={} count={} "
                 "neighbors={} wire_bytes={}",
                 dryRun ? "DRY" : "FIRE", kSkillIdThrowBomb, target.monsterId,
                 bomb.bagId, bomb.itemId, bomb.count, neighborIds.size(),
                 buf.size() * 4);

    if (dryRun)
        return 1;

    auto beginAddr = PatternResolver::Get("NetBeginSend");
    auto sendAddr  = PatternResolver::Get("NetSkillSendPackage");
    if (!beginAddr || !sendAddr)
    {
        spdlog::error("GGTB::CastBombRaw: pattern unresolved (begin={:x} send={:x})",
                      beginAddr, sendAddr);
        return 0;
    }

    auto pBegin = reinterpret_cast<NetBeginSendFn>(beginAddr);
    auto pSend  = reinterpret_cast<NetSkillSendPackageFn>(sendAddr);

    int rv = CallSkillSendPackageSEH(pBegin, pSend, buf.data(), buf.size() * sizeof(uint32_t));
    if (rv < 0)
    {
        spdlog::warn("GGTB::CastBombRaw: SEH skillId={} target={}",
                     kSkillIdThrowBomb, target.monsterId);
        return 0;
    }
    return 1;
}

// ---------- CG_ITEM_DROP (drop all bombs) ----------
namespace
{
// sub_B2C790 — __thiscall(this=netBuf, protocolId, a3, a4). Generic 3-arg
// packet builder used for CG_ITEM_DROP (411012). Engine call shape inside
// sub_8FCC00 (drag-and-drop):
//   buf = Net__BeginSend();
//   sub_B2C790(buf, 411012, unknown_libname_3607(cursorItem) + 13, count);
//
// IMPORTANT — bagId encoding mismatch between cursor item and bag-slot item:
//   * Cursor item +0x00 stores the RAW slot index (j). Engine's drag-and-drop
//     reads cursor +0x00 and adds 13 to get the wire field.
//   * Bag-slot item +0x00 stores the WIRE-FORMAT bagId (= j + 13 already).
//     Verified via sendlog 2026-05-10: throw-bomb (proto 411022) sent
//     bombItemId=14 reading bag-slot +0x00, and the server hit the right slot.
//
// Earlier versions of this drop helper added +13 to bag-slot.bagId, doubling
// the offset (slot 1 → wire 27 instead of 14) and dropping at the wrong slot.
// 我们直接读 bag slot,不走光标,所以拿到的就是 wire 值,不要再加 13。
using NetSendTripleFn = int (__thiscall *)(void *, int, int, int);

constexpr int kProtocolItemDrop = 411012;

static int CallSendTripleSEH(NetBeginSendFn pBegin, NetSendTripleFn pSend,
                             int protocolId, int a3, int a4)
{
    __try
    {
        void *buf = pBegin();
        if (!buf)
            return -1;
        return pSend(buf, protocolId, a3, a4);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}
} // anonymous

int DropAllBombsInBag()
{
    auto beginAddr = PatternResolver::Get("NetBeginSend");
    auto sendAddr  = PatternResolver::Get("NetSendTriple");
    auto contAddr  = PatternResolver::Get("ItemContainerPtr");
    if (!beginAddr || !sendAddr || !contAddr)
    {
        spdlog::error("GGTB::DropAllBombs: pattern unresolved (begin={:x} send={:x} cont={:x})",
                      beginAddr, sendAddr, contAddr);
        return 0;
    }

    uintptr_t container = 0;
    if (!ReadContainerPtrSEH(contAddr, &container) || !container)
        return 0;

    auto pBegin = reinterpret_cast<NetBeginSendFn>(beginAddr);
    auto pSend  = reinterpret_cast<NetSendTripleFn>(sendAddr);

    int sent = 0;
    for (size_t i = 0; i < kItemFullSlotCount; ++i)
    {
        uintptr_t  slot = container + kItemArrayBaseOffset + i * kItemStride;
        BagSlotRaw raw{};
        if (!ReadBagSlotSEH(slot, &raw))
            continue;
        if (!raw.itemTable || raw.itemId == 0 || raw.count == 0)
            continue;
        if (GetItemClass(raw.itemId) != 3)
            continue;

        // bag-slot item+0x00 已经是 wire 格式 (= slotIndex+13);不要再 +13。
        // 详见上面 comment block 关于 cursor item / bag slot 的差异。
        int wireBagId = static_cast<int>(raw.bagId);
        int count     = static_cast<int>(raw.count);
        int rv = CallSendTripleSEH(pBegin, pSend, kProtocolItemDrop, wireBagId, count);
        if (rv < 0)
        {
            spdlog::warn("GGTB::DropAllBombs: SEH bagId={} itemId={} count={}",
                         raw.bagId, raw.itemId, raw.count);
            continue;
        }
        spdlog::info("GGTB::DropAllBombs: dropped slot={} wireBag={} itemId={} count={}",
                     i, wireBagId, raw.itemId, raw.count);
        ++sent;
    }
    return sent;
}

bool DropBagItem(uint32_t wireBagId, uint32_t count)
{
    if (wireBagId == 0 || count == 0)
        return false;

    auto beginAddr = PatternResolver::Get("NetBeginSend");
    auto sendAddr  = PatternResolver::Get("NetSendTriple");
    if (!beginAddr || !sendAddr)
    {
        spdlog::error("GGTB::DropBagItem: pattern unresolved (begin={:x} send={:x})",
                      beginAddr, sendAddr);
        return false;
    }

    auto pBegin = reinterpret_cast<NetBeginSendFn>(beginAddr);
    auto pSend  = reinterpret_cast<NetSendTripleFn>(sendAddr);

    // wireBagId 是 BagItemInfo::bagId (item+0x00),已经是 wire 格式;不要再 +13。
    // 参见 DropAllBombsInBag 上方的 cursor-vs-bag-slot 编码说明。
    int rv = CallSendTripleSEH(pBegin, pSend, kProtocolItemDrop,
                               static_cast<int>(wireBagId),
                               static_cast<int>(count));
    if (rv < 0)
    {
        spdlog::warn("GGTB::DropBagItem: SEH wireBag={} count={}", wireBagId, count);
        return false;
    }
    spdlog::info("GGTB::DropBagItem: wireBag={} count={}", wireBagId, count);
    return true;
}

// ---------- Local player wallet ----------
int64_t GetLocalMoney()
{
    auto p = GetLocalUserPtr();
    if (!p)
        return 0;
    int64_t v = 0;
    __try
    {
        v = *reinterpret_cast<volatile int64_t *>(p + kLocalMoneyOffset);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
    return v;
}

int64_t GetLocalHp()
{
    auto p = GetLocalUserPtr();
    if (!p)
        return 0;
    int32_t v = 0;
    __try
    {
        int32_t raw = *reinterpret_cast<volatile int32_t *>(p + kLocalUserHpOffset);
        int32_t key = *reinterpret_cast<volatile int32_t *>(kHpXorKeyAddr);
        v = raw ^ key;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
    return v;
}

uint32_t GetLocalUserId()
{
    auto p = GetLocalUserPtr();
    if (!p)
        return 0;
    uint32_t v = 0;
    __try
    {
        v = *reinterpret_cast<volatile uint32_t *>(p + kUserSelfIdOffset);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
    return v;
}

bool IsLocalDead()
{
    return GetLocalHp() <= 0;
}

// ---------- 登录凭据 (账号/密码,明文) ----------

namespace {
// 从 localUser+offset 读定长字段为 C 字符串。先跳过前导 \0,再取到下一个 \0 / maxLen。
// 必须跳前导 \0:id_input 控件 +128 源缓冲带前导 \0,memmove 进 localUser 后账号字段
// 实测是 "\0gongyu9011213"(见 Login__BuildAndSendLoginPacket),不跳会读成空串。SEH 安全。
static bool ReadFixedStringSEH(uintptr_t addr, char *out, size_t maxLen)
{
    __try
    {
        size_t start = 0;
        while (start < maxLen && *reinterpret_cast<volatile char *>(addr + start) == '\0')
            ++start;
        size_t n = 0;
        for (size_t i = start; i < maxLen; ++i)
        {
            char c = *reinterpret_cast<volatile char *>(addr + i);
            if (c == '\0')
                break;
            out[n++] = c;
        }
        out[n] = '\0';
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
} // namespace

bool GetLoginCredentials(LoginCredentials &out)
{
    uintptr_t user = GetLocalUserPtr();
    if (!user)
    {
        spdlog::warn("GGTB::GetLoginCredentials: localUser not ready");
        return false;
    }

    char acc[kLoginFieldSize + 1] = {};
    char pwd[kLoginFieldSize + 1] = {};
    bool okAcc = ReadFixedStringSEH(user + kLoginAccountOffset, acc, kLoginFieldSize);
    bool okPwd = ReadFixedStringSEH(user + kLoginPasswordOffset, pwd, kLoginFieldSize);
    if (!okAcc || acc[0] == '\0')
    {
        spdlog::warn("GGTB::GetLoginCredentials: account empty/unreadable (okAcc={})", okAcc);
        return false;
    }

    out.account = acc;
    out.password = okPwd ? pwd : "";
    // 不打印密码;只记账号 + 密码长度,便于排查而不泄露。
    spdlog::info("GGTB::GetLoginCredentials: account='{}' pwdLen={}", out.account, out.password.size());
    return true;
}

// ---------- Current map id ----------
//
// Reads g_MapIdTable[g_CurMapSlot]. Both globals are pattern-resolved
// (their addresses CAN move across binary revisions). Returns 0 if either
// resolve fails, the slot index is wild (we cap at 2048 to catch torn reads
// during scene transitions), or memory is unmapped.
//
// IDA verification (18+ ref sites, e.g. AutoMover_Update @ 0x607893):
//   mov eax, ds:dword_D67CB4          ; eax = slot index VALUE
//   mov edx, ds:dword_D6AE78[eax*4]   ; edx = g_MapIdTable[slot]
// `dword_D67CB4` is an `int` (slot index), NOT an `int*`. Earlier revision
// of this function treated it as a pointer and did an extra deref, which
// produced 0 / torn reads → AutoRevive's `waitInTown` never satisfied.
// sub_978670 helper confirms the same pattern: it does `return
// g_MapIdTable[slot] == 125;` against the slot passed in directly.
uint32_t GetCurrentMapId()
{
    auto slotAddr   = PatternResolver::Get("CurMapSlot");
    auto mapTblAddr = PatternResolver::Get("MapIdTable");
    if (!slotAddr || !mapTblAddr)
        return 0;

    // slot index is stored DIRECTLY at &dword_D67CB4 — single deref.
    uint32_t slot = 0;
    if (!SafeReadDword(slotAddr, slot))
        return 0;
    if (slot >= 2048) // sanity: table is small; torn reads land here
        return 0;

    // g_MapIdTable[slot]
    uint32_t mapId = 0;
    if (!SafeReadDword(mapTblAddr + slot * sizeof(uint32_t), mapId))
        return 0;
    return mapId;
}

// ---------- AutoRevive ----------
namespace
{
using NetSendDwordFn        = int(__thiscall *)(void *, int, int);

static int CallNetSendDwordSEH(NetBeginSendFn pBegin, NetSendDwordFn pSend,
                               int proto, int value)
{
    __try
    {
        void *buf = pBegin();
        if (!buf)
            return -1;
        return pSend(buf, proto, value);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}
} // namespace

bool ReviveToTown(bool safetyCheck, int reviveMode)
{
    (void)safetyCheck; // Kept for remote-command compatibility; always send.

    auto beginAddr = PatternResolver::Get("NetBeginSend");
    auto sendAddr  = PatternResolver::Get("NetSendDword");
    if (!beginAddr || !sendAddr)
    {
        spdlog::error("GGTB::ReviveToTown: pattern unresolved (begin={:x} send={:x})",
                      beginAddr, sendAddr);
        return false;
    }

    auto pBegin = reinterpret_cast<NetBeginSendFn>(beginAddr);
    auto pSend  = reinterpret_cast<NetSendDwordFn>(sendAddr);

    constexpr int kProtoPlayerRevive = 412017;
    int rv = CallNetSendDwordSEH(pBegin, pSend, kProtoPlayerRevive, reviveMode);
    if (rv < 0)
    {
        spdlog::warn("GGTB::ReviveToTown: SEH on send mode={}", reviveMode);
        return false;
    }
    spdlog::info("GGTB::ReviveToTown: sent CG_PLAYER_REVIVE(412017) mode={} rv={}",
                 reviveMode, rv);
    return true;
}

bool RequestLevelUpCheck(int payload)
{
    auto beginAddr = PatternResolver::Get("NetBeginSend");
    auto sendAddr  = PatternResolver::Get("NetSendDword");
    if (!beginAddr || !sendAddr)
    {
        spdlog::error("GGTB::RequestLevelUpCheck: pattern unresolved (begin={:x} send={:x})",
                      beginAddr, sendAddr);
        return false;
    }

    auto pBegin = reinterpret_cast<NetBeginSendFn>(beginAddr);
    auto pSend  = reinterpret_cast<NetSendDwordFn>(sendAddr);

    constexpr int kProtoLevelUpCheck = 412016;
    int rv = CallNetSendDwordSEH(pBegin, pSend, kProtoLevelUpCheck, payload);
    if (rv < 0)
    {
        spdlog::warn("GGTB::RequestLevelUpCheck: SEH on send payload={}", payload);
        return false;
    }
    spdlog::info("GGTB::RequestLevelUpCheck: sent CG_LEVEL_UP_CHECK(412016) payload={} rv={}",
                 payload, rv);
    return true;
}

// ---------- Auto-trade (CExchange trade flow) ----------
// 三条交易包(412028/412033/412035)都是单 DWORD body,复用 AutoRevive 的
// NetSendDword SEH 路径。同意/锁定/确认这层只发包就够,引擎从服务端响应里推进
// 交易状态机;但"本地把交易窗口弹出来"那一步服务端不负责 —— 必须像手动点接受
// 按钮一样本地调 Trade_OpenLocalExchangeWindow,见下方 OpenLocalTradeWindow。
namespace
{
bool SendTradeDwordPacket(int proto, int value, const char *tag)
{
    auto beginAddr = PatternResolver::Get("NetBeginSend");
    auto sendAddr  = PatternResolver::Get("NetSendDword");
    if (!beginAddr || !sendAddr)
    {
        spdlog::error("GGTB::{}: pattern unresolved (begin={:x} send={:x})",
                      tag, beginAddr, sendAddr);
        return false;
    }
    auto pBegin = reinterpret_cast<NetBeginSendFn>(beginAddr);
    auto pSend  = reinterpret_cast<NetSendDwordFn>(sendAddr);
    int rv = CallNetSendDwordSEH(pBegin, pSend, proto, value);
    if (rv < 0)
    {
        spdlog::warn("GGTB::{}: SEH on send proto={} value={}", tag, proto, value);
        return false;
    }
    spdlog::info("GGTB::{}: sent proto={} value={} rv={}", tag, proto, value, rv);
    return true;
}
} // namespace

bool SendTradeAccept(uint32_t requesterId)
{
    return SendTradeDwordPacket(412028, static_cast<int>(requesterId), "SendTradeAccept");
}
bool SendTradeDecline(uint32_t requesterId)
{
    return SendTradeDwordPacket(412029, static_cast<int>(requesterId), "SendTradeDecline");
}
bool SendTradeLock()
{
    return SendTradeDwordPacket(412033, 412033, "SendTradeLock");
}
bool SendTradeConfirm()
{
    return SendTradeDwordPacket(412035, 412035, "SendTradeConfirm");
}
bool SendTradeCancel()
{
    return SendTradeDwordPacket(412036, 412036, "SendTradeCancel");
}

// 自动接受时本地开交易窗口 —— 复刻 Trade_RequestPopup accept 分支的第一步。
// 手动点「接受」按钮做的是 Trade_OpenLocalExchangeWindow(ctrl) + 发 412028 两件
// 事;我们之前只发 412028,服务端虽认为已同意,本地窗口却停在请求态,直到对方
// 放物品才被动刷出来。这里把 control 32(CExchange)取出来调引擎开窗函数补齐。
//
// control 32 由 GC_OnTradeRequest_07CC62 收到请求时 sub_9E9C50(32,...) 建好。worker
// 跨线程轮询到请求信号时通常已建好;若派发竞态尚未建好,GetUIContent 返回 0,本
// 函数返回 false 让调用方下个 poll 再试。三个引擎调用全程 SEH 包。
namespace
{
using UIMgrGetSingletonFn = void *(__cdecl *)();
using UIMgrGetUIContentFn = int(__thiscall *)(void *, int);
using TradeOpenWindowFn   = int(__thiscall *)(int);

// 返回:1 = 已开窗;0 = control 32 还不在(或单例为空),调用方应重试;-1 = SEH。
static int CallOpenTradeWindowSEH(UIMgrGetSingletonFn pMgr,
                                  UIMgrGetUIContentFn pGet,
                                  TradeOpenWindowFn pOpen)
{
    __try
    {
        void *mgr = pMgr();
        if (!mgr)
            return 0;
        int ctrl = pGet(mgr, 32); // CExchange trade window control id = 32
        if (!ctrl)
            return 0;
        pOpen(ctrl);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}
} // namespace

bool OpenLocalTradeWindow()
{
    auto mgrAddr  = PatternResolver::Get("UIManagerGetSingleton");
    auto getAddr  = PatternResolver::Get("UIManagerGetUIContent");
    auto openAddr = PatternResolver::Get("TradeOpenLocalWindow");
    if (!mgrAddr || !getAddr || !openAddr)
    {
        spdlog::error("GGTB::OpenLocalTradeWindow: pattern unresolved "
                      "(mgr={:x} get={:x} open={:x})", mgrAddr, getAddr, openAddr);
        return false;
    }

    int rv = CallOpenTradeWindowSEH(
        reinterpret_cast<UIMgrGetSingletonFn>(mgrAddr),
        reinterpret_cast<UIMgrGetUIContentFn>(getAddr),
        reinterpret_cast<TradeOpenWindowFn>(openAddr));
    if (rv < 0)
    {
        spdlog::warn("GGTB::OpenLocalTradeWindow: SEH");
        return false;
    }
    if (rv == 0)
        return false; // control 32 not built yet (or singleton null) — caller retries
    spdlog::info("GGTB::OpenLocalTradeWindow: opened CExchange window (ctrl 32)");
    return true;
}

// ---------- MailBox::SendMoneyMail (proto 411524, op=0) ----------
namespace
{
// IDA's auto-prototype is __stdcall(int, const char*, char Src, int, const char*),
// but the (char, int) pair is just a misread int64 split across two stack slots.
// Real signature on the wire is __stdcall(int op, const char *recipient,
// int64_t money, const char *body). x86 __stdcall pushes args right-to-left so
// passing int64 here puts low-dword first then high-dword on the stack — same
// shape the captured packet bytes confirmed.
using MailSendMoneyFn = char(__stdcall *)(int op, const char *recipient,
                                          int64_t money, const char *body);

static int CallSendMoneyMailSEH(MailSendMoneyFn fn, const char *recipient,
                                int64_t money, const char *body)
{
    __try
    {
        return fn(0, recipient, money, body);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}
} // namespace

bool SendMoneyMail(const char *recipient, int64_t money, const char *body)
{
    if (!recipient || !*recipient)
        return false;

    // recipient gets memmove'd into a 16-byte slot via strlen — anything longer
    // overflows into the op/money fields. Engine cap matches kNameMaxLen/2.
    size_t nameLen = strnlen(recipient, 32);
    if (nameLen == 0 || nameLen > 15)
    {
        spdlog::warn("GGTB::SendMoneyMail: invalid recipient length {}", nameLen);
        return false;
    }
    if (money <= 0)
    {
        spdlog::warn("GGTB::SendMoneyMail: invalid money {}", money);
        return false;
    }

    auto fnAddr = PatternResolver::Get("MailSendMoney");
    if (!fnAddr)
    {
        spdlog::error("GGTB::SendMoneyMail: pattern unresolved");
        return false;
    }

    auto fn = reinterpret_cast<MailSendMoneyFn>(fnAddr);
    int rv  = CallSendMoneyMailSEH(fn, recipient, money, body ? body : "");
    if (rv < 0)
    {
        spdlog::warn("GGTB::SendMoneyMail: SEH caught for recipient='{}' money={}",
                     recipient, money);
        return false;
    }
    spdlog::info("GGTB::SendMoneyMail: recipient='{}' money={} body_len={}",
                 recipient, money, body ? strlen(body) : 0);
    return rv != 0;
}

// ---------- CG_PUBLIC_CHAT (proto 411001) ----------
namespace
{
using NetSendChatStrFn = int(__thiscall *)(void *, int, const char *);
constexpr int kProtoCgPublicChat = 411001;

static int CallSendPublicChatSEH(NetBeginSendFn pBegin, NetSendChatStrFn pSend,
                                 const char *message)
{
    __try
    {
        void *buf = pBegin();
        if (!buf)
            return -1;
        return pSend(buf, kProtoCgPublicChat, message);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

static std::string Utf8ToBig5Lossy(const char *src)
{
    if (!src || !*src)
        return {};

    int wlen = MultiByteToWideChar(CP_UTF8, 0, src, -1, nullptr, 0);
    if (wlen <= 0)
        return src;

    std::wstring w(static_cast<size_t>(wlen), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, src, -1, w.data(), wlen) <= 0)
        return src;

    int blen = WideCharToMultiByte(950, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (blen <= 0)
        return src;

    std::string out(static_cast<size_t>(blen), '\0');
    if (WideCharToMultiByte(950, 0, w.c_str(), -1, out.data(), blen, nullptr, nullptr) <= 0)
        return src;

    if (!out.empty() && out.back() == '\0')
        out.pop_back();
    return out;
}
} // namespace

bool SendPublicChat(const char *message)
{
    if (!message || !*message)
        return false;

    auto encoded = Utf8ToBig5Lossy(message);
    if (encoded.empty())
        return false;

    auto beginAddr = PatternResolver::Get("NetBeginSend");
    auto sendAddr  = PatternResolver::Get("NetSendChatStr");
    if (!beginAddr || !sendAddr)
    {
        spdlog::error("GGTB::SendPublicChat: pattern unresolved");
        return false;
    }

    auto pBegin = reinterpret_cast<NetBeginSendFn>(beginAddr);
    auto pSend  = reinterpret_cast<NetSendChatStrFn>(sendAddr);
    int rv = CallSendPublicChatSEH(pBegin, pSend, encoded.c_str());
    if (rv < 0)
    {
        spdlog::warn("GGTB::SendPublicChat: SEH caught");
        return false;
    }
    spdlog::info("GGTB::SendPublicChat: msg='{}' bytes={} rv={}",
                 message, encoded.size(), rv);
    return true;
}

// ---------- City teleport ("/<cityName>" chat command, proto 411076) ----------
// Reverse-engineered chain (full call graph in CLocalPlayer.h):
//   destId = ResolveTeleportDestByName(CUIManager::GetUIContent(99), "/cityName")
//   then Net__SendDword(BeginSend(), 411076, destId) performs the warp. We
//   reproduce both halves without the chat-window object. Reuses Utf8ToBig5Lossy
//   (above) and CallNetSendDwordSEH / NetSendDwordFn (AutoRevive section).
namespace
{
using UIMgrSingletonFn  = void *(__cdecl *)();
using UIMgrGetContentFn = void *(__thiscall *)(void *, int);
using ResolveTeleportFn = int(__thiscall *)(void *, const char *); // this=UIContent(99)
using PlayerMoveSyncFn  = int(__thiscall *)(void *);               // this=netBuf

constexpr int kUiContentTeleportTable = 99; // CUIManager::GetUIContent slot
constexpr int kProtoTeleportReq       = 411076;

// >0 destId on success; 0 = no match / level / money / valid gate; -1 = SEH.
static int CallResolveTeleportSEH(UIMgrSingletonFn pMgr, UIMgrGetContentFn pGet,
                                  ResolveTeleportFn pResolve, const char *nameWithSlash)
{
    __try
    {
        void *mgr = pMgr();
        if (!mgr)
            return 0;
        void *content = pGet(mgr, kUiContentTeleportTable);
        if (!content)
            return 0;
        int destId = pResolve(content, nameWithSlash);
        return destId > 0 ? destId : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

// Best-effort position sync, mirroring Teleport_SendPacket. Own BeginSend unit;
// its internal gates may no-op it, and failure must never block the warp.
static void CallMoveSyncSEH(NetBeginSendFn pBegin, PlayerMoveSyncFn pSync)
{
    __try
    {
        void *buf = pBegin();
        if (buf)
            pSync(buf);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}
} // namespace

int ResolveTeleportDestId(const char *cityNameUtf8)
{
    if (!cityNameUtf8 || !*cityNameUtf8)
        return 0;

    // Drop any leading slashes the caller sent, convert UTF-8 -> Big5 (the table
    // is Big5; '/' is ASCII and survives), then prepend exactly one '/' — the
    // resolver compares name+1, skipping the slash.
    const char *p = cityNameUtf8;
    while (*p == '/')
        ++p;
    std::string big5 = Utf8ToBig5Lossy(p);
    if (big5.empty())
        return 0;
    std::string name = "/" + big5;

    auto mgrAddr     = PatternResolver::Get("UIManagerGetSingleton");
    auto getAddr     = PatternResolver::Get("UIManagerGetUIContent");
    auto resolveAddr = PatternResolver::Get("ResolveTeleportDestByName");
    if (!mgrAddr || !getAddr || !resolveAddr)
    {
        spdlog::error("GGTB::ResolveTeleportDestId: pattern unresolved "
                      "(mgr={:x} get={:x} resolve={:x})", mgrAddr, getAddr, resolveAddr);
        return 0;
    }

    int destId = CallResolveTeleportSEH(
        reinterpret_cast<UIMgrSingletonFn>(mgrAddr),
        reinterpret_cast<UIMgrGetContentFn>(getAddr),
        reinterpret_cast<ResolveTeleportFn>(resolveAddr),
        name.c_str());
    if (destId < 0)
    {
        spdlog::warn("GGTB::ResolveTeleportDestId: SEH for name='{}'", cityNameUtf8);
        return 0;
    }
    if (destId == 0)
    {
        spdlog::info("GGTB::ResolveTeleportDestId: no match for name='{}' "
                     "(unknown city / level / money / unavailable)", cityNameUtf8);
        return 0;
    }
    spdlog::info("GGTB::ResolveTeleportDestId: name='{}' -> destId={}", cityNameUtf8, destId);
    return destId;
}

bool SendTeleportToDest(int destId)
{
    if (destId <= 0)
        return false;

    auto beginAddr = PatternResolver::Get("NetBeginSend");
    auto sendAddr  = PatternResolver::Get("NetSendDword");
    if (!beginAddr || !sendAddr)
    {
        spdlog::error("GGTB::SendTeleportToDest: pattern unresolved (begin={:x} send={:x})",
                      beginAddr, sendAddr);
        return false;
    }
    auto pBegin = reinterpret_cast<NetBeginSendFn>(beginAddr);

    // Best-effort position sync first, like the engine; non-fatal if it no-ops.
    auto syncAddr = PatternResolver::Get("SendPlayerMoveSync");
    if (syncAddr)
        CallMoveSyncSEH(pBegin, reinterpret_cast<PlayerMoveSyncFn>(syncAddr));

    int rv = CallNetSendDwordSEH(pBegin, reinterpret_cast<NetSendDwordFn>(sendAddr),
                                 kProtoTeleportReq, destId);
    if (rv < 0)
    {
        spdlog::warn("GGTB::SendTeleportToDest: SEH on send destId={}", destId);
        return false;
    }
    spdlog::info("GGTB::SendTeleportToDest: sent teleport(411076) destId={} rv={}", destId, rv);
    return true;
}

bool TeleportByCityName(const char *cityNameUtf8)
{
    int destId = ResolveTeleportDestId(cityNameUtf8);
    if (destId <= 0)
    {
        spdlog::warn("GGTB::TeleportByCityName: could not resolve city='{}'",
                     cityNameUtf8 ? cityNameUtf8 : "(null)");
        return false;
    }
    return SendTeleportToDest(destId);
}

// ---------- CG_NPC_DIALOG_SELECT (proto 411026) ----------
namespace
{
// Net__SendDialogSelect @ 0xB2C930 — __thiscall(this=netBuf, proto, a3, a4, a5).
// 4-arg variant of NetSendTriple: emits one extra DWORD after the body.
using NetSendDialogSelectFn = int (__thiscall *)(void *, int, int, int, int);

constexpr int kProtocolDialogSelect  = 411026;
constexpr int kDialogSelectSubAction = 1;

static int CallSendDialogSelectSEH(NetBeginSendFn pBegin, NetSendDialogSelectFn pSend,
                                   int proto, int a3, int a4, int a5)
{
    __try
    {
        void *buf = pBegin();
        if (!buf)
            return -1;
        return pSend(buf, proto, a3, a4, a5);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}
} // anonymous

bool SendDialogSelect(uint32_t npcId, uint32_t dialogOption, uint32_t sub)
{
    auto beginAddr = PatternResolver::Get("NetBeginSend");
    auto sendAddr  = PatternResolver::Get("NetSendDialogSelect");
    if (!beginAddr || !sendAddr)
    {
        spdlog::error("GGTB::SendDialogSelect: pattern unresolved (begin={:x} send={:x})",
                      beginAddr, sendAddr);
        return false;
    }

    auto pBegin = reinterpret_cast<NetBeginSendFn>(beginAddr);
    auto pSend  = reinterpret_cast<NetSendDialogSelectFn>(sendAddr);

    // Engine arg order is (a3=dialogOption, a4=npcId, a5=sub) — verified 2026-05-09.
    // See CLocalPlayer.h doc + IDA Net__SendDialogSelect @ 0xB2C930.
    int rv = CallSendDialogSelectSEH(pBegin, pSend, kProtocolDialogSelect,
                                     static_cast<int>(dialogOption),
                                     static_cast<int>(npcId),
                                     static_cast<int>(sub));
    if (rv < 0)
    {
        spdlog::warn("GGTB::SendDialogSelect: SEH npc={} opt={}", npcId, dialogOption);
        return false;
    }
    spdlog::info("GGTB::SendDialogSelect: npc={} opt={} sub={}",
                 npcId, dialogOption, sub);
    return true;
}

// ---------- Live NPC-dialog snapshot (web UI driver) ----------
//
// Reads the engine's g_NpcDialogState linked-list dump and converts it to a
// JSON-friendly structure. Walks at most kDialogMaxOptions to bound the loop
// against torn state. All field reads go through SafeReadDword/SafeReadString
// so a state-pointer-being-recycled mid-call (rare race during scene change)
// just produces an empty snapshot, not a crash.
DialogSnapshot GetDialogSnapshot()
{
    DialogSnapshot snap{};
    snap.open = false;
    snap.mode = DialogMode::Closed;

    auto stateAddr = PatternResolver::Get("NpcDialogState");
    if (!stateAddr)
        return snap;

    uint32_t stateObj = 0;
    if (!SafeReadDword(stateAddr, stateObj) || !stateObj)
        return snap;

    uint32_t mode = 0;
    if (!SafeReadDword(stateObj + kDialogStateModeOffset, mode))
        return snap;
    if (mode == 0 || mode > 2)
        return snap; // closed (or weird value — be safe)

    snap.open = true;
    snap.mode = static_cast<DialogMode>(mode);
    SafeReadDword(stateObj + kDialogStateNpcInteractIdOffset, snap.npcInteractId);
    SafeReadDword(stateObj + kDialogStateMonsterTblIdOffset,  snap.monsterTblId);

    // Helper — walk an option linked list starting at `head`, append to options.
    // index continues from snap.options.size() so multi-list merges into a
    // single 0-based sequence the server expects.
    auto walkOptionList = [&](uint32_t head) {
        int    visited = 0;
        for (uint32_t node = head; node && visited < kDialogMaxOptions; ++visited)
        {
            DialogOption opt;
            opt.index = static_cast<uint32_t>(snap.options.size());

            char textBuf[kDialogOptionTextMaxLen + 1] = {};
            if (SafeReadString(node + kDialogOptionTextOffset, textBuf, sizeof(textBuf)) && textBuf[0])
                opt.text = Big5ToUtf8(textBuf);

            SafeReadDword(node + kDialogOptionTagOffset, opt.tag);
            // 解 opt: option_node+396 -> scriptEntry, +12 -> scriptCtx, +332 -> opt
            // 跟 OnNpcDialogOption_Quest 取值方式完全一致。
            uint32_t scriptEntry = opt.tag;
            uint32_t scriptCtx   = 0;
            if (scriptEntry &&
                SafeReadDword(scriptEntry + kDialogScriptEntryCtxOffset, scriptCtx) &&
                scriptCtx)
            {
                SafeReadDword(scriptCtx + kDialogScriptCtxOptOffset, opt.opt);
            }
            snap.options.push_back(std::move(opt));

            uint32_t next = 0;
            if (!SafeReadDword(node + kDialogOptionNextOffset, next))
                break;
            node = next;
        }
    };

    if (snap.mode == DialogMode::Confirm)
    {
        // mode==1: single confirm-entry at state+1044 — body text at entry+4.
        uint32_t entry = 0;
        if (SafeReadDword(stateObj + kDialogStateConfirmEntryOffset, entry) && entry)
        {
            char bodyBuf[kDialogBodyTextMaxLen + 1] = {};
            if (SafeReadString(entry + kDialogOptionTextOffset, bodyBuf, sizeof(bodyBuf)))
                snap.body = Big5ToUtf8(bodyBuf);

            // Synthesize a single "OK" option so the UI has something to click.
            // We still send index=0 — the engine treats single-confirm exactly
            // like a multi-choice with one entry on the server side.
            DialogOption ok;
            ok.index = 0;
            ok.text  = "[确定]"; // placeholder; engine doesn't carry "OK" text here
            SafeReadDword(entry + kDialogOptionTagOffset, ok.tag);
            // opt 解法跟 Choice 一致: entry+396 -> scriptEntry, +12 -> scriptCtx, +332 -> opt
            // (Npc__ConfirmDialogOptionLocal 在 confirm 模式下走的也是 entry+396 这条路。)
            if (ok.tag)
            {
                uint32_t okCtx = 0;
                if (SafeReadDword(ok.tag + kDialogScriptEntryCtxOffset, okCtx) && okCtx)
                    SafeReadDword(okCtx + kDialogScriptCtxOptOffset, ok.opt);
            }
            snap.options.push_back(std::move(ok));
        }
    }
    else // Choice (mode == 2)
    {
        // Engine builds option list in one of two places depending on tree
        // shape — walk both, in the same order BuildDialogChoiceList does:
        // first the "multi-choice" head (state+1052), then the wrapper head
        // (state+1056). Both are usually mutually exclusive but the engine
        // walks both so we mirror that.
        uint32_t choiceHead = 0;
        uint32_t wrapHead   = 0;
        SafeReadDword(stateObj + kDialogStateChoiceHeadOffset,  choiceHead);
        SafeReadDword(stateObj + kDialogStateWrapperHeadOffset, wrapHead);
        if (choiceHead) walkOptionList(choiceHead);
        if (wrapHead && wrapHead != choiceHead) walkOptionList(wrapHead);
    }

    return snap;
}

// ---------- Engine-driven dialog option click ----------
//
// 直接发 411026 包(SendDialogSelect)能让 server 收到选项,但客户端 UI 状态
// 不会跟着更新 —— state+1044/1052 仍指向旧选项,server 推下一菜单回来时
// 引擎在本地 build dialog tree 的逻辑会从「错的 selected entry」往下走,出现
// 菜单不刷新或者「点对话框 -> NPC 消失」之类的现象。
//
// 真正的「点一下」 = 让引擎自己跑两步:
//   1) Npc__ConfirmDialogOptionLocal(state, &g_DialogOptionPendingResult)
//      —— 内部以 state+1044(当前 highlighted 选项) 为输入,自己处理 sub-menu
//      descend + 把 quest tag 写到 g_DialogOptionPendingResult
//   2) OnNpcDialogOption_Quest(&byte_ED40F0, g_DialogOptionPendingResult, 0)
//      —— 才是真正发 411026 包并清理 g_NpcDialogState UI 状态
//
// 所以 SelectDialogOption(idx) 要:
//   a) 在 state+1052 / +1056 链表上走到第 idx 个节点
//   b) 把这个节点指针写进 state+1044(等价 UI 把光标移到这一条上)
//   c) 把 g_DialogOptionPendingResult 清零
//   d) 调 Confirm,再调 Send
namespace
{
using NpcConfirmDialogOptionFn = char(__thiscall *)(uintptr_t state, uint32_t *resultOut);
using NpcSendDialogOptionFn    = int(__thiscall *)(uintptr_t uiParent, uint32_t result, int mode);

static char CallConfirmSEH(NpcConfirmDialogOptionFn fn, uintptr_t state, uint32_t *out)
{
    __try { return fn(state, out); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static int CallSendOptSEH(NpcSendDialogOptionFn fn, uintptr_t uiParent, uint32_t result, int mode)
{
    __try { return fn(uiParent, result, mode); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// Walk option linked list (state+1052 -> next at +392) to find the node at
// `targetIndex`. Returns 0 if list shorter than that or torn.
static uint32_t FindOptionNodeAtIndex(uintptr_t state, uint32_t targetIndex)
{
    uint32_t head = 0;
    if (!SafeReadDword(state + kDialogStateChoiceHeadOffset, head) || !head)
    {
        // Fall back to wrapper head (state+1056) — used for single-entry "OK" menus.
        if (!SafeReadDword(state + kDialogStateWrapperHeadOffset, head) || !head)
            return 0;
    }

    uint32_t node = head;
    uint32_t i    = 0;
    int      hops = 0;
    while (node && hops < kDialogMaxOptions)
    {
        if (i == targetIndex)
            return node;
        uint32_t next = 0;
        if (!SafeReadDword(node + kDialogOptionNextOffset, next))
            return 0;
        node = next;
        ++i;
        ++hops;
    }
    // If we ran out walking +1052, try chaining onto +1056 — engine's
    // BuildDialogChoiceList walks both in sequence for some script shapes.
    if (head)
    {
        uint32_t wrap = 0;
        SafeReadDword(state + kDialogStateWrapperHeadOffset, wrap);
        if (wrap && wrap != head)
        {
            uint32_t n2 = wrap;
            int      h2 = 0;
            while (n2 && h2 < kDialogMaxOptions)
            {
                if (i == targetIndex)
                    return n2;
                uint32_t next = 0;
                if (!SafeReadDword(n2 + kDialogOptionNextOffset, next))
                    return 0;
                n2 = next;
                ++i;
                ++h2;
            }
        }
    }
    return 0;
}
} // anonymous

bool SelectDialogOption(uint32_t dialogOption)
{
    auto stateGlobal = PatternResolver::Get("NpcDialogState");
    auto confirmAddr = PatternResolver::Get("NpcConfirmDialogOption");
    auto sendAddr    = PatternResolver::Get("NpcSendDialogOption");
    auto resultAddr  = PatternResolver::Get("NpcDialogPendingResult");
    auto uiAddr      = PatternResolver::Get("NpcDialogParam2Ptr");
    if (!stateGlobal || !confirmAddr || !sendAddr || !resultAddr || !uiAddr)
    {
        spdlog::error("GGTB::SelectDialogOption: pattern unresolved (state={:x} confirm={:x} send={:x} result={:x} ui={:x})",
                      stateGlobal, confirmAddr, sendAddr, resultAddr, uiAddr);
        return false;
    }

    uint32_t state = 0;
    if (!SafeReadDword(stateGlobal, state) || !state)
    {
        spdlog::warn("GGTB::SelectDialogOption: g_NpcDialogState=0 (no dialog open)");
        return false;
    }

    uint32_t mode = 0;
    SafeReadDword(state + kDialogStateModeOffset, mode);
    if (mode == 0)
    {
        spdlog::warn("GGTB::SelectDialogOption: dialog closed (state.mode=0)");
        return false;
    }

    // Walk to the option the user clicked, then make that the engine's
    // "currently highlighted" selection (state+1044).
    uint32_t optNode = FindOptionNodeAtIndex(state, dialogOption);
    if (!optNode)
    {
        spdlog::warn("GGTB::SelectDialogOption: option index {} not found (mode={})",
                     dialogOption, mode);
        return false;
    }

    // Engine's UI keeps state+1044 in sync with the visible cursor. We mimic
    // that by poking it directly — Npc__ConfirmDialogOptionLocal reads from
    // state+1044 to decide which branch to descend into / which tag to send.
    *reinterpret_cast<volatile uint32_t *>(state + kDialogStateConfirmEntryOffset) = optNode;

    // Clear the pending-result slot so we can tell whether Confirm actually
    // produced a tag (Confirm only writes it when the option has +332 > 0).
    *reinterpret_cast<volatile uint32_t *>(resultAddr) = 0;

    auto fnConfirm = reinterpret_cast<NpcConfirmDialogOptionFn>(confirmAddr);
    auto fnSend    = reinterpret_cast<NpcSendDialogOptionFn>(sendAddr);

    char descended = CallConfirmSEH(fnConfirm, state,
                                    reinterpret_cast<uint32_t *>(resultAddr));

    uint32_t pending = 0;
    SafeReadDword(resultAddr, pending);

    // Mirror the engine's sub_8FBF20 sequence: always call OnNpcDialogOption_Quest
    // after Confirm; it internally short-circuits when pending==0 (no tag, e.g.
    // descended into a sub-menu locally without sending). Calling it
    // unconditionally also handles cleanup state (g_NpcDialogState+1024,
    // dword_ED3DC4 reset, npcwideshot sprite reset).
    int sendRv = CallSendOptSEH(fnSend, uiAddr, pending, 0);

    spdlog::info("GGTB::SelectDialogOption: state={:x} opt={} node={:x} mode={} descended={} pending={:x} sendRv={}",
                 state, dialogOption, optNode, mode,
                 static_cast<int>(descended), pending, sendRv);

    // Engine treats both "descended into next menu" and "sent packet" as success.
    // Only fail if Confirm SEH'd (descended==0 *and* mode was Choice/Confirm).
    return true;
}

bool ConfirmDialog()
{
    // mode==1 (single confirm) — engine's BuildDialogChoiceList already
    // pre-points state+1044 at the single entry, so option 0 just clicks it.
    return SelectDialogOption(0);
}

// ---------- Bag inventory snapshot ----------

std::vector<BagItemInfo> GetBagItems()
{
    std::vector<BagItemInfo> result;
    auto containerPtrAddr = PatternResolver::Get("ItemContainerPtr");
    if (!containerPtrAddr)
        return result;

    uintptr_t container = 0;
    if (!ReadContainerPtrSEH(containerPtrAddr, &container) || !container)
        return result;

    result.reserve(64);
    for (size_t i = 0; i < kItemFullSlotCount; ++i)
    {
        uintptr_t  slot = container + kItemArrayBaseOffset + i * kItemStride;
        BagSlotRaw raw{};
        if (!ReadBagSlotSEH(slot, &raw))
            continue;
        if (!raw.itemTable || raw.itemId == 0)
            continue;
        if (raw.count == 0)
            raw.count = 1;

        BagItemInfo info{raw.bagId, static_cast<uint32_t>(i), raw.itemId, raw.count, {}};
        char nameBuf[64] = {};
        if (ReadItemNameSEH(raw.itemTable, nameBuf, sizeof(nameBuf)))
            info.name = Big5ToUtf8(nameBuf);
        result.push_back(std::move(info));
    }
    return result;
}

// ---------- MailBox::SendItemMail (proto 411524, op=1) ----------
namespace
{
using MailSendItemFn = char(__stdcall *)(int op, const char *recipient,
                                         int itemBagId, int itemCount,
                                         const char *body);

static int CallSendItemMailSEH(MailSendItemFn fn, const char *recipient,
                               uint32_t itemBagId, uint32_t itemCount,
                               const char *body)
{
    __try
    {
        return fn(1, recipient, static_cast<int>(itemBagId),
                  static_cast<int>(itemCount), body);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}
} // namespace

bool SendItemMail(const char *recipient, uint32_t itemBagId,
                  uint32_t itemCount, const char *body)
{
    if (!recipient || !*recipient)
        return false;

    size_t nameLen = strnlen(recipient, 32);
    if (nameLen == 0 || nameLen > 15)
    {
        spdlog::warn("GGTB::SendItemMail: invalid recipient length {}", nameLen);
        return false;
    }
    if (itemCount == 0)
    {
        spdlog::warn("GGTB::SendItemMail: itemCount is 0");
        return false;
    }

    auto fnAddr = PatternResolver::Get("MailSendItem");
    if (!fnAddr)
    {
        spdlog::error("GGTB::SendItemMail: pattern unresolved");
        return false;
    }

    auto fn = reinterpret_cast<MailSendItemFn>(fnAddr);
    int rv  = CallSendItemMailSEH(fn, recipient, itemBagId, itemCount,
                                  body ? body : "");
    if (rv < 0)
    {
        spdlog::warn("GGTB::SendItemMail: SEH caught recipient='{}' bagId={} count={}",
                     recipient, itemBagId, itemCount);
        return false;
    }
    spdlog::info("GGTB::SendItemMail: recipient='{}' bagId={} count={}",
                 recipient, itemBagId, itemCount);
    return rv != 0;
}

// ---------- Cash bag (商城背包) inventory snapshot ----------

std::vector<BagItemInfo> GetCashBagItems()
{
    std::vector<BagItemInfo> result;
    auto containerPtrAddr = PatternResolver::Get("CashContainerPtr");
    if (!containerPtrAddr)
        return result;

    uintptr_t container = 0;
    if (!ReadContainerPtrSEH(containerPtrAddr, &container) || !container)
        return result;

    result.reserve(kCashSlotCount);
    for (size_t i = 0; i < kCashSlotCount; ++i)
    {
        uintptr_t  slot = container + kCashArrayBaseOffset + i * kItemStride;
        BagSlotRaw raw{};
        if (!ReadBagSlotSEH(slot, &raw))
            continue;
        if (!raw.itemTable || raw.itemId == 0)
            continue;
        if (raw.count == 0)
            raw.count = 1;

        BagItemInfo info{raw.bagId, static_cast<uint32_t>(i), raw.itemId, raw.count, {}};
        char nameBuf[64] = {};
        if (ReadItemNameSEH(raw.itemTable, nameBuf, sizeof(nameBuf)))
            info.name = Big5ToUtf8(nameBuf);
        result.push_back(std::move(info));
    }
    return result;
}

// ---------- Use cash item (proto 411156) ----------

bool UseCashItem(uint32_t slotIndex)
{
    if (slotIndex >= kCashSlotCount)
    {
        spdlog::warn("GGTB::UseCashItem: slotIndex {} out of range", slotIndex);
        return false;
    }

    auto beginAddr = PatternResolver::Get("NetBeginSend");
    auto sendAddr  = PatternResolver::Get("NetSendTriple");
    if (!beginAddr || !sendAddr)
    {
        spdlog::error("GGTB::UseCashItem: net pattern unresolved");
        return false;
    }

    auto pBegin = reinterpret_cast<NetBeginSendFn>(beginAddr);
    auto pSend  = reinterpret_cast<NetSendTripleFn>(sendAddr);

    int wireSlot = static_cast<int>(slotIndex) + kCashSlotWireBase;
    int rv = CallSendTripleSEH(pBegin, pSend, kProtocolUseCashItem, wireSlot, 0);
    if (rv < 0)
    {
        spdlog::warn("GGTB::UseCashItem: SEH caught slot={}", slotIndex);
        return false;
    }
    spdlog::info("GGTB::UseCashItem: slot={} wire={} rv={}", slotIndex, wireSlot, rv);
    return true;
}

// ---------- Magic Spring / 发条 (CMagicSpringOption) ----------
namespace
{
static bool ReadU32SEH_Spring(uintptr_t addr, uint32_t *out)
{
    __try { *out = *reinterpret_cast<uint32_t *>(addr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool ReadSpringBlockSEH(uintptr_t item, uint32_t out[7])
{
    __try
    {
        const uint32_t *src = reinterpret_cast<const uint32_t *>(item + kSpringBlockOffset);
        for (int i = 0; i < 7; ++i) out[i] = src[i];
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// 扫普通(bag=0)再 cash(bag=1)背包,找 subtype==targetSubtype 的发条。命中填 bagOut +
// wireOut(=item+0x00, 已是 wire 格式)。
static bool FindSpringBySubtype(uint32_t targetSubtype, int &bagOut, uint32_t &wireOut)
{
    auto scan = [&](const char *ptrName, uintptr_t base, size_t count, int bagId) -> bool {
        auto addr = PatternResolver::Get(ptrName);
        if (!addr) return false;
        uintptr_t container = 0;
        if (!ReadContainerPtrSEH(addr, &container) || !container) return false;
        for (size_t i = 0; i < count; ++i)
        {
            uintptr_t  slot = container + base + i * kItemStride;
            BagSlotRaw raw{};
            if (!ReadBagSlotSEH(slot, &raw) || !raw.itemTable || raw.itemId == 0)
                continue;
            // subtype 在 *itemTable*+0x110, 不是 item+0x110 (item+0x110 恒为 0)。
            // 见 IDA Bag_FindItemBySubtype_Normal: *(*(item+0x20)+272)==a2。
            uint32_t sub = 0;
            if (!ReadU32SEH_Spring(raw.itemTable + kItemSubtypeOffset, &sub) || sub != targetSubtype)
                continue;
            bagOut  = bagId;
            wireOut = raw.bagId;
            return true;
        }
        return false;
    };
    if (scan("ItemContainerPtr", kItemArrayBaseOffset, kItemFullSlotCount, 0)) return true;
    if (scan("CashContainerPtr", kCashArrayBaseOffset, kCashSlotCount, 1)) return true;
    return false;
}
} // anonymous

SpringState ReadSpringState(uint32_t equipSlotIndex)
{
    SpringState st{};
    st.valid = false;
    if (equipSlotIndex >= kItemFullSlotCount)
        return st;

    auto contAddr = PatternResolver::Get("ItemContainerPtr");
    if (!contAddr)
        return st;
    uintptr_t container = 0;
    if (!ReadContainerPtrSEH(contAddr, &container) || !container)
        return st;

    uintptr_t  item = container + kItemArrayBaseOffset + equipSlotIndex * kItemStride;
    BagSlotRaw raw{};
    if (!ReadBagSlotSEH(item, &raw) || !raw.itemTable || raw.itemId == 0)
        return st; // 空槽

    uint32_t blk[7];
    if (!ReadSpringBlockSEH(item, blk))
        return st;

    st.valid    = true;
    st.grade    = blk[0];
    st.attrs[0] = {blk[1], static_cast<int32_t>(blk[2])};
    st.attrs[1] = {blk[3], static_cast<int32_t>(blk[4])};
    st.attrs[2] = {blk[5], static_cast<int32_t>(blk[6])};
    return st;
}

bool WashSpring(uint32_t equipSlotIndex, int springType, std::string *errOut)
{
    auto setErr = [&](const char *m) { if (errOut) *errOut = m; };
    if (springType < 0 || springType > 2) { setErr("bad springType (0..2)"); return false; }
    if (equipSlotIndex >= kItemFullSlotCount) { setErr("equip slot out of range"); return false; }

    uint32_t targetSubtype = static_cast<uint32_t>(kSpringSubtypeBase + springType); // 65/66/67
    int      springBag  = 0;
    uint32_t springWire = 0;
    if (!FindSpringBySubtype(targetSubtype, springBag, springWire))
    {
        setErr("spring item not found in normal/cash bag");
        return false;
    }

    auto beginAddr = PatternResolver::Get("NetBeginSend");
    auto sendAddr  = PatternResolver::Get("NetSendDialogSelect"); // proto-generic 20B/3DW sender
    if (!beginAddr || !sendAddr) { setErr("net pattern unresolved"); return false; }
    auto pBegin = reinterpret_cast<NetBeginSendFn>(beginAddr);
    auto pSend  = reinterpret_cast<NetSendDialogSelectFn>(sendAddr);

    int equipWire = static_cast<int>(equipSlotIndex) + static_cast<int>(kSpringWireSlotBase);
    // body = [发条背包, 装备 wire 槽, 发条 wire 槽] —— 见 WashClockwork_Send_411590 @0x7F1F90
    int rv = CallSendDialogSelectSEH(pBegin, pSend, kProtocolWashSpring,
                                     springBag, equipWire, static_cast<int>(springWire));
    if (rv < 0) { setErr("SEH during send"); return false; }
    spdlog::info("GGTB::WashSpring: type={} equipSlot={} equipWire={} springBag={} springWire={} rv={}",
                 springType, equipSlotIndex, equipWire, springBag, springWire, rv);
    return true;
}

namespace
{
using StringTableCopyFn = char *(__cdecl *)(unsigned int, char *, size_t);
static bool CallStringTableCopySEH(StringTableCopyFn fn, unsigned int id, char *buf, size_t sz)
{
    __try { fn(id, buf, sz); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static std::string FetchGameString(int strId)
{
    auto addr = PatternResolver::Get("StringTableCopy");
    if (!addr) return {};
    auto fn = reinterpret_cast<StringTableCopyFn>(addr);
    char buf[160] = {};
    if (!CallStringTableCopySEH(fn, static_cast<unsigned int>(strId), buf, sizeof(buf)))
        return {};
    return Big5ToUtf8(buf);
}
} // anonymous

std::string GetSpringAttrName(uint32_t attrId, bool *isPercentOut)
{
    // attr id -> 字符串表 id,出处 sub_7F2520 的 switch(1..23) 反汇编实证(2026-06-14)。
    // 注:Hex-Rays 对本函数 8..20 的常量提取有误,以反汇编 push 值为准。pct=百分比(+N%)。
    //   1攻擊力 2魔法力 3命中 4迴避率 5防禦力 6必殺技 7攻擊速度 8移動速度
    //   9HP 10AP 11HP% 12AP% 13增加傷害力% 14減少傷害力%
    //   21減少道具配戴限制等級 22經驗值獲得量增加% 23副本傷害增加%
    static const struct { uint32_t id; int strId; bool pct; } kSingle[] = {
        {1, 79, false},   {2, 80, false},   {3, 78, false},   {4, 84, false},
        {5, 81, false},   {6, 83, false},   {7, 82, false},   {8, 1349, false},
        {9, 1350, false}, {10, 1351, false}, {11, 1350, true}, {12, 1351, true},
        {13, 887, true},  {14, 888, true},
        {21, 3206, false}, {22, 3207, true}, {23, 3208, true},
    };
    // 复合(15..20):"每N等級增加<属性> +M",值 lo16/hi16 打包。strId b = 属性名:
    //   2830力量 2831敏捷 2832智力 2833幸運 2834體力 2835精神。展示用属性名 + "(每級)"。
    static const struct { uint32_t id; int b; } kDual[] = {
        {15, 2830}, {16, 2831}, {17, 2832}, {18, 2833}, {19, 2834}, {20, 2835},
    };

    for (auto &m : kSingle)
        if (m.id == attrId)
        {
            if (isPercentOut) *isPercentOut = m.pct;
            return FetchGameString(m.strId);
        }
    for (auto &m : kDual)
        if (m.id == attrId)
        {
            if (isPercentOut) *isPercentOut = false;
            std::string nm = FetchGameString(m.b);
            while (!nm.empty() && (nm.back() == ' ' || nm.back() == '\t')) nm.pop_back();
            if (nm.empty()) return {};
            return nm + u8"(每級)";
        }
    if (isPercentOut) *isPercentOut = false;
    return {};
}

// ---------- Summoned vendor shop packets ----------
namespace
{
constexpr uintptr_t kVendorItemNameOffset  = 0x0C;
constexpr uintptr_t kVendorItemPriceOffset = 0x3F4;

using ShopTableManagerFn    = void *(__cdecl *)(int);
using ShopTableGetElemFn    = void *(__thiscall *)(void *, int, int, int);
using ShopListGetAtFn       = uint32_t *(__thiscall *)(void *, int);
using ShopItemTableByIdFn   = uintptr_t(__thiscall *)(void *, int, int);
using ShopFindBagSlotFn     = int(__thiscall *)(void *, int, int, int, int);
using UIManagerGetSingletonFn = void *(__cdecl *)();
using UIManagerGetUIContentFn = void *(__thiscall *)(void *, int);
using UIManagerIsContentOpenFn = int(__thiscall *)(void *, int);
using UIManagerCloseContentFn = uint8_t(__thiscall *)(void *, int);
using UIManagerCloseActiveContentFn = void(__thiscall *)(void *, void *);
using UIContentMarkCloseFn = void(__thiscall *)(void *);
using VendorUiCloseFn = uint8_t(__thiscall *)(void *);
// CUIManager::BeginContent(this=mgr, contentEnum, rendererArg)。__thiscall, retn 8。
// 返回找到/打开的 UIContent 对象指针(0=失败)。打开仓库 = BeginContent(mgr, 2, renderer)。
// 参数顺序由 case 0x63D 反汇编确认(先 push renderer 后 push 2 → arg0=enum, arg1=renderer)。
using UIManagerBeginContentFn = void *(__thiscall *)(void *, int, int);

static void *CallShopTableManagerSEH(ShopTableManagerFn fn, int type)
{
    __try { return fn(type); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static void *CallShopTableGetElemSEH(ShopTableGetElemFn fn, void *mgr,
                                     int tableType, int vendorId, int sub)
{
    __try { return fn(mgr, tableType, vendorId, sub); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static uint32_t *CallShopListGetAtSEH(ShopListGetAtFn fn, void *list, int idx)
{
    __try { return fn(list, idx); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static uintptr_t CallShopItemTableByIdSEH(ShopItemTableByIdFn fn, void *container,
                                          int itemId, int sub)
{
    __try { return fn(container, itemId, sub); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static int CallShopFindBagSlotSEH(ShopFindBagSlotFn fn, void *container,
                                  int itemId, int a2, int a3, int count)
{
    __try { return fn(container, itemId, a2, a3, count); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static void *CallUIManagerGetSingletonSEH(UIManagerGetSingletonFn fn)
{
    __try { return fn(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static void *CallUIManagerGetUIContentSEH(UIManagerGetUIContentFn fn, void *mgr, int id)
{
    __try { return fn(mgr, id); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static int CallUIManagerIsContentOpenSEH(UIManagerIsContentOpenFn fn, void *mgr, int id)
{
    __try { return fn(mgr, id) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static int CallUIManagerCloseContentSEH(UIManagerCloseContentFn fn, void *mgr, int id)
{
    __try { return fn(mgr, id) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static bool CallUIManagerCloseActiveContentSEH(UIManagerCloseActiveContentFn fn,
                                               void *mgr, void *renderer)
{
    __try
    {
        fn(mgr, renderer);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool CallUIContentMarkCloseSEH(UIContentMarkCloseFn fn, void *content)
{
    __try
    {
        fn(content);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static void *CallUIManagerBeginContentSEH(UIManagerBeginContentFn fn, void *mgr,
                                          int contentEnum, int rendererArg)
{
    // 实参顺序铁定:case 0x63D 先 push renderer 后 push 2,故进函数 arg0=contentEnum、
    // arg1=renderer。__thiscall fn(mgr, contentEnum, rendererArg) 正好复刻这条压栈。
    __try { return fn(mgr, contentEnum, rendererArg); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static int CallVendorUiCloseSEH(VendorUiCloseFn fn, void *content)
{
    __try
    {
        return fn(content) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

static bool ReadShopItemIdSEH(uint32_t *p, uint32_t &out)
{
    __try
    {
        out = *p;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool GetItemContainer(uintptr_t &container)
{
    container = 0;
    auto addr = PatternResolver::Get("ItemContainerPtr");
    return addr && ReadContainerPtrSEH(addr, &container) && container;
}

static int FindVendorTargetSlot(uint32_t itemId, uint32_t count)
{
    uintptr_t container = 0;
    if (!GetItemContainer(container))
        return -1;
    auto findAddr = PatternResolver::Get("ShopFindInventorySlot");
    if (!findAddr)
        return -1;
    auto fn = reinterpret_cast<ShopFindBagSlotFn>(findAddr);
    return CallShopFindBagSlotSEH(fn, reinterpret_cast<void *>(container),
                                  static_cast<int>(itemId), 0, 0,
                                  static_cast<int>(count));
}

static int FindFallbackEmptyBagWireSlot(const std::unordered_set<uint32_t> *avoidTargetSlots)
{
    uintptr_t container = 0;
    if (!GetItemContainer(container))
        return -1;

    for (size_t i = 0; i < kItemFullSlotCount; ++i)
    {
        uintptr_t slot = container + kItemArrayBaseOffset + i * kItemStride;
        BagSlotRaw raw{};
        if (!ReadBagSlotSEH(slot, &raw))
            continue;
        if (raw.itemTable || raw.itemId != 0)
            continue;

        uint32_t wireSlot = static_cast<uint32_t>(i) + kCashSlotWireBase;
        if (avoidTargetSlots && avoidTargetSlots->find(wireSlot) != avoidTargetSlots->end())
            continue;
        return static_cast<int>(wireSlot);
    }
    return -1;
}

static int FindVendorTargetSlotAvoiding(uint32_t itemId, uint32_t count,
                                        const std::unordered_set<uint32_t> *avoidTargetSlots)
{
    int targetSlot = FindVendorTargetSlot(itemId, count);
    if (targetSlot <= 0)
        return targetSlot;
    if (!avoidTargetSlots || avoidTargetSlots->find(static_cast<uint32_t>(targetSlot)) == avoidTargetSlots->end())
        return targetSlot;

    int fallback = FindFallbackEmptyBagWireSlot(avoidTargetSlots);
    if (fallback > 0)
    {
        spdlog::info("GGTB::FindVendorTargetSlotAvoiding: targetSlot {} already used, fallback empty slot {}",
                     targetSlot, fallback);
        return fallback;
    }

    spdlog::warn("GGTB::FindVendorTargetSlotAvoiding: targetSlot {} reused and no fallback empty slot found",
                 targetSlot);
    return targetSlot;
}

struct VendorUiCandidate
{
    int      id = -1;
    void    *ui = nullptr;
    uint32_t vtable = 0;
    uint32_t closeSlot = 0;
    int      isOpen = -1;
};

static bool ReadUiVtableCloseSlot(void *ui, uint32_t &vtable, uint32_t &closeSlot)
{
    vtable = 0;
    closeSlot = 0;
    if (!ui)
        return false;
    if (!SafeReadDword(reinterpret_cast<uintptr_t>(ui), vtable) || !vtable)
        return false;
    SafeReadDword(static_cast<uintptr_t>(vtable) + 0x58, closeSlot);
    return true;
}

static bool IsVendorUiVtable(uint32_t vtable, uint32_t closeSlot,
                             uintptr_t vendorVtableAddr, uintptr_t vendorCloseAddr)
{
    return vtable == static_cast<uint32_t>(vendorVtableAddr) ||
           closeSlot == static_cast<uint32_t>(vendorCloseAddr);
}

static bool FindVendorUiContent(UIManagerGetUIContentFn pGet,
                                UIManagerIsContentOpenFn pIsOpen,
                                void *mgr,
                                uintptr_t vendorVtableAddr,
                                uintptr_t vendorCloseAddr,
                                VendorUiCandidate &out)
{
    constexpr int kMaxUiContentId = 256;

    for (int id = 0; id < kMaxUiContentId; ++id)
    {
        int open = CallUIManagerIsContentOpenSEH(pIsOpen, mgr, id);
        if (open <= 0)
            continue;

        void *ui = CallUIManagerGetUIContentSEH(pGet, mgr, id);
        if (!ui)
            continue;

        uint32_t vtable = 0;
        uint32_t closeSlot = 0;
        if (!ReadUiVtableCloseSlot(ui, vtable, closeSlot))
            continue;

        bool match = IsVendorUiVtable(vtable, closeSlot, vendorVtableAddr, vendorCloseAddr);

        if (id == 31 && !match)
        {
            spdlog::info("GGTB::FindVendorUiContent: id=31 is not merchant open={} ui={:x} vtable={:x} closeSlot={:x}",
                         open, reinterpret_cast<uintptr_t>(ui), vtable, closeSlot);
        }

        if (!match)
            continue;

        VendorUiCandidate cand{};
        cand.id = id;
        cand.ui = ui;
        cand.vtable = vtable;
        cand.closeSlot = closeSlot;
        cand.isOpen = open;

        spdlog::info("GGTB::FindVendorUiContent: merchant candidate id={} open={} ui={:x} vtable={:x} closeSlot={:x}",
                     cand.id, cand.isOpen, reinterpret_cast<uintptr_t>(cand.ui),
                     cand.vtable, cand.closeSlot);

        out = cand;
        return true;
    }

    return false;
}
} // namespace

std::vector<VendorShopItem> GetVendorShopItems(uint32_t vendorId)
{
    std::vector<VendorShopItem> result;
    auto mgrAddr   = PatternResolver::Get("ShopTableManager");
    auto elemAddr  = PatternResolver::Get("ShopTableGetElem");
    auto listAddr  = PatternResolver::Get("ShopListGetAt");
    auto itemAddr  = PatternResolver::Get("ShopItemTableById");
    if (!mgrAddr || !elemAddr || !listAddr || !itemAddr)
    {
        spdlog::error("GGTB::GetVendorShopItems: pattern unresolved (mgr={:x} elem={:x} list={:x} item={:x})",
                      mgrAddr, elemAddr, listAddr, itemAddr);
        return result;
    }

    uintptr_t container = 0;
    if (!GetItemContainer(container))
    {
        spdlog::warn("GGTB::GetVendorShopItems: g_pItemContainer not resolved");
        return result;
    }

    auto pMgr  = reinterpret_cast<ShopTableManagerFn>(mgrAddr);
    auto pElem = reinterpret_cast<ShopTableGetElemFn>(elemAddr);
    auto pList = reinterpret_cast<ShopListGetAtFn>(listAddr);
    auto pItem = reinterpret_cast<ShopItemTableByIdFn>(itemAddr);

    void *mgr = CallShopTableManagerSEH(pMgr, 17);
    if (!mgr)
    {
        spdlog::warn("GGTB::GetVendorShopItems: shop table manager null");
        return result;
    }
    void *vendorRow = CallShopTableGetElemSEH(pElem, mgr, 17, static_cast<int>(vendorId), 0);
    if (!vendorRow)
    {
        spdlog::warn("GGTB::GetVendorShopItems: vendorId={} row null", vendorId);
        return result;
    }

    void *list = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(vendorRow) + 12);
    result.reserve(kVendorMaxShopItems);
    for (uint32_t i = 0; i < kVendorMaxShopItems; ++i)
    {
        uint32_t *entry = CallShopListGetAtSEH(pList, list, static_cast<int>(i));
        if (!entry)
            continue;
        uint32_t itemId = 0;
        if (!ReadShopItemIdSEH(entry, itemId) || itemId == 0)
            continue;
        uintptr_t itemTable = CallShopItemTableByIdSEH(pItem,
            reinterpret_cast<void *>(container), static_cast<int>(itemId), 0);
        if (!itemTable)
            continue;

        VendorShopItem info{};
        info.shopIndex = i;
        info.itemId    = itemId;
        SafeReadDword(itemTable + kVendorItemPriceOffset, info.unitPrice);
        char nameBuf[128] = {};
        if (ReadItemNameSEH(itemTable, nameBuf, sizeof(nameBuf)))
            info.name = Big5ToUtf8(nameBuf);
        result.push_back(std::move(info));
    }
    return result;
}

bool SendVendorOpen(uint32_t token)
{
    auto beginAddr = PatternResolver::Get("NetBeginSend");
    auto sendAddr  = PatternResolver::Get("NetSendDword");
    if (!beginAddr || !sendAddr)
    {
        spdlog::error("GGTB::SendVendorOpen: pattern unresolved (begin={:x} send={:x})",
                      beginAddr, sendAddr);
        return false;
    }

    auto pBegin = reinterpret_cast<NetBeginSendFn>(beginAddr);
    auto pSend  = reinterpret_cast<NetSendDwordFn>(sendAddr);
    int rv = CallNetSendDwordSEH(pBegin, pSend, kProtocolVendorOpen,
                                 static_cast<int>(token));
    if (rv < 0)
    {
        spdlog::warn("GGTB::SendVendorOpen: SEH token={:08X}", token);
        return false;
    }
    spdlog::info("GGTB::SendVendorOpen: token={:08X} rv={}", token, rv);
    return true;
}

bool SendVendorBuy(uint32_t vendorId, uint32_t shopIndex, uint32_t count,
                   uint32_t targetSlot, uint32_t token)
{
    if (count == 0)
    {
        spdlog::warn("GGTB::SendVendorBuy: invalid count=0");
        return false;
    }
    if (targetSlot == 0)
    {
        spdlog::warn("GGTB::SendVendorBuy: invalid targetSlot=0");
        return false;
    }

    auto beginAddr = PatternResolver::Get("NetBeginSend");
    auto sendAddr  = PatternResolver::Get("NetSkillSendPackage");
    if (!beginAddr || !sendAddr)
    {
        spdlog::error("GGTB::SendVendorBuy: pattern unresolved (begin={:x} send={:x})",
                      beginAddr, sendAddr);
        return false;
    }

    auto pBegin = reinterpret_cast<NetBeginSendFn>(beginAddr);
    auto pSend  = reinterpret_cast<NetSkillSendPackageFn>(sendAddr);
    uint32_t payload[5] = { vendorId, shopIndex, count, targetSlot, token };
    int rv = CallSkillSendPackageSEH(pBegin, pSend, payload, sizeof(payload),
                                     kProtocolVendorBuy);
    if (rv < 0)
    {
        spdlog::warn("GGTB::SendVendorBuy: SEH vendorId={} shopIndex={} count={} targetSlot={} token={:08X}",
                     vendorId, shopIndex, count, targetSlot, token);
        return false;
    }
    spdlog::info("GGTB::SendVendorBuy: vendorId={} shopIndex={} count={} targetSlot={} token={:08X} rv={}",
                 vendorId, shopIndex, count, targetSlot, token, rv);
    return true;
}

bool SendVendorBuyShopItem(uint32_t itemId, uint32_t shopIndex, uint32_t count,
                           uint32_t vendorId, uint32_t token,
                           const std::unordered_set<uint32_t> *avoidTargetSlots,
                           VendorBuyResult *out)
{
    if (itemId == 0 || count == 0)
    {
        spdlog::warn("GGTB::SendVendorBuyShopItem: invalid itemId={} count={}", itemId, count);
        return false;
    }

    int targetSlot = FindVendorTargetSlotAvoiding(itemId, count, avoidTargetSlots);
    if (targetSlot <= 0)
    {
        spdlog::warn("GGTB::SendVendorBuyShopItem: no target bag slot itemId={} shopIndex={} count={} rv={}",
                     itemId, shopIndex, count, targetSlot);
        return false;
    }

    bool ok = SendVendorBuy(vendorId, shopIndex, count,
                            static_cast<uint32_t>(targetSlot), token);
    if (ok && out)
    {
        out->vendorId   = vendorId;
        out->shopIndex  = shopIndex;
        out->itemId     = itemId;
        out->count      = count;
        out->targetSlot = static_cast<uint32_t>(targetSlot);
        out->token      = token;
    }
    return ok;
}

bool SendVendorBuyItem(uint32_t itemId, uint32_t count,
                       uint32_t vendorId, uint32_t token,
                       const std::unordered_set<uint32_t> *avoidTargetSlots,
                       VendorBuyResult *out)
{
    if (itemId == 0 || count == 0)
    {
        spdlog::warn("GGTB::SendVendorBuyItem: invalid itemId={} count={}", itemId, count);
        return false;
    }

    auto items = GetVendorShopItems(vendorId);
    auto it = std::find_if(items.begin(), items.end(),
                           [itemId](const VendorShopItem &row) {
                               return row.itemId == itemId;
                           });
    if (it == items.end())
    {
        spdlog::warn("GGTB::SendVendorBuyItem: itemId={} not found vendorId={} rows={}",
                     itemId, vendorId, items.size());
        return false;
    }

    return SendVendorBuyShopItem(itemId, it->shopIndex, count, vendorId, token,
                                 avoidTargetSlots, out);
}

bool CloseVendorWindowLocal(bool *packetSentByUiHandler)
{
    if (packetSentByUiHandler)
        *packetSentByUiHandler = false;

    auto mgrAddr          = PatternResolver::Get("UIManagerGetSingleton");
    auto getAddr          = PatternResolver::Get("UIManagerGetUIContent");
    auto isOpenAddr       = PatternResolver::Get("UIManagerIsContentOpen");
    auto markCloseAddr    = PatternResolver::Get("UIManagerCloseContent");
    auto closeActiveAddr  = PatternResolver::Get("UIManagerCloseActiveContent");
    auto contentMarkAddr  = PatternResolver::Get("UIContentMarkClose");
    auto rendererPtrAddr  = PatternResolver::Get("RendererPtr");
    auto vendorVtableAddr = PatternResolver::Get("VendorUiVTable");
    auto vendorCloseAddr  = PatternResolver::Get("VendorUiClose");
    if (!mgrAddr || !getAddr || !isOpenAddr || !markCloseAddr || !closeActiveAddr ||
        !contentMarkAddr || !rendererPtrAddr || !vendorVtableAddr || !vendorCloseAddr)
    {
        spdlog::error("GGTB::CloseVendorWindowLocal: pattern unresolved "
                      "(mgr={:x} get={:x} isOpen={:x} markClose={:x} closeActive={:x} contentMark={:x} rendererPtr={:x} vendorVtable={:x} vendorClose={:x})",
                      mgrAddr, getAddr, isOpenAddr, markCloseAddr, closeActiveAddr,
                      contentMarkAddr, rendererPtrAddr, vendorVtableAddr, vendorCloseAddr);
        return false;
    }

    auto pMgr = reinterpret_cast<UIManagerGetSingletonFn>(mgrAddr);
    auto pGet = reinterpret_cast<UIManagerGetUIContentFn>(getAddr);
    auto pIsOpen = reinterpret_cast<UIManagerIsContentOpenFn>(isOpenAddr);
    auto pMarkClose = reinterpret_cast<UIManagerCloseContentFn>(markCloseAddr);
    auto pCloseActive = reinterpret_cast<UIManagerCloseActiveContentFn>(closeActiveAddr);
    auto pContentMark = reinterpret_cast<UIContentMarkCloseFn>(contentMarkAddr);

    void *mgr = CallUIManagerGetSingletonSEH(pMgr);
    if (!mgr)
    {
        spdlog::warn("GGTB::CloseVendorWindowLocal: UI manager null");
        return false;
    }

    VendorUiCandidate vendor{};
    if (!FindVendorUiContent(pGet, pIsOpen, mgr, vendorVtableAddr, vendorCloseAddr, vendor))
    {
        spdlog::warn("GGTB::CloseVendorWindowLocal: merchant UI not found vendorVtable={:x} vendorClose={:x}",
                     vendorVtableAddr, vendorCloseAddr);
        return false;
    }

    uint32_t renderer = 0;
    if (!SafeReadDword(rendererPtrAddr, renderer) || !renderer)
    {
        spdlog::warn("GGTB::CloseVendorWindowLocal: renderer null/invalid ptrAddr={:x} renderer={:x}",
                     rendererPtrAddr, renderer);
        return false;
    }

    bool directMark = CallUIContentMarkCloseSEH(pContentMark, vendor.ui);
    int markRv = CallUIManagerCloseContentSEH(pMarkClose, mgr, vendor.id);
    if (markRv < 0)
    {
        spdlog::warn("GGTB::CloseVendorWindowLocal: UIManager mark-close SEH id={} directMark={}",
                     vendor.id, directMark);
    }
    if (!directMark && markRv <= 0)
    {
        spdlog::warn("GGTB::CloseVendorWindowLocal: mark-close failed id={} ui={:x} directMark={} managerRv={}",
                     vendor.id, reinterpret_cast<uintptr_t>(vendor.ui), directMark, markRv);
        return false;
    }

    spdlog::info("GGTB::CloseVendorWindowLocal: marked merchant close id={} directMark={} managerRv={} mgr={:x} renderer={:x} vendorUi={:x}",
                 vendor.id, directMark, markRv,
                 reinterpret_cast<uintptr_t>(mgr), renderer,
                 reinterpret_cast<uintptr_t>(vendor.ui));

    bool processed = CallUIManagerCloseActiveContentSEH(
        pCloseActive, mgr, reinterpret_cast<void *>(static_cast<uintptr_t>(renderer)));
    if (!processed)
    {
        spdlog::warn("GGTB::CloseVendorWindowLocal: CloseActiveContent SEH id={}", vendor.id);
        return false;
    }

    int afterOpen = CallUIManagerIsContentOpenSEH(pIsOpen, mgr, vendor.id);
    void *afterUi = CallUIManagerGetUIContentSEH(pGet, mgr, vendor.id);
    uint32_t afterVtable = 0;
    uint32_t afterCloseSlot = 0;
    bool afterStillVendor =
        afterUi &&
        ReadUiVtableCloseSlot(afterUi, afterVtable, afterCloseSlot) &&
        IsVendorUiVtable(afterVtable, afterCloseSlot, vendorVtableAddr, vendorCloseAddr);

    bool closed = (afterOpen == 0) || !afterStillVendor;
    int vendorCloseRv = -2;
    bool fallbackProcessed = false;
    if (!closed)
    {
        auto pVendorClose = reinterpret_cast<VendorUiCloseFn>(vendorCloseAddr);
        vendorCloseRv = CallVendorUiCloseSEH(pVendorClose, vendor.ui);
        if (vendorCloseRv >= 0)
        {
            if (packetSentByUiHandler)
                *packetSentByUiHandler = true;
            directMark = CallUIContentMarkCloseSEH(pContentMark, vendor.ui) || directMark;
            fallbackProcessed = CallUIManagerCloseActiveContentSEH(
                pCloseActive, mgr, reinterpret_cast<void *>(static_cast<uintptr_t>(renderer)));

            afterOpen = CallUIManagerIsContentOpenSEH(pIsOpen, mgr, vendor.id);
            afterUi = CallUIManagerGetUIContentSEH(pGet, mgr, vendor.id);
            afterVtable = 0;
            afterCloseSlot = 0;
            afterStillVendor =
                afterUi &&
                ReadUiVtableCloseSlot(afterUi, afterVtable, afterCloseSlot) &&
                IsVendorUiVtable(afterVtable, afterCloseSlot, vendorVtableAddr, vendorCloseAddr);
            closed = (afterOpen == 0) || !afterStillVendor;
        }
    }

    if (packetSentByUiHandler)
        *packetSentByUiHandler = *packetSentByUiHandler || closed;

    spdlog::info("GGTB::CloseVendorWindowLocal: close-active done id={} directMark={} managerRv={} vendorCloseRv={} fallbackProcessed={} afterOpen={} afterUi={:x} afterVtable={:x} afterCloseSlot={:x} closed={}",
                 vendor.id, directMark, markRv, vendorCloseRv, fallbackProcessed,
                 afterOpen, reinterpret_cast<uintptr_t>(afterUi), afterVtable,
                 afterCloseSlot, closed);
    if (!closed)
    {
        spdlog::warn("GGTB::CloseVendorWindowLocal: merchant UI still open after close-active route id={} ui={:x}",
                     vendor.id, reinterpret_cast<uintptr_t>(vendor.ui));
    }
    return closed;
}

bool SendVendorClose(uint32_t token)
{
    auto beginAddr = PatternResolver::Get("NetBeginSend");
    auto sendAddr  = PatternResolver::Get("NetSendDword");
    if (!beginAddr || !sendAddr)
    {
        spdlog::error("GGTB::SendVendorClose: pattern unresolved (begin={:x} send={:x})",
                      beginAddr, sendAddr);
        return false;
    }

    auto pBegin = reinterpret_cast<NetBeginSendFn>(beginAddr);
    auto pSend  = reinterpret_cast<NetSendDwordFn>(sendAddr);
    int rv = CallNetSendDwordSEH(pBegin, pSend, kProtocolVendorClose,
                                 static_cast<int>(token));
    if (rv < 0)
    {
        spdlog::warn("GGTB::SendVendorClose: SEH token={:08X}", token);
        return false;
    }
    spdlog::info("GGTB::SendVendorClose: token={:08X} rv={}", token, rv);
    return true;
}

// ---------- Account-shared bank (账号共享仓库) ----------

// BeginContent 返回的仓库 UIContent 对象指针,供 SendBankClose 标记关闭。openBank /
// closeBank 是 broker 串行下发的命令(同一命令线程),用 atomic 仅为跨命令可见性保险。
namespace { std::atomic<void *> g_bankContent{nullptr}; }

bool SendBankOpen()
{
    auto mgrAddr      = PatternResolver::Get("UIManagerGetSingleton");
    auto beginAddr    = PatternResolver::Get("UIManagerBeginContent");
    auto rendererAddr = PatternResolver::Get("RendererPtr");
    if (!mgrAddr || !beginAddr || !rendererAddr)
    {
        spdlog::error("GGTB::SendBankOpen: pattern unresolved (mgr={:x} begin={:x} renderer={:x})",
                      mgrAddr, beginAddr, rendererAddr);
        return false;
    }

    auto pMgr   = reinterpret_cast<UIManagerGetSingletonFn>(mgrAddr);
    auto pBegin = reinterpret_cast<UIManagerBeginContentFn>(beginAddr);

    void *mgr = CallUIManagerGetSingletonSEH(pMgr);
    if (!mgr)
    {
        spdlog::warn("GGTB::SendBankOpen: UI manager null");
        return false;
    }

    uint32_t renderer = 0;
    if (!SafeReadDword(rendererAddr, renderer) || !renderer)
    {
        spdlog::warn("GGTB::SendBankOpen: renderer null ptrAddr={:x} renderer={:x}",
                     rendererAddr, renderer);
        return false;
    }

    // 复刻 UICmdDispatcher__OnButton case 0x63D: BeginContent(mgr, enum=2, renderer)。
    // 内部建本地仓库 UI content 并发 411154/411644。返回 content 对象指针(0=失败)。
    void *content = CallUIManagerBeginContentSEH(
        pBegin, mgr, kBankContentEnum, static_cast<int>(renderer));
    if (!content)
    {
        spdlog::warn("GGTB::SendBankOpen: BeginContent returned null (mgr={:x} renderer={:x})",
                     reinterpret_cast<uintptr_t>(mgr), renderer);
        return false;
    }
    g_bankContent.store(content, std::memory_order_release);
    spdlog::info("GGTB::SendBankOpen: opened bank UI content={:x}",
                 reinterpret_cast<uintptr_t>(content));
    return true;
}

bool SendBankClose()
{
    auto mgrAddr         = PatternResolver::Get("UIManagerGetSingleton");
    auto rendererAddr    = PatternResolver::Get("RendererPtr");
    auto markCloseAddr   = PatternResolver::Get("UIContentMarkClose");
    auto closeActiveAddr = PatternResolver::Get("UIManagerCloseActiveContent");
    if (!mgrAddr || !rendererAddr || !markCloseAddr || !closeActiveAddr)
    {
        spdlog::error("GGTB::SendBankClose: pattern unresolved (mgr={:x} renderer={:x} mark={:x} closeActive={:x})",
                      mgrAddr, rendererAddr, markCloseAddr, closeActiveAddr);
        return false;
    }

    auto pMgr         = reinterpret_cast<UIManagerGetSingletonFn>(mgrAddr);
    auto pMarkClose   = reinterpret_cast<UIContentMarkCloseFn>(markCloseAddr);
    auto pCloseActive = reinterpret_cast<UIManagerCloseActiveContentFn>(closeActiveAddr);

    void *mgr = CallUIManagerGetSingletonSEH(pMgr);
    if (!mgr)
    {
        spdlog::warn("GGTB::SendBankClose: UI manager null");
        return false;
    }
    uint32_t renderer = 0;
    SafeReadDword(rendererAddr, renderer);

    // 标记仓库 content 关闭,再让 UIManager 处理关闭(与 CloseVendorWindowLocal 同范式)。
    void *content = g_bankContent.load(std::memory_order_acquire);
    bool marked = content ? CallUIContentMarkCloseSEH(pMarkClose, content) : false;
    bool processed = renderer
        ? CallUIManagerCloseActiveContentSEH(
              pCloseActive, mgr, reinterpret_cast<void *>(static_cast<uintptr_t>(renderer)))
        : false;
    g_bankContent.store(nullptr, std::memory_order_release);
    spdlog::info("GGTB::SendBankClose: content={:x} marked={} processed={}",
                 reinterpret_cast<uintptr_t>(content), marked, processed);
    return processed;
}

std::vector<BankItemInfo> GetBankItems()
{
    std::vector<NetLog::BankEntry> raw;
    NetLog::GetBankSnapshot(raw);
    std::vector<BankItemInfo> out;
    out.reserve(raw.size());
    for (const auto &e : raw)
        out.push_back(BankItemInfo{e.uid, e.itemId, e.count});
    return out;
}

int SendBankMoveToCash(uint32_t uid, uint32_t itemId, uint32_t count)
{
    if (uid == 0 || count == 0)
    {
        spdlog::warn("GGTB::SendBankMoveToCash: invalid uid={} count={}", uid, count);
        return -1;
    }

    // 挑目标 cash 格:优先已有同 itemId 的格(堆叠),否则首个空格;满则失败。
    auto cash = GetCashBagItems();
    std::unordered_set<uint32_t> used;
    int dest = -1;
    for (const auto &it : cash)
    {
        used.insert(it.slotIndex);
        if (itemId != 0 && it.itemId == itemId && dest < 0)
            dest = static_cast<int>(it.slotIndex);
    }
    if (dest < 0)
    {
        for (uint32_t s = 0; s < kCashSlotCount; ++s)
        {
            if (!used.count(s))
            {
                dest = static_cast<int>(s);
                break;
            }
        }
    }
    if (dest < 0)
    {
        spdlog::warn("GGTB::SendBankMoveToCash: no free cash slot uid={} itemId={}", uid, itemId);
        return -1;
    }

    auto beginAddr = PatternResolver::Get("NetBeginSend");
    auto sendAddr  = PatternResolver::Get("NetSkillSendPackage");
    if (!beginAddr || !sendAddr)
    {
        spdlog::error("GGTB::SendBankMoveToCash: pattern unresolved (begin={:x} send={:x})",
                      beginAddr, sendAddr);
        return -1;
    }

    auto pBegin = reinterpret_cast<NetBeginSendFn>(beginAddr);
    auto pSend  = reinterpret_cast<NetSkillSendPackageFn>(sendAddr);
    // body = [destCashWireSlot(slot+13), bankUniqueId, 0, count]。与摊贩购买同发送器。
    uint32_t payload[4] = {
        static_cast<uint32_t>(dest) + kCashSlotWireBase, uid, 0, count
    };
    int rv = CallSkillSendPackageSEH(pBegin, pSend, payload, sizeof(payload),
                                     kProtocolBankMove);
    if (rv < 0)
    {
        spdlog::warn("GGTB::SendBankMoveToCash: SEH uid={} itemId={} count={} dest={}",
                     uid, itemId, count, dest);
        return -1;
    }
    spdlog::info("GGTB::SendBankMoveToCash: uid={} itemId={} count={} -> cash slot {} rv={}",
                 uid, itemId, count, dest, rv);
    return dest;
}

// ---------- Daily rewards: 在线奖励(Access) / 签到奖励(Attendance) ----------
namespace
{
// 开窗 __stdcall(int 忽略)。领取 __thiscall:在线=entry(读 entry+0xC/0x10 当 body),
// 签到=this 忽略(无 body)。两者 0 个栈参 / retn 0,故 __thiscall(void*) 复刻调用点。
using RewardOpenFn       = int(__stdcall *)(int);
using RewardEntryClaimFn = int(__thiscall *)(void *);

static void CallRewardOpenSEH(RewardOpenFn fn)
{
    __try { fn(1); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static int CallRewardEntryClaimSEH(RewardEntryClaimFn fn, void *entry)
{
    __try { return fn(entry); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

struct RewardCounts { int claimed = 0; int claimable = 0; int locked = 0; };

// 枚举弹窗两段 entry vector(page0@+0x44/+0x48, page1@+0x50/+0x54),按 entry+0x08 的 status
// 统计;onlineClaimFn!=null 时对每个 status==1 的 entry 直发领取(在线奖励逐档领)。
static RewardCounts EnumRewardDialog(uintptr_t dlg, RewardEntryClaimFn onlineClaimFn)
{
    RewardCounts c{};
    if (!dlg)
        return c;
    const uintptr_t vecs[2][2] = {
        { dlg + kRewardDlgPage0BeginOffset, dlg + kRewardDlgPage0EndOffset },
        { dlg + kRewardDlgPage1BeginOffset, dlg + kRewardDlgPage1EndOffset },
    };
    for (auto &v : vecs)
    {
        uint32_t begin = 0, end = 0;
        if (!SafeReadDword(v[0], begin) || !SafeReadDword(v[1], end))
            continue;
        if (!begin || end <= begin)
            continue;
        uint32_t n = (end - begin) / 4;
        if (n > 128) // 防御:vector 撕裂时别越界扫
            n = 128;
        for (uint32_t i = 0; i < n; ++i)
        {
            uint32_t entry = 0;
            if (!SafeReadDword(begin + 4 * i, entry) || !entry)
                continue;
            uint32_t status = 0;
            if (!SafeReadDword(entry + kRewardEntryStatusOffset, status))
                continue;
            if (status == 1)
            {
                c.claimable++;
                if (onlineClaimFn)
                    CallRewardEntryClaimSEH(onlineClaimFn, reinterpret_cast<void *>(entry));
            }
            else if (status == 2)
                c.claimed++;
            else
                c.locked++;
        }
    }
    return c;
}
} // namespace

RewardClaimResult ClaimDailyRewards(bool doOnline, bool doSignin)
{
    RewardClaimResult r{};

    auto mgrAddr         = PatternResolver::Get("UIManagerGetSingleton");
    auto getContentAddr  = PatternResolver::Get("UIManagerGetUIContent");
    auto markCloseAddr   = PatternResolver::Get("UIContentMarkClose");
    auto closeByIdAddr   = PatternResolver::Get("UIManagerCloseContent");
    auto closeActiveAddr = PatternResolver::Get("UIManagerCloseActiveContent");
    auto rendererPtrAddr = PatternResolver::Get("RendererPtr");
    auto accessOpenAddr  = PatternResolver::Get("RewardAccessOpen");
    auto attendOpenAddr  = PatternResolver::Get("RewardAttendanceOpen");
    auto accessClaimAddr = PatternResolver::Get("RewardAccessClaim");
    auto attendClaimAddr = PatternResolver::Get("RewardAttendanceClaim");
    if (!mgrAddr || !getContentAddr || !accessOpenAddr || !attendOpenAddr ||
        !accessClaimAddr || !attendClaimAddr)
    {
        spdlog::error("GGTB::ClaimDailyRewards: pattern unresolved "
                      "(mgr={:x} get={:x} aOpen={:x} dOpen={:x} aClaim={:x} dClaim={:x})",
                      mgrAddr, getContentAddr, accessOpenAddr, attendOpenAddr,
                      accessClaimAddr, attendClaimAddr);
        return r;
    }

    auto pMgr         = reinterpret_cast<UIManagerGetSingletonFn>(mgrAddr);
    auto pGetContent  = reinterpret_cast<UIManagerGetUIContentFn>(getContentAddr);
    auto pAccessOpen  = reinterpret_cast<RewardOpenFn>(accessOpenAddr);
    auto pAttendOpen  = reinterpret_cast<RewardOpenFn>(attendOpenAddr);
    auto pAccessClaim = reinterpret_cast<RewardEntryClaimFn>(accessClaimAddr);
    auto pAttendClaim = reinterpret_cast<RewardEntryClaimFn>(attendClaimAddr);
    auto pMarkClose    = markCloseAddr ? reinterpret_cast<UIContentMarkCloseFn>(markCloseAddr) : nullptr;
    auto pCloseById    = closeByIdAddr ? reinterpret_cast<UIManagerCloseContentFn>(closeByIdAddr) : nullptr;
    auto pCloseActive  = closeActiveAddr ? reinterpret_cast<UIManagerCloseActiveContentFn>(closeActiveAddr) : nullptr;
    uint32_t renderer  = 0;
    if (rendererPtrAddr)
        SafeReadDword(rendererPtrAddr, renderer);

    void *mgr = CallUIManagerGetSingletonSEH(pMgr);
    if (!mgr)
    {
        spdlog::warn("GGTB::ClaimDailyRewards: UI manager null");
        return r;
    }

    // 关闭奖励窗 = 复刻 vendor 关窗(CloseVendorWindowLocal,实测能关)。光把 content+0x25 置 1 不够:
    // 那只是「标记待关」,框架还要 UIManagerCloseActiveContent(0x5EA090, 传 renderer)去 *处理* 这个
    // marked-close 队列,窗口才真正收起。之前只 mark(叶子 UIContentMarkClose / 命令处理器 "exit")没调
    // CloseActiveContent,所以弹窗一直留屏 —— 这就是「关不掉」的根因。三步:① 叶子按对象 mark +
    // ② 按 id mark(UIManagerCloseContent)+ ③ CloseActiveContent(renderer) 推队列。
    // (写 g_currentForegroundUIId=0 只是关窗后的 *结果*,不是手段,别用。)
    auto isOpenAddr = PatternResolver::Get("UIManagerIsContentOpen");
    auto pIsOpen    = isOpenAddr ? reinterpret_cast<UIManagerIsContentOpenFn>(isOpenAddr) : nullptr;

    auto pushClose = [&](int id, uintptr_t dlg) {
        if (dlg && pMarkClose)
            CallUIContentMarkCloseSEH(pMarkClose, reinterpret_cast<void *>(dlg));
        if (pCloseById)
            CallUIManagerCloseContentSEH(pCloseById, mgr, id);
        if (pCloseActive && renderer)
            CallUIManagerCloseActiveContentSEH(
                pCloseActive, mgr, reinterpret_cast<void *>(static_cast<uintptr_t>(renderer)));
    };

    auto closeDlg = [&](int id, uintptr_t dlg) {
        // 本函数跑在 pipe 命令线程,关窗由 UI 线程兑现;开太快(关一个紧接着开下一个)上一个会留屏。
        // 阻塞等它真的关掉:每轮 mark + CloseActiveContent 推一次队列,轮询 IsContentOpen 直到 false
        // (最多 ~2.5s),再停 ~400ms 让 UI 安定,才允许开下一个窗。
        pushClose(id, dlg);
        for (int i = 0; i < 50; ++i)
        {
            if (!pIsOpen || CallUIManagerIsContentOpenSEH(pIsOpen, mgr, id) != 1)
                break; // 0=已关 / -1=异常,都不再等
            pushClose(id, dlg);
            Sleep(50);
        }
        Sleep(400);
    };

    // 开窗后服务器要回流奖励列表才会填充 entry 表。轮询等到非空或 ~2s 超时。
    auto waitPopulated = [&](int id) -> uintptr_t {
        for (int i = 0; i < 20; ++i)
        {
            uintptr_t d = reinterpret_cast<uintptr_t>(
                CallUIManagerGetUIContentSEH(pGetContent, mgr, id));
            if (d)
            {
                uint32_t b0 = 0, e0 = 0, b1 = 0, e1 = 0;
                SafeReadDword(d + kRewardDlgPage0BeginOffset, b0);
                SafeReadDword(d + kRewardDlgPage0EndOffset, e0);
                SafeReadDword(d + kRewardDlgPage1BeginOffset, b1);
                SafeReadDword(d + kRewardDlgPage1EndOffset, e1);
                if ((b0 && e0 > b0) || (b1 && e1 > b1))
                    return d;
            }
            Sleep(100);
        }
        return reinterpret_cast<uintptr_t>(CallUIManagerGetUIContentSEH(pGetContent, mgr, id));
    };

    // 一次只开一个窗:开 → 领 → 关 → 等它真的关掉并安定(closeDlg)→ 才开下一个。先签到后在线。
    // ---- 签到奖励(content 61):只要有可领就一次性 412561 领今天 ----
    if (doSignin)
    {
        CallRewardOpenSEH(pAttendOpen);
        uintptr_t dlg61 = waitPopulated(kRewardAttendanceContentId);
        RewardCounts signin = EnumRewardDialog(dlg61, nullptr);
        r.signinClaimed   = signin.claimed;
        r.signinClaimable = signin.claimable;
        r.signinLocked    = signin.locked;
        if (signin.claimable > 0)
        {
            CallRewardEntryClaimSEH(pAttendClaim, reinterpret_cast<void *>(dlg61));
            // 领取会触发服务器回包刷新这个弹窗,刷新会重置 +0x25 把关窗顶掉。先等它安定再关。
            Sleep(800);
        }
        closeDlg(kRewardAttendanceContentId, dlg61);
    }

    // ---- 在线奖励(content 62):逐档直发 412563 ----
    if (doOnline)
    {
        CallRewardOpenSEH(pAccessOpen);
        uintptr_t dlg62 = waitPopulated(kRewardAccessContentId);
        RewardCounts online = EnumRewardDialog(dlg62, pAccessClaim);
        r.onlineClaimed   = online.claimed;
        r.onlineClaimable = online.claimable;
        r.onlineLocked    = online.locked;
        // EnumRewardDialog 内对每个 status==1 的档已逐档直发领取;同上,等服务器回包刷新安定再关。
        if (online.claimable > 0)
            Sleep(800);
        closeDlg(kRewardAccessContentId, dlg62);
    }

    r.ok = true;
    spdlog::info("GGTB::ClaimDailyRewards: online[claimed={} claimable={} locked={}] "
                 "signin[claimed={} claimable={} locked={}]",
                 r.onlineClaimed, r.onlineClaimable, r.onlineLocked,
                 r.signinClaimed, r.signinClaimable, r.signinLocked);
    return r;
}

RewardPeekResult PeekRewardClaimable()
{
    RewardPeekResult r{};

    auto mgrAddr        = PatternResolver::Get("UIManagerGetSingleton");
    auto getContentAddr = PatternResolver::Get("UIManagerGetUIContent");
    if (!mgrAddr || !getContentAddr)
        return r;

    auto pMgr        = reinterpret_cast<UIManagerGetSingletonFn>(mgrAddr);
    auto pGetContent = reinterpret_cast<UIManagerGetUIContentFn>(getContentAddr);

    void *mgr = CallUIManagerGetSingletonSEH(pMgr);
    if (!mgr)
        return r;
    r.ok = true;

    // 只读 +0x38(HUD 闪烁位):有可领=1。不开窗、不发包。content 由服务器登录推送填好。
    auto peekOne = [&](int id, bool &resolved, bool &claimable) {
        uintptr_t c = reinterpret_cast<uintptr_t>(
            CallUIManagerGetUIContentSEH(pGetContent, mgr, id));
        if (!c)
            return;
        resolved = true;
        uint8_t hasClaimable = 0;
        if (SafeReadByte(c + kRewardDlgHasClaimableOffset, hasClaimable))
            claimable = (hasClaimable != 0);
    };
    peekOne(kRewardAccessContentId, r.onlineResolved, r.onlineClaimable);
    peekOne(kRewardAttendanceContentId, r.signinResolved, r.signinClaimable);
    return r;
}

// ---------- Walk-to-world-position (CLocalUser::SetAfterAction) ----------
namespace
{
// __thiscall — engine signature is (int x, int y, int action, int target).
// x/y are stored at this+32F0/32F4 as **truncated integers** of the float
// world coords. Verified against OnPlayerMoveClick @ 0x871D10 which uses
// `cvttss2si` (truncate float->int) before pushing the args, AND against
// AutoHunt__TickBattle @ 0x608fa9 which uses `(int)*(float *)(v34 + 60)`
// (plain C cast = truncation). Do NOT bit-cast — bit-casting 12345.6f gives
// 0x4640E666 which the engine then writes verbatim into the destination
// field, and TraceMove walks the avatar toward integer 1.27e9 forever.
using SetAfterActionFn = int(__thiscall *)(void *thisLocalUser, int x, int y,
                                           int action, int target);

static int CallSetAfterActionSEH(SetAfterActionFn fn, void *user, float x, float y,
                                 int action, int target)
{
    __try
    {
        return fn(user, static_cast<int>(x), static_cast<int>(y), action, target);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}
} // namespace

bool MoveTo(float worldX, float worldY, int action, uint32_t targetId)
{
    auto user = GetLocalUserPtr();
    if (!user)
    {
        spdlog::warn("GGTB::MoveTo: g_pLocalUser not resolved");
        return false;
    }
    auto fnAddr = PatternResolver::Get("SetAfterAction");
    if (!fnAddr)
    {
        spdlog::error("GGTB::MoveTo: SetAfterAction pattern unresolved");
        return false;
    }

    // OnPlayerMoveClick toggles these globals around the call — mirror it so
    // anything that reads "current intent" sees a walk-click in flight.
    // dword_DFD518 = 1 (walk-click intent), then 0 after. byte_DFD4E6 = 0 after.
    // Both addresses are optional; missing patterns just skip the toggle.
    auto intentAddr = PatternResolver::Get("AfterActionIntent");
    auto flag2Addr  = PatternResolver::Get("AfterActionFlag2");
    if (intentAddr)
        *reinterpret_cast<volatile uint32_t *>(intentAddr) = 1;

    auto fn = reinterpret_cast<SetAfterActionFn>(fnAddr);
    int rv = CallSetAfterActionSEH(fn, reinterpret_cast<void *>(user),
                                   worldX, worldY, action,
                                   static_cast<int>(targetId));

    if (intentAddr)
        *reinterpret_cast<volatile uint32_t *>(intentAddr) = 0;
    if (flag2Addr)
        *reinterpret_cast<volatile uint8_t *>(flag2Addr) = 0;

    if (rv < 0)
    {
        spdlog::warn("GGTB::MoveTo: SEH caught x={} y={}", worldX, worldY);
        return false;
    }
    // SetAfterAction returns >0 on success-path; the reject-log paths return
    // small ints from spdlog wrappers. Treat any non-negative as "engine took it"
    // — the ActionMove patch (if installed) forces the success path regardless.
    spdlog::info("GGTB::MoveTo: x={} y={} action={} target={} rv={}",
                 worldX, worldY, action, targetId, rv);
    return rv > 0;
}

// ---------- Walk-and-talk-to-NPC (mirrors OnTargetCreatureClick_TalkOrAttack @ 0x9584A0) ----------
namespace
{
// EntityManager::FindCreatureById — __thiscall(this=*g_pCreatureMgr, id) -> CCreature*.
using FindCreatureByIdFn = uintptr_t(__thiscall *)(void *mgr, int id);

static uintptr_t CallFindCreatureSEH(FindCreatureByIdFn fn, void *mgr, uint32_t id)
{
    __try { return fn(mgr, static_cast<int>(id)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// SEH-only wrapper around SetAfterAction. Kept in its own function because
// TalkOrAttack() holds std::string locals (via spdlog formatting) which the MSVC
// C2712 restriction forbids inside a function that contains __try.
static int CallSetAfterActionForTalkSEH(uintptr_t fnAddr, void *user,
                                        int x, int y, int action, int target)
{
    auto fn = reinterpret_cast<int(__thiscall *)(void *, int, int, int, int)>(fnAddr);
    __try { return fn(user, x, y, action, target); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return INT_MIN; }
}

// Calls Npc__LoadDialogScript(state, monsterTblId). Returns 0 on SEH or on
// "no script" (i.e. not an NPC). 1 means the engine found a dialog script
// for this template id.
using NpcLoadDialogScriptFn = int(__thiscall *)(void *state, int monsterTblId);
static int CallNpcLoadDialogScriptSEH(uintptr_t fnAddr, void *state, uint32_t monsterTblId)
{
    auto fn = reinterpret_cast<NpcLoadDialogScriptFn>(fnAddr);
    __try { return fn(state, static_cast<int>(monsterTblId)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// Calls Npc__OpenDialogByCreatureRef(mgr, &targetIdRef, state, ui, p1, p2). cdecl.
// Returns 0 on SEH; engine returns 1 on success and 0 on no-script.
using NpcOpenDialogByCreatureRefFn = int(__cdecl *)(void *mgr, void *idRef, void *state,
                                                   void *ui, int p1, void *p2);
static int CallNpcOpenDialogSEH(uintptr_t fnAddr, void *mgr, void *idRef, void *state,
                                void *ui, int p1, void *p2)
{
    auto fn = reinterpret_cast<NpcOpenDialogByCreatureRefFn>(fnAddr);
    __try { return fn(mgr, idRef, state, ui, p1, p2); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// Wrapper around all NPC discriminator lookups: returns true iff the creature
// at `cre` is an NPC according to the engine's own Npc__LoadDialogScript test.
// Best-effort — false on any read failure or missing pattern.
//
// IMPORTANT: PatternResolver::Get("NpcDialogState") returns the ADDRESS of the
// global (0xED347C / RVA 0xAD347C), not its value. The engine calls
// Npc__LoadDialogScript with `mov ecx, ds:g_NpcDialogState; push ecx` — i.e.
// the VALUE at that address is the actual state-object pointer. We MUST deref
// stateAddr before passing it as `this`; otherwise Npc__LoadDialogScript walks
// random nearby memory and always returns 0 (every creature classifies as
// monster, including known NPCs like 60010 咕咕-龍族小姐姐).
static bool CreatureIsNpc(uintptr_t cre)
{
    auto loadAddr  = PatternResolver::Get("NpcLoadDialogScript");
    auto stateAddr = PatternResolver::Get("NpcDialogState");
    if (!loadAddr || !stateAddr || !cre)
        return false;

    uint32_t statTable = 0;
    if (!SafeReadDword(cre + kCreatureStatTableOffset, statTable) || !statTable)
        return false;

    uint32_t monsterTblId = 0;
    if (!SafeReadDword(statTable + kStatTableMonsterTblIdOffset, monsterTblId) || !monsterTblId)
        return false;

    // Deref the global to get the actual state-object pointer. If the global is
    // still 0 (dialog system not initialised — e.g. very early map load), bail
    // safe rather than crashing the loader.
    uint32_t stateObj = 0;
    if (!SafeReadDword(stateAddr, stateObj) || !stateObj)
        return false;

    return CallNpcLoadDialogScriptSEH(loadAddr, reinterpret_cast<void *>(stateObj),
                                      monsterTblId) != 0;
}
} // namespace

bool TalkOrAttack(uint32_t creatureId)
{
    if (creatureId == 0)
    {
        spdlog::warn("GGTB::TalkOrAttack: creatureId must be > 0");
        return false;
    }

    auto user = GetLocalUserPtr();
    if (!user)
    {
        spdlog::warn("GGTB::TalkOrAttack: g_pLocalUser not resolved");
        return false;
    }

    auto findAddr  = PatternResolver::Get("FindCreatureById");
    auto mgrAddr   = PatternResolver::Get("CreatureMgrPtr");
    auto setAddr   = PatternResolver::Get("SetAfterAction");
    auto idAddr    = PatternResolver::Get("TargetCreatureId");
    auto intentAddr= PatternResolver::Get("AfterActionIntent");
    auto flag2Addr = PatternResolver::Get("AfterActionFlag2");
    if (!findAddr || !mgrAddr || !setAddr || !idAddr)
    {
        spdlog::error("GGTB::TalkOrAttack: pattern unresolved find={:x} mgr={:x} set={:x} id={:x}",
                      findAddr, mgrAddr, setAddr, idAddr);
        return false;
    }

    uint32_t mgr = 0;
    if (!SafeReadDword(mgrAddr, mgr) || !mgr)
    {
        spdlog::warn("GGTB::TalkOrAttack: CreatureMgr not resolved");
        return false;
    }

    auto fnFind = reinterpret_cast<FindCreatureByIdFn>(findAddr);
    uintptr_t cre = CallFindCreatureSEH(fnFind, reinterpret_cast<void *>(mgr), creatureId);
    if (!cre)
    {
        spdlog::warn("GGTB::TalkOrAttack: creature id={} not found in mgr", creatureId);
        return false;
    }

    // Read queued ground XY (engine's "click target" position).
    int32_t cX = 0, cY = 0;
    if (!SafeReadDword(cre + kCreatureQueuedXOffset, *reinterpret_cast<uint32_t *>(&cX)) ||
        !SafeReadDword(cre + kCreatureQueuedYOffset, *reinterpret_cast<uint32_t *>(&cY)))
    {
        spdlog::warn("GGTB::TalkOrAttack: failed to read pos (cre={:x})", cre);
        return false;
    }

    bool isNpc = CreatureIsNpc(cre);

    if (isNpc)
    {
        // NPC path: walk to NPC (action=1, pure walk, NO interact intent so the
        // engine doesn't auto-send any combat packet), then open the dialog UI
        // directly. The dialog open call is local; the server will only honor
        // subsequent actions (buy/sell/quest) when we're actually in range —
        // but pre-opening the UI is harmless either way.
        int rv = CallSetAfterActionForTalkSEH(setAddr, reinterpret_cast<void *>(user),
                                              cX, cY, 1, 0);  // action=1 (pure walk)
        if (rv == INT_MIN)
        {
            spdlog::warn("GGTB::TalkOrAttack[NPC]: SEH on SetAfterAction id={}", creatureId);
            return false;
        }

        // Bootstrap the dialog UI now. Even if we're still walking, the engine
        // doesn't crash on this — the dialog ends up onscreen and the server
        // gates any actions until we're in interact range.
        auto openAddr  = PatternResolver::Get("NpcOpenDialogByCreatureRef");
        auto stateAddr = PatternResolver::Get("NpcDialogState");
        auto uiAddr    = PatternResolver::Get("NpcDialogUITable");
        auto p1Addr    = PatternResolver::Get("NpcDialogParam1Ptr");
        auto p2Addr    = PatternResolver::Get("NpcDialogParam2Ptr");
        if (!openAddr || !stateAddr || !uiAddr || !p1Addr || !p2Addr)
        {
            spdlog::warn("GGTB::TalkOrAttack[NPC]: dialog pattern unresolved (open={:x} state={:x} ui={:x} p1={:x} p2={:x}) — walked but dialog not opened",
                         openAddr, stateAddr, uiAddr, p1Addr, p2Addr);
            return true; // walked OK, just no dialog
        }

        // Write g_TargetCreatureId BEFORE calling the opener (it derefs &idRef).
        *reinterpret_cast<volatile int32_t *>(idAddr) = static_cast<int32_t>(creatureId);

        // Per-IDA disasm of the engine's own call site at 0x9EA230..0x9EA260:
        //   push offset byte_ED40F0         ; a6: ADDRESS of byte_ED40F0
        //   push ds:dword_189DEE8            ; a5: VALUE at dword_189DEE8
        //   push ds:dword_ED3D10             ; a4: VALUE at dword_ED3D10
        //   push ds:g_NpcDialogState         ; a3: VALUE at g_NpcDialogState
        //   push offset g_TargetCreatureId   ; a2: ADDRESS of g_TargetCreatureId
        //   push ds:g_pCreatureMgr           ; a1: VALUE at g_pCreatureMgr
        //   call Npc__OpenDialogByCreatureRef
        // So mgr/state/ui/p1 must be DEREFERENCED before calling. idRef and p2
        // are passed by-address (they're treated as static globals; the engine
        // reads from them inside).
        uint32_t mgrDeref = 0;
        SafeReadDword(mgrAddr, mgrDeref); // mgr ptr already validated above
        uint32_t stateDeref = 0;
        SafeReadDword(stateAddr, stateDeref);
        uint32_t uiDeref = 0;
        SafeReadDword(uiAddr, uiDeref);
        uint32_t p1Deref = 0;
        SafeReadDword(p1Addr, p1Deref);

        int dlgRv = CallNpcOpenDialogSEH(openAddr,
                                         reinterpret_cast<void *>(mgrDeref),
                                         reinterpret_cast<void *>(idAddr),
                                         reinterpret_cast<void *>(stateDeref),
                                         reinterpret_cast<void *>(uiDeref),
                                         static_cast<int>(p1Deref),
                                         reinterpret_cast<void *>(p2Addr));

        spdlog::info("GGTB::TalkOrAttack[NPC]: id={} dst=({},{}) walk_rv={} dlg_rv={} state={:x}",
                     creatureId, cX, cY, rv, dlgRv, stateDeref);
        return true;
    }
    else
    {
        // Monster path: mirror OnTargetCreatureClick_TalkOrAttack exactly.
        // Write g_TargetCreatureId + g_AfterActionIntent=3, then SetAfterAction
        // with action=3 (walk+attack). Engine walks the player, target loop
        // sends combat packets on arrival.
        *reinterpret_cast<volatile int32_t *>(idAddr) = static_cast<int32_t>(creatureId);
        if (intentAddr)
            *reinterpret_cast<volatile uint32_t *>(intentAddr) = 3;
        if (flag2Addr)
            *reinterpret_cast<volatile uint8_t *>(flag2Addr) = 0;

        int rv = CallSetAfterActionForTalkSEH(setAddr, reinterpret_cast<void *>(user),
                                              cX, cY, 3, 0);
        if (rv == INT_MIN)
        {
            spdlog::warn("GGTB::TalkOrAttack[MOB]: SEH on SetAfterAction id={}", creatureId);
            return false;
        }
        if (intentAddr)
            *reinterpret_cast<volatile uint32_t *>(intentAddr) = 0;

        spdlog::info("GGTB::TalkOrAttack[MOB]: id={} dst=({},{}) rv={}",
                     creatureId, cX, cY, rv);
        return rv > 0;
    }
}

// ---------- Nearby-NPC snapshot ----------
//
// Walks CreatureMgr exactly like GetNearbyMonsters does, but with a different
// filter: we KEEP kind==7 (real NPCs) which GetNearbyMonsters rejects, AND we
// keep monsters (kind!=8 and not dead) so the UI can show both kinds. The
// distinction is on the `kind` field — UI/JSON decides whether to render the
// "talk" button or "attack" affordance.
namespace
{
int VisitNpcCandidate(uintptr_t cre, float lx, float ly, float lz, float maxDist,
                      std::vector<NearbyNpc> &out)
{
    if (!cre)
        return 1;

    uint32_t statTable = 0;
    if (!SafeReadDword(cre + kCreatureStatTableOffset, statTable) || !statTable)
        return 2;

    // kind sits inside StatTable (statTable+0xDC), NOT on CCreature itself.
    uint32_t kind = 0;
    if (!SafeReadDword(statTable + kStatTableKindOffset, kind))
        return 3;
    // Drop pets only. NPCs and monsters both share kind==3 in this build and
    // can only be told apart by their data-table id at statTable+236.
    if (kind == kMonsterKindPet)
        return 4;

    uint32_t state = 0;
    if (!SafeReadDword(cre + kCreatureStateOffset, state))
        return 5;
    if (state == kCreatureStateDead)
        return 6;

    int64_t hp = 0;
    SafeReadInt64(cre + kCreatureHpOffset, hp); // NPCs may have hp==0/sentinel; don't reject

    uint32_t id = 0;
    if (!SafeReadDword(cre + kCreatureIdOffset, id) || !id)
        return 9;

    float x = 0, y = 0, z = 0;
    if (!SafeReadFloat(cre + kCreaturePosXOffset, x) ||
        !SafeReadFloat(cre + kCreaturePosYOffset, y) ||
        !SafeReadFloat(cre + kCreaturePosZOffset, z))
        return 10;

    float dx = x - lx, dy = y - ly, dz = z - lz;
    float d  = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (maxDist > 0 && d > maxDist)
        return 11;

    uint32_t level = 0;
    SafeReadDword(cre + kMonsterLevelOffset, level);

    // monsterTblId (statTable+236) — UI uses this to decide NPC vs monster.
    // NPC templates in this build cluster around 5xxxx-6xxxx; monsters live
    // in 1xxx-3xxx. UI does the heuristic split, not us — server has the
    // final say anyway. Best-effort read; 0 == "unknown" passes through.
    uint32_t monsterTblId = 0;
    SafeReadDword(statTable + kStatTableMonsterTblIdOffset, monsterTblId);

    // hasDialog: ask the engine's OWN test "is this template an NPC with a
    // dialog script?". This is the authoritative NPC discriminator used by
    // every NPC-click path in the game. NpcDialogState is a POINTER-holding
    // global — must deref before passing as `this` (see CreatureIsNpc for the
    // full explanation of the bug this used to cause).
    bool hasDialog = false;
    if (monsterTblId)
    {
        auto loadAddr  = PatternResolver::Get("NpcLoadDialogScript");
        auto stateAddr = PatternResolver::Get("NpcDialogState");
        if (loadAddr && stateAddr)
        {
            uint32_t stateObj = 0;
            if (SafeReadDword(stateAddr, stateObj) && stateObj)
            {
                hasDialog = CallNpcLoadDialogScriptSEH(loadAddr,
                                                       reinterpret_cast<void *>(stateObj),
                                                       monsterTblId) != 0;
            }
        }
    }

    // Display name lives at statTable+8 as a Big5 char[100]. See header comment.
    // SEH-wrapped via SafeReadString. Convert to UTF-8 for the UI; empty on miss.
    std::string nameUtf8;
    char nameBuf[kStatTableNameMaxLen + 1] = {};
    if (SafeReadString(statTable + kStatTableNameOffset, nameBuf, sizeof(nameBuf)) && nameBuf[0])
        nameUtf8 = Big5ToUtf8(nameBuf);

    out.push_back({id, kind, monsterTblId, hasDialog, level, d, x, y, z, hp,
                   std::move(nameUtf8), cre});
    return 0;
}
} // namespace

std::vector<NearbyNpc> GetNearbyNpcs(float maxDistance)
{
    std::vector<NearbyNpc> result;

    float lx = 0, ly = 0, lz = 0;
    if (!GetLocalPosition(lx, ly, lz))
    {
        spdlog::warn("GGTB::GetNearbyNpcs: g_pLocalUser not resolved");
        return result;
    }

    auto mgrPtrAddr = PatternResolver::Get("CreatureMgrPtr");
    if (!mgrPtrAddr)
    {
        spdlog::warn("GGTB::GetNearbyNpcs: CreatureMgrPtr pattern unresolved");
        return result;
    }

    uint32_t mgr = 0;
    if (!SafeReadDword(mgrPtrAddr, mgr) || !mgr)
    {
        spdlog::warn("GGTB::GetNearbyNpcs: CreatureMgr singleton is null");
        return result;
    }

    uint32_t node = 0;
    if (!SafeReadDword(mgr + kCreatureMgrListHeadOffset, node))
    {
        spdlog::warn("GGTB::GetNearbyNpcs: failed to read mgr+{:x} (list head)",
                     kCreatureMgrListHeadOffset);
        return result;
    }

    constexpr int kMaxNodes = 2048;
    int           visited   = 0;
    int           reject[16] = {};
    // Track kind distribution (expanded to 256 buckets; engine kinds we know
    // include 7=NPC, 8=pet, 10, 27, 31, 36, 58 + monster-class values often
    // well above 16) and snapshot first few entries for correlation.
    int kindHist[256] = {};
    int totalNonZeroKind = 0;
    int diagSamples = 0;
    char diagBuf[512] = {};
    int  diagLen = 0;

    for (; visited < kMaxNodes && node; ++visited)
    {
        // Peek kind + a few candidate-discriminator fields. Per IDA, the engine's
        // OnTargetCreatureClick rejects when `(kind==3 && statTable+236 > 0)`,
        // so statTable+236 is the likely NPC/monster discriminator within kind==3.
        // statTable+240 is checked by AutoTarget for "aggression/bounty"
        // (a3 & 2 && stat+240 > 0). statTable+160 looks like level/CL field.
        uint32_t kind = 0, statTablePeek = 0;
        uint32_t idPeek = 0, stat236 = 0, stat240 = 0, stat160 = 0;
        int64_t  hpPeek = 0;
        SafeReadDword(node + kCreatureIdOffset, idPeek);
        SafeReadInt64(node + kCreatureHpOffset, hpPeek);
        if (SafeReadDword(node + kCreatureStatTableOffset, statTablePeek) && statTablePeek)
        {
            SafeReadDword(statTablePeek + kStatTableKindOffset, kind);
            SafeReadDword(statTablePeek + 236, stat236);
            SafeReadDword(statTablePeek + 240, stat240);
            SafeReadDword(statTablePeek + 160, stat160);
            if (kind < 256) kindHist[kind]++;
            if (kind != 0) totalNonZeroKind++;
        }

        // Capture first 12 with full discriminator set
        if (diagSamples < 12)
        {
            int n = snprintf(diagBuf + diagLen, sizeof(diagBuf) - diagLen,
                             "%s(id=%u k=%u s160=%u s236=%u s240=%u hp=%lld)",
                             diagSamples ? " " : "",
                             idPeek, kind, stat160, stat236, stat240,
                             static_cast<long long>(hpPeek));
            if (n > 0) diagLen += n;
            ++diagSamples;
        }

        int r = VisitNpcCandidate(node, lx, ly, lz, maxDistance, result);
        if (r >= 0 && r < 16) reject[r]++;
        if (!SafeReadDword(node + kCreatureNextOffset, node))
            break;
    }

    std::sort(result.begin(), result.end(),
              [](const NearbyNpc &a, const NearbyNpc &b) {
                  return a.distance < b.distance;
              });

    // Build a compact "top kinds" string: any bucket with >0 entries.
    char kindTop[256] = {};
    int  kindTopLen = 0;
    for (int k = 0; k < 256; ++k)
    {
        if (kindHist[k] == 0) continue;
        int n = snprintf(kindTop + kindTopLen, sizeof(kindTop) - kindTopLen,
                         "%s%d=%d", kindTopLen ? "," : "", k, kindHist[k]);
        if (n > 0) kindTopLen += n;
        if (kindTopLen > 200) { snprintf(kindTop + kindTopLen, sizeof(kindTop) - kindTopLen, "..."); break; }
    }

    // Sanity: how many accepted entries did the engine's NPC discriminator
    // tag as having a dialog script? After the deref bug fix this should be
    // > 0 on any map with NPCs (e.g. 19131/19129/19123 = 咕咕- 龍族小姐姐 /
    // 傳送 / 轉職). If this stays at 0 with known NPCs in range, deref order
    // or PatternResolver "NpcDialogState" address is wrong again — see the
    // comments in CreatureIsNpc + PatternResolver registry for the pitfall.
    int npcCount = 0;
    int mobCount = 0;
    for (const auto &n : result)
    {
        if (n.hasDialog) ++npcCount; else ++mobCount;
    }

    // Resolve + log the NpcDialogState global value once so we can verify it
    // wasn't 0 at scan time (would explain hasDialog=false for everything).
    uintptr_t stateAddr = PatternResolver::Get("NpcDialogState");
    uint32_t  stateObj  = 0;
    if (stateAddr) SafeReadDword(stateAddr, stateObj);

    spdlog::info(
        "GGTB::GetNearbyNpcs: mgr={:x} visited={} accepted={} npc={} mob={} "
        "rej(null={} stat={} kind={} pet={} state={} dead={} id={} pos={} range={}) "
        "kinds=[{}] totalNonZero={} maxDist={} stateGlobal={:x}={:x} samples=[{}]",
        static_cast<uintptr_t>(mgr), visited,
        static_cast<int>(result.size()), npcCount, mobCount,
        reject[1], reject[2], reject[3], reject[4], reject[5],
        reject[6], reject[9], reject[10], reject[11],
        kindTop, totalNonZeroKind, maxDistance,
        stateAddr, static_cast<uintptr_t>(stateObj), diagBuf);

    return result;
}

// ---------- Map collision / pathfind helpers ----------

namespace
{
struct MapView
{
    uint32_t collisionGrid; // pointer (raw uintptr_t cast)
    int      width;
    int      height;
    bool     valid;
};

bool ReadCurMapView(MapView &v)
{
    v = {};
    auto mapPtrAddr = PatternResolver::Get("CurMapPtr");
    if (!mapPtrAddr)
        return false;
    uint32_t mapObj = 0;
    if (!SafeReadDword(mapPtrAddr, mapObj) || !mapObj)
        return false;
    if (!SafeReadDword(mapObj + 0x10, v.collisionGrid) || !v.collisionGrid)
        return false;
    uint32_t w = 0, h = 0;
    if (!SafeReadDword(mapObj + 0x14, w) ||
        !SafeReadDword(mapObj + 0x18, h))
        return false;
    if (!w || !h || w > 100000 || h > 100000)
        return false;
    v.width  = static_cast<int>(w);
    v.height = static_cast<int>(h);
    v.valid  = true;
    return true;
}

// Mirrors Map__IsBlocked @ 0xA97080 in C: returns true (blocked) on out-of-
// bounds or any nonzero cell value. Map-not-loaded is treated as "blocked"
// (the engine does the same — its earliest-out returns 1).
bool TileBlocked(const MapView &v, int tx, int ty)
{
    if (!v.valid)
        return true;
    if (tx < 0 || ty < 0 || tx >= v.width || ty >= v.height)
        return true;
    uint32_t cell = 0;
    uintptr_t cellAddr = v.collisionGrid +
                         static_cast<uintptr_t>(tx + v.width * ty) * 4u;
    if (!SafeReadDword(cellAddr, cell))
        return true;
    return cell != 0;
}
} // anonymous

bool IsTileWalkable(int tileX, int tileY)
{
    bool walkable = false;
    __try
    {
        MapView v{};
        if (!ReadCurMapView(v))
            return false;
        walkable = !TileBlocked(v, tileX, tileY);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        walkable = false;
    }
    return walkable;
}

bool RaycastFurthestWalkable(int sx, int sy, int dx, int dy,
                             int &outX, int &outY)
{
    outX = sx;
    outY = sy;
    bool ok = false;
    __try
    {
        MapView v{};
        if (!ReadCurMapView(v))
            return false;
        // Start tile must itself be walkable; otherwise we can't even leave.
        if (TileBlocked(v, sx, sy))
            return false;

        int diffX = dx - sx;
        int diffY = dy - sy;
        // No-move case: start == dst, trivially walkable.
        if (diffX == 0 && diffY == 0)
        {
            outX = sx;
            outY = sy;
            return true;
        }

        // Step-by-step Bresenham-ish walk in tile space. Stop at the last
        // walkable tile before any blocked one. Mirrors the engine's
        // Map__RaycastFurthestWalkable_XY (0xA96DB0) logic, which uses
        // float steps; integer steps are sufficient for tile-grid use.
        int absX = diffX < 0 ? -diffX : diffX;
        int absY = diffY < 0 ? -diffY : diffY;
        int steps = absX > absY ? absX : absY;
        if (steps <= 0)
        {
            outX = sx;
            outY = sy;
            return true;
        }
        int lastX = sx, lastY = sy;
        for (int i = 1; i <= steps; ++i)
        {
            int tx = sx + (diffX * i) / steps;
            int ty = sy + (diffY * i) / steps;
            if (TileBlocked(v, tx, ty))
                break;
            lastX = tx;
            lastY = ty;
        }
        outX = lastX;
        outY = lastY;
        ok   = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = false;
    }
    return ok;
}

uint32_t GetTargetCreatureId()
{
    auto idAddr = PatternResolver::Get("TargetCreatureId");
    if (!idAddr)
        return 0;
    uint32_t id = 0;
    if (!SafeReadDword(idAddr, id))
        return 0;
    // Engine uses -1 to mean "no target" in some code paths; treat both as 0.
    if (id == 0xFFFFFFFFu)
        return 0;
    return id;
}

namespace
{
// SEH-only reader for CCreature fields. MSVC C2712 forbids __try in functions
// that need object unwinding, so the actual try lives here in a leaf with no
// non-trivial locals.
struct CreatureFields
{
    uint32_t statTable;
    uint32_t kind;
    uint32_t idCheck;
    int64_t  hp;
    float    x, y, z;
};

static bool ReadCreatureFieldsSEH(uintptr_t cre, uint32_t expectId,
                                  CreatureFields *out)
{
    __try
    {
        out->statTable = 0;
        out->kind      = 0;
        out->idCheck   = 0;
        out->hp        = 0;
        out->x = out->y = out->z = 0.0f;
        if (!SafeReadDword(cre + kCreatureStatTableOffset, out->statTable) || !out->statTable)
            return false;
        SafeReadDword(out->statTable + kStatTableKindOffset, out->kind);
        if (!SafeReadDword(cre + kCreatureIdOffset, out->idCheck) || out->idCheck != expectId)
            return false;
        if (!SafeReadFloat(cre + kCreaturePosXOffset, out->x) ||
            !SafeReadFloat(cre + kCreaturePosYOffset, out->y) ||
            !SafeReadFloat(cre + kCreaturePosZOffset, out->z))
            return false;
        SafeReadInt64(cre + kCreatureHpOffset, out->hp); // best-effort
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
} // anonymous

bool LookupCreatureById(uint32_t creatureId, CreatureSnapshot &out)
{
    out = {};
    if (!creatureId)
        return false;

    auto findAddr = PatternResolver::Get("FindCreatureById");
    auto mgrAddr  = PatternResolver::Get("CreatureMgrPtr");
    if (!findAddr || !mgrAddr)
        return false;

    uint32_t mgr = 0;
    if (!SafeReadDword(mgrAddr, mgr) || !mgr)
        return false;

    auto fnFind = reinterpret_cast<FindCreatureByIdFn>(findAddr);
    uintptr_t cre = CallFindCreatureSEH(fnFind, reinterpret_cast<void *>(mgr),
                                        creatureId);
    if (!cre)
        return false;

    CreatureFields f{};
    if (!ReadCreatureFieldsSEH(cre, creatureId, &f))
        return false;

    out.addr = cre;
    out.id   = f.idCheck;
    out.kind = f.kind;
    out.hp   = f.hp;
    out.x    = f.x;
    out.y    = f.y;
    out.z    = f.z;
    return true;
}

// ---------- Active buffs (BuffHelper) ----------
namespace
{
// FindHostBuffContainer @ 0x6486C0 — __thiscall(this=&g_BuffHelper, hostType,
// hostId). Returns the per-host std::list container (0 if the host has no
// active buffs). We model __thiscall as __fastcall (ecx=this, edx=unused) so we
// can pass the BuffHelper singleton explicitly.
using BuffFindHostContainerFn = uintptr_t(__fastcall *)(uintptr_t thisPtr,
                                                        int edxDummy, int hostType,
                                                        int hostId);

uintptr_t CallFindBuffContainerSEH(BuffFindHostContainerFn fn, uintptr_t thisPtr,
                                   int hostType, int hostId)
{
    __try
    {
        return fn(thisPtr, 0, hostType, hostId);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

bool SafeReadPtr(uintptr_t addr, uintptr_t &out)
{
    __try
    {
        out = *reinterpret_cast<volatile uintptr_t *>(addr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Raw snapshot of one buff node, read under SEH so a torn list/scene transition
// can't crash the worker thread. buffDesc resolution stays inside the same SEH
// scope (the BuffDesc array can be reallocated during a map load).
struct BuffInstRaw
{
    uint32_t buffId;
    uint32_t duration;
    int64_t  endTs;
    uint32_t boolActive;
    uint32_t category;
    uint32_t flags;
    char     name[kBuffNameMaxLen];
    bool     descOk;
};

bool ReadBuffInstSEH(uintptr_t node, BuffInstRaw *out)
{
    __try
    {
        out->buffId     = *reinterpret_cast<volatile uint16_t *>(node + kBuffInstIdOffset);
        out->duration   = *reinterpret_cast<volatile uint32_t *>(node + kBuffInstDurationOffset);
        out->endTs      = *reinterpret_cast<volatile int64_t  *>(node + kBuffInstEndTsOffset);
        out->boolActive = *reinterpret_cast<volatile uint32_t *>(node + kBuffInstBoolOffset);

        uintptr_t desc = *reinterpret_cast<volatile uintptr_t *>(node + kBuffInstDescOffset);
        out->descOk = false;
        out->category = 0;
        out->flags = 0;
        out->name[0] = 0;
        if (desc)
        {
            out->category = *reinterpret_cast<volatile uint8_t *>(desc + kBuffDescCategoryOffset);
            out->flags    = *reinterpret_cast<volatile uint32_t *>(desc + kBuffDescFlagsOffset);
            const char *nm = reinterpret_cast<const char *>(desc + kBuffDescNameOffset);
            size_t i = 0;
            for (; i < kBuffNameMaxLen - 1; ++i)
            {
                char c = nm[i];
                out->name[i] = c;
                if (!c)
                    break;
            }
            out->name[i] = 0;
            out->descOk = true;
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

BuffKind CategoryToKind(uint32_t category)
{
    // RenderNormalBuffBar whitelists {1,2,5}; RenderCashBuffBar whitelists {4,0}.
    return (category == 1 || category == 2 || category == 5) ? BuffKind::Normal
                                                             : BuffKind::Cash;
}
} // anonymous

uint32_t GetSkillBuffId(uint32_t skillId)
{
    auto tableAddr = PatternResolver::Get("SkillBuffMap");
    if (!tableAddr)
        return 0;
    for (size_t i = 0; i < kSkillBuffMapCount; ++i)
    {
        uint32_t sid = 0, bid = 0;
        if (!SafeReadDword(tableAddr + i * 8, sid))
            break;
        if (sid == skillId)
        {
            SafeReadDword(tableAddr + i * 8 + 4, bid);
            return bid;
        }
    }
    return 0;
}

uint32_t GetBuffSourceSkillId(uint32_t buffId)
{
    if (buffId == 0)
        return 0;
    auto tableAddr = PatternResolver::Get("SkillBuffMap");
    if (!tableAddr)
        return 0;
    for (size_t i = 0; i < kSkillBuffMapCount; ++i)
    {
        uint32_t sid = 0, bid = 0;
        if (!SafeReadDword(tableAddr + i * 8, sid))
            break;
        SafeReadDword(tableAddr + i * 8 + 4, bid);
        if (bid == buffId)
            return sid;
    }
    return 0;
}

std::vector<ActiveBuff> GetHostBuffs(uint32_t hostId, int hostType, int kindFilter)
{
    std::vector<ActiveBuff> result;
    if (hostId == 0)
        return result;

    auto findAddr = PatternResolver::Get("BuffFindHostContainer");
    if (!findAddr)
    {
        spdlog::error("GGTB::GetHostBuffs: BuffFindHostContainer unresolved");
        return result;
    }
    auto thisAddr = PatternResolver::Get("BuffHelperThis");
    if (!thisAddr)
    {
        spdlog::error("GGTB::GetHostBuffs: BuffHelperThis unresolved");
        return result;
    }

    auto pFind = reinterpret_cast<BuffFindHostContainerFn>(findAddr);
    uintptr_t container = CallFindBuffContainerSEH(pFind, thisAddr, hostType,
                                                   static_cast<int>(hostId));
    if (!container)
        return result; // host has no buffs

    // Walk the intrusive circular list. The stored links point AT each node's
    // +0x43 link field; node base = link - 0x43; the chain ends back at the
    // sentinel slot (container + 0x59). See header for the RE derivation.
    const uintptr_t end = container + kBuffContainerSentinelOffset;
    uintptr_t link = 0;
    if (!SafeReadPtr(end, link))
        return result;

    DWORD   nowMs  = GetTickCount();
    int64_t nowSec = static_cast<int64_t>(_time64(nullptr));

    size_t guard = 0;
    while (link && link != end && guard < kBuffMaxListWalk)
    {
        ++guard;
        uintptr_t node = link - kBuffNodeLinkOffset;

        BuffInstRaw raw{};
        if (!ReadBuffInstSEH(node, &raw))
            break;

        // Advance early so a malformed entry below doesn't strand the walk.
        uintptr_t nextLink = 0;
        if (!SafeReadPtr(link, nextLink))
            break;
        link = nextLink;

        if (!raw.descOk)
            continue;

        BuffKind kind = CategoryToKind(raw.category);
        if (kindFilter == 0 && kind != BuffKind::Normal)
            continue;
        if (kindFilter == 1 && kind != BuffKind::Cash)
            continue;

        ActiveBuff b{};
        b.buffId     = raw.buffId;
        b.category   = raw.category;
        b.kind       = kind;
        b.name       = Big5ToUtf8(raw.name);
        b.duration   = raw.duration;
        b.isBoolBuff = (raw.flags & kBuffFlagBool) != 0;
        b.skillId    = GetBuffSourceSkillId(raw.buffId);
        b.instAddr   = node;

        if (b.isBoolBuff)
        {
            b.remainingMs = -1; // toggle buff, no countdown
        }
        else if (raw.flags & kBuffFlagSeconds)
        {
            int64_t rem = raw.endTs - nowSec;
            b.remainingMs = rem > 0 ? rem * 1000 : 0;
        }
        else
        {
            int64_t rem = raw.endTs - static_cast<int64_t>(nowMs);
            b.remainingMs = rem > 0 ? rem : 0;
        }

        result.push_back(std::move(b));
    }

    std::sort(result.begin(), result.end(),
              [](const ActiveBuff &a, const ActiveBuff &c) {
                  // -1 (no countdown) sorts last; otherwise ascending by remaining.
                  int64_t ra = a.remainingMs < 0 ? INT64_MAX : a.remainingMs;
                  int64_t rc = c.remainingMs < 0 ? INT64_MAX : c.remainingMs;
                  return ra < rc;
              });
    return result;
}

struct CashIconRaw
{
    uint32_t typeId;
    uint32_t iconId;
    char     name[kBuffNameMaxLen];
    uint8_t  blink;
};

// Reads one 280-byte HUD cash-icon record under SEH (the vector can be
// reallocated mid-walk during a scene transition).
bool ReadCashIconSEH(uintptr_t rec, CashIconRaw *out)
{
    __try
    {
        out->typeId = *reinterpret_cast<volatile uint32_t *>(rec + kHudCashRecTypeOffset);
        out->iconId = *reinterpret_cast<volatile uint32_t *>(rec + kHudCashRecIconOffset);
        out->blink  = *reinterpret_cast<volatile uint8_t  *>(rec + kHudCashRecBlinkOffset);
        const char *nm = reinterpret_cast<const char *>(rec + kHudCashRecNameOffset);
        size_t i = 0;
        for (; i < kBuffNameMaxLen - 1; ++i)
        {
            char c = nm[i];
            out->name[i] = c;
            if (!c)
                break;
        }
        out->name[i] = 0;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

std::vector<ActiveBuff> GetCashItemIcons()
{
    std::vector<ActiveBuff> result;

    auto hud = PatternResolver::Get("GameHUDThis");
    if (!hud)
    {
        spdlog::error("GGTB::GetCashItemIcons: GameHUDThis unresolved");
        return result;
    }

    uintptr_t begin = 0, end = 0;
    if (!SafeReadPtr(hud + kHudCashVecBeginOffset, begin) ||
        !SafeReadPtr(hud + kHudCashVecEndOffset, end))
        return result;
    if (!begin || end <= begin)
        return result;

    size_t count = (end - begin) / kHudCashRecStride;
    if (count > kHudCashMaxRecords)
        count = kHudCashMaxRecords; // torn/garbage guard

    for (size_t i = 0; i < count; ++i)
    {
        CashIconRaw raw{};
        if (!ReadCashIconSEH(begin + i * kHudCashRecStride, &raw))
            break;

        std::string name = Big5ToUtf8(raw.name);
        if (name.empty())
            continue; // unused/blank slot

        ActiveBuff b{};
        b.buffId      = 0;             // HUD icons have no BuffDesc buffId
        b.category    = 0;             // synthetic; classified as Cash
        b.kind        = BuffKind::Cash;
        b.name        = std::move(name);
        b.duration    = 0;
        b.remainingMs = -1;            // no countdown exposed on the record
        b.skillId     = raw.iconId;    // diagnostic: HUD icon-table key
        b.isBoolBuff  = false;
        b.instAddr    = begin + i * kHudCashRecStride;
        result.push_back(std::move(b));
    }
    return result;
}

PartySnapshot GetPartyMembers(bool includeBuffs)
{
    PartySnapshot snap{};
    snap.inParty   = false;
    snap.role      = 0;
    snap.selfIndex = -1;

    auto countAddr   = PatternResolver::Get("PartyMemberCount");
    auto arrayAddr   = PatternResolver::Get("PartyMemberArray");
    auto selfIdxAddr = PatternResolver::Get("PartySelfIndex");
    auto roleAddr    = PatternResolver::Get("PartySelfRole");
    auto stateAddr   = PatternResolver::Get("PartyMemberStateArray");
    if (!countAddr || !arrayAddr)
        return snap;

    uint32_t count = 0;
    if (!SafeReadDword(countAddr, count) || count == 0 || count > kPartyMaxMembers)
        return snap; // solo / not in a party (or torn read)

    int32_t selfIndex = -1;
    if (selfIdxAddr)
    {
        uint32_t v = 0;
        if (SafeReadDword(selfIdxAddr, v))
            selfIndex = static_cast<int32_t>(v);
    }
    uint32_t role = 0;
    if (roleAddr)
        SafeReadDword(roleAddr, role);

    snap.inParty   = true;
    snap.role      = static_cast<int>(role);
    snap.selfIndex = selfIndex;

    for (uint32_t i = 0; i < count; ++i)
    {
        uintptr_t entry = arrayAddr + kPartyEntryStride * i;

        char nameBuf[kPartyEntryNameMaxLen + 1] = {};
        SafeReadString(entry + kPartyEntryNameOffset, nameBuf, sizeof(nameBuf));

        uint32_t online = 0, mid = 0;
        SafeReadDword(entry + kPartyEntryOnlineOffset, online);
        SafeReadDword(entry + kPartyEntryIdOffset, mid);

        PartyMember m{};
        m.index    = static_cast<int>(i);
        m.name     = Big5ToUtf8(nameBuf);
        m.userId   = mid;
        m.isSelf   = (static_cast<int32_t>(i) == selfIndex);
        m.online   = (static_cast<int32_t>(online) > 0);
        m.hp       = -1;
        m.maxHp    = -1;
        m.nearby   = false;
        m.distance = -1.0f;

        if (stateAddr)
        {
            uintptr_t st = stateAddr + kPartyStateStride * i;
            uint32_t  hp = 0, maxHp = 0;
            if (SafeReadDword(st + kPartyStateHpOffset, hp))
                m.hp = static_cast<int32_t>(hp);
            if (SafeReadDword(st + kPartyStateMaxHpOffset, maxHp))
                m.maxHp = static_cast<int32_t>(maxHp);
        }

        // The self slot is blanked in the roster/state table (the engine draws
        // the local player from the player object). Back-fill from the locals.
        if (m.isSelf)
        {
            uint32_t selfId = GetLocalUserId();
            if (selfId)
                m.userId = selfId;
            std::string selfName = GetLocalPlayerName();
            if (!selfName.empty())
                m.name = std::move(selfName);
            m.online   = true;
            m.nearby   = true;   // self is trivially in range
            m.distance = 0.0f;
            if (m.hp <= 0)
                m.hp = static_cast<int32_t>(GetLocalHp());
        }
        else if (m.userId != 0 && m.userId != 0xFFFFFFFF)
        {
            // Teammate is "nearby" iff the engine loaded them into the
            // around-player AOI list — that's the same gate the engine uses to
            // resolve a remote target, so a buff cast only reaches them when
            // nearby. Used by the broker's party-buff keeper to skip far members.
            NearbyPlayer np;
            if (LookupAroundPlayerById(m.userId, np))
            {
                m.nearby   = true;
                m.distance = np.distance;
                // 组队状态表的 HP 更新慢/不准,用 AOI 里 CUser 的实时 HP 覆盖(明文 +0x1B00)。
                if (np.hp >= 0)
                    m.hp = np.hp;
            }
        }

        if (includeBuffs && m.userId != 0 && m.userId != 0xFFFFFFFF)
        {
            // Self buffs key on hostType 0; teammates on hostType 2 — the same
            // key CMessenger_RenderPartyMemberBuffIcons (0x8452F0) passes to the
            // engine's buff-icon renderer for remote party members.
            int hostType = m.isSelf ? 0 : 2;
            m.buffs = GetHostBuffs(m.userId, hostType, -1);
        }

        snap.members.push_back(std::move(m));
    }
    return snap;
}

std::vector<ActiveBuff> GetActiveBuffs(int kindFilter)
{
    std::vector<ActiveBuff> result = GetHostBuffs(GetLocalUserId(), 0, kindFilter);

    // BuffHelper only owns one cash buff; the rest of the cash icons live on the
    // HUD object. Merge them in whenever the caller wants Cash (all or Cash-only).
    if (kindFilter != 0)
    {
        auto cash = GetCashItemIcons();
        result.insert(result.end(),
                      std::make_move_iterator(cash.begin()),
                      std::make_move_iterator(cash.end()));
    }
    return result;
}

} // namespace GGTB
