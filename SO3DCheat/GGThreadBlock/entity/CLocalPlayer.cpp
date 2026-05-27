#include "CLocalPlayer.h"
#include "../util/PatternResolver.h"
#include "../util/UserConfig.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <climits>
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

// 公屏聊天 sender 反查:走 EntityManager 单链表(AOI 范围内的 CUser),按 +0x70(userId)
// 匹配。比 rb-tree FindUserById 稳得多(后者跟 insert/delete 抢锁会爆)。AOI 之外的
// 玩家(野外大区)拿不到名字 — 调用方 fallback 显示 User#<id>。返回 UTF-8。
std::string LookupAroundPlayerNameById(uint32_t userId)
{
    if (!userId)
        return {};

    auto p = GetLocalUserPtr();
    if (p)
    {
        uint32_t selfId = 0;
        if (SafeReadDword(p + kUserSelfIdOffset, selfId) && selfId == userId)
        {
            char nameBuf[kNameMaxLen + 1] = {};
            if (SafeReadString(p + kNameOffset, nameBuf, sizeof(nameBuf)) && nameBuf[0])
                return Big5ToUtf8(nameBuf);
        }
    }

    auto mgrAddrPtr = PatternResolver::Get("EntityManagerPtr");
    if (!mgrAddrPtr)
        return {};
    uint32_t mgr = 0;
    if (!SafeReadDword(mgrAddrPtr, mgr) || !mgr)
        return {};

    uint32_t node = 0;
    if (!SafeReadDword(mgr + kUserMgrListHeadOffset, node))
        return {};

    constexpr int kMaxNodes = 1024;
    for (int visited = 0; visited < kMaxNodes && node; ++visited)
    {
        uint32_t nodeId = 0;
        if (SafeReadDword(node + kUserSelfIdOffset, nodeId) && nodeId == userId)
        {
            char nameBuf[kNameMaxLen + 1] = {};
            if (SafeReadString(node + kNameOffset, nameBuf, sizeof(nameBuf)) && nameBuf[0])
                return Big5ToUtf8(nameBuf);
            return {};
        }
        if (!SafeReadDword(node + kUserNextOffset, node))
            break;
    }
    return {};
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
                                   const void *buf, size_t size)
{
    __try
    {
        void *netBuf = pBegin();
        if (!netBuf)
            return -1;
        return pSend(netBuf, kRoutingTagSkill, buf, size);
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
    if (safetyCheck && !IsLocalDead())
    {
        spdlog::warn("GGTB::ReviveToTown: player not dead (hp={})", GetLocalHp());
        return false;
    }

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
} // namespace

bool SendPublicChat(const char *message)
{
    if (!message || !*message)
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
    int rv = CallSendPublicChatSEH(pBegin, pSend, message);
    if (rv < 0)
    {
        spdlog::warn("GGTB::SendPublicChat: SEH caught");
        return false;
    }
    spdlog::info("GGTB::SendPublicChat: msg='{}' rv={}", message, rv);
    return true;
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

bool SendDialogSelect(uint32_t npcId, uint32_t dialogOption)
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

    // Engine arg order is (a3=dialogOption, a4=npcId) — verified 2026-05-09.
    // See CLocalPlayer.h doc + IDA Net__SendDialogSelect @ 0xB2C930.
    int rv = CallSendDialogSelectSEH(pBegin, pSend, kProtocolDialogSelect,
                                     static_cast<int>(dialogOption),
                                     static_cast<int>(npcId),
                                     kDialogSelectSubAction);
    if (rv < 0)
    {
        spdlog::warn("GGTB::SendDialogSelect: SEH npc={} opt={}", npcId, dialogOption);
        return false;
    }
    spdlog::info("GGTB::SendDialogSelect: npc={} opt={} sub={}",
                 npcId, dialogOption, kDialogSelectSubAction);
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

} // namespace GGTB
