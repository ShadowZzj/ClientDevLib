#include "CLocalPlayer.h"
#include "../util/PatternResolver.h"
#include "../util/UserConfig.h"
#include "../util/NetLog.h"
#include "../modules/PickupFilter.h"
#include <Detours/build/include/detours.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <atomic>
#include <cmath>
#include <ctime>
#include <limits>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <TlHelp32.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
//  Move-while-acting (split into 普攻移动 / 技能移动)
//
//  SetAfterAction (0x7539E0) discriminates skill vs attack by SkillMode
//  (this+0x370C): {3,5,8} = a skill is in progress -> reject at gate 2
//  (loc_753A54); a normal attack runs at 370C==0 and falls through to the
//  non-skill path (loc_753A80) where the generic ready/anim gates reject it.
//  So the two reject paths are patched INDEPENDENTLY (each a 5-byte jmp to the
//  success block 0x753CC4), giving leak-free attack/skill toggles.
//
//  The 5 TraceMove / MoveSync / Status21
//  execution-path gates are SHARED and refcounted (patched when EITHER toggle is
//  on, restored when BOTH are off) — they only advance a move SetAfterAction
//  already queued, so sharing them never leaks. See CLocalPlayer.h for the full
//  rationale and per-site byte tables.
// ============================================================

// --- per-toggle SetAfterAction gates ---
static BYTE s_attackMoveGateOrig   [kAttackMoveGateSize]        = {};
static BYTE s_skillMoveGateOrig    [kSkillMoveGateSize]         = {};
static bool s_attackMoveGateOrigCaptured   = false;
static bool s_skillMoveGateOrigCaptured    = false;

// --- shared gates (stun entry gate + execution-path gates) ---
static BYTE s_sharedMoveStunGateOrig    [kSharedMoveStunGateSize]     = {};
static BYTE s_sharedMoveTraceGate1Orig  [kSharedMoveTraceGate1Size]   = {};
static BYTE s_sharedMoveTraceOrChainOrig[kSharedMoveTraceOrChainSize] = {};
static BYTE s_sharedMoveTraceGate2Orig  [kSharedMoveTraceGate2Size]   = {};
static BYTE s_sharedMoveSyncGateOrig    [kSharedMoveSyncGateSize]     = {};
static BYTE s_sharedMoveStatus21GateOrig[kSharedMoveStatus21GateSize] = {};
static bool s_sharedMoveStunGateOrigCaptured     = false;
static bool s_sharedMoveTraceGate1OrigCaptured   = false;
static bool s_sharedMoveTraceOrChainOrigCaptured = false;
static bool s_sharedMoveTraceGate2OrigCaptured   = false;
static bool s_sharedMoveSyncGateOrigCaptured     = false;
static bool s_sharedMoveStatus21GateOrigCaptured = false;

static bool s_attackMovePatched = false;
static bool s_skillMovePatched  = false;
static bool s_sharedMovePatched = false;

// Code cave for SkillMove's combo-aware gate. Allocated lazily on first patch and
// kept for the process lifetime (never freed) so the game thread can never be mid-
// execution inside it when we restore. 28 bytes of stub; 64 is plenty.
static void *s_skillMoveCave = nullptr;

// Code caves for the two shared TraceMove recovery-timer gates. Same lifetime rule
// as s_skillMoveCave (never freed): a restore only rewrites the 9-byte hook back to
// the original comiss+jbe; any thread still inside a cave returns via its own E9 to
// live game code, so leaving the cave mapped is safe.
static void *s_traceGate1Cave = nullptr;
static void *s_traceGate2Cave = nullptr;

// 9-byte hook: `E9 rel32` -> cave, padded with 4 NOPs (overwrites comiss(7)+jbe(2)).
static void MakeTrampoline9(BYTE *out, uintptr_t hookVA, uintptr_t target)
{
    out[0] = 0xE9;
    int32_t rel = static_cast<int32_t>(target - (hookVA + 5));
    memcpy(&out[1], &rel, 4);
    out[5] = out[6] = out[7] = out[8] = 0x90;
}

// Build a cave that reproduces a TraceMove recovery-timer gate
//   `comiss [this+recoveryOff], 0.0 ; jbe continueVA else nomoveVA`
// but honors the timer ONLY while a combo chain is live (this+0x2BDC > 0). For a
// plain skill/attack (comboState==0) it bypasses the timer (always continue) so
// move-while-cast is preserved. During a combo the ORIGINAL recovery is honored,
// so forced movement can't cancel the per-cast recovery window and 123.dll's
// auto-combo can't re-fire every frame (packet flood -> disconnect). This is the
// real fix for BOTH 普攻移动 and 技能移动 bursting, because both share these gates.
// Position-independent: rel32s are computed from the runtime cave/target addresses
// and the 0.0 compare uses `xorps xmm1,xmm1` (no absolute g_kReadyFactorZero ref).
static void *BuildComboRecoveryGateCave(uint32_t recoveryOff, uintptr_t continueVA,
                                        uintptr_t nomoveVA)
{
    void *cave = VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!cave)
        return nullptr;
    const uintptr_t c = reinterpret_cast<uintptr_t>(cave);
    BYTE s[64];
    size_t n = 0;
    s[n++] = 0x8B; s[n++] = 0x55; s[n++] = 0xF0;                          // mov edx,[ebp-10h] ; this
    s[n++] = 0x83; s[n++] = 0xBA;                                         // cmp dword ptr
    s[n++] = 0xDC; s[n++] = 0x2B; s[n++] = 0x00; s[n++] = 0x00;           //   [edx+2BDCh],
    s[n++] = 0x00;                                                        //   0  ; comboState
    s[n++] = 0x7E; size_t jleFix = n; s[n++] = 0x00;                      // jle  L (<=0 -> bypass)
    s[n++] = 0xF3; s[n++] = 0x0F; s[n++] = 0x10; s[n++] = 0x82;           // movss xmm0,[edx+recoveryOff]
    memcpy(&s[n], &recoveryOff, 4); n += 4;
    s[n++] = 0x0F; s[n++] = 0x57; s[n++] = 0xC9;                          // xorps xmm1,xmm1  ; 0.0
    s[n++] = 0x0F; s[n++] = 0x2F; s[n++] = 0xC1;                          // comiss xmm0,xmm1
    s[n++] = 0x76; size_t jbeFix = n; s[n++] = 0x00;                      // jbe  L (timer<=0 -> continue)
    s[n++] = 0xE9;                                                        // jmp nomoveVA
    { int32_t rel = static_cast<int32_t>(nomoveVA - (c + n + 4)); memcpy(&s[n], &rel, 4); n += 4; }
    size_t L = n;
    s[n++] = 0xE9;                                                        // L: jmp continueVA
    { int32_t rel = static_cast<int32_t>(continueVA - (c + n + 4)); memcpy(&s[n], &rel, 4); n += 4; }
    s[jleFix] = static_cast<BYTE>(L - (jleFix + 1));
    s[jbeFix] = static_cast<BYTE>(L - (jbeFix + 1));
    memcpy(cave, s, n);
    FlushInstructionCache(GetCurrentProcess(), cave, n);
    return cave;
}

static bool ApplySharedMoveSites()
{
    if (s_sharedMovePatched)
        return true;

    auto addrStunGate   = PatternResolver::Get("SharedMoveStunGate");
    auto addrTraceGate1 = PatternResolver::Get("SharedMoveTraceGate1");
    auto addrTraceOr    = PatternResolver::Get("SharedMoveTraceOrChain");
    auto addrTraceGate2 = PatternResolver::Get("SharedMoveTraceGate2");
    auto addrMoveSync   = PatternResolver::Get("SharedMoveSyncGate");
    auto addrStatus21   = PatternResolver::Get("SharedMoveStatus21Gate");
    if (!addrStunGate || !addrTraceGate1 || !addrTraceOr || !addrTraceGate2 || !addrMoveSync || !addrStatus21)
    {
        spdlog::error("GGTB: SharedMove pattern(s) unresolved (stun={:x} g1={:x} or={:x} g2={:x} ms={:x} s21={:x})",
                      addrStunGate, addrTraceGate1, addrTraceOr, addrTraceGate2, addrMoveSync, addrStatus21);
        return false;
    }

    // The two TraceMove recovery-timer gates (+0x3468 @TraceGate1, +0x2BCC
    // @TraceGate2) are NO LONGER flipped to an unconditional jmp. A blanket bypass
    // let movement advance through a combo's per-cast recovery window, cancelling
    // the recovery so 123.dll's auto-combo re-fired every frame -> packet flood ->
    // disconnect (both toggles triggered it, since both share these sites). Each
    // gate is instead redirected to a code cave that honors the timer while a combo
    // chain is live (comboState this+0x2BDC > 0) and only bypasses it for a plain
    // skill/attack (==0), so move-while-cast survives but the combo can't burst.
    // The hook overwrites comiss(7)+jbe(2) at addr-7, so the pattern still points at
    // the jbe; continue = addr+0x20 (0x...FCA/0x...11D), nomove = addr+2.
    if (!s_traceGate1Cave)
        s_traceGate1Cave = BuildComboRecoveryGateCave(0x3468, addrTraceGate1 + 0x20, addrTraceGate1 + 2);
    if (!s_traceGate2Cave)
        s_traceGate2Cave = BuildComboRecoveryGateCave(0x2BCC, addrTraceGate2 + 0x20, addrTraceGate2 + 2);
    if (!s_traceGate1Cave || !s_traceGate2Cave)
    {
        spdlog::error("GGTB: SharedMove combo-gate cave alloc failed");
        return false;
    }

    // EB = jmp short (preserves 0x22 disp) — stun gate falls THROUGH to gate 2.
    BYTE patchStunGate  [kSharedMoveStunGateSize]     = {0xEB};
    // E9 rel32 -> combo-gate cave (overwrites comiss+jbe at addr-7), NOP tail.
    BYTE patchTraceGate1[kSharedMoveTraceGate1Size];
    MakeTrampoline9(patchTraceGate1, addrTraceGate1 - 7, reinterpret_cast<uintptr_t>(s_traceGate1Cave));
    // E9 19 01 00 00 = jmp +0x119 (0x756FCD -> 0x7570EB), skip the anim/skill OR-chain.
    BYTE patchTraceOr   [kSharedMoveTraceOrChainSize] = {0xE9, 0x19, 0x01, 0x00, 0x00};
    BYTE patchTraceGate2[kSharedMoveTraceGate2Size];
    MakeTrampoline9(patchTraceGate2, addrTraceGate2 - 7, reinterpret_cast<uintptr_t>(s_traceGate2Cave));
    // EB = jmp short (preserves 0x07 disp) — SendPlayerMoveSyncPacket m_bCanMove gate.
    BYTE patchMoveSync  [kSharedMoveSyncGateSize]     = {0xEB};
    // EB = jmp short (preserves 0x1E disp) — TraceMove inner status-21 gate.
    BYTE patchStatus21  [kSharedMoveStatus21GateSize] = {0xEB};

    bool allOk = true;
    if (!WriteBytes(addrStunGate, patchStunGate, kSharedMoveStunGateSize,
                    s_sharedMoveStunGateOrig, s_sharedMoveStunGateOrigCaptured))
        allOk = false;
    if (!WriteBytes(addrTraceGate1 - 7, patchTraceGate1, kSharedMoveTraceGate1Size,
                    s_sharedMoveTraceGate1Orig, s_sharedMoveTraceGate1OrigCaptured))
        allOk = false;
    if (!WriteBytes(addrTraceOr, patchTraceOr, kSharedMoveTraceOrChainSize,
                    s_sharedMoveTraceOrChainOrig, s_sharedMoveTraceOrChainOrigCaptured))
        allOk = false;
    if (!WriteBytes(addrTraceGate2 - 7, patchTraceGate2, kSharedMoveTraceGate2Size,
                    s_sharedMoveTraceGate2Orig, s_sharedMoveTraceGate2OrigCaptured))
        allOk = false;
    if (!WriteBytes(addrMoveSync, patchMoveSync, kSharedMoveSyncGateSize,
                    s_sharedMoveSyncGateOrig, s_sharedMoveSyncGateOrigCaptured))
        allOk = false;
    if (!WriteBytes(addrStatus21, patchStatus21, kSharedMoveStatus21GateSize,
                    s_sharedMoveStatus21GateOrig, s_sharedMoveStatus21GateOrigCaptured))
        allOk = false;

    if (allOk)
    {
        s_sharedMovePatched = true;
        spdlog::info("GGTB: SharedMove sites patched (TraceMove gates combo-gated)");
    }
    return allOk;
}

// Restore the shared sites only when NEITHER toggle still needs them.
static bool RestoreSharedMoveSitesIfUnused()
{
    if (!s_sharedMovePatched)
        return true;
    if (s_attackMovePatched || s_skillMovePatched)
        return true;

    auto addrStunGate   = PatternResolver::Get("SharedMoveStunGate");
    auto addrTraceGate1 = PatternResolver::Get("SharedMoveTraceGate1");
    auto addrTraceOr    = PatternResolver::Get("SharedMoveTraceOrChain");
    auto addrTraceGate2 = PatternResolver::Get("SharedMoveTraceGate2");
    auto addrMoveSync   = PatternResolver::Get("SharedMoveSyncGate");
    auto addrStatus21   = PatternResolver::Get("SharedMoveStatus21Gate");

    bool allOk = true;
    if (!RestoreBytes(addrStunGate, kSharedMoveStunGateSize,
                      s_sharedMoveStunGateOrig, s_sharedMoveStunGateOrigCaptured))
        allOk = false;
    if (!RestoreBytes(addrTraceGate1 - 7, kSharedMoveTraceGate1Size,
                      s_sharedMoveTraceGate1Orig, s_sharedMoveTraceGate1OrigCaptured))
        allOk = false;
    if (!RestoreBytes(addrTraceOr, kSharedMoveTraceOrChainSize,
                      s_sharedMoveTraceOrChainOrig, s_sharedMoveTraceOrChainOrigCaptured))
        allOk = false;
    if (!RestoreBytes(addrTraceGate2 - 7, kSharedMoveTraceGate2Size,
                      s_sharedMoveTraceGate2Orig, s_sharedMoveTraceGate2OrigCaptured))
        allOk = false;
    if (!RestoreBytes(addrMoveSync, kSharedMoveSyncGateSize,
                      s_sharedMoveSyncGateOrig, s_sharedMoveSyncGateOrigCaptured))
        allOk = false;
    if (!RestoreBytes(addrStatus21, kSharedMoveStatus21GateSize,
                      s_sharedMoveStatus21GateOrig, s_sharedMoveStatus21GateOrigCaptured))
        allOk = false;

    if (allOk)
    {
        s_sharedMovePatched = false;
        spdlog::info("GGTB: SharedMove sites restored");
    }
    return allOk;
}

bool PatchAttackMove()
{
    if (s_attackMovePatched)
        return true;

    auto addrGate = PatternResolver::Get("AttackMoveGate");
    if (!addrGate)
    {
        spdlog::error("GGTB: AttackMoveGate pattern unresolved");
        return false;
    }

    // E9 3F 02 00 00 = jmp +0x23F (0x753A80 -> 0x753CC4 success path).
    BYTE patchGate[kAttackMoveGateSize] = {0xE9, 0x3F, 0x02, 0x00, 0x00};
    if (!WriteBytes(addrGate, patchGate, kAttackMoveGateSize,
                    s_attackMoveGateOrig, s_attackMoveGateOrigCaptured))
        return false;

    if (!ApplySharedMoveSites())
    {
        RestoreBytes(addrGate, kAttackMoveGateSize,
                     s_attackMoveGateOrig, s_attackMoveGateOrigCaptured);
        return false;
    }

    s_attackMovePatched = true;
    spdlog::info("GGTB: AttackMove patched (gate={:x})", addrGate);
    return true;
}

bool RestoreAttackMove()
{
    if (!s_attackMovePatched)
        return true;

    auto addrGate = PatternResolver::Get("AttackMoveGate");
    bool allOk = RestoreBytes(addrGate, kAttackMoveGateSize,
                              s_attackMoveGateOrig, s_attackMoveGateOrigCaptured);
    if (allOk)
    {
        s_attackMovePatched = false;
        spdlog::info("GGTB: AttackMove restored");
    }
    if (!RestoreSharedMoveSitesIfUnused())
        allOk = false;
    return allOk;
}

bool PatchSkillMove()
{
    if (s_skillMovePatched)
        return true;

    auto addrGate = PatternResolver::Get("SkillMoveGate");
    if (!addrGate)
    {
        spdlog::error("GGTB: SkillMoveGate pattern unresolved");
        return false;
    }

    // Combo-aware gate. Instead of unconditionally jumping to the success block,
    // the gate jumps to a trampoline that inspects comboState (this+0x2BDC): >0
    // means a 连续技 (combo chain) is live, so we run the ORIGINAL reject (deny the
    // move -> recovery frames are NOT cancelled -> 123.dll auto-combo can't burst);
    // ==0 means a plain skill cast, so we jump to success (move-while-cast intact).
    //   0x2BDC is the combo-chain encoding the HUD's ComboNum indicator draws from
    //   (see CLocalUser__ExecuteComboSkill @0x97D200); no skill-cast path writes it,
    //   so a normal skill leaves it 0.
    if (!s_skillMoveCave)
    {
        s_skillMoveCave = VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE,
                                       PAGE_EXECUTE_READWRITE);
        if (!s_skillMoveCave)
        {
            spdlog::error("GGTB: SkillMove cave alloc failed");
            return false;
        }
    }

    const uintptr_t cave      = reinterpret_cast<uintptr_t>(s_skillMoveCave);
    const uintptr_t successVA = addrGate + 0x270; // 0x753A54 -> 0x753CC4 success block
    const uintptr_t rejectVA  = addrGate + 0x9;   // 0x753A54 -> 0x753A5D (push ecx), 原拒绝块

    BYTE stub[28];
    size_t n = 0;
    stub[n++] = 0x8B; stub[n++] = 0x45; stub[n++] = 0xFC;             // mov eax,[ebp-4]  ; this
    stub[n++] = 0x83; stub[n++] = 0xB8;                               // cmp dword ptr
    stub[n++] = 0xDC; stub[n++] = 0x2B; stub[n++] = 0x00; stub[n++] = 0x00; //   [eax+2BDCh],
    stub[n++] = 0x00;                                                 //   0        ; comboState
    stub[n++] = 0x7F; stub[n++] = 0x05;                              // jg  do_reject (+5)
    stub[n++] = 0xE9;                                                 // jmp successVA
    {
        int32_t rel = static_cast<int32_t>(successVA - (cave + n + 4));
        memcpy(&stub[n], &rel, 4); n += 4;
    }
    stub[n++] = 0x8B; stub[n++] = 0x88;                               // do_reject: mov ecx,
    stub[n++] = 0x0C; stub[n++] = 0x37; stub[n++] = 0x00; stub[n++] = 0x00; //   [eax+370Ch]
    stub[n++] = 0xE9;                                                 // jmp rejectVA
    {
        int32_t rel = static_cast<int32_t>(rejectVA - (cave + n + 4));
        memcpy(&stub[n], &rel, 4); n += 4;
    }
    memcpy(s_skillMoveCave, stub, n);
    FlushInstructionCache(GetCurrentProcess(), s_skillMoveCave, n);

    // Gate: E9 rel32 -> cave (replaces `mov eax,[ebp-4]; mov ecx,[eax+370Ch]`).
    BYTE patchGate[kSkillMoveGateSize] = {0xE9};
    {
        int32_t rel = static_cast<int32_t>(cave - (addrGate + 5));
        memcpy(&patchGate[1], &rel, 4);
    }

    if (!WriteBytes(addrGate, patchGate, kSkillMoveGateSize,
                    s_skillMoveGateOrig, s_skillMoveGateOrigCaptured))
        return false;

    if (!ApplySharedMoveSites())
    {
        RestoreBytes(addrGate, kSkillMoveGateSize,
                     s_skillMoveGateOrig, s_skillMoveGateOrigCaptured);
        return false;
    }

    s_skillMovePatched = true;
    spdlog::info("GGTB: SkillMove patched (gate={:x} cave={:x}, combo-aware)",
                 addrGate, reinterpret_cast<uintptr_t>(s_skillMoveCave));
    return true;
}

bool RestoreSkillMove()
{
    if (!s_skillMovePatched)
        return true;

    auto addrGate = PatternResolver::Get("SkillMoveGate");

    bool allOk = true;
    if (!RestoreBytes(addrGate, kSkillMoveGateSize,
                      s_skillMoveGateOrig, s_skillMoveGateOrigCaptured))
        allOk = false;

    if (allOk)
    {
        s_skillMovePatched = false;
        spdlog::info("GGTB: SkillMove restored");
    }
    if (!RestoreSharedMoveSitesIfUnused())
        allOk = false;
    return allOk;
}

// ============================================================
//  No-Shift player attack — bypass the live 123.dll VM hook at the DIK_LSHIFT
//  read and land on CLocalUser__CheckPkAttackEligible's original Shift-success
//  block. Other PK eligibility gates in the same predicate are untouched.
// ============================================================

static BYTE s_noShiftAttackEntryOrig[kNoShiftAttackEntryPatchSize] = {};
static bool s_noShiftAttackEntryOrigCaptured                       = false;
static bool s_noShiftAttackPatched                                 = false;

static bool ValidateNoShiftAttackSiteSEH(uintptr_t entry, uintptr_t accept)
{
    __try
    {
        auto *site = reinterpret_cast<const volatile BYTE *>(entry);
        if (site[8] != 0x85 || site[9] != 0xC9 ||
            (site[10] != 0x75 && site[10] != 0xEB) || site[11] != 0x1B ||
            site[12] != 0x8B || site[13] != 0x55 || site[14] != 0x08)
            return false;

        if (site[0] == 0xE9)
        {
            const auto rel = *reinterpret_cast<const volatile int32_t *>(entry + 1);
            return entry + kNoShiftAttackEntryPatchSize + rel != accept;
        }
        return site[0] == 0x0F && site[1] == 0xB6;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool PatchNoShiftAttack()
{
    if (s_noShiftAttackPatched)
        return true;

    auto entry = PatternResolver::Get("NoShiftAttackShiftReadSite");
    if (!entry)
    {
        spdlog::error("GGTB: NoShiftAttackShiftReadSite unresolved");
        return false;
    }

    const uintptr_t accept = entry + kNoShiftAttackAcceptOffset;
    if (!ValidateNoShiftAttackSiteSEH(entry, accept))
    {
        spdlog::error("GGTB: NoShiftAttack site validation failed @ {:x}", entry);
        return false;
    }

    BYTE            patch[kNoShiftAttackEntryPatchSize] = {0xE9};
    const auto rel = static_cast<int32_t>(accept - (entry + kNoShiftAttackEntryPatchSize));
    memcpy(patch + 1, &rel, sizeof(rel));

    if (!WriteBytes(entry, patch, kNoShiftAttackEntryPatchSize,
                    s_noShiftAttackEntryOrig, s_noShiftAttackEntryOrigCaptured))
        return false;
    if (!FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(entry),
                               kNoShiftAttackEntryPatchSize))
    {
        RestoreBytes(entry, kNoShiftAttackEntryPatchSize,
                     s_noShiftAttackEntryOrig, s_noShiftAttackEntryOrigCaptured);
        s_noShiftAttackEntryOrigCaptured = false;
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(entry),
                              kNoShiftAttackEntryPatchSize);
        spdlog::error("GGTB: NoShiftAttack entry cache flush failed @ {:x}", entry);
        return false;
    }

    s_noShiftAttackPatched = true;
    spdlog::info("GGTB: NoShiftAttack patched @ {:x} -> {:x}", entry, accept);
    return true;
}

bool RestoreNoShiftAttack()
{
    if (!s_noShiftAttackPatched)
        return true;

    auto entry = PatternResolver::Get("NoShiftAttackShiftReadSite");
    if (!entry)
        return false;

    if (!RestoreBytes(entry, kNoShiftAttackEntryPatchSize,
                      s_noShiftAttackEntryOrig, s_noShiftAttackEntryOrigCaptured))
        return false;

    bool flushed = FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(entry),
                                         kNoShiftAttackEntryPatchSize) != FALSE;
    s_noShiftAttackEntryOrigCaptured = false;
    s_noShiftAttackPatched = false;
    if (flushed)
        spdlog::info("GGTB: NoShiftAttack restored");
    else
        spdlog::error("GGTB: NoShiftAttack restore cache flush failed @ {:x}", entry);
    return flushed;
}

// ============================================================
//  Ignore crowd-control (眩晕/沉默 bypass)
//
//  4 in-place branch flips that defeat the stun(18)/silence(19) gates at the
//  item-use and skill-key entry points. See CLocalPlayer.h header for the full
//  rationale and the exact original/patch bytes.
//
//    ItemStunGate     @ 0x9A4865: 7E -> EB   (jle -> jmp short, keep disp 05)
//    CastStunGate     @ 0x9A6410: 7F 1A -> 90 90 (jg -> nop nop)
//    CastSilenceGate  @ 0x9A642A: 7E -> EB   (jle -> jmp short, keep disp 05)
//    CastStunTimeGate @ 0x9A646E: 76 -> EB   (jbe -> jmp short, keep disp 05)
// ============================================================

static BYTE s_ignoreCCItemStunGateOrig    [kIgnoreCCItemStunGateSize]     = {};
static BYTE s_ignoreCCCastStunGateOrig     [kIgnoreCCCastStunGateSize]     = {};
static BYTE s_ignoreCCCastSilenceGateOrig  [kIgnoreCCCastSilenceGateSize]  = {};
static BYTE s_ignoreCCCastStunTimeGateOrig [kIgnoreCCCastStunTimeGateSize] = {};
static bool s_ignoreCCItemStunGateOrigCaptured    = false;
static bool s_ignoreCCCastStunGateOrigCaptured     = false;
static bool s_ignoreCCCastSilenceGateOrigCaptured  = false;
static bool s_ignoreCCCastStunTimeGateOrigCaptured = false;
static bool s_ignoreCCPatched = false;

bool PatchIgnoreCC()
{
    if (s_ignoreCCPatched)
        return true;

    auto addrItem      = PatternResolver::Get("IgnoreCCItemStunGate");
    auto addrCastStun  = PatternResolver::Get("IgnoreCCCastStunGate");
    auto addrCastSil   = PatternResolver::Get("IgnoreCCCastSilenceGate");
    auto addrCastTime  = PatternResolver::Get("IgnoreCCCastStunTimeGate");
    if (!addrItem || !addrCastStun || !addrCastSil || !addrCastTime)
    {
        spdlog::error("GGTB: IgnoreCC pattern(s) unresolved (item={:x} stun={:x} sil={:x} time={:x})",
                      addrItem, addrCastStun, addrCastSil, addrCastTime);
        return false;
    }

    BYTE patchItem    [kIgnoreCCItemStunGateSize]     = {0xEB};       // jle -> jmp short
    BYTE patchCastStun[kIgnoreCCCastStunGateSize]     = {0x90, 0x90}; // jg  -> nop nop
    BYTE patchCastSil [kIgnoreCCCastSilenceGateSize]  = {0xEB};       // jle -> jmp short
    BYTE patchCastTime[kIgnoreCCCastStunTimeGateSize] = {0xEB};       // jbe -> jmp short

    bool allOk = true;
    if (!WriteBytes(addrItem, patchItem, kIgnoreCCItemStunGateSize,
                    s_ignoreCCItemStunGateOrig, s_ignoreCCItemStunGateOrigCaptured))
        allOk = false;
    if (!WriteBytes(addrCastStun, patchCastStun, kIgnoreCCCastStunGateSize,
                    s_ignoreCCCastStunGateOrig, s_ignoreCCCastStunGateOrigCaptured))
        allOk = false;
    if (!WriteBytes(addrCastSil, patchCastSil, kIgnoreCCCastSilenceGateSize,
                    s_ignoreCCCastSilenceGateOrig, s_ignoreCCCastSilenceGateOrigCaptured))
        allOk = false;
    if (!WriteBytes(addrCastTime, patchCastTime, kIgnoreCCCastStunTimeGateSize,
                    s_ignoreCCCastStunTimeGateOrig, s_ignoreCCCastStunTimeGateOrigCaptured))
        allOk = false;

    if (allOk)
    {
        s_ignoreCCPatched = true;
        spdlog::info("GGTB: IgnoreCC patched (item={:x} stun={:x} sil={:x} time={:x})",
                     addrItem, addrCastStun, addrCastSil, addrCastTime);
    }
    return allOk;
}

bool RestoreIgnoreCC()
{
    if (!s_ignoreCCPatched)
        return true;

    auto addrItem      = PatternResolver::Get("IgnoreCCItemStunGate");
    auto addrCastStun  = PatternResolver::Get("IgnoreCCCastStunGate");
    auto addrCastSil   = PatternResolver::Get("IgnoreCCCastSilenceGate");
    auto addrCastTime  = PatternResolver::Get("IgnoreCCCastStunTimeGate");

    bool allOk = true;
    if (!RestoreBytes(addrItem, kIgnoreCCItemStunGateSize,
                      s_ignoreCCItemStunGateOrig, s_ignoreCCItemStunGateOrigCaptured))
        allOk = false;
    if (!RestoreBytes(addrCastStun, kIgnoreCCCastStunGateSize,
                      s_ignoreCCCastStunGateOrig, s_ignoreCCCastStunGateOrigCaptured))
        allOk = false;
    if (!RestoreBytes(addrCastSil, kIgnoreCCCastSilenceGateSize,
                      s_ignoreCCCastSilenceGateOrig, s_ignoreCCCastSilenceGateOrigCaptured))
        allOk = false;
    if (!RestoreBytes(addrCastTime, kIgnoreCCCastStunTimeGateSize,
                      s_ignoreCCCastStunTimeGateOrig, s_ignoreCCCastStunTimeGateOrigCaptured))
        allOk = false;

    if (allOk)
    {
        s_ignoreCCPatched = false;
        spdlog::info("GGTB: IgnoreCC restored");
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

bool SafeWriteFloat(uintptr_t addr, float v)
{
    __try
    {
        *reinterpret_cast<volatile float *>(addr) = v;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SafeWriteDword(uintptr_t addr, uint32_t v)
{
    __try
    {
        *reinterpret_cast<volatile uint32_t *>(addr) = v;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SafeWriteByte(uintptr_t addr, uint8_t v)
{
    __try
    {
        *reinterpret_cast<volatile uint8_t *>(addr) = v;
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

// 走 EntityManager AOI 单链表,筛出正在摆个人摊的玩家(CUser+0x2DEC!=0)。与
// GetAroundPlayers 同一遍历范式,只是过滤条件换成摊位标志,并额外读店铺名/类型/userId。
std::vector<StallPlayer> GetNearbyStallPlayers(float maxDistance)
{
    std::vector<StallPlayer> result;

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

    constexpr int kMaxNodes = 1024;
    for (int visited = 0; visited < kMaxNodes && node; ++visited)
    {
        do
        {
            if (localUser && node == localUser)
                break; // 跳过自己(自己摆摊不需要点开)

            // 不按 kUserKindOffset==5 过滤:实测这条 EntityManager 链表里的玩家
            // 节点 +0x1AE8 几乎全是 0(市场里 115/119 个节点 kind=0,只有 1 个=5),
            // 用 kind==5 会把所有摊主滤掉。摆摊标志位本身就只在玩家身上,直接用它判定。
            uint32_t stallFlag = 0;
            if (!SafeReadDword(node + kUserStallFlagOffset, stallFlag) || (stallFlag & 0xFF) == 0)
                break; // 不在摆摊

            float x = 0, y = 0, z = 0;
            if (!SafeReadFloat(node + kPositionXOffset, x) ||
                !SafeReadFloat(node + kPositionYOffset, y) ||
                !SafeReadFloat(node + kPositionZOffset, z))
                break;

            float dx = x - lx, dy = y - ly, dz = z - lz;
            float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (maxDistance > 0.0f && distance > maxDistance)
                break;

            uint32_t userId = 0;
            if (!SafeReadDword(node + kUserSelfIdOffset, userId) || !userId)
                break;

            char nameBuf[kNameMaxLen + 1] = {};
            if (!SafeReadString(node + kNameOffset, nameBuf, sizeof(nameBuf)) || !nameBuf[0])
                break;

            char stallBuf[kUserStallNameMaxLen + 1] = {};
            SafeReadString(node + kUserStallNameOffset, stallBuf, sizeof(stallBuf));

            uint32_t stallType = 0;
            SafeReadDword(node + kUserStallTypeOffset, stallType);

            StallPlayer sp;
            sp.name      = Big5ToUtf8(nameBuf);
            sp.stallName = stallBuf[0] ? Big5ToUtf8(stallBuf) : std::string{};
            sp.userId    = userId;
            sp.stallType = stallType;
            sp.distance  = distance;
            sp.x = x;
            sp.y = y;
            sp.z = z;
            result.push_back(std::move(sp));
        } while (false);

        if (!SafeReadDword(node + kUserNextOffset, node))
            break;
    }

    std::sort(result.begin(), result.end(),
              [](const StallPlayer &a, const StallPlayer &b) { return a.distance < b.distance; });
    return result;
}

// ============================================================
//  Drop-item iteration + auto-pickup helper
// ============================================================


std::vector<DropItemInfo> GetNearbyDropItems(float maxDistance,
                                             bool  includeUnpickable,
                                             bool *ready)
{
    std::vector<DropItemInfo> result;
    if (ready)
        *ready = false;

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

    // A successful head read (including a null head for a genuinely empty
    // container) plus a resolved local position distinguishes "no drops" from
    // a transiently unavailable ItemContainer for remote workflow callers.
    // Existing AutoPickup callers do not request this readiness bit and keep
    // their prior best-effort behavior.
    if (ready)
        *ready = haveLocal;

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
using LookupDropItemFn = uintptr_t(__thiscall *)(void *, int);

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

static uintptr_t CallLookupDropItemSEH(LookupDropItemFn fn, void *container,
                                       uint32_t dropId)
{
    __try
    {
        return fn(container, static_cast<int>(dropId));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return UINTPTR_MAX;
    }
}
} // namespace

DropItemLookupState QueryDropItemById(uint32_t dropId, DropItemInfo &out)
{
    out = {};
    if (!dropId)
        return DropItemLookupState::Unavailable;

    auto containerPtrAddr = PatternResolver::Get("ItemContainerPtr");
    auto lookupAddr = PatternResolver::Get("LookupDropItemById");
    if (!containerPtrAddr || !lookupAddr)
        return DropItemLookupState::Unavailable;

    uint32_t container = 0;
    if (!SafeReadDword(containerPtrAddr, container) || !container)
        return DropItemLookupState::Unavailable;

    uintptr_t drop = CallLookupDropItemSEH(
        reinterpret_cast<LookupDropItemFn>(lookupAddr),
        reinterpret_cast<void *>(container), dropId);
    if (drop == UINTPTR_MAX)
        return DropItemLookupState::Unavailable;
    if (!drop)
        return DropItemLookupState::Missing;

    uint32_t actualDropId = 0;
    uint32_t itemId = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    uint8_t canPickByte = 0;
    if (!SafeReadDword(drop + kDropIdOffset, actualDropId) ||
        !SafeReadDword(drop + kDropItemIdOffset, itemId) ||
        !SafeReadFloat(drop + kDropPosXOffset, x) ||
        !SafeReadFloat(drop + kDropPosYOffset, y) ||
        !SafeReadFloat(drop + kDropPosZOffset, z) ||
        !SafeReadByte(drop + kDropCanPickOffset, canPickByte) ||
        actualDropId != dropId)
        return DropItemLookupState::Unavailable;

    float lx = 0.0f, ly = 0.0f, lz = 0.0f;
    if (!GetLocalPosition(lx, ly, lz))
        return DropItemLookupState::Unavailable;
    const float dx = x - lx;
    const float dy = y - ly;
    const float dz = z - lz;
    out = {actualDropId, itemId, std::sqrt(dx * dx + dy * dy + dz * dz),
           x, y, z, canPickByte != 0};
    return DropItemLookupState::Present;
}

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

    // type lives inside StatTable (statTable+0xDC), NOT on CCreature. Only real
    // monster types are valid attack targets — NPCs/teleports (type==3), trees
    // (0xA) and everything else are rejected. The old code matched NPC by
    // type==7 which is a DIFFERENT build's value, so on the live game it never
    // rejected NPCs (they only got dropped by the hp<=0 check below, by luck).
    uint32_t type = 0;
    if (!SafeReadDword(statTable + kStatTableTypeOffset, type))
        return 3;
    if (!IsMonsterType(type))
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

    out.push_back({monsterId, type, level, d, x, y, z, hp, cre});
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

// 分解宝石 —— 与 DropBagItem 同一套 NetSendTriple 机制,仅协议号不同 (411571)。
// NetSendTriple (Net__SendPacket16B_2DW_Tracked @ idb 0xB2C790) 对 411571 不在其
// dialog-track 白名单里,不产生额外副作用,发出的就是 [16][411571][wireBag][count]。
// 这和游戏原生分解 CG_SendDismantleGem_411571 (idb 0x71D300) 的字节完全一致。
bool SendDecomposeGem(uint32_t wireBagId, uint32_t count)
{
    if (wireBagId == 0 || count == 0)
        return false;

    auto beginAddr = PatternResolver::Get("NetBeginSend");
    auto sendAddr  = PatternResolver::Get("NetSendTriple");
    if (!beginAddr || !sendAddr)
    {
        spdlog::error("GGTB::SendDecomposeGem: pattern unresolved (begin={:x} send={:x})",
                      beginAddr, sendAddr);
        return false;
    }

    auto pBegin = reinterpret_cast<NetBeginSendFn>(beginAddr);
    auto pSend  = reinterpret_cast<NetSendTripleFn>(sendAddr);

    constexpr int kProtocolDismantleGem = 411571;
    int rv = CallSendTripleSEH(pBegin, pSend, kProtocolDismantleGem,
                               static_cast<int>(wireBagId),
                               static_cast<int>(count));
    if (rv < 0)
    {
        spdlog::warn("GGTB::SendDecomposeGem: SEH wireBag={} count={}", wireBagId, count);
        return false;
    }
    spdlog::info("GGTB::SendDecomposeGem: wireBag={} count={}", wireBagId, count);
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

bool TryGetLocalHp(int64_t &hp)
{
    hp = 0;
    auto p = GetLocalUserPtr();
    if (!p)
        return false;
    __try
    {
        uint32_t userId = *reinterpret_cast<volatile uint32_t *>(p + kUserSelfIdOffset);
        if (userId == 0 || userId == UINT32_MAX)
            return false;
        uint32_t raw = *reinterpret_cast<volatile uint32_t *>(p + kLocalUserHpOffset);
        uint32_t key = *reinterpret_cast<volatile uint32_t *>(kHpXorKeyAddr);
        hp = static_cast<int64_t>(raw ^ key);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        hp = 0;
        return false;
    }
    return true;
}

int64_t GetLocalHp()
{
    int64_t hp = 0;
    TryGetLocalHp(hp);
    return hp;
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
    int64_t hp = 0;
    return TryGetLocalHp(hp) && hp == 0;
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

// TerrainHeightAt(this=g_pCurMap, float X, float Y) -> float Z. __thiscall with
// two float stack args (verified off OnPlayerMoveClick @ 0x871D76 and the
// per-frame TraceMove tail sub_682E60 @ 0x6830C9: *(this+0x40)=fn(*(this+0x3C),
// *(this+0x44))). Returns NaN sentinel on SEH so the caller can skip the Z write.
using TerrainHeightFn = float(__thiscall *)(void *map, float x, float y);
static float CallTerrainHeightSEH(TerrainHeightFn fn, void *map, float x, float y)
{
    __try { return fn(map, x, y); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return std::numeric_limits<float>::quiet_NaN(); }
}

// Deref the g_pCurMap pointer slot under SEH (it's NULL/torn during scene
// transitions). Returns nullptr on any fault or null/sentinel value.
static void *ReadMapPtrSEH(uintptr_t slot)
{
    __try
    {
        uintptr_t p = *reinterpret_cast<uintptr_t *>(slot);
        if (p == 0 || p == 0xFFFFFFFF)
            return nullptr;
        return reinterpret_cast<void *>(p);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
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
    if (dialogOption == 9575)
    {
        spdlog::warn("GGTB::SendDialogSelect: opt=9575 requires targeted native dungeon entry");
        return false;
    }
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
namespace
{
constexpr uint32_t kNpcOrdinaryDialogUiId = 10;
constexpr uintptr_t kDialogScriptEntryChildListOffset = 0x018;
constexpr uintptr_t kDialogScriptListEntryOffset = 0x004;
constexpr uintptr_t kDialogScriptListNextOffset = 0x008;
constexpr int kDialogWarpTableType = 11;
constexpr uintptr_t kDialogWarpTableIdOffset = 0x008;
constexpr int kDialogScriptMaxDepth = 16;
constexpr int kDialogScriptMaxNodes = 256;

using DialogSealTableGetInstanceFn = void *(__cdecl *)();
using DialogSealTableGetElemFn = int(__thiscall *)(void *, int, int, int);

static bool QueryDialogWarpTableIdSEH(DialogSealTableGetInstanceFn getInstance,
                                      DialogSealTableGetElemFn getElem,
                                      uint32_t warpKey, uint32_t *tableId)
{
    __try
    {
        void *manager = getInstance();
        if (!manager) return false;
        int element = getElem(manager, kDialogWarpTableType, static_cast<int>(warpKey), 0);
        if (!element) return false;
        *tableId = *reinterpret_cast<uint32_t *>(element + kDialogWarpTableIdOffset);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool ResolveDialogScriptMeta(uint32_t scriptEntry, uint32_t *opt,
                                    uint32_t *warpKey, uint32_t *warpTableId)
{
    if (opt) *opt = 0;
    if (warpKey) *warpKey = 0;
    if (warpTableId) *warpTableId = 0;
    if (!scriptEntry) return true;

    uint32_t scriptCtx = 0;
    if (!SafeReadDword(scriptEntry + kDialogScriptEntryCtxOffset, scriptCtx) || !scriptCtx)
        return false;

    uint32_t resolvedOpt = 0;
    uint32_t resolvedWarpKey = 0;
    if (!SafeReadDword(scriptCtx + kDialogScriptCtxOptOffset, resolvedOpt) ||
        !SafeReadDword(scriptCtx + kDialogScriptCtxWarpKeyOffset, resolvedWarpKey))
        return false;

    if (opt) *opt = resolvedOpt;
    if (warpKey) *warpKey = resolvedWarpKey;

    uint32_t resolvedTableId = 0;
    if (resolvedWarpKey != 0)
    {
        auto getInstanceAddr = PatternResolver::Get("SealTableMgrGetInstance");
        auto getElemAddr = PatternResolver::Get("SealTableMgrGetTableElem");
        if (getInstanceAddr && getElemAddr)
        {
            QueryDialogWarpTableIdSEH(
                reinterpret_cast<DialogSealTableGetInstanceFn>(getInstanceAddr),
                reinterpret_cast<DialogSealTableGetElemFn>(getElemAddr),
                resolvedWarpKey, &resolvedTableId);
        }
    }

    if (warpTableId) *warpTableId = resolvedTableId;
    return true;
}

enum class DialogScriptSearchResult
{
    Invalid,
    NotFound,
    Found,
};

static DialogScriptSearchResult SearchDialogScriptForOpt(
    uint32_t scriptEntry, uint32_t targetOpt, int depth, int *visitedCount,
    std::unordered_set<uint32_t> &visited)
{
    if (!scriptEntry) return DialogScriptSearchResult::NotFound;
    if (depth > kDialogScriptMaxDepth || *visitedCount >= kDialogScriptMaxNodes ||
        !visited.insert(scriptEntry).second)
        return DialogScriptSearchResult::Invalid;
    ++*visitedCount;

    uint32_t opt = 0;
    if (!ResolveDialogScriptMeta(scriptEntry, &opt, nullptr, nullptr))
        return DialogScriptSearchResult::Invalid;
    if (opt == targetOpt) return DialogScriptSearchResult::Found;

    uint32_t child = 0;
    if (!SafeReadDword(scriptEntry + kDialogScriptEntryChildListOffset, child))
        return DialogScriptSearchResult::Invalid;
    for (int i = 0; child && i < kDialogScriptMaxNodes; ++i)
    {
        uint32_t childEntry = 0;
        uint32_t next = 0;
        if (!SafeReadDword(child + kDialogScriptListEntryOffset, childEntry) ||
            !SafeReadDword(child + kDialogScriptListNextOffset, next))
            return DialogScriptSearchResult::Invalid;
        auto childResult = SearchDialogScriptForOpt(
            childEntry, targetOpt, depth + 1, visitedCount, visited);
        if (childResult != DialogScriptSearchResult::NotFound) return childResult;
        child = next;
    }
    if (child) return DialogScriptSearchResult::Invalid;
    return DialogScriptSearchResult::NotFound;
}

static DialogScriptSearchResult SearchDialogScriptForOpt(uint32_t scriptEntry,
                                                         uint32_t targetOpt)
{
    int visitedCount = 0;
    std::unordered_set<uint32_t> visited;
    return SearchDialogScriptForOpt(scriptEntry, targetOpt, 0, &visitedCount, visited);
}
} // anonymous namespace

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
    auto foregroundAddr = PatternResolver::Get("ForegroundUIId");
    if (!stateAddr || !foregroundAddr)
        return snap;

    uint32_t foreground = 0;
    if (!SafeReadDword(foregroundAddr, foreground) ||
        foreground != kNpcOrdinaryDialogUiId)
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
            opt.tag = 0;
            opt.opt = 0;
            opt.warpKey = 0;
            opt.warpTableId = 0;

            char textBuf[kDialogOptionTextMaxLen + 1] = {};
            if (SafeReadString(node + kDialogOptionTextOffset, textBuf, sizeof(textBuf)) && textBuf[0])
                opt.text = Big5ToUtf8(textBuf);

            SafeReadDword(node + kDialogOptionTagOffset, opt.tag);
            // 解 opt: option_node+396 -> scriptEntry, +12 -> scriptCtx, +332 -> opt
            // 跟 OnNpcDialogOption_Quest 取值方式完全一致。
            ResolveDialogScriptMeta(opt.tag, &opt.opt, &opt.warpKey, &opt.warpTableId);
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
            ok.tag = 0;
            ok.opt = 0;
            ok.warpKey = 0;
            ok.warpTableId = 0;
            SafeReadDword(entry + kDialogOptionTagOffset, ok.tag);
            ResolveDialogScriptMeta(ok.tag, &ok.opt, &ok.warpKey, &ok.warpTableId);
            snap.options.push_back(std::move(ok));
        }
    }
    else // Choice (mode == 2)
    {
        // Mode 2's real click callback (0x8FAAB0) walks only state+1052
        // (0x41C). state+1056 is a confirm-wrapper build list, not a second
        // visible choice list.
        uint32_t choiceHead = 0;
        SafeReadDword(stateObj + kDialogStateChoiceHeadOffset, choiceHead);
        if (choiceHead) walkOptionList(choiceHead);
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
using NpcChooseDialogOptionFn  = char(__thiscall *)(uintptr_t state, uint32_t *resultOut,
                                                    int optionIndex);
using NativeGameLoopFn         = uintptr_t(__thiscall *)(void *gameHud, int active,
                                                         float frameDt);
using NativeDialogUpdateFn     = char(__thiscall *)(uintptr_t state, float frameDt,
                                                    uint32_t *resultOut);
using NativeWheelTalkFn        = uintptr_t(__thiscall *)(void *wheel);
using NativeFindCreatureFn     = uintptr_t(__thiscall *)(void *manager, int creatureId);

enum class NativeUiTaskKind : uint32_t
{
    TalkOrAttack,
    OpenNpcWheelDialog,
    DialogClick,
    SelectDropForPickup,
};

enum class NativeUiTaskPhase : uint32_t
{
    Queued,
    Executing,
    Completed,
    Cancelled,
};

enum class NativeUiWaitResult : uint32_t
{
    Completed,
    Unavailable,
    Stopping,
    QueueFull,
    TimedOutBeforeExecution,
    Cancelled,
    ExecutingAmbiguous,
};

struct NativeUiTask
{
    explicit NativeUiTask(NativeUiTaskKind taskKind) : kind(taskKind) {}

    NativeUiTaskKind              kind;
    std::atomic<NativeUiTaskPhase> phase{NativeUiTaskPhase::Queued};
    std::mutex                    waitMutex;
    std::condition_variable       waitCv;

    uint32_t npcId = 0;
    uint32_t dropId = 0;
    uint32_t expectedItemId = 0;
    uint32_t expectedMapId = 0;
    float maxDropDistance = 0.0f;
    uintptr_t expectedState = 0;
    uint32_t expectedMode = 0;
    uint32_t optionIndex = 0;
    uint32_t expectedNode = 0;
    uint32_t expectedScript = 0;
    uint32_t currentNodeScript = 0;
    uint32_t expectedPendingBefore = 0;
    uint32_t targetOpt = 0;
    uint32_t continuationSteps = 0;
    uint64_t selectSequenceBefore = 0;

    bool operationOk = false;
    bool retryable = false;
    bool callbackInvoked = false;
    bool alreadyOpen = false;
    bool alreadyGone = false;
    bool actionQueued = false;
    float dropDistance = 0.0f;
    bool drainTargetConfirmPages = false;
    char callbackReturn = 0;
    uint32_t pendingScript = 0;
    uint32_t foregroundUiId = 0;
    std::atomic<bool> cancelRequested{false};
    std::string error;
};

constexpr size_t kNativeUiQueueCapacity = 8;
constexpr DWORD kNativeUiTaskTimeoutMs = 2500;
constexpr DWORD kNativeUiExecutingGraceMs = 1000;
constexpr uint32_t kNativeUiMaxTargetConfirmPages = 4;
constexpr uint32_t kNpcInteractionWheelUiId = 88;
constexpr uintptr_t kNpcWheelCreatureOffset = 0x38;

std::mutex s_nativeUiHookMutex;
std::mutex s_nativeGameQueueMutex;
std::mutex s_nativeDialogQueueMutex;
std::deque<std::shared_ptr<NativeUiTask>> s_nativeGameQueue;
std::deque<std::shared_ptr<NativeUiTask>> s_nativeDialogQueue;
std::mutex s_nativeDialogActiveMutex;
std::shared_ptr<NativeUiTask> s_nativeDialogActive;
std::shared_ptr<NativeUiTask> s_nativeDialogExecuting;
std::atomic<bool> s_nativeUiInstalled{false};
std::atomic<bool> s_nativeUiAccepting{false};
std::atomic<DWORD> s_nativeGameThreadId{0};
std::atomic<LONG> s_nativeUiHookInFlight{0};
std::mutex s_nativeUiHookInFlightMutex;
std::condition_variable s_nativeUiHookInFlightCv;
NativeGameLoopFn s_originalGameLoop = nullptr;
NativeDialogUpdateFn s_originalDialogUpdate = nullptr;

struct NativeGameLoopFrame
{
    NativeGameLoopFrame *previous = nullptr;
    std::shared_ptr<NativeUiTask> dialogTaskExecuted;
};

thread_local NativeGameLoopFrame *t_nativeGameLoopFrame = nullptr;
thread_local uint32_t t_nativeGameLoopDepth = 0;
thread_local bool t_nativeRootDialogClickExecuted = false;

class NativeUiHookInFlightGuard
{
public:
    NativeUiHookInFlightGuard()
    {
        s_nativeUiHookInFlight.fetch_add(1, std::memory_order_acq_rel);
    }

    ~NativeUiHookInFlightGuard()
    {
        if (s_nativeUiHookInFlight.fetch_sub(1, std::memory_order_acq_rel) == 1)
            s_nativeUiHookInFlightCv.notify_all();
    }
};

struct DialogClickContext
{
    uintptr_t stateGlobal;
    uintptr_t state;
    uintptr_t resultAddr;
    NpcConfirmDialogOptionFn confirm;
    NpcChooseDialogOptionFn choose;
    uint32_t mode;
    uint32_t npcId;
};

static bool CallConfirmSafe(NpcConfirmDialogOptionFn fn, uintptr_t state,
                            uint32_t *out, char *returnValue)
{
    __try
    {
        char value = fn(state, out);
        if (returnValue) *returnValue = value;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool CallChooseSafe(NpcChooseDialogOptionFn fn, uintptr_t state,
                           uint32_t *out, int optionIndex, char *returnValue)
{
    __try
    {
        char value = fn(state, out, optionIndex);
        if (returnValue) *returnValue = value;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool CallWheelTalkSafe(NativeWheelTalkFn fn, void *wheel)
{
    __try
    {
        fn(wheel);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static uintptr_t CallFindCreatureForWheelSafe(NativeFindCreatureFn fn, void *manager,
                                               uint32_t creatureId)
{
    __try
    {
        return fn(manager, static_cast<int>(creatureId));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

static int CallSetAfterActionForDropSafe(uintptr_t fnAddr, void *user,
                                         int x, int y)
{
    auto fn = reinterpret_cast<int(__thiscall *)(void *, int, int, int, int)>(fnAddr);
    __try
    {
        return fn(user, x, y, 4, 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return INT_MIN;
    }
}

static void CompleteNativeUiTask(const std::shared_ptr<NativeUiTask> &task)
{
    if (!task) return;
    task->phase.store(NativeUiTaskPhase::Completed, std::memory_order_release);
    task->waitCv.notify_all();
}

static std::shared_ptr<NativeUiTask> PopNativeUiTask(
    std::mutex &queueMutex, std::deque<std::shared_ptr<NativeUiTask>> &queue)
{
    std::lock_guard<std::mutex> lock(queueMutex);
    while (!queue.empty())
    {
        auto task = std::move(queue.front());
        queue.pop_front();
        NativeUiTaskPhase expected = NativeUiTaskPhase::Queued;
        if (task && task->phase.compare_exchange_strong(
                        expected, NativeUiTaskPhase::Executing,
                        std::memory_order_acq_rel, std::memory_order_acquire))
            return task;
    }
    return nullptr;
}

static NativeUiWaitResult QueueAndWaitNativeUiTask(
    const std::shared_ptr<NativeUiTask> &task, std::mutex &queueMutex,
    std::deque<std::shared_ptr<NativeUiTask>> &queue, std::string *dispatchError,
    DWORD timeoutMs = kNativeUiTaskTimeoutMs)
{
    if (dispatchError) dispatchError->clear();
    if (!task || !s_nativeUiInstalled.load(std::memory_order_acquire))
    {
        if (dispatchError) *dispatchError = "native UI bridge is unavailable";
        return NativeUiWaitResult::Unavailable;
    }
    if (!s_nativeUiAccepting.load(std::memory_order_acquire))
    {
        if (dispatchError) *dispatchError = "native UI bridge is stopping";
        return NativeUiWaitResult::Stopping;
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (!s_nativeUiAccepting.load(std::memory_order_relaxed))
        {
            if (dispatchError) *dispatchError = "native UI bridge is stopping";
            return NativeUiWaitResult::Stopping;
        }
        if (queue.size() >= kNativeUiQueueCapacity)
        {
            if (dispatchError) *dispatchError = "native UI task queue is full";
            return NativeUiWaitResult::QueueFull;
        }
        queue.push_back(task);
    }

    auto terminal = [&task]() {
        auto phase = task->phase.load(std::memory_order_acquire);
        return phase == NativeUiTaskPhase::Completed ||
               phase == NativeUiTaskPhase::Cancelled;
    };
    std::unique_lock<std::mutex> waitLock(task->waitMutex);
    if (!task->waitCv.wait_for(waitLock, std::chrono::milliseconds(timeoutMs), terminal))
    {
        NativeUiTaskPhase expected = NativeUiTaskPhase::Queued;
        if (task->phase.compare_exchange_strong(
                expected, NativeUiTaskPhase::Cancelled,
                std::memory_order_acq_rel, std::memory_order_acquire))
        {
            task->waitCv.notify_all();
            if (dispatchError)
                *dispatchError = "native UI task timed out before GameLoop execution";
            return NativeUiWaitResult::TimedOutBeforeExecution;
        }

        // Once GameLoop has claimed a task the current click may already have
        // happened. Revoke all future continuation clicks, then give the owning
        // frame a short chance to publish the exact outcome.
        task->cancelRequested.store(true, std::memory_order_release);
        if (!task->waitCv.wait_for(
                waitLock, std::chrono::milliseconds(kNativeUiExecutingGraceMs), terminal))
        {
            if (dispatchError)
                *dispatchError = "native UI task is still executing on GameLoop";
            return NativeUiWaitResult::ExecutingAmbiguous;
        }
    }

    if (task->phase.load(std::memory_order_acquire) == NativeUiTaskPhase::Cancelled)
    {
        if (dispatchError) *dispatchError = "native UI task was cancelled";
        return NativeUiWaitResult::Cancelled;
    }
    return NativeUiWaitResult::Completed;
}

static void CancelNativeUiQueue(
    std::mutex &queueMutex, std::deque<std::shared_ptr<NativeUiTask>> &queue)
{
    std::lock_guard<std::mutex> lock(queueMutex);
    for (auto &task : queue)
    {
        if (!task) continue;
        NativeUiTaskPhase expected = NativeUiTaskPhase::Queued;
        if (task->phase.compare_exchange_strong(
                expected, NativeUiTaskPhase::Cancelled,
                std::memory_order_acq_rel, std::memory_order_acquire))
            task->waitCv.notify_all();
    }
    queue.clear();
}

static void CancelActiveDialogTask()
{
    std::lock_guard<std::mutex> lock(s_nativeDialogActiveMutex);
    if (s_nativeDialogActive)
    {
        s_nativeDialogActive->cancelRequested.store(true, std::memory_order_release);
        NativeUiTaskPhase expected = NativeUiTaskPhase::Executing;
        if (s_nativeDialogActive->phase.compare_exchange_strong(
                expected, NativeUiTaskPhase::Cancelled,
                std::memory_order_acq_rel, std::memory_order_acquire))
            s_nativeDialogActive->waitCv.notify_all();
        s_nativeDialogActive.reset();
    }
    if (s_nativeDialogExecuting)
        s_nativeDialogExecuting->cancelRequested.store(true, std::memory_order_release);
}

static std::shared_ptr<NativeUiTask> TakeNativeDialogTask()
{
    {
        std::lock_guard<std::mutex> lock(s_nativeDialogActiveMutex);
        if (s_nativeDialogActive)
        {
            auto task = std::move(s_nativeDialogActive);
            s_nativeDialogActive.reset();
            s_nativeDialogExecuting = task;
            return task;
        }
    }
    auto task = PopNativeUiTask(s_nativeDialogQueueMutex, s_nativeDialogQueue);
    if (task)
    {
        std::lock_guard<std::mutex> lock(s_nativeDialogActiveMutex);
        s_nativeDialogExecuting = task;
    }
    return task;
}

static void PublishNativeDialogContinuation(const std::shared_ptr<NativeUiTask> &task)
{
    std::lock_guard<std::mutex> lock(s_nativeDialogActiveMutex);
    if (s_nativeDialogExecuting == task) s_nativeDialogExecuting.reset();
    if (!task || task->cancelRequested.load(std::memory_order_acquire))
    {
        if (task)
        {
            NativeUiTaskPhase expected = NativeUiTaskPhase::Executing;
            if (task->phase.compare_exchange_strong(
                    expected, NativeUiTaskPhase::Cancelled,
                    std::memory_order_acq_rel, std::memory_order_acquire))
                task->waitCv.notify_all();
        }
        return;
    }
    s_nativeDialogActive = task;
}

static void ClearNativeDialogExecuting(const std::shared_ptr<NativeUiTask> &task)
{
    std::lock_guard<std::mutex> lock(s_nativeDialogActiveMutex);
    if (s_nativeDialogExecuting == task) s_nativeDialogExecuting.reset();
}

static bool LoadDialogClickContext(DialogClickContext *context)
{
    if (!context) return false;
    auto stateGlobal = PatternResolver::Get("NpcDialogState");
    auto confirmAddr = PatternResolver::Get("NpcConfirmDialogOption");
    auto chooseAddr = PatternResolver::Get("NpcChooseDialogOption");
    auto resultAddr = PatternResolver::Get("NpcDialogPendingResult");
    if (!stateGlobal || !confirmAddr || !chooseAddr || !resultAddr)
        return false;

    uint32_t state = 0;
    uint32_t mode = 0;
    uint32_t npcId = 0;
    if (!SafeReadDword(stateGlobal, state) || !state ||
        !SafeReadDword(state + kDialogStateModeOffset, mode) ||
        (mode != static_cast<uint32_t>(DialogMode::Confirm) &&
         mode != static_cast<uint32_t>(DialogMode::Choice)) ||
        !SafeReadDword(state + kDialogStateNpcInteractIdOffset, npcId))
        return false;

    context->stateGlobal = stateGlobal;
    context->state = state;
    context->resultAddr = resultAddr;
    context->confirm = reinterpret_cast<NpcConfirmDialogOptionFn>(confirmAddr);
    context->choose = reinterpret_cast<NpcChooseDialogOptionFn>(chooseAddr);
    context->mode = mode;
    context->npcId = npcId;
    return true;
}

static bool CollectDialogOptionNodes(uintptr_t state, uint32_t mode,
                                     std::vector<uint32_t> &nodes)
{
    nodes.clear();
    if (mode == static_cast<uint32_t>(DialogMode::Confirm))
    {
        uint32_t current = 0;
        if (!SafeReadDword(state + kDialogStateConfirmEntryOffset, current) || !current)
            return false;
        nodes.push_back(current);
        return true;
    }
    if (mode != static_cast<uint32_t>(DialogMode::Choice)) return false;

    uint32_t node = 0;
    if (!SafeReadDword(state + kDialogStateChoiceHeadOffset, node))
        return false;

    std::unordered_set<uint32_t> visited;
    for (int hops = 0; node && hops < kDialogMaxOptions; ++hops)
    {
        if (!visited.insert(node).second) return false;
        nodes.push_back(node);
        uint32_t next = 0;
        if (!SafeReadDword(node + kDialogOptionNextOffset, next)) return false;
        if (nodes.size() >= static_cast<size_t>(kDialogMaxOptions))
        {
            return next == 0;
        }
        node = next;
    }
    return !nodes.empty();
}

static uint32_t FindOptionNodeAtIndex(uintptr_t state, uint32_t mode,
                                      uint32_t targetIndex)
{
    std::vector<uint32_t> nodes;
    if (!CollectDialogOptionNodes(state, mode, nodes) || targetIndex >= nodes.size()) return 0;
    return nodes[targetIndex];
}

static bool ResolveDialogOptionNode(uint32_t node, uint32_t *tag, uint32_t *opt,
                                    uint32_t *warpKey, uint32_t *warpTableId)
{
    if (tag) *tag = 0;
    uint32_t scriptEntry = 0;
    if (!node || !SafeReadDword(node + kDialogOptionTagOffset, scriptEntry)) return false;
    if (tag) *tag = scriptEntry;
    return ResolveDialogScriptMeta(scriptEntry, opt, warpKey, warpTableId);
}

static bool ValidateDialogTaskOnGameLoop(uintptr_t state, uint32_t *pendingOut,
                                         const std::shared_ptr<NativeUiTask> &task)
{
    if (!state || !pendingOut || !task || state != task->expectedState)
    {
        if (task) task->error = "native dialog state changed before click";
        return false;
    }

    uint32_t mode = 0;
    uint32_t npcId = 0;
    uint32_t pending = 0;
    if (!SafeReadDword(state + kDialogStateModeOffset, mode) ||
        !SafeReadDword(state + kDialogStateNpcInteractIdOffset, npcId) ||
        !SafeReadDword(reinterpret_cast<uintptr_t>(pendingOut), pending) ||
        npcId != task->npcId)
    {
        task->error = "native dialog mode, npc, or pending state changed before click";
        return false;
    }

    const bool drainingTarget = task->drainTargetConfirmPages && task->continuationSteps > 0;
    const uint32_t expectedPending = drainingTarget
                                         ? task->expectedScript
                                         : task->expectedPendingBefore;
    if (pending != expectedPending)
    {
        task->error = "native dialog retained-pending value changed before click";
        return false;
    }
    if (drainingTarget && mode != static_cast<uint32_t>(DialogMode::Confirm))
    {
        task->error = "target opt child requires a choice; refusing to select it automatically";
        return false;
    }
    if (drainingTarget)
    {
        uint32_t selectedScript = 0;
        if (!SafeReadDword(state + kDialogStateSelectedScriptOffset, selectedScript) ||
            selectedScript != task->expectedScript)
        {
            task->error = "target opt child is no longer rooted at the armed script";
            return false;
        }
    }
    if (!drainingTarget && mode != task->expectedMode)
    {
        task->error = "native dialog mode changed before click";
        return false;
    }

    uint32_t node = 0;
    if (mode == static_cast<uint32_t>(DialogMode::Confirm))
    {
        if (!SafeReadDword(state + kDialogStateConfirmEntryOffset, node))
        {
            task->error = "native confirm node is unreadable";
            return false;
        }
    }
    else if (mode == static_cast<uint32_t>(DialogMode::Choice))
    {
        if (!SafeReadDword(state + kDialogStateChoiceHeadOffset, node))
        {
            task->error = "native choice list is unreadable";
            return false;
        }
        const uint32_t requestedIndex = drainingTarget ? 0 : task->optionIndex;
        for (uint32_t i = 0; node && i < requestedIndex; ++i)
        {
            if (!SafeReadDword(node + kDialogOptionNextOffset, node))
            {
                task->error = "native choice list changed before click";
                return false;
            }
        }
    }
    else
    {
        task->error = "native dialog is no longer in a clickable mode";
        return false;
    }

    uint32_t script = 0;
    if (!node || !SafeReadDword(node + kDialogOptionTagOffset, script))
    {
        task->error = "native dialog option changed before click";
        return false;
    }
    if (!drainingTarget &&
        (node != task->expectedNode || script != task->expectedScript))
    {
        task->error = "native dialog option changed before click";
        return false;
    }
    if (drainingTarget && script)
    {
        uint32_t childOpt = 0;
        if (!ResolveDialogScriptMeta(script, &childOpt, nullptr, nullptr) ||
            (childOpt != 0 && childOpt != task->targetOpt))
        {
            task->error = "target opt child exposes a different server option";
            return false;
        }
    }
    task->expectedMode = mode;
    task->optionIndex = drainingTarget ? 0 : task->optionIndex;
    task->expectedNode = node;
    task->currentNodeScript = script;
    return true;
}

static void ExecuteGameThreadTask(const std::shared_ptr<NativeUiTask> &task)
{
    if (!task) return;
    if (task->kind == NativeUiTaskKind::SelectDropForPickup)
    {
        if (task->expectedMapId && GetCurrentMapId() != task->expectedMapId)
        {
            task->error = "map changed before native drop pickup";
            return;
        }
        int64_t localHp = 0;
        if (!TryGetLocalHp(localHp))
        {
            task->error = "local player status is unavailable before native drop pickup";
            return;
        }
        if (localHp == 0)
        {
            task->error = "character is dead before native drop pickup";
            return;
        }

        DropItemInfo drop{};
        auto state = QueryDropItemById(task->dropId, drop);
        if (state == DropItemLookupState::Unavailable)
        {
            task->error = "drop lookup is unavailable on GameLoop";
            return;
        }
        if (state == DropItemLookupState::Missing)
        {
            task->operationOk = true;
            task->alreadyGone = true;
            return;
        }
        task->expectedItemId = task->expectedItemId ? task->expectedItemId : drop.itemId;
        task->dropDistance = drop.distance;
        if (drop.itemId != task->expectedItemId)
        {
            task->error = "drop itemId changed before native pickup";
            return;
        }
        if (!PickupFilter::Instance().ShouldPick(drop.itemId))
        {
            task->error = "drop no longer matches the pickup filter";
            return;
        }
        if (!drop.canPick)
        {
            task->error = "drop is not pickable yet";
            return;
        }
        if (task->maxDropDistance > 0.0f && drop.distance > task->maxDropDistance)
        {
            task->error = "drop is outside the native pickup dispatch distance";
            return;
        }

        auto user = GetLocalUserPtr();
        auto setAddr = PatternResolver::Get("SetAfterAction");
        auto targetAddr = PatternResolver::Get("TargetDropItemId");
        auto intentAddr = PatternResolver::Get("AfterActionIntent");
        auto flag2Addr = PatternResolver::Get("AfterActionFlag2");
        if (!user || !setAddr || !targetAddr || !intentAddr || !flag2Addr)
        {
            task->error = "native drop pickup patterns are unresolved";
            return;
        }

        const int targetX = static_cast<int>(std::floor(drop.x + 0.5f));
        const int targetY = static_cast<int>(std::floor(drop.y + 0.5f));
        if (!SafeWriteDword(intentAddr, 4) ||
            !SafeWriteDword(targetAddr, task->dropId))
        {
            SafeWriteDword(intentAddr, 0);
            task->error = "failed to select the native drop target";
            return;
        }

        const int rv = CallSetAfterActionForDropSafe(
            setAddr, reinterpret_cast<void *>(user), targetX, targetY);
        SafeWriteByte(flag2Addr, 0);
        uint32_t skillMode = 0;
        if (SafeReadDword(user + kUserSkillModeOffset, skillMode) && skillMode != 3)
            SafeWriteDword(user + kUserSelectedItemIdOffset, 0);
        if (rv == INT_MIN)
        {
            SafeWriteDword(intentAddr, 0);
            task->error = "native drop SetAfterAction raised an exception";
            return;
        }

        task->operationOk = true;
        task->actionQueued = rv > 0;
        spdlog::info(
            "GGTB::NativeDropPickup: selected dropId={} itemId={} action=4 dst=({},{}) d={:.1f} rv={}",
            task->dropId, drop.itemId, targetX, targetY, drop.distance, rv);
        return;
    }

    if (task->kind == NativeUiTaskKind::TalkOrAttack)
    {
        uint32_t foreground = 0;
        uint32_t targetId = 0;
        auto foregroundAddr = PatternResolver::Get("ForegroundUIId");
        auto targetAddr = PatternResolver::Get("TargetCreatureId");
        if (foregroundAddr) SafeReadDword(foregroundAddr, foreground);
        if (targetAddr) SafeReadDword(targetAddr, targetId);
        DialogSnapshot dialog = GetDialogSnapshot();
        if ((dialog.open && dialog.npcInteractId == task->npcId) ||
            (foreground == kNpcInteractionWheelUiId && targetId == task->npcId))
        {
            task->operationOk = true;
            task->alreadyOpen = true;
            task->foregroundUiId = foreground;
            return;
        }
        task->operationOk = TalkOrAttack(task->npcId);
        if (!task->operationOk)
            task->error = "engine rejected the native creature click";
        return;
    }

    if (task->kind != NativeUiTaskKind::OpenNpcWheelDialog)
    {
        task->error = "invalid GameLoop task kind";
        return;
    }

    auto foregroundAddr = PatternResolver::Get("ForegroundUIId");
    auto targetAddr = PatternResolver::Get("TargetCreatureId");
    auto wheelGlobal = PatternResolver::Get("NpcInteractionWheelObject");
    auto callbackAddr = PatternResolver::Get("NpcInteractionWheelTalkCallback");
    auto managerGlobal = PatternResolver::Get("CreatureMgrPtr");
    auto findAddr = PatternResolver::Get("FindCreatureById");
    if (!foregroundAddr || !targetAddr || !wheelGlobal || !callbackAddr ||
        !managerGlobal || !findAddr)
    {
        task->error = "interaction-wheel patterns are unresolved";
        return;
    }

    DialogSnapshot alreadyDialog = GetDialogSnapshot();
    if (alreadyDialog.open && alreadyDialog.npcInteractId == task->npcId)
    {
        task->operationOk = true;
        task->alreadyOpen = true;
        task->foregroundUiId = kNpcOrdinaryDialogUiId;
        return;
    }

    uint32_t foreground = 0;
    uint32_t targetId = 0;
    uint32_t wheel = 0;
    uint32_t wheelCreature = 0;
    uint32_t wheelCreatureId = 0;
    uint32_t manager = 0;
    if (!SafeReadDword(foregroundAddr, foreground) ||
        !SafeReadDword(targetAddr, targetId) ||
        !SafeReadDword(wheelGlobal, wheel) || !wheel ||
        !SafeReadDword(wheel + kNpcWheelCreatureOffset, wheelCreature) || !wheelCreature ||
        !SafeReadDword(wheelCreature + kCreatureIdOffset, wheelCreatureId) ||
        !SafeReadDword(managerGlobal, manager) || !manager)
    {
        task->retryable = true;
        task->error = "interaction wheel is not fully populated yet";
        task->foregroundUiId = foreground;
        return;
    }
    task->foregroundUiId = foreground;
    if (foreground != kNpcInteractionWheelUiId || targetId != task->npcId ||
        wheelCreatureId != task->npcId)
    {
        task->retryable = true;
        task->error = "interaction wheel is not open for the requested npc";
        return;
    }

    uintptr_t creature = CallFindCreatureForWheelSafe(
        reinterpret_cast<NativeFindCreatureFn>(findAddr),
        reinterpret_cast<void *>(manager), task->npcId);
    if (!creature || creature != wheelCreature)
    {
        task->retryable = true;
        task->error = "interaction-wheel creature identity is unstable";
        return;
    }

    uint32_t statTable = 0;
    uint32_t creatureType = 0;
    if (!SafeReadDword(creature + kCreatureStatTableOffset, statTable) || !statTable ||
        !SafeReadDword(statTable + kStatTableTypeOffset, creatureType) ||
        creatureType != kCreatureTypeTalkable)
    {
        task->error = "interaction-wheel target is no longer a talkable npc";
        return;
    }

    task->callbackInvoked = true;
    task->operationOk = CallWheelTalkSafe(
        reinterpret_cast<NativeWheelTalkFn>(callbackAddr), reinterpret_cast<void *>(wheel));
    if (!task->operationOk)
        task->error = "SEH while invoking the native wheel dialog callback";
}

static void FinishGameThreadTask(const std::shared_ptr<NativeUiTask> &task)
{
    if (!task || task->kind != NativeUiTaskKind::OpenNpcWheelDialog ||
        !task->callbackInvoked || !task->operationOk)
        return;

    auto foregroundAddr = PatternResolver::Get("ForegroundUIId");
    uint32_t foreground = 0;
    if (foregroundAddr) SafeReadDword(foregroundAddr, foreground);
    task->foregroundUiId = foreground;
    DialogSnapshot snapshot = GetDialogSnapshot();
    if (foreground != kNpcOrdinaryDialogUiId || !snapshot.open ||
        snapshot.npcInteractId != task->npcId)
    {
        task->operationOk = false;
        task->retryable = false; // callback was already invoked; never click it twice.
        task->error = "native wheel callback did not establish the expected NPC dialog";
    }
}

static bool FinishDialogThreadTask(const std::shared_ptr<NativeUiTask> &task)
{
    if (!task || !task->drainTargetConfirmPages) return true;

    auto latest = NetLog::GetLastDungeonDialogSelect();
    if (latest.sequence > task->selectSequenceBefore && latest.npcId == task->npcId &&
        latest.opt == task->targetOpt && latest.sub == 1)
    {
        task->operationOk = true;
        return true;
    }
    if (!task->callbackInvoked)
    {
        task->operationOk = false;
        if (task->error.empty()) task->error = "target dialog callback was not invoked";
        return true;
    }
    if (task->callbackReturn == 0)
    {
        task->operationOk = false;
        task->error = "GameLoop closed the target dialog without emitting the exact select";
        return true;
    }
    if (task->pendingScript != task->expectedScript)
    {
        task->operationOk = false;
        task->error = "target dialog did not retain the armed pending script";
        return true;
    }
    if (task->cancelRequested.load(std::memory_order_acquire))
    {
        task->operationOk = false;
        task->error = "target dialog continuation was cancelled after a claimed click";
        return true;
    }
    if (task->continuationSteps >= kNativeUiMaxTargetConfirmPages)
    {
        task->operationOk = false;
        task->error = "target dialog exceeded the confirm-only continuation limit";
        return true;
    }

    DialogClickContext context{};
    if (!LoadDialogClickContext(&context) || context.state != task->expectedState ||
        context.npcId != task->npcId ||
        context.mode != static_cast<uint32_t>(DialogMode::Confirm))
    {
        task->operationOk = false;
        task->error = "target opt child is not the expected confirm-only dialog";
        return true;
    }

    ++task->continuationSteps;
    task->callbackInvoked = false;
    task->callbackReturn = 0;
    task->pendingScript = task->expectedScript;
    task->operationOk = false;
    PublishNativeDialogContinuation(task);
    return false;
}

static uintptr_t __fastcall HookNativeGameLoop(void *gameHud, void *, int active,
                                               float frameDt)
{
    NativeUiHookInFlightGuard inFlight;
    if (!s_originalGameLoop) return 0;

    const bool outermost = t_nativeGameLoopDepth == 0;
    NativeGameLoopFrame frame{};
    frame.previous = t_nativeGameLoopFrame;
    t_nativeGameLoopFrame = &frame;
    ++t_nativeGameLoopDepth;
    if (outermost)
    {
        s_nativeGameThreadId.store(GetCurrentThreadId(), std::memory_order_release);
        t_nativeRootDialogClickExecuted = false;
    }

    std::shared_ptr<NativeUiTask> gameTask;
    if (outermost)
    {
        gameTask = PopNativeUiTask(s_nativeGameQueueMutex, s_nativeGameQueue);
        if (gameTask) ExecuteGameThreadTask(gameTask);
    }

    uintptr_t result = s_originalGameLoop(gameHud, active, frameDt);

    if (gameTask)
    {
        FinishGameThreadTask(gameTask);
        CompleteNativeUiTask(gameTask);
    }
    if (frame.dialogTaskExecuted)
    {
        bool complete = FinishDialogThreadTask(frame.dialogTaskExecuted);
        if (complete)
        {
            ClearNativeDialogExecuting(frame.dialogTaskExecuted);
            CompleteNativeUiTask(frame.dialogTaskExecuted);
        }
    }

    --t_nativeGameLoopDepth;
    t_nativeGameLoopFrame = frame.previous;
    if (outermost) t_nativeRootDialogClickExecuted = false;
    return result;
}

static char __fastcall HookNativeDialogUpdate(uintptr_t state, void *, float frameDt,
                                              uint32_t *pendingOut)
{
    NativeUiHookInFlightGuard inFlight;
    if (!s_originalDialogUpdate) return 0;
    if (!t_nativeGameLoopFrame || t_nativeGameLoopFrame->dialogTaskExecuted ||
        t_nativeRootDialogClickExecuted)
        return s_originalDialogUpdate(state, frameDt, pendingOut);

    auto task = TakeNativeDialogTask();
    if (!task) return s_originalDialogUpdate(state, frameDt, pendingOut);

    t_nativeRootDialogClickExecuted = true;
    t_nativeGameLoopFrame->dialogTaskExecuted = task;
    if (task->cancelRequested.load(std::memory_order_acquire))
    {
        task->error = "native dialog task was cancelled before the next click";
        return s_originalDialogUpdate(state, frameDt, pendingOut);
    }
    if (!ValidateDialogTaskOnGameLoop(state, pendingOut, task))
    {
        task->retryable = !task->drainTargetConfirmPages;
        return s_originalDialogUpdate(state, frameDt, pendingOut);
    }

    bool called = false;
    char nativeReturn = 0;
    if (task->expectedMode == static_cast<uint32_t>(DialogMode::Confirm))
    {
        auto fn = reinterpret_cast<NpcConfirmDialogOptionFn>(
            PatternResolver::Get("NpcConfirmDialogOption"));
        called = fn && CallConfirmSafe(fn, state, pendingOut, &nativeReturn);
    }
    else
    {
        auto fn = reinterpret_cast<NpcChooseDialogOptionFn>(
            PatternResolver::Get("NpcChooseDialogOption"));
        called = fn && CallChooseSafe(fn, state, pendingOut,
                                      static_cast<int>(task->optionIndex), &nativeReturn);
    }

    if (!called)
    {
        task->error = "SEH while invoking the native dialog click callback";
        return s_originalDialogUpdate(state, frameDt, pendingOut);
    }

    uint32_t pending = 0;
    if (!SafeReadDword(reinterpret_cast<uintptr_t>(pendingOut), pending))
    {
        task->error = "native dialog pending result became unreadable";
        return nativeReturn;
    }
    task->callbackInvoked = true;
    task->callbackReturn = nativeReturn;
    task->pendingScript = pending;
    task->operationOk = true;
    return nativeReturn;
}

static bool InvokeDialogClick(const DialogClickContext &context, uint32_t optionIndex,
                               uint32_t node, uint32_t scriptEntry,
                               uint32_t expectedPendingBefore,
                               bool drainTargetConfirmPages, uint32_t targetOpt,
                               uint64_t selectSequenceBefore,
                               std::shared_ptr<NativeUiTask> *completedTask,
                               std::string *dispatchError,
                               NativeUiWaitResult *waitResult)
{
    auto task = std::make_shared<NativeUiTask>(NativeUiTaskKind::DialogClick);
    task->npcId = context.npcId;
    task->expectedState = context.state;
    task->expectedMode = context.mode;
    task->optionIndex = optionIndex;
    task->expectedNode = node;
    task->expectedScript = scriptEntry;
    task->currentNodeScript = scriptEntry;
    task->expectedPendingBefore = expectedPendingBefore;
    task->drainTargetConfirmPages = drainTargetConfirmPages;
    task->targetOpt = targetOpt;
    task->selectSequenceBefore = selectSequenceBefore;
    NativeUiWaitResult waited = QueueAndWaitNativeUiTask(
        task, s_nativeDialogQueueMutex, s_nativeDialogQueue, dispatchError);
    if (completedTask) *completedTask = task;
    if (waitResult) *waitResult = waited;
    return waited == NativeUiWaitResult::Completed;
}

class NativeUiThreadHandleBatch
{
   public:
    NativeUiThreadHandleBatch() = default;
    NativeUiThreadHandleBatch(const NativeUiThreadHandleBatch &) = delete;
    NativeUiThreadHandleBatch &operator=(const NativeUiThreadHandleBatch &) = delete;

    ~NativeUiThreadHandleBatch()
    {
        for (HANDLE thread : threads_) CloseHandle(thread);
    }

    LONG Enlist(HANDLE thread)
    {
        // Detours stores this exact handle and resumes it during commit/abort.
        // Track it before suspension so allocation failure cannot strand a thread.
        try
        {
            threads_.push_back(thread);
        }
        catch (...)
        {
            CloseHandle(thread);
            return ERROR_NOT_ENOUGH_MEMORY;
        }

        LONG update = DetourUpdateThread(thread);
        if (update != NO_ERROR)
        {
            threads_.pop_back();
            CloseHandle(thread);
        }
        return update;
    }

   private:
    std::vector<HANDLE> threads_;
};

static LONG EnlistNativeUiThreads(NativeUiThreadHandleBatch &threadHandles)
{
    LONG current = DetourUpdateThread(GetCurrentThread());
    if (current != NO_ERROR) return current;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return static_cast<LONG>(GetLastError());

    const DWORD processId = GetCurrentProcessId();
    const DWORD currentThreadId = GetCurrentThreadId();
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (!Thread32First(snapshot, &entry))
    {
        LONG error = static_cast<LONG>(GetLastError());
        CloseHandle(snapshot);
        return error;
    }

    LONG result = NO_ERROR;
    do
    {
        if (entry.th32OwnerProcessID != processId || entry.th32ThreadID == currentThreadId)
            continue;
        HANDLE thread = OpenThread(THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                       THREAD_QUERY_INFORMATION,
                                   FALSE, entry.th32ThreadID);
        if (!thread)
        {
            DWORD error = GetLastError();
            if (error == ERROR_INVALID_PARAMETER || error == ERROR_NOT_FOUND) continue;
            result = static_cast<LONG>(error);
            break;
        }
        LONG update = threadHandles.Enlist(thread);
        if (update != NO_ERROR)
        {
            result = update;
            break;
        }
    } while (Thread32Next(snapshot, &entry));

    CloseHandle(snapshot);
    return result;
}

}  // namespace

bool InstallNativeUiBridge()
{
    std::lock_guard<std::mutex> lock(s_nativeUiHookMutex);
    if (s_nativeUiInstalled.load(std::memory_order_acquire)) return true;

    auto gameLoopAddr = PatternResolver::Get("GameLoop");
    auto dialogUpdateAddr = PatternResolver::Get("NpcDialogUpdate");
    if (!gameLoopAddr || !dialogUpdateAddr)
    {
        spdlog::error("GGTB::NativeUiBridge: unresolved gameLoop={:x} dialogUpdate={:x}",
                      gameLoopAddr, dialogUpdateAddr);
        return false;
    }

    s_originalGameLoop = reinterpret_cast<NativeGameLoopFn>(gameLoopAddr);
    s_originalDialogUpdate = reinterpret_cast<NativeDialogUpdateFn>(dialogUpdateAddr);
    NativeUiThreadHandleBatch threadHandles;
    LONG begin = DetourTransactionBegin();
    LONG update = begin == NO_ERROR ? EnlistNativeUiThreads(threadHandles) : begin;
    LONG attachGame =
        update == NO_ERROR
            ? DetourAttach(reinterpret_cast<PVOID *>(&s_originalGameLoop), HookNativeGameLoop)
            : update;
    LONG attachDialog = attachGame == NO_ERROR
                            ? DetourAttach(reinterpret_cast<PVOID *>(&s_originalDialogUpdate),
                                           HookNativeDialogUpdate)
                            : attachGame;
    if (begin != NO_ERROR || update != NO_ERROR || attachGame != NO_ERROR ||
        attachDialog != NO_ERROR)
    {
        if (begin == NO_ERROR) DetourTransactionAbort();
        s_originalGameLoop = nullptr;
        s_originalDialogUpdate = nullptr;
        spdlog::error("GGTB::NativeUiBridge: attach failed begin={} update={} game={} dialog={}",
                      begin, update, attachGame, attachDialog);
        return false;
    }

    LONG commit = DetourTransactionCommit();
    if (commit != NO_ERROR)
    {
        s_originalGameLoop = nullptr;
        s_originalDialogUpdate = nullptr;
        spdlog::error("GGTB::NativeUiBridge: DetourTransactionCommit failed {}", commit);
        return false;
    }

    s_nativeUiAccepting.store(true, std::memory_order_release);
    s_nativeUiInstalled.store(true, std::memory_order_release);
    spdlog::info("GGTB::NativeUiBridge: GameLoop/dialog hooks installed");
    return true;
}

void RequestNativeUiBridgeStopNoWait()
{
    s_nativeUiAccepting.store(false, std::memory_order_release);
}

void StopNativeUiBridge()
{
    s_nativeUiAccepting.store(false, std::memory_order_release);
    CancelNativeUiQueue(s_nativeGameQueueMutex, s_nativeGameQueue);
    CancelNativeUiQueue(s_nativeDialogQueueMutex, s_nativeDialogQueue);
    CancelActiveDialogTask();
}

bool UninstallNativeUiBridge()
{
    std::lock_guard<std::mutex> lock(s_nativeUiHookMutex);
    StopNativeUiBridge();
    if (!s_nativeUiInstalled.load(std::memory_order_acquire)) return true;

    NativeUiThreadHandleBatch threadHandles;
    LONG begin = DetourTransactionBegin();
    LONG update = begin == NO_ERROR ? EnlistNativeUiThreads(threadHandles) : begin;
    LONG detachDialog = update == NO_ERROR
                            ? DetourDetach(reinterpret_cast<PVOID *>(&s_originalDialogUpdate),
                                           HookNativeDialogUpdate)
                            : update;
    LONG detachGame =
        detachDialog == NO_ERROR
            ? DetourDetach(reinterpret_cast<PVOID *>(&s_originalGameLoop), HookNativeGameLoop)
            : detachDialog;
    LONG commit =
        (begin == NO_ERROR && update == NO_ERROR && detachDialog == NO_ERROR &&
         detachGame == NO_ERROR)
            ? DetourTransactionCommit()
            : (begin == NO_ERROR ? (DetourTransactionAbort(), ERROR_INVALID_FUNCTION) : begin);
    if (commit != NO_ERROR)
    {
        spdlog::error("GGTB::NativeUiBridge: detach failed update={} dialog={} game={} commit={}",
                      update, detachDialog, detachGame, commit);
        // The jump or trampoline may still be live. Preserve both pointers and
        // the installed state; the owner must keep this DLL resident.
        return false;
    }

    std::unique_lock<std::mutex> inFlightLock(s_nativeUiHookInFlightMutex);
    bool drained = s_nativeUiHookInFlightCv.wait_for(
        inFlightLock, std::chrono::seconds(5), []() {
            return s_nativeUiHookInFlight.load(std::memory_order_acquire) == 0;
        });
    if (!drained)
    {
        spdlog::error(
            "GGTB::NativeUiBridge: hooks detached but {} hook frames are still in flight; DLL must remain resident",
            s_nativeUiHookInFlight.load(std::memory_order_acquire));
        return false;
    }

    spdlog::info("GGTB::NativeUiBridge: hooks detached and in-flight frames drained");

    s_nativeUiInstalled.store(false, std::memory_order_release);
    s_nativeGameThreadId.store(0, std::memory_order_release);
    s_originalGameLoop = nullptr;
    s_originalDialogUpdate = nullptr;
    return true;
}

bool IsNativeUiBridgeReady()
{
    return s_nativeUiInstalled.load(std::memory_order_acquire) &&
           s_nativeUiAccepting.load(std::memory_order_acquire);
}

bool IsNativeDropPickupReady()
{
    return IsNativeUiBridgeReady() &&
           PatternResolver::Get("LookupDropItemById") != 0 &&
           PatternResolver::Get("ItemContainerPtr") != 0 &&
           PatternResolver::Get("SetAfterAction") != 0 &&
           PatternResolver::Get("TargetDropItemId") != 0 &&
           PatternResolver::Get("AfterActionIntent") != 0 &&
           PatternResolver::Get("AfterActionFlag2") != 0;
}

NativeDropPickupResult SelectDropForPickupOnGameThread(
    uint32_t dropId, uint32_t expectedItemId, uint32_t expectedMapId,
    float maxDistance)
{
    NativeDropPickupResult result{};
    result.dropId = dropId;
    result.itemId = expectedItemId;

    auto task = std::make_shared<NativeUiTask>(NativeUiTaskKind::SelectDropForPickup);
    task->dropId = dropId;
    task->expectedItemId = expectedItemId;
    task->expectedMapId = expectedMapId;
    task->maxDropDistance = maxDistance;
    std::string dispatchError;
    NativeUiWaitResult waitResult = QueueAndWaitNativeUiTask(
        task, s_nativeGameQueueMutex, s_nativeGameQueue, &dispatchError);
    if (waitResult != NativeUiWaitResult::Completed)
    {
        result.executionAmbiguous =
            waitResult == NativeUiWaitResult::ExecutingAmbiguous;
        result.error = dispatchError;
        return result;
    }

    result.ok = task->operationOk;
    result.alreadyGone = task->alreadyGone;
    result.actionQueued = task->actionQueued;
    result.itemId = task->expectedItemId;
    result.distance = task->dropDistance;
    result.error = task->error;
    return result;
}

bool TalkOrAttackOnGameThread(uint32_t creatureId, std::string *error)
{
    if (error) error->clear();
    auto task = std::make_shared<NativeUiTask>(NativeUiTaskKind::TalkOrAttack);
    task->npcId = creatureId;
    std::string dispatchError;
    NativeUiWaitResult waitResult = QueueAndWaitNativeUiTask(
        task, s_nativeGameQueueMutex, s_nativeGameQueue, &dispatchError);
    if (waitResult != NativeUiWaitResult::Completed)
    {
        if (error) *error = dispatchError;
        return false;
    }
    if (!task->operationOk && error) *error = task->error;
    return task->operationOk;
}

NpcWheelDialogResult OpenNpcDialogFromInteractionWheel(uint32_t npcId)
{
    NpcWheelDialogResult result{};
    result.npcId = npcId;
    auto task = std::make_shared<NativeUiTask>(NativeUiTaskKind::OpenNpcWheelDialog);
    task->npcId = npcId;
    std::string dispatchError;
    NativeUiWaitResult waitResult = QueueAndWaitNativeUiTask(
        task, s_nativeGameQueueMutex, s_nativeGameQueue, &dispatchError);
    if (waitResult != NativeUiWaitResult::Completed)
    {
        result.retryable = waitResult == NativeUiWaitResult::QueueFull ||
                           waitResult == NativeUiWaitResult::TimedOutBeforeExecution;
        result.error = dispatchError;
        return result;
    }
    result.ok = task->operationOk;
    result.retryable = task->retryable;
    result.callbackInvoked = task->callbackInvoked;
    result.alreadyOpen = task->alreadyOpen;
    result.foregroundUiId = task->foregroundUiId;
    result.error = task->error;
    return result;
}

namespace
{
static bool ExecuteDialogNode(const DialogClickContext &context, uint32_t optionIndex,
                              uint32_t node, uint32_t scriptEntry,
                              const uint32_t *expectedPending, uint32_t *actualPending)
{
    uint32_t currentState = 0;
    uint32_t currentMode = 0;
    uint32_t currentNpcId = 0;
    uint32_t currentScriptEntry = 0;
    if (!SafeReadDword(context.stateGlobal, currentState) || currentState != context.state ||
        !SafeReadDword(context.state + kDialogStateModeOffset, currentMode) ||
        currentMode != context.mode ||
        !SafeReadDword(context.state + kDialogStateNpcInteractIdOffset, currentNpcId) ||
        currentNpcId != context.npcId ||
        !SafeReadDword(node + kDialogOptionTagOffset, currentScriptEntry) ||
        currentScriptEntry != scriptEntry)
        return false;
    if (context.mode == static_cast<uint32_t>(DialogMode::Confirm))
    {
        uint32_t currentNode = 0;
        if (!SafeReadDword(context.state + kDialogStateConfirmEntryOffset, currentNode) ||
            currentNode != node)
            return false;
    }
    else
    {
        std::vector<uint32_t> currentNodes;
        if (!CollectDialogOptionNodes(context.state, context.mode, currentNodes) ||
            std::find(currentNodes.begin(), currentNodes.end(), node) == currentNodes.end())
            return false;
    }

    std::shared_ptr<NativeUiTask> task;
    std::string dispatchError;
    NativeUiWaitResult waitResult = NativeUiWaitResult::Unavailable;
    if (!InvokeDialogClick(context, optionIndex, node, scriptEntry, 0, false, 0, 0,
                           &task, &dispatchError, &waitResult))
    {
        spdlog::warn(
            "GGTB::DialogClick: dispatch failed state={:x} npc={} mode={} index={} waitResult={} error={}",
            context.state, context.npcId, context.mode, optionIndex,
            static_cast<uint32_t>(waitResult), dispatchError);
        return false;
    }
    if (!task || !task->operationOk)
    {
        spdlog::warn(
            "GGTB::DialogClick: native callback failed state={:x} npc={} mode={} index={} error={}",
            context.state, context.npcId, context.mode, optionIndex,
            task ? task->error : "missing task result");
        return false;
    }

    uint32_t pending = task->pendingScript;
    if (actualPending) *actualPending = pending;
    if (expectedPending && pending != *expectedPending) return false;

    spdlog::info(
        "GGTB::DialogClick: state={:x} node={:x} script={:x} mode={} index={} nativeRv={} pending={:x}",
        context.state, node, scriptEntry, context.mode, optionIndex,
        static_cast<int>(task->callbackReturn), pending);
    return true;
}
} // anonymous

bool SelectDialogOption(uint32_t dialogOption)
{
    DialogClickContext context{};
    if (!LoadDialogClickContext(&context)) return false;
    uint32_t optNode = FindOptionNodeAtIndex(context.state, context.mode, dialogOption);
    if (!optNode)
    {
        spdlog::warn("GGTB::SelectDialogOption: option index {} not found (mode={})",
                     dialogOption, context.mode);
        return false;
    }

    uint32_t scriptEntry = 0;
    uint32_t resolvedOpt = 0;
    if (!ResolveDialogOptionNode(optNode, &scriptEntry, &resolvedOpt, nullptr, nullptr)) return false;
    if (resolvedOpt == 9575)
    {
        spdlog::warn("GGTB::SelectDialogOption: opt=9575 requires targeted dungeon entry");
        return false;
    }
    return ExecuteDialogNode(context, dialogOption, optNode, scriptEntry, nullptr, nullptr);
}

DialogAdvanceResult AdvanceDialogTowardOpt(uint32_t npcId, uint32_t targetOpt,
                                           bool allowAdvance)
{
    DialogAdvanceResult result{};
    result.optionIndex = UINT32_MAX;
    auto fail = [&result](const char *message, bool retryable = false) {
        result.retryable = retryable;
        result.error = message;
        return result;
    };
    if (!npcId || !targetOpt) return fail("npcId/targetOpt must be non-zero");

    DialogClickContext context{};
    if (!LoadDialogClickContext(&context))
        return fail("native NPC dialog is not open", true);
    if (context.npcId != npcId)
        return fail("native NPC dialog belongs to a different npc", true);

    std::vector<uint32_t> nodes;
    if (!CollectDialogOptionNodes(context.state, context.mode, nodes))
        return fail("native NPC dialog has no readable options", true);

    struct NodeMeta
    {
        uint32_t index;
        uint32_t node;
        uint32_t tag;
        uint32_t opt;
        uint32_t warpKey;
        uint32_t warpTableId;
    };
    std::vector<NodeMeta> metas;
    metas.reserve(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        NodeMeta meta{static_cast<uint32_t>(i), nodes[i], 0, 0, 0, 0};
        if (!ResolveDialogOptionNode(meta.node, &meta.tag, &meta.opt,
                                     &meta.warpKey, &meta.warpTableId))
            return fail("native NPC dialog option metadata is unreadable", true);
        metas.push_back(meta);
    }

    std::vector<NodeMeta> targets;
    for (const auto &meta : metas)
    {
        if (meta.opt == targetOpt) targets.push_back(meta);
    }
    if (targets.size() > 1) return fail("multiple visible options expose the target opt");
    if (targets.size() == 1)
    {
        const auto &target = targets.front();
        result.ok = true;
        result.found = true;
        result.optionIndex = target.index;
        result.warpKey = target.warpKey;
        result.warpTableId = target.warpTableId;
        return result;
    }

    NodeMeta selected{};
    bool hasSelected = false;
    if (context.mode == static_cast<uint32_t>(DialogMode::Confirm))
    {
        const auto &current = metas.front();
        if (current.opt != 0) return fail("confirm page exposes a non-target terminal opt");
        if (current.tag == 0)
        {
            uint32_t next = 0;
            if (!SafeReadDword(current.node + kDialogOptionNextOffset, next) || !next)
                return fail("confirm page has no safe continuation toward the target opt", true);
        }
        else
        {
            auto search = SearchDialogScriptForOpt(current.tag, targetOpt);
            if (search == DialogScriptSearchResult::Invalid)
                return fail("confirm page script subtree is unreadable", true);
            if (search != DialogScriptSearchResult::Found)
                return fail("confirm page cannot reach the target opt");
        }
        selected = current;
        hasSelected = true;
    }
    else
    {
        for (const auto &meta : metas)
        {
            if (meta.opt != 0 || meta.tag == 0) continue;
            auto search = SearchDialogScriptForOpt(meta.tag, targetOpt);
            if (search == DialogScriptSearchResult::Invalid)
                return fail("dialog branch script subtree is unreadable", true);
            if (search == DialogScriptSearchResult::Found)
            {
                if (hasSelected) return fail("multiple dialog branches can reach the target opt");
                selected = meta;
                hasSelected = true;
            }
        }
    }
    if (!hasSelected) return fail("no native dialog branch can reach the target opt");
    if (!allowAdvance) return fail("target opt is not visible and local advance is disabled");

    uint32_t expectedPending = 0;
    uint32_t actualPending = 0;
    if (!ExecuteDialogNode(context, selected.index, selected.node, selected.tag,
                           &expectedPending, &actualPending))
        return fail(actualPending == 0
                        ? "native dialog local advance failed"
                        : "native dialog tried to send a non-target opt");

    DialogClickContext nextContext{};
    std::vector<uint32_t> nextNodes;
    if (!LoadDialogClickContext(&nextContext) || nextContext.npcId != npcId ||
        !CollectDialogOptionNodes(nextContext.state, nextContext.mode, nextNodes))
        return fail("native dialog closed during a local-only advance");
    if (nextContext.mode == context.mode && nextNodes == nodes)
        return fail("native dialog did not change after a local-only advance");

    result.ok = true;
    result.advanced = true;
    result.optionIndex = selected.index;
    return result;
}

bool SelectDialogOptionChecked(uint32_t npcId, uint32_t targetOpt,
                               uint32_t dialogOption, uint32_t expectedWarpTableId)
{
    if (!npcId || !targetOpt || !expectedWarpTableId) return false;
    DialogClickContext context{};
    if (!LoadDialogClickContext(&context) || context.npcId != npcId) return false;

    uint32_t node = FindOptionNodeAtIndex(context.state, context.mode, dialogOption);
    uint32_t tag = 0;
    uint32_t opt = 0;
    uint32_t warpKey = 0;
    uint32_t warpTableId = 0;
    if (!ResolveDialogOptionNode(node, &tag, &opt, &warpKey, &warpTableId) ||
        tag == 0 || opt != targetOpt || warpKey == 0 ||
        warpTableId != expectedWarpTableId)
    {
        spdlog::warn(
            "GGTB::SelectDialogOptionChecked: mismatch npc={} actualNpc={} index={} opt={}/{} warp={}/{} table={}/{}",
            npcId, context.npcId, dialogOption, opt, targetOpt, warpKey, 0,
            warpTableId, expectedWarpTableId);
        return false;
    }

    auto beforeSelect = NetLog::GetLastDungeonDialogSelect();
    std::shared_ptr<NativeUiTask> task;
    std::string dispatchError;
    NativeUiWaitResult waitResult = NativeUiWaitResult::Unavailable;
    if (!InvokeDialogClick(context, dialogOption, node, tag, 0, true, targetOpt,
                           beforeSelect.sequence, &task, &dispatchError, &waitResult))
    {
        spdlog::warn(
            "GGTB::SelectDialogOptionChecked: native transaction dispatch failed npc={} opt={} index={} tag={:x} waitResult={} error={}",
            npcId, targetOpt, dialogOption, tag, static_cast<uint32_t>(waitResult),
            dispatchError);
        return false;
    }
    if (!task || !task->operationOk)
    {
        spdlog::warn(
            "GGTB::SelectDialogOptionChecked: native transaction failed npc={} opt={} index={} tag={:x} steps={} error={}",
            npcId, targetOpt, dialogOption, tag,
            task ? task->continuationSteps : 0,
            task ? task->error : "missing task result");
        return false;
    }
    auto afterSelect = NetLog::GetLastDungeonDialogSelect();
    if (afterSelect.sequence <= beforeSelect.sequence ||
        afterSelect.npcId != npcId || afterSelect.opt != targetOpt || afterSelect.sub != 1)
    {
        spdlog::warn(
            "GGTB::SelectDialogOptionChecked: GameLoop did not emit expected select npc={} opt={} beforeSeq={} afterSeq={} actual={}/{}/{}",
            npcId, targetOpt, beforeSelect.sequence, afterSelect.sequence,
            afterSelect.npcId, afterSelect.opt, afterSelect.sub);
        return false;
    }
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

// ---------- 技能宝石合成 (Compose / 三合一) ----------
namespace
{
// CSealTableManager::GetInstance() -> mgr;  GetTableElem(mgr, type, index, a4) -> elem ptr|0。
// 静态地址(VA, imagebase 0x400000):GetInstance @0xB6C6C0, GetTableElem @0xB6D450。
// 走 PatternResolver(注册名 SealTableMgrGetInstance / SealTableMgrGetTableElem),与项目惯例一致。
using SealTableMgrGetInstanceFn = void *(__cdecl *)();
using SealTableMgrGetTableElemFn = int(__thiscall *)(void *mgr, int type, int index, int a4);

// Net__SendPacket20B_3DW_NoTrack @0xB2C810 — __thiscall(netBuf, proto, w0, w1, w2)。
// 打包 {u32 20, proto, w0, w1, w2} 明文发出,无 dialog 状态闸/追踪副作用。
using NetSend20B3DWFn = int(__thiscall *)(void *netBuf, int proto, int w0, int w1, int w2);

static bool ReadU32SEH_Compose(uintptr_t addr, uint32_t *out)
{
    __try { *out = *reinterpret_cast<uint32_t *>(addr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// 取某宝石的等级键:gradeKey = GetTableElem(55, *(itemTable+0x454))[+0x14]。
// 顶级/不可合成(或表查不到)返回 kComposeNoGrade(4),与引擎 Compose_GetGemGradeKey_Tbl55 一致。
//
// 注意:整个流程拆成「纯 SEH 小函数(无 C++ 对象)」+「外层 C++ 包装」。MSVC 在 /EHsc 下,
// 同一函数里既有需要栈展开的 C++ 对象、又有 __try,会报 C2712;所以 __try 只放在不含任何
// 析构对象的纯函数里。
static bool ComposeReadRecipeIdSEH(uintptr_t itemTable, uint32_t *out)
{
    __try { *out = *reinterpret_cast<uint32_t *>(itemTable + kItemRecipeFieldOffset); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool ComposeQueryGradeKeySEH(SealTableMgrGetInstanceFn pGetInst,
                                    SealTableMgrGetTableElemFn pGetElem,
                                    uint32_t recipeId, uint32_t *out)
{
    __try
    {
        void *mgr = pGetInst();
        if (!mgr)
            return false;
        int elem = pGetElem(mgr, kSealTableTypeGemGrade, static_cast<int>(recipeId), 0);
        if (!elem)
            return false;
        *out = *reinterpret_cast<uint32_t *>(elem + kGemGradeTableElemOffset);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static uint32_t ComposeGemGradeKeySEH(uintptr_t itemTable)
{
    auto giAddr = PatternResolver::Get("SealTableMgrGetInstance");
    auto teAddr = PatternResolver::Get("SealTableMgrGetTableElem");
    if (!giAddr || !teAddr)
        return kComposeNoGrade;
    auto pGetInst = reinterpret_cast<SealTableMgrGetInstanceFn>(giAddr);
    auto pGetElem = reinterpret_cast<SealTableMgrGetTableElemFn>(teAddr);

    uint32_t recipeId = 0;
    if (!ComposeReadRecipeIdSEH(itemTable, &recipeId))
        return kComposeNoGrade;
    uint32_t gradeKey = 0;
    if (!ComposeQueryGradeKeySEH(pGetInst, pGetElem, recipeId, &gradeKey))
        return kComposeNoGrade;
    return gradeKey;
}

static int CallNetSend20B3DWSEH(NetBeginSendFn pBegin, NetSend20B3DWFn pSend,
                                int proto, int w0, int w1, int w2)
{
    __try
    {
        void *buf = pBegin();
        if (!buf)
            return -1;
        return pSend(buf, proto, w0, w1, w2);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}
} // anonymous

std::vector<ComposeGemGroup> ListComposeGemGroups()
{
    std::vector<ComposeGemGroup> result;
    auto containerPtrAddr = PatternResolver::Get("ItemContainerPtr");
    if (!containerPtrAddr)
        return result;

    uintptr_t container = 0;
    if (!ReadContainerPtrSEH(containerPtrAddr, &container) || !container)
        return result;

    // gradeKey -> result index,稳定分组(同 gradeKey 跨 itemId 合并)。
    std::unordered_map<uint32_t, size_t> keyToIdx;

    for (size_t i = 0; i < kItemFullSlotCount; ++i)
    {
        uintptr_t  slot = container + kItemArrayBaseOffset + i * kItemStride;
        BagSlotRaw raw{};
        if (!ReadBagSlotSEH(slot, &raw))
            continue;
        if (!raw.itemTable || raw.itemId == 0)
            continue;

        // 是不是技能宝石:itemTable+0x110(subtype) ∈ {72,73,90}。
        uint32_t subtype = 0;
        if (!ReadU32SEH_Compose(raw.itemTable + kItemSubtypeOffset, &subtype))
            continue;
        if (subtype != 72 && subtype != 73 && subtype != 90)
            continue;

        // 等级键;顶级(4)不可再合成,跳过。
        uint32_t gradeKey = ComposeGemGradeKeySEH(raw.itemTable);
        if (gradeKey == static_cast<uint32_t>(kComposeNoGrade))
            continue;

        uint32_t cnt = raw.count ? raw.count : 1;
        uint32_t wire = static_cast<uint32_t>(i) + kComposeWireSlotBase;

        auto it = keyToIdx.find(gradeKey);
        if (it == keyToIdx.end())
        {
            ComposeGemGroup g{};
            g.gradeKey     = gradeKey;
            g.sampleItemId = raw.itemId;
            char nameBuf[64] = {};
            if (ReadItemNameSEH(raw.itemTable, nameBuf, sizeof(nameBuf)))
                g.sampleName = Big5ToUtf8(nameBuf);
            g.totalCount = 0;
            keyToIdx[gradeKey] = result.size();
            result.push_back(std::move(g));
            it = keyToIdx.find(gradeKey);
        }
        ComposeGemGroup &g = result[it->second];
        g.totalCount += cnt;
        // 一槽多件:同 wire 槽重复 cnt 次(三合一按"件"取槽,同堆叠就是同一个 wire)。
        for (uint32_t k = 0; k < cnt; ++k)
            g.wireSlots.push_back(wire);
    }
    return result;
}

bool ComposeSendRaw(uint32_t slotA, uint32_t slotB, uint32_t slotC, std::string *errOut)
{
    auto setErr = [&](const char *m) { if (errOut) *errOut = m; };
    if (slotA >= kItemFullSlotCount || slotB >= kItemFullSlotCount || slotC >= kItemFullSlotCount)
    {
        setErr("slot index out of range (0..191)");
        return false;
    }

    auto beginAddr = PatternResolver::Get("NetBeginSend");
    auto sendAddr  = PatternResolver::Get("NetSendPacket20B3DW");
    if (!beginAddr || !sendAddr)
    {
        setErr("net pattern unresolved (NetBeginSend / NetSendPacket20B3DW)");
        return false;
    }
    auto pBegin = reinterpret_cast<NetBeginSendFn>(beginAddr);
    auto pSend  = reinterpret_cast<NetSend20B3DWFn>(sendAddr);

    int w0 = static_cast<int>(slotA + kComposeWireSlotBase);
    int w1 = static_cast<int>(slotB + kComposeWireSlotBase);
    int w2 = static_cast<int>(slotC + kComposeWireSlotBase);
    int rv = CallNetSend20B3DWSEH(pBegin, pSend, kProtocolCompose, w0, w1, w2);
    if (rv < 0)
    {
        setErr("SEH during compose send");
        return false;
    }
    spdlog::info("GGTB::ComposeSendRaw: slots={}/{}/{} wire={}/{}/{} rv={}",
                 slotA, slotB, slotC, w0, w1, w2, rv);
    return true;
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

// ---------- 打开附近玩家的个人摊位 (CG 411042) ----------
namespace
{
// CG_SendVisitStreetStall_411042 (0xB2B650) — __thiscall(client=NetBeginSend(), userId,
// stallType, Src=0)。内部门控:client+12(连接就绪)/ g_currentForegroundUIId!=4 /
// client+133(对话框)/ FindUserById(userId) 的摊位标志 CUser+0x2DEC!=0。成功写帧
// [len28][411042][userId][16×0] 并发出,返回 Net__SendPacket_Plaintext 结果;被门控拦下返回 0。
// 复刻点击路径(sub_9941A0@0x994693):发包后把摊主 userId 存进买摊窗口 content28+0x38,
// 服务器回 511555/511589/511595 据此填窗并弹出。全程走引擎自身的发送/UI call,不裸发包,不卡移动。
using StreetStallVisitFn = int(__thiscall *)(void *, int, int, void *);

static int CallVisitStallSEH(NetBeginSendFn pBegin, StreetStallVisitFn pSend,
                             UIManagerGetSingletonFn pSing, UIManagerGetUIContentFn pGet,
                             uint32_t userId, uint32_t stallType)
{
    __try
    {
        void *client = pBegin();
        if (!client)
            return -1;
        int rv = pSend(client, static_cast<int>(userId), static_cast<int>(stallType), nullptr);
        void *mgr = pSing();
        if (mgr)
        {
            void *content = pGet(mgr, kStallBuyContentId);
            if (content)
                *reinterpret_cast<uint32_t *>(reinterpret_cast<uintptr_t>(content) +
                                              kStallBuyContentOwnerIdOffset) = userId;
        }
        return rv;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}
} // anonymous

bool OpenStreetStall(uint32_t userId, uint32_t stallType)
{
    if (!userId)
        return false;

    auto beginAddr = PatternResolver::Get("NetBeginSend");
    auto sendAddr  = PatternResolver::Get("StreetStallVisitSend");
    auto singAddr  = PatternResolver::Get("UIManagerGetSingleton");
    auto getAddr   = PatternResolver::Get("UIManagerGetUIContent");
    if (!beginAddr || !sendAddr || !singAddr || !getAddr)
    {
        spdlog::error("GGTB::OpenStreetStall: pattern unresolved (begin={:x} send={:x} sing={:x} get={:x})",
                      beginAddr, sendAddr, singAddr, getAddr);
        return false;
    }

    int rv = CallVisitStallSEH(
        reinterpret_cast<NetBeginSendFn>(beginAddr),
        reinterpret_cast<StreetStallVisitFn>(sendAddr),
        reinterpret_cast<UIManagerGetSingletonFn>(singAddr),
        reinterpret_cast<UIManagerGetUIContentFn>(getAddr),
        userId, stallType);
    if (rv < 0)
    {
        spdlog::warn("GGTB::OpenStreetStall: SEH/send failed userId={} stallType={}", userId, stallType);
        return false;
    }
    spdlog::info("GGTB::OpenStreetStall: visited userId={} stallType={} rv={}", userId, stallType, rv);
    return rv > 0;
}

// ---------- 传送到地图格坐标 (CG 411597) ----------
namespace
{
// Net__SendPacket_Plaintext (0xB1CA60) — __thiscall(client, pktBuf, length)。最底层明文
// 发送:pktBuf 指向 {u32 totalLen, u32 proto, ...body}。加密开启时(client+13)会就地把
// body 往后挪 8 字节插时间戳/校验,故缓冲必须远大于帧长(引擎自己用 256B 栈缓冲发这条)。
using NetSendPlaintextFn = int(__thiscall *)(void *client, void *pkt, int len);

static int CallWarpCoordSEH(NetBeginSendFn pBegin, NetSendPlaintextFn pSend, int x, int y)
{
    __try
    {
        void *client = pBegin();
        if (!client)
            return -1;
        uint8_t  buf[256];
        memset(buf, 0, sizeof(buf));
        uint32_t *w = reinterpret_cast<uint32_t *>(buf);
        w[0] = 16;
        w[1] = static_cast<uint32_t>(kProtocolStreetStallWarp);
        w[2] = static_cast<uint32_t>(x);
        w[3] = static_cast<uint32_t>(y);
        return pSend(client, buf, 16);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}
} // namespace

bool WarpToCoordinate(int x, int y)
{
    auto beginAddr = PatternResolver::Get("NetBeginSend");
    auto sendAddr  = PatternResolver::Get("NetSendPacketPlaintext");
    if (!beginAddr || !sendAddr)
    {
        spdlog::error("GGTB::WarpToCoordinate: pattern unresolved (begin={:x} send={:x})",
                      beginAddr, sendAddr);
        return false;
    }

    int rv = CallWarpCoordSEH(reinterpret_cast<NetBeginSendFn>(beginAddr),
                              reinterpret_cast<NetSendPlaintextFn>(sendAddr), x, y);
    if (rv < 0)
    {
        spdlog::warn("GGTB::WarpToCoordinate: SEH/send failed x={} y={}", x, y);
        return false;
    }
    spdlog::info("GGTB::WarpToCoordinate: sent CG 411597 x={} y={} rv={}", x, y, rv);
    return true;
}

// ---------- 组队列表浏览 / 自动组队 (Party board) ----------
namespace
{
// 复用上面 anon namespace 里的 UIManager*SEH 包装 + SafeRead*。这里补三个 CMessenger
// 发送器 + 一个 u16 读取器。__try/__except 不能与有析构的 C++ 对象同函数,故全留在
// 这些零对象的小包装里;FetchPartyBoard 本体只调它们,自身不含 SEH。
using CMsgOpenPartyListFn = int(__thiscall *)(void *);          // op 0x4400
using CMsgReqPartyPageFn  = int(__thiscall *)(void *, int16_t); // op 0x4402
using CMsgJoinPartyFn     = int(__thiscall *)(void *, int);     // op 0x4413, __thiscall(messenger, partyId)
using CMsgLeavePartyFn    = int(__thiscall *)(void *);          // op 0x440B, __thiscall(messenger), 空 body

static bool CallCMsgOpenPartyListSEH(CMsgOpenPartyListFn fn, void *messenger)
{
    __try { fn(messenger); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool CallCMsgReqPartyPageSEH(CMsgReqPartyPageFn fn, void *messenger, int16_t page)
{
    __try { fn(messenger, page); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool CallCMsgJoinPartySEH(CMsgJoinPartyFn fn, void *messenger, int partyId)
{
    __try { fn(messenger, partyId); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool CallCMsgLeavePartySEH(CMsgLeavePartyFn fn, void *messenger)
{
    __try { fn(messenger); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool SafeReadWord(uintptr_t addr, uint16_t &out)
{
    __try { out = *reinterpret_cast<volatile uint16_t *>(addr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// 读 content(=GetUIContent(17))当前页 8 行,partyId==0 跳过。
static void ReadPartyBoardPage(uintptr_t content, std::vector<PartyBoardEntry> &out)
{
    for (size_t i = 0; i < kPartyBoardRowsPerPage; ++i)
    {
        uintptr_t row = content + kPartyBoardRowBase + i * kPartyBoardRowStride;
        uint32_t  pid = 0;
        if (!SafeReadDword(row + kPartyBoardOffPartyId, pid) || pid == 0)
            continue;

        PartyBoardEntry e;
        e.partyId = pid;
        uint16_t v = 0;
        if (SafeReadWord(row + kPartyBoardOffDistrib, v))    e.distribution = v;
        if (SafeReadWord(row + kPartyBoardOffCurMembers, v)) e.curMembers = v;
        if (SafeReadWord(row + kPartyBoardOffMaxMembers, v)) e.maxMembers = v;
        if (SafeReadWord(row + kPartyBoardOffMapId, v))      e.mapId = v;

        char nameBuf[kPartyBoardNameLen + 1] = {0};
        if (SafeReadString(row + kPartyBoardOffName, nameBuf, sizeof(nameBuf)))
            e.partyName = Big5ToUtf8(nameBuf);
        char leadBuf[kPartyBoardLeaderLen + 1] = {0};
        if (SafeReadString(row + kPartyBoardOffLeader, leadBuf, sizeof(leadBuf)))
            e.leaderName = Big5ToUtf8(leadBuf);

        out.push_back(std::move(e));
    }
}

static std::string TrimWs(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
} // namespace

PartyBoardSnapshot FetchPartyBoard()
{
    PartyBoardSnapshot snap;

    auto mgrAddr         = PatternResolver::Get("UIManagerGetSingleton");
    auto beginAddr       = PatternResolver::Get("UIManagerBeginContent");
    auto getAddr         = PatternResolver::Get("UIManagerGetUIContent");
    auto rendererAddr    = PatternResolver::Get("RendererPtr");
    auto msgrPtrAddr     = PatternResolver::Get("MessengerPtr");
    auto openAddr        = PatternResolver::Get("CMessengerSendOpenPartyList");
    auto reqAddr         = PatternResolver::Get("CMessengerReqPartyListPage");
    auto markAddr        = PatternResolver::Get("UIContentMarkClose");
    auto closeActiveAddr = PatternResolver::Get("UIManagerCloseActiveContent");
    if (!mgrAddr || !beginAddr || !getAddr || !rendererAddr || !msgrPtrAddr || !openAddr || !reqAddr)
    {
        spdlog::error("GGTB::FetchPartyBoard: pattern unresolved");
        return snap;
    }

    auto pMgr   = reinterpret_cast<UIManagerGetSingletonFn>(mgrAddr);
    auto pBegin = reinterpret_cast<UIManagerBeginContentFn>(beginAddr);
    auto pGet   = reinterpret_cast<UIManagerGetUIContentFn>(getAddr);
    auto pOpen  = reinterpret_cast<CMsgOpenPartyListFn>(openAddr);
    auto pReq   = reinterpret_cast<CMsgReqPartyPageFn>(reqAddr);

    void *mgr = CallUIManagerGetSingletonSEH(pMgr);
    if (!mgr)
    {
        spdlog::warn("GGTB::FetchPartyBoard: UI manager null");
        return snap;
    }
    uint32_t renderer = 0, messenger = 0;
    SafeReadDword(rendererAddr, renderer);
    SafeReadDword(msgrPtrAddr, messenger);
    if (!renderer || !messenger)
    {
        spdlog::warn("GGTB::FetchPartyBoard: renderer/messenger null r={:x} m={:x}", renderer, messenger);
        return snap;
    }

    // 1) 发开列表包 0x4400  2) BeginContent(17) 本地开窗(内部发 0x4402 page1)。
    CallCMsgOpenPartyListSEH(pOpen, reinterpret_cast<void *>(messenger));
    void *content = CallUIManagerBeginContentSEH(pBegin, mgr, kPartyBoardContentEnum,
                                                 static_cast<int>(renderer));
    if (!content) // 开窗 gate 失败时退一步:content 17 可能预建,直接取
        content = CallUIManagerGetUIContentSEH(pGet, mgr, kPartyBoardContentEnum);
    if (!content)
    {
        spdlog::warn("GGTB::FetchPartyBoard: party content (17) unavailable");
        return snap;
    }
    uintptr_t cbase = reinterpret_cast<uintptr_t>(content);

    // 等 page1 回流:maxPage 来自 0x4400 概要包(先到),行数据来自 0x4402 page1 回复(后到)。
    // 只等 maxPage>0 会在行数据落地前就读,导致第 1 页被当空页跳过 —— 故再等首行 pid 非零。
    // 空服没有任何队伍,首行永远为 0,走满超时后按空处理。
    int  maxPage   = 0;
    bool rowsReady = false;
    for (int i = 0; i < 40; ++i) // ~2s
    {
        uint16_t mp = 0;
        if (maxPage == 0 && SafeReadWord(cbase + kPartyBoardOffMaxPage, mp) && mp > 0)
            maxPage = mp;
        uint32_t pid0 = 0;
        if (SafeReadDword(cbase + kPartyBoardRowBase + kPartyBoardOffPartyId, pid0) && pid0 != 0)
            rowsReady = true;
        if (maxPage > 0 && rowsReady)
            break;
        Sleep(50);
    }
    uint16_t cp = 0;
    SafeReadWord(cbase + kPartyBoardOffCurPage, cp);
    snap.curPage = cp;
    snap.maxPage = maxPage;
    snap.ok      = true;

    std::unordered_set<uint32_t> seen;
    auto mergeCurrentPage = [&]() {
        std::vector<PartyBoardEntry> page;
        ReadPartyBoardPage(cbase, page);
        for (auto &e : page)
            if (seen.insert(e.partyId).second)
                snap.entries.push_back(std::move(e));
    };
    mergeCurrentPage(); // page 1

    // 后续页:发 0x4402(p),等 content+838(curPage)==p 再读。
    for (int p = 2; p <= maxPage; ++p)
    {
        CallCMsgReqPartyPageSEH(pReq, reinterpret_cast<void *>(messenger), static_cast<int16_t>(p));
        for (int i = 0; i < 30; ++i)
        {
            uint16_t now = 0;
            if (SafeReadWord(cbase + kPartyBoardOffCurPage, now) && now == static_cast<uint16_t>(p))
                break;
            Sleep(50);
        }
        mergeCurrentPage();
    }

    // 关窗:mark + CloseActiveContent(与仓库/摊贩同范式),抓完恢复干净状态。
    if (markAddr && closeActiveAddr)
    {
        auto pMark   = reinterpret_cast<UIContentMarkCloseFn>(markAddr);
        auto pCloseA = reinterpret_cast<UIManagerCloseActiveContentFn>(closeActiveAddr);
        CallUIContentMarkCloseSEH(pMark, content);
        CallUIManagerCloseActiveContentSEH(
            pCloseA, mgr, reinterpret_cast<void *>(static_cast<uintptr_t>(renderer)));
    }

    spdlog::info("GGTB::FetchPartyBoard: maxPage={} entries={}", maxPage, snap.entries.size());
    return snap;
}

// 加入前若已在别的队伍,先发退队包(op 0x440B)。队长无法把已在其它队伍里的人拉进来,
// 故必须先退。g_PartySelfRole>0 表示在队伍;退队后轮询等它复位(<=0)再返回,确保服务器
// 已把自己移出,避免退队未完成就发加入被拒。不在队伍时直接返回(退队 call 本身也是空操作)。
static void LeaveCurrentPartyIfAny(uint32_t messenger)
{
    auto roleAddr = PatternResolver::Get("PartySelfRole");
    if (!roleAddr)
        return;
    uint32_t roleRaw = 0;
    if (!SafeReadDword(roleAddr, roleRaw) || static_cast<int32_t>(roleRaw) <= 0)
        return; // 不在队伍

    auto leaveAddr = PatternResolver::Get("CMessengerLeaveParty");
    if (!leaveAddr)
    {
        spdlog::warn("GGTB::LeaveCurrentPartyIfAny: leave pattern unresolved");
        return;
    }
    CallCMsgLeavePartySEH(reinterpret_cast<CMsgLeavePartyFn>(leaveAddr),
                          reinterpret_cast<void *>(messenger));
    for (int i = 0; i < 40; ++i) // ~2s 等服务器把自己移出队伍
    {
        uint32_t now = 0;
        if (SafeReadDword(roleAddr, now) && static_cast<int32_t>(now) <= 0)
            break;
        Sleep(50);
    }
    spdlog::info("GGTB::LeaveCurrentPartyIfAny: left previous party before join");
}

bool SendJoinPartyById(uint32_t partyId)
{
    if (partyId == 0)
        return false;
    auto addr        = PatternResolver::Get("CMessengerJoinPartyById");
    auto msgrPtrAddr = PatternResolver::Get("MessengerPtr");
    if (!addr || !msgrPtrAddr)
    {
        spdlog::error("GGTB::SendJoinPartyById: pattern unresolved");
        return false;
    }
    uint32_t messenger = 0;
    SafeReadDword(msgrPtrAddr, messenger);
    if (!messenger)
    {
        spdlog::warn("GGTB::SendJoinPartyById: messenger null");
        return false;
    }
    // 先退掉当前队伍(若有),否则加入会被服务器拒。
    LeaveCurrentPartyIfAny(messenger);
    // op 0x4413 是 __thiscall(messenger, partyId):函数把 ECX 存起来再当 this 传给打包 call。
    bool ok = CallCMsgJoinPartySEH(reinterpret_cast<CMsgJoinPartyFn>(addr),
                                   reinterpret_cast<void *>(messenger),
                                   static_cast<int>(partyId));
    spdlog::info("GGTB::SendJoinPartyById: partyId={} ok={}", partyId, ok);
    return ok;
}

bool JoinPartyByLeaderName(const std::string &leaderNameUtf8, uint32_t *outJoinedId,
                           std::string *outErr)
{
    std::string target = TrimWs(leaderNameUtf8);
    if (target.empty())
    {
        if (outErr) *outErr = "创建人名字为空";
        return false;
    }

    PartyBoardSnapshot snap = FetchPartyBoard();
    if (!snap.ok)
    {
        if (outErr) *outErr = "打开组队列表失败";
        return false;
    }

    for (const auto &e : snap.entries)
    {
        if (TrimWs(e.leaderName) == target)
        {
            bool ok = SendJoinPartyById(e.partyId);
            if (outJoinedId) *outJoinedId = e.partyId;
            if (!ok && outErr) *outErr = "找到队伍但发送加入包失败";
            return ok;
        }
    }

    if (outErr)
        *outErr = "未找到创建人为「" + target + "」的队伍(列表 " +
                  std::to_string(snap.entries.size()) + " 个)";
    return false;
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
    // SetAfterAction returns the local-user pointer on its success path. A zero
    // return is a transient action-gate rejection (the previous move/animation
    // may still be draining), not proof that the requested map route is blocked.
    // Keep zero as false; PathWalker backs off and retries it without replanning.
    spdlog::info("GGTB::MoveTo: x={} y={} action={} target={} rv={}",
                 worldX, worldY, action, targetId, rv);
    return rv > 0;
}

// ---------- Teleport (instant warp; bypasses TraceMove interpolation) ----------
bool Teleport(float worldX, float worldY)
{
    auto user = GetLocalUserPtr();
    if (!user)
    {
        spdlog::warn("GGTB::Teleport: g_pLocalUser not resolved");
        return false;
    }

    // Engine reads/writes XY as truncated ints; keep the floats for the visible
    // position fields (which are floats) and ints for the after-action target.
    const int   ix = static_cast<int>(worldX);
    const int   iy = static_cast<int>(worldY);
    const float fx = static_cast<float>(ix);
    const float fy = static_cast<float>(iy);

    // 1) Snap the VISIBLE position (+0x3C/+0x44). Z (+0x40) is intentionally left
    //    alone — the engine re-derives terrain height next frame.
    bool wrote = SafeWriteFloat(user + kPositionXOffset, fx) &&
                 SafeWriteFloat(user + kPositionYOffset, fy);
    if (!wrote)
    {
        spdlog::warn("GGTB::Teleport: SEH writing visible pos x={} y={}", worldX, worldY);
        return false;
    }

    // 2) Mirror the after-action target to the same point and clear the pending
    //    flag so TraceMove sees the move as already complete (no interpolation,
    //    no "walk toward target" packets).
    SafeWriteDword(user + kAfterActionTargetXOffset, static_cast<uint32_t>(ix));
    SafeWriteDword(user + kAfterActionTargetYOffset, static_cast<uint32_t>(iy));
    SafeWriteByte (user + kAfterActionPendingOffset, 0);

    // 2b) Snap the VERTICAL/Z (+0x40) to the destination's terrain height. The
    //     engine normally recomputes Z every frame in TraceMove's tail
    //     (sub_682E60: *(this+0x40)=TerrainHeightAt(X,Y)); since we clear pending
    //     and skip that path, we must query it ourselves — otherwise the avatar
    //     keeps the SOURCE Z and ends up underground (warp to lower terrain) or
    //     floating (warp to higher terrain). g_pCurMap is the map object that
    //     owns the terrain mesh; CurMapPtr resolves to its pointer slot.
    auto mapPtrSlot = PatternResolver::Get("CurMapPtr");
    auto terrainAddr = PatternResolver::Get("TerrainHeightAt");
    if (mapPtrSlot && terrainAddr)
    {
        void *map = ReadMapPtrSEH(mapPtrSlot);
        if (map)
        {
            float z = CallTerrainHeightSEH(
                reinterpret_cast<TerrainHeightFn>(terrainAddr), map, fx, fy);
            if (z == z) // not NaN
                SafeWriteFloat(user + kPositionZOffset, z);
            else
                spdlog::warn("GGTB::Teleport: TerrainHeightAt SEH x={} y={}", ix, iy);
        }
    }
    else
    {
        spdlog::warn("GGTB::Teleport: terrain-height pattern unresolved "
                     "(map={:x} fn={:x}) — Z not adjusted, may clip ground",
                     mapPtrSlot, terrainAddr);
    }

    // 3) Push the absolute coords to the server (411000). Best-effort — the
    //    engine's own gates may no-op it, and the server still validates the
    //    jump distance. Reuses the same helper as the city-teleport path.
    auto beginAddr = PatternResolver::Get("NetBeginSend");
    auto syncAddr  = PatternResolver::Get("SendPlayerMoveSync");
    if (beginAddr && syncAddr)
    {
        CallMoveSyncSEH(reinterpret_cast<NetBeginSendFn>(beginAddr),
                        reinterpret_cast<PlayerMoveSyncFn>(syncAddr));
    }
    else
    {
        spdlog::warn("GGTB::Teleport: move-sync pattern unresolved (begin={:x} sync={:x}) "
                     "— local pos moved but server not notified",
                     beginAddr, syncAddr);
    }

    spdlog::info("GGTB::Teleport: x={} y={}", ix, iy);
    return true;
}

// ---------- Native world-click dispatch for NPCs and monsters ----------
namespace
{
// EntityManager::FindCreatureById — __thiscall(this=*g_pCreatureMgr, id) -> CCreature*.
using FindCreatureByIdFn = uintptr_t(__thiscall *)(void *mgr, int id);

static uintptr_t CallFindCreatureSEH(FindCreatureByIdFn fn, void *mgr, uint32_t id)
{
    __try { return fn(mgr, static_cast<int>(id)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// SEH-only wrapper around SetAfterAction. Kept in its own function because the
// callers hold std::string locals, which MSVC forbids in a function with __try.
static int CallSetAfterActionForCreatureSEH(uintptr_t fnAddr, void *user,
                                            int x, int y, int action, int target)
{
    auto fn = reinterpret_cast<int(__thiscall *)(void *, int, int, int, int)>(fnAddr);
    __try { return fn(user, x, y, action, target); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return INT_MIN; }
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
    if (!findAddr || !mgrAddr || !setAddr || !idAddr || !intentAddr || !flag2Addr)
    {
        spdlog::error(
            "GGTB::TalkOrAttack: pattern unresolved find={:x} mgr={:x} set={:x} id={:x} intent={:x} flag2={:x}",
            findAddr, mgrAddr, setAddr, idAddr, intentAddr, flag2Addr);
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

    uint32_t statTable = 0;
    uint32_t creatureType = 0;
    if (!SafeReadDword(cre + kCreatureStatTableOffset, statTable) || !statTable ||
        !SafeReadDword(statTable + kStatTableTypeOffset, creatureType))
    {
        spdlog::warn("GGTB::TalkOrAttack: failed to classify creature id={}", creatureId);
        return false;
    }
    const bool isNpc = creatureType == kCreatureTypeTalkable;
    if (!isNpc && !IsMonsterType(creatureType))
    {
        spdlog::warn("GGTB::TalkOrAttack: unsupported creature type={} id={}",
                     creatureType, creatureId);
        return false;
    }

    // Mirror WorldClick__DispatchTargetOrGround: kind=3 (talkable NPC) queues
    // action 5, while kind=2 (combat creature) queues action 3. The main game
    // loop consumes action 5 and opens the NPC dialog on its own thread.
    const uint32_t action = isNpc ? 5u : 3u;
    *reinterpret_cast<volatile int32_t *>(idAddr) = static_cast<int32_t>(creatureId);
    *reinterpret_cast<volatile uint32_t *>(intentAddr) = action;
    if (!isNpc)
        *reinterpret_cast<volatile uint8_t *>(flag2Addr) = 0;

    int rv = CallSetAfterActionForCreatureSEH(setAddr, reinterpret_cast<void *>(user),
                                              cX, cY, static_cast<int>(action), 0);
    if (isNpc)
    {
        *reinterpret_cast<volatile uint8_t *>(flag2Addr) = 0;
        uint32_t skillMode = 0;
        if (SafeReadDword(user + kUserSkillModeOffset, skillMode) && skillMode != 3)
            SafeWriteDword(user + kUserSelectedItemIdOffset, 0);
    }
    if (rv == INT_MIN)
    {
        spdlog::warn("GGTB::TalkOrAttack[{}]: SEH on SetAfterAction id={}",
                     isNpc ? "NPC" : "MOB", creatureId);
        return false;
    }

    spdlog::info(
        "GGTB::TalkOrAttack[{}]: queued native click id={} action={} dst=({},{}) rv={}",
        isNpc ? "NPC" : "MOB", creatureId, action, cX, cY, rv);
    return rv > 0;
}

bool SelectMonsterForAttack(uint32_t creatureId, MonsterTargetSelectionResult &result,
                            std::string *error)
{
    result = {};
    if (error) error->clear();
    if (creatureId == 0)
    {
        if (error) *error = "creatureId must be greater than zero";
        return false;
    }

    CreatureSnapshot creature{};
    if (!LookupCreatureById(creatureId, creature))
    {
        if (error) *error = "creature is no longer available";
        return false;
    }
    if (!IsMonsterType(creature.kind))
    {
        if (error) *error = "creature is not a supported combat monster";
        spdlog::warn("GGTB::SelectMonsterForAttack: unsupported creature type={} id={}",
                     creature.kind, creatureId);
        return false;
    }
    if (creature.hp <= 0)
    {
        if (error) *error = "creature is no longer alive";
        return false;
    }

    int32_t targetX = 0;
    int32_t targetY = 0;
    if (!SafeReadDword(creature.addr + kCreatureQueuedXOffset,
                       *reinterpret_cast<uint32_t *>(&targetX)) ||
        !SafeReadDword(creature.addr + kCreatureQueuedYOffset,
                       *reinterpret_cast<uint32_t *>(&targetY)))
    {
        if (error) *error = "creature attack position is unreadable";
        return false;
    }

    auto user = GetLocalUserPtr();
    auto setAddr = PatternResolver::Get("SetAfterAction");
    auto idAddr = PatternResolver::Get("TargetCreatureId");
    auto intentAddr = PatternResolver::Get("AfterActionIntent");
    auto flag2Addr = PatternResolver::Get("AfterActionFlag2");
    if (!user || !setAddr || !idAddr || !intentAddr || !flag2Addr)
    {
        if (error) *error = "combat target patterns are unresolved";
        return false;
    }

    if (!SafeWriteDword(idAddr, creatureId) ||
        !SafeWriteDword(intentAddr, 3) ||
        !SafeWriteByte(flag2Addr, 0))
    {
        if (error) *error = "failed to select the combat target";
        return false;
    }

    int rv = CallSetAfterActionForCreatureSEH(setAddr, reinterpret_cast<void *>(user),
                                              targetX, targetY, 3, 0);
    SafeWriteByte(flag2Addr, 0);
    if (rv == INT_MIN)
    {
        if (error) *error = "native monster attack dispatch raised an exception";
        return false;
    }

    uint32_t selectedId = 0;
    if (!SafeReadDword(idAddr, selectedId) || selectedId != creatureId)
    {
        if (error) *error = "combat target changed before selection was confirmed";
        return false;
    }

    result.creatureId = creatureId;
    result.creatureType = creature.kind;
    result.hp = creature.hp;
    result.targetSelected = true;
    result.attackQueued = rv > 0;
    spdlog::info(
        "GGTB::SelectMonsterForAttack: selected id={} type={} hp={} actionQueued={} rv={}",
        creatureId, creature.kind, creature.hp, result.attackQueued, rv);
    return true;
}

// ---------- Nearby-NPC snapshot ----------
//
// Walks CreatureMgr exactly like GetNearbyMonsters does, but WITHOUT the
// monster-only filter: we keep NPCs, monsters and interactables (only dead
// entries are dropped) so the UI can show and label them all. `attackable`
// preserves the existing explicit attack whitelist. `buffGateMonster` is a
// separate conservative classification based on the live table24 dump plus
// the native auto-target exclusion field at statTable+0xF0.
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

    // type sits inside StatTable (statTable+0xDC), NOT on CCreature itself. We
    // keep every type here (NPC, monster, tree, teleport) and let the caller
    // filter — the monster tab shows all of them. No pet drop: the live pet
    // type value is unverified, and a wrong constant would silently drop real
    // creatures (a monster type could collide with a stale pet id).
    uint32_t type = 0;
    if (!SafeReadDword(statTable + kStatTableTypeOffset, type))
        return 3;

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

    // Legacy statTable+0xEC data-key, surfaced for UI/debug. The actual table24
    // row index used by BuffKeeper is statTable+0x04 and is read separately.
    uint32_t monsterTblId = 0;
    SafeReadDword(statTable + kStatTableMonsterTblIdOffset, monsterTblId);

    // Keep the legacy attack whitelist separate from BuffKeeper's ordinary-
    // enemy semantics. The latter requires the native F0 gate plus real combat
    // stats; unreadable fields fail closed.
    const bool isNpc      = (type == kCreatureTypeTalkable);
    const bool attackable = IsMonsterType(type);
    uint32_t tableIndex = 0;
    uint32_t autoTargetExcludeRaw = 0;
    uint32_t attack = 0, hitRate = 0, missRate = 0;
    const bool buffGateFieldsKnown =
        SafeReadDword(statTable + kStatTableIndexOffset, tableIndex) &&
        SafeReadDword(statTable + kStatTableAutoTargetExcludeOffset, autoTargetExcludeRaw) &&
        SafeReadDword(statTable + kStatTableAttackOffset, attack) &&
        SafeReadDword(statTable + kStatTableHitRateOffset, hitRate) &&
        SafeReadDword(statTable + kStatTableMissRateOffset, missRate);
    const bool buffGateMonster =
        buffGateFieldsKnown &&
        IsBuffGateMonster(tableIndex, type, static_cast<int32_t>(autoTargetExcludeRaw),
                          attack, hitRate, missRate);

    // Full/template HP at statTable+0xA8 (see kStatTableMaxHpOffset comment).
    // Best-effort; 0 for most NPCs (they carry no combat template).
    int64_t maxHp = 0;
    SafeReadInt64(statTable + kStatTableMaxHpOffset, maxHp);

    // Display name lives at statTable+8 as a Big5 char[100]. See header comment.
    // SEH-wrapped via SafeReadString. Convert to UTF-8 for the UI; empty on miss.
    std::string nameUtf8;
    char nameBuf[kStatTableNameMaxLen + 1] = {};
    if (SafeReadString(statTable + kStatTableNameOffset, nameBuf, sizeof(nameBuf)) && nameBuf[0])
        nameUtf8 = Big5ToUtf8(nameBuf);

    out.push_back({id, type, monsterTblId, isNpc, attackable, buffGateMonster,
                   level, d, x, y, z, hp, maxHp, std::move(nameUtf8), cre});
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

    for (; visited < kMaxNodes && node; ++visited)
    {
        int r = VisitNpcCandidate(node, lx, ly, lz, maxDistance, result);
        if (r >= 0 && r < 16) reject[r]++;
        if (!SafeReadDword(node + kCreatureNextOffset, node))
            break;
    }

    std::sort(result.begin(), result.end(),
              [](const NearbyNpc &a, const NearbyNpc &b) {
                  return a.distance < b.distance;
              });

    // Throttle the summary to ~5s: MonsterListModule polls this every 150ms while
    // its tab is open, and an unconditional info line per poll floods ggtb.log.
    static DWORD lastLogMs = 0;
    const DWORD  now       = GetTickCount();
    if (now - lastLogMs >= 5000)
    {
        lastLogMs = now;
        int npcCount = 0, attackableCount = 0, buffGateMonsterCount = 0;
        for (const auto &n : result)
        {
            if (n.isNpc)
                npcCount++;
            if (n.attackable)
                attackableCount++;
            if (n.buffGateMonster)
                buffGateMonsterCount++;
        }
        spdlog::info(
            "GGTB::GetNearbyNpcs: mgr={:x} visited={} accepted={} npc={} "
            "attackable={} buffGateMonster={} "
            "rej(null={} stat={} typeRd={} unused={} state={} dead={} id={} pos={} range={}) maxDist={}",
            static_cast<uintptr_t>(mgr), visited,
            static_cast<int>(result.size()), npcCount, attackableCount,
            buffGateMonsterCount,
            reject[1], reject[2], reject[3], reject[4], reject[5],
            reject[6], reject[9], reject[10], reject[11], maxDistance);
    }

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
        SafeReadDword(out->statTable + kStatTableTypeOffset, out->kind);
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

// ===================== Segmented path walker =====================
//
// See CLocalPlayer.h for the full rationale. Short version: one SetAfterAction
// only walks straight toward a single target and stalls at the first blocked
// tile, so far targets don't move. This worker re-issues MoveTo segment by
// segment — each segment snapped to a verified-walkable tile via the engine
// collision grid — so the avatar keeps going and skirts convex obstacles.
namespace
{
// World units == tiles (positions are tile+0.5 centers). Conservative tuning:
// gentle issue rate, generous timeouts.
constexpr int   kPathPollMs       = 80;    // arrival poll cadence
constexpr int   kPathRejectRetryMs = 120;  // SetAfterAction rv=0 is usually a short busy gate
constexpr int   kPathRejectRetries = 12;   // observe/retry for ~1.4s before reporting busy
constexpr int   kPathPendingGraceMs = 160; // pending can clear before the CG_MOVE lands locally
constexpr int   kPathReplanBackoffMs = 250;// never spin A* repeatedly in one scheduler slice
constexpr float kPathArriveEps    = 1.5f;  // within this of final -> issue final segment
constexpr float kPathWaypointEps  = 0.8f;  // close enough to a waypoint
constexpr int   kPathSegTimeoutMs = 2500;  // give up waiting on one segment, re-evaluate
constexpr float kPathMinProgress  = 1.0f;  // moved less than this in a segment -> no headway
// Hard giveup is TIME-based, not replan-count based: only fail after the avatar's
// tile hasn't changed for this long. A single segment the engine refuses (e.g. a
// 1-tile diagonal into a doorway corner — TraceMove's CalcDir returns "no valid
// dir" and clears pending without moving) must NOT kill the whole path; we skip it
// and aim at the next waypoint. Any net tile movement resets this clock.
constexpr int   kPathStuckMs      = 30000; // zero net tile movement this long -> fail
constexpr int   kPathRoutePenalty = 10000; // prefer a genuine alternate over a failed local step
constexpr int   kPathRoutePenaltyCap = 60000;
constexpr int   kPathSmoothMaxSpan = 24;   // algorithmic waypoints; no workflow midpoints needed

// Grid A* starts local, then doubles the search margin when a maze corridor
// leaves the initial start/goal box. The final full-map pass is still bounded by
// maxCells, so callers keep one logical destination without workflow waypoints.
constexpr int kPathAstarMargins[] = {48, 96, 192};
constexpr int kPathAstarMaxCells  = 400000; // full 512x512 maps fit under this cap
constexpr int kPathAstarMaxPops   = kPathAstarMaxCells;

struct PathWalkerCtl
{
    std::thread        thread;
    std::atomic<bool>  stop{false};
    std::mutex         mtx; // guards the snapshot fields below
    PathState          state = PathState::Idle;
    float              tgtX = 0, tgtY = 0;
    float              curX = 0, curY = 0;
    int                segments = 0;
    std::string        error;
};
PathWalkerCtl g_pathCtl;

void PathSetSnapshot(PathState st, float cx, float cy, int segs, const char *err)
{
    std::lock_guard<std::mutex> lk(g_pathCtl.mtx);
    g_pathCtl.state    = st;
    g_pathCtl.curX     = cx;
    g_pathCtl.curY     = cy;
    g_pathCtl.segments = segs;
    g_pathCtl.error    = err ? err : "";
}

bool PathReadPending(uint8_t &pending)
{
    auto user = GetLocalUserPtr();
    if (!user)
        return false;
    return SafeReadByte(user + kAfterActionPendingOffset, pending);
}

inline int   FloorTile(float w) { return static_cast<int>(std::floor(w)); }
inline float TileCenter(int t)  { return static_cast<float>(t) + 0.5f; }
inline float Dist2(float ax, float ay, float bx, float by)
{
    float dx = ax - bx, dy = ay - by;
    return dx * dx + dy * dy;
}

enum class PathPlanResult
{
    Found,
    Cancelled,
    EndpointBlocked,
    InvalidBounds,
    CellBudgetExceeded,
    PopBudgetExceeded,
    Exhausted,
};

struct PathPlanDiag
{
    int            margin = 0; // -1 = full map
    int            minX   = 0;
    int            maxX   = 0;
    int            minY   = 0;
    int            maxY   = 0;
    int64_t        cells  = 0;
    int            pops   = 0;
    PathPlanResult result = PathPlanResult::InvalidBounds;
};

using PathTilePenalties = std::unordered_map<uint64_t, int>;

uint64_t PathTileKey(int x, int y)
{
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
           static_cast<uint32_t>(y);
}

const char *PathPlanResultName(PathPlanResult result)
{
    switch (result)
    {
    case PathPlanResult::Found:              return "found";
    case PathPlanResult::Cancelled:          return "cancelled";
    case PathPlanResult::EndpointBlocked:    return "endpoint-blocked";
    case PathPlanResult::InvalidBounds:      return "invalid-bounds";
    case PathPlanResult::CellBudgetExceeded: return "cell-budget";
    case PathPlanResult::PopBudgetExceeded:  return "pop-budget";
    case PathPlanResult::Exhausted:           return "exhausted";
    }
    return "unknown";
}

// 8-neighbour grid A* in tile space over the engine collision grid. Each call
// searches one requested bbox (or the whole map for margin=-1); the worker
// retries with wider boxes. Diagonal steps refuse to cut a wall corner.
PathPlanResult PathPlanGrid(const MapView &v, int sx, int sy, int gx, int gy,
                            int searchMargin,
                            const PathTilePenalties &tilePenalties,
                            std::vector<std::pair<int, int>> &outPath,
                            PathPlanDiag &diag)
{
    outPath.clear();
    diag        = {};
    diag.margin = searchMargin;
    if (TileBlocked(v, sx, sy) || TileBlocked(v, gx, gy))
    {
        diag.result = PathPlanResult::EndpointBlocked;
        return diag.result;
    }

    // Local int helpers — Windows.h's min/max macros are live in this TU, so
    // std::min/std::max would macro-expand into garbage. Same for abs below.
    auto imin = [](int a, int b) { return a < b ? a : b; };
    auto imax = [](int a, int b) { return a > b ? a : b; };
    auto iabs = [](int a) { return a < 0 ? -a : a; };

    int minX = searchMargin < 0 ? 0 : imax(0, imin(sx, gx) - searchMargin);
    int minY = searchMargin < 0 ? 0 : imax(0, imin(sy, gy) - searchMargin);
    int maxX = searchMargin < 0 ? v.width - 1 : imin(v.width - 1, imax(sx, gx) + searchMargin);
    int maxY = searchMargin < 0 ? v.height - 1 : imin(v.height - 1, imax(sy, gy) + searchMargin);
    int bw = maxX - minX + 1;
    int bh = maxY - minY + 1;
    diag.minX  = minX;
    diag.maxX  = maxX;
    diag.minY  = minY;
    diag.maxY  = maxY;
    diag.cells = static_cast<int64_t>(bw) * bh;
    if (bw <= 0 || bh <= 0)
    {
        diag.result = PathPlanResult::InvalidBounds;
        return diag.result;
    }
    if (diag.cells > kPathAstarMaxCells)
    {
        diag.result = PathPlanResult::CellBudgetExceeded;
        return diag.result;
    }

    const int N = bw * bh;
    auto idx = [&](int x, int y) { return (y - minY) * bw + (x - minX); };

    std::vector<int>  gScore(N, INT_MAX);
    std::vector<int>  came(N, -1);
    std::vector<char> closed(N, 0);

    auto H = [&](int x, int y) {
        int dx = iabs(x - gx), dy = iabs(y - gy);
        int mn = imin(dx, dy), mx = imax(dx, dy);
        return 14 * mn + 10 * (mx - mn); // octile, same scale as the 10/14 step costs
    };

    // (f, cellIndex) min-heap via std::greater. Lazy deletion: a cell can sit in
    // the heap multiple times; the closed[] guard drops stale pops.
    using QNode = std::pair<int, int>;
    std::priority_queue<QNode, std::vector<QNode>, std::greater<QNode>> open;

    const int start = idx(sx, sy), goal = idx(gx, gy);
    gScore[start] = 0;
    open.emplace(H(sx, sy), start);

    static const int dX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int dY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    static const int dC[8] = {10, 10, 10, 10, 14, 14, 14, 14};

    int  pops  = 0;
    bool found = false;
    while (!open.empty())
    {
        if ((pops & 0x3FF) == 0 && g_pathCtl.stop.load())
        {
            diag.pops   = pops;
            diag.result = PathPlanResult::Cancelled;
            return diag.result;
        }
        QNode top = open.top();
        open.pop();
        int cur = top.second;
        if (closed[cur])
            continue;
        closed[cur] = 1;
        if (cur == goal) { found = true; break; }
        if (++pops > kPathAstarMaxPops)
        {
            diag.pops   = pops;
            diag.result = PathPlanResult::PopBudgetExceeded;
            return diag.result;
        }

        int cx = minX + (cur % bw);
        int cy = minY + (cur / bw);
        for (int k = 0; k < 8; ++k)
        {
            int nx = cx + dX[k], ny = cy + dY[k];
            if (nx < minX || ny < minY || nx > maxX || ny > maxY)
                continue;
            if (TileBlocked(v, nx, ny))
                continue;
            // Diagonal: don't slip through a wall corner — both orthogonal
            // neighbours the diagonal touches must be open too.
            if (k >= 4 &&
                (TileBlocked(v, cx + dX[k], cy) || TileBlocked(v, cx, cy + dY[k])))
                continue;
            int ni = idx(nx, ny);
            if (closed[ni])
                continue;
            int extraCost = 0;
            auto penalty = tilePenalties.find(PathTileKey(nx, ny));
            if (penalty != tilePenalties.end())
                extraCost = penalty->second;
            int ng = gScore[cur] + dC[k] + extraCost;
            if (ng < gScore[ni])
            {
                gScore[ni] = ng;
                came[ni]   = cur;
                open.emplace(ng + H(nx, ny), ni);
            }
        }
    }
    if (!found)
    {
        diag.pops   = pops;
        diag.result = PathPlanResult::Exhausted;
        return diag.result;
    }

    for (int c = goal; c != -1; c = came[c])
        outPath.emplace_back(minX + (c % bw), minY + (c / bw));
    std::reverse(outPath.begin(), outPath.end());
    diag.pops   = pops;
    diag.result = PathPlanResult::Found;
    return diag.result;
}

// Funnel / string-pull the dense per-tile A* path into a short waypoint list:
// from the current anchor, extend forward while a clear straight line of sight
// (RaycastFurthestWalkable) still reaches path[j]; emit the furthest reachable
// node and repeat from there. Lets the engine walk long straight legs instead of
// poking it once per tile. Progress is guaranteed: path[anchor+1] is an adjacent
// walkable tile so LOS to it is always clear.
void PathSmooth(const std::vector<std::pair<int, int>> &path,
                std::vector<std::pair<int, int>> &outWp)
{
    outWp.clear();
    if (path.empty())
        return;
    outWp.push_back(path.front());
    size_t anchor = 0;
    while (anchor + 1 < path.size())
    {
        size_t reach = anchor + 1;
        for (size_t j = anchor + 1; j < path.size(); ++j)
        {
            int spanX = path[j].first - path[anchor].first;
            int spanY = path[j].second - path[anchor].second;
            if (spanX < 0) spanX = -spanX;
            if (spanY < 0) spanY = -spanY;
            if (spanX > kPathSmoothMaxSpan || spanY > kPathSmoothMaxSpan)
                break;
            int rx = 0, ry = 0;
            if (RaycastFurthestWalkable(path[anchor].first, path[anchor].second,
                                        path[j].first, path[j].second, rx, ry) &&
                rx == path[j].first && ry == path[j].second)
                reach = j;
            else
                break;
        }
        outWp.push_back(path[reach]);
        anchor = reach;
    }
}

void PathWorkerLoop(float finalX, float finalY, int finalAction, uint32_t finalTargetId)
{
    spdlog::info("GGTB::PathWalker: start -> ({:.1f},{:.1f}) action={} target={}",
                 finalX, finalY, finalAction, finalTargetId);

    int   segments = 0;
    int   replans  = 0;
    float cx = 0, cy = 0, cz = 0;

    auto fail = [&](const char *why) {
        spdlog::warn("GGTB::PathWalker: fail seg={} replans={} reason={}", segments, replans, why);
        PathSetSnapshot(PathState::Failed, cx, cy, segments, why);
    };

    if (!GetLocalPosition(cx, cy, cz))
    {
        fail("g_pLocalUser not resolved");
        return;
    }

    int finalTX = FloorTile(finalX), finalTY = FloorTile(finalY);
    if (!IsTileWalkable(finalTX, finalTY))
    {
        // Goal dropped on a wall — snap it to the furthest walkable tile along
        // the line from our current pos, so we at least end up adjacent.
        int sX = FloorTile(cx), sY = FloorTile(cy), rX = sX, rY = sY;
        if (RaycastFurthestWalkable(sX, sY, finalTX, finalTY, rX, rY))
        {
            finalTX = rX;
            finalTY = rY;
        }
    }

    std::vector<std::pair<int, int>> waypoints;
    size_t                           wpIdx = 0;
    std::string                      lastPlanError = "path planner unavailable";
    PathTilePenalties                routePenalties;

    // (Re)plan: run A* from the current tile to the goal tile, then funnel-smooth
    // into waypoints. Called up front and again whenever we stall (a dynamic
    // blocker moved in, or the engine bailed on a segment). wpIdx starts at 1 to
    // skip our own start tile. Returns false if A* finds no route.
    auto replan = [&]() -> bool {
        MapView v{};
        if (!ReadCurMapView(v))
        {
            lastPlanError = "map collision grid not ready";
            return false;
        }
        std::vector<std::pair<int, int>> raw;
        PathPlanDiag usedDiag{};
        bool         planned      = false;
        bool         searchedFull = false;

        auto attempt = [&](int margin) {
            PathPlanDiag diag{};
            PathPlanResult result = PathPlanGrid(v, FloorTile(cx), FloorTile(cy),
                                                 finalTX, finalTY, margin,
                                                 routePenalties, raw, diag);
            spdlog::info(
                "GGTB::PathWalker: A* scope={} margin={} box=({},{})->({},{}) cells={} pops={} result={}",
                diag.margin < 0 ? "full" : "boxed", diag.margin,
                diag.minX, diag.minY, diag.maxX, diag.maxY,
                diag.cells, diag.pops, PathPlanResultName(result));
            usedDiag = diag;
            if (result == PathPlanResult::Found)
                planned = true;
            searchedFull = diag.minX == 0 && diag.minY == 0 &&
                           diag.maxX == v.width - 1 && diag.maxY == v.height - 1;
            return result;
        };

        PathPlanResult lastResult = PathPlanResult::InvalidBounds;
        int64_t fullCells = static_cast<int64_t>(v.width) * v.height;
        if (!routePenalties.empty() && fullCells <= kPathAstarMaxCells)
        {
            // Once a locally accepted move proves unable to advance, search the
            // whole affordable map so a route outside the first bbox can win.
            lastResult = attempt(-1);
        }
        else
        {
            for (int margin : kPathAstarMargins)
            {
                lastResult = attempt(margin);
                if (planned || searchedFull ||
                    lastResult == PathPlanResult::Cancelled ||
                    lastResult == PathPlanResult::EndpointBlocked ||
                    lastResult == PathPlanResult::InvalidBounds ||
                    lastResult == PathPlanResult::CellBudgetExceeded ||
                    lastResult == PathPlanResult::PopBudgetExceeded)
                    break;
            }
        }
        if (!planned && !searchedFull &&
            lastResult == PathPlanResult::Exhausted)
            lastResult = attempt(-1); // final complete-map fallback, still maxCells-bounded

        if (!planned)
        {
            switch (lastResult)
            {
            case PathPlanResult::Cancelled:
                lastPlanError = "path planning cancelled";
                break;
            case PathPlanResult::EndpointBlocked:
                lastPlanError = "path start or target tile is blocked";
                break;
            case PathPlanResult::CellBudgetExceeded:
                lastPlanError = "path search exceeds cell budget";
                break;
            case PathPlanResult::PopBudgetExceeded:
                lastPlanError = "path search exceeds pop budget";
                break;
            case PathPlanResult::Exhausted:
                lastPlanError = searchedFull ? "no path in full collision grid"
                                             : "no path within adaptive search bounds";
                break;
            default:
                lastPlanError = "path planner returned invalid bounds";
                break;
            }
            return false;
        }
        PathSmooth(raw, waypoints);
        wpIdx = (waypoints.size() > 1) ? 1 : 0;
        ++replans;
        spdlog::info("GGTB::PathWalker: plan #{} — {} tiles -> {} waypoints "
                     "(scope={} margin={} penalties={})",
                     replans, raw.size(), waypoints.size(),
                     usedDiag.margin < 0 ? "full" : "boxed", usedDiag.margin,
                     routePenalties.size());
        return waypoints.size() > wpIdx;
    };

    if (!replan())
    {
        if (g_pathCtl.stop.load())
            PathSetSnapshot(PathState::Idle, cx, cy, segments, "cancelled");
        else
            fail(lastPlanError.c_str());
        return;
    }

    // Net-movement clock: the hard giveup is "avatar's tile hasn't changed for
    // kPathStuckMs", not a raw replan count. Any tile change resets it.
    int   lastTileX = FloorTile(cx), lastTileY = FloorTile(cy);
    DWORD lastMoveTick = GetTickCount();
    auto  noteMovement = [&]() {
        int tx = FloorTile(cx), ty = FloorTile(cy);
        if (tx != lastTileX || ty != lastTileY)
        {
            lastTileX = tx;
            lastTileY = ty;
            lastMoveTick = GetTickCount();
        }
    };
    auto stuckTooLong = [&]() {
        return GetTickCount() - lastMoveTick > static_cast<DWORD>(kPathStuckMs);
    };

    enum SegResult { SegReached, SegMoved, SegStalled, SegRejected, SegLost, SegCancelled };

    // Issue one MoveTo to a tile center and poll until we land on that tile, the
    // engine clears pending, or the segment times out. Updates cx/cy/segments.
    auto walkSegment = [&](int tileX, int tileY) -> SegResult {
        float wpX = TileCenter(tileX), wpY = TileCenter(tileY);
        float requestStartX = cx, requestStartY = cy;
        int rejectRetries = 0;
        while (!MoveTo(wpX, wpY, 1, 0))
        {
            // rv=0 is SetAfterAction's temporary action gate. The previous
            // accepted move can still update our position after this call, so
            // observe before retrying and never turn a busy gate into an A* run.
            ++rejectRetries;
            if (g_pathCtl.stop.load())
                return SegCancelled;
            Sleep(kPathRejectRetryMs);
            if (!GetLocalPosition(cx, cy, cz))
                return SegLost;
            noteMovement();
            PathSetSnapshot(PathState::Walking, cx, cy, segments, "");
            if ((FloorTile(cx) == tileX && FloorTile(cy) == tileY) ||
                Dist2(wpX, wpY, cx, cy) <= kPathWaypointEps * kPathWaypointEps)
                return SegReached;
            if (Dist2(requestStartX, requestStartY, cx, cy) >=
                kPathMinProgress * kPathMinProgress)
                return SegMoved;
            if (rejectRetries >= kPathRejectRetries)
            {
                spdlog::info("GGTB::PathWalker: movement gate busy after {} retries "
                             "at ({},{}) toward ({},{})",
                             rejectRetries, FloorTile(cx), FloorTile(cy), tileX, tileY);
                return SegRejected;
            }
        }
        if (rejectRetries > 0)
            spdlog::info("GGTB::PathWalker: movement gate recovered after {} retries",
                         rejectRetries);
        ++segments;
        float segStartX = cx, segStartY = cy;
        DWORD segStart   = GetTickCount();
        for (;;)
        {
            if (g_pathCtl.stop.load())
                return SegCancelled;
            Sleep(kPathPollMs);
            if (!GetLocalPosition(cx, cy, cz))
                return SegLost;
            noteMovement();
            PathSetSnapshot(PathState::Walking, cx, cy, segments, "");

            if ((FloorTile(cx) == tileX && FloorTile(cy) == tileY) ||
                Dist2(wpX, wpY, cx, cy) <= kPathWaypointEps * kPathWaypointEps)
                return SegReached;

            uint8_t pending = 1;
            if (PathReadPending(pending) && pending == 0 &&
                GetTickCount() - segStart >= static_cast<DWORD>(kPathPendingGraceMs))
                break; // engine finished/abandoned the move — re-evaluate
            if (GetTickCount() - segStart > static_cast<DWORD>(kPathSegTimeoutMs))
                break; // too long — re-evaluate
        }
        if ((FloorTile(cx) == tileX && FloorTile(cy) == tileY) ||
            Dist2(wpX, wpY, cx, cy) <= kPathWaypointEps * kPathWaypointEps)
            return SegReached;
        if (Dist2(segStartX, segStartY, cx, cy) >= kPathMinProgress * kPathMinProgress)
            return SegMoved;
        return SegStalled;
    };

    // The engine refuses some legal-looking diagonal steps (a 1-tile diagonal whose
    // destination hugs a wall corner: TraceMove's CalcDir returns "no valid dir" and
    // clears pending without moving — this is what stalls native click-move at
    // doorways). Cardinal (axis-aligned) steps are always honoured, so when a segment
    // stalls we step ONE cardinal tile toward the waypoint to thread the corner. A*'s
    // corner rule guarantees at least one orthogonal of any diagonal is walkable.
    std::vector<std::pair<int, int>> stalledRecoverySteps;
    auto cardinalNudge = [&](int towardX, int towardY) -> SegResult {
        stalledRecoverySteps.clear();
        int curTX = FloorTile(cx), curTY = FloorTile(cy);
        int sx = (towardX > curTX) - (towardX < curTX);
        int sy = (towardY > curTY) - (towardY < curTY);
        // Prefer the axis with the larger remaining distance.
        bool xFirst = (towardX - curTX) * sx >= (towardY - curTY) * sy;
        for (int pass = 0; pass < 2; ++pass)
        {
            bool doX = (pass == 0) == xFirst;
            if (doX && sx != 0 && IsTileWalkable(curTX + sx, curTY))
            {
                SegResult result = walkSegment(curTX + sx, curTY);
                if (result != SegStalled)
                    return result;
                stalledRecoverySteps.emplace_back(curTX + sx, curTY);
            }
            if (!doX && sy != 0 && IsTileWalkable(curTX, curTY + sy))
            {
                SegResult result = walkSegment(curTX, curTY + sy);
                if (result != SegStalled)
                    return result;
                stalledRecoverySteps.emplace_back(curTX, curTY + sy);
            }
        }
        return SegStalled; // boxed in — caller will replan / time out
    };

    while (!g_pathCtl.stop.load())
    {
        if (!GetLocalPosition(cx, cy, cz))
        {
            fail("lost local player mid-path");
            return;
        }
        noteMovement();
        PathSetSnapshot(PathState::Walking, cx, cy, segments, "");

        // Arrived? Either within eps of the requested point, or standing on the
        // goal tile (covers a goal that was snapped off a wall). Final hop
        // carries the requested action/target so action=3 engages at the dest.
        int curTX = FloorTile(cx), curTY = FloorTile(cy);
        if (Dist2(finalX, finalY, cx, cy) <= kPathArriveEps * kPathArriveEps ||
            (curTX == finalTX && curTY == finalTY))
        {
            MoveTo(finalX, finalY, finalAction, finalTargetId);
            spdlog::info("GGTB::PathWalker: arrived after {} segments / {} replans",
                         segments, replans);
            PathSetSnapshot(PathState::Arrived, cx, cy, segments, "");
            return;
        }

        // Ran off the end of the current plan without arriving — replan from here.
        if (wpIdx >= waypoints.size())
        {
            if (stuckTooLong())
            {
                fail("stuck — engine won't advance toward goal");
                return;
            }
            if (!replan())
            {
                if (g_pathCtl.stop.load())
                    PathSetSnapshot(PathState::Idle, cx, cy, segments, "cancelled");
                else
                    fail(lastPlanError.c_str());
                return;
            }
            continue;
        }

        // Already standing on this waypoint's tile — advance (a MoveTo here would be
        // a no-op the engine reports as "already arrived").
        if (curTX == waypoints[wpIdx].first && curTY == waypoints[wpIdx].second)
        {
            ++wpIdx;
            continue;
        }

        SegResult r = walkSegment(waypoints[wpIdx].first, waypoints[wpIdx].second);
        if (r == SegCancelled)
        {
            PathSetSnapshot(PathState::Idle, cx, cy, segments, "cancelled");
            return;
        }
        if (r == SegLost)
        {
            fail("lost local player mid-segment");
            return;
        }
        if (r == SegReached)
        {
            ++wpIdx; // landed on the waypoint tile — advance
            continue;
        }
        if (r == SegMoved)
            continue; // progressed but stopped short — re-issue the same waypoint
        if (r == SegRejected)
        {
            // SetAfterAction's action gate is busy. Route geometry did not fail,
            // so keep the same plan and let the time-based watchdog decide.
            if (stuckTooLong())
            {
                fail("movement action gate stayed busy");
                return;
            }
            continue;
        }

        // SegStalled: the command was accepted but produced no movement. Thread
        // the corner with cardinal steps before treating it as a route problem.
        SegResult n = cardinalNudge(waypoints[wpIdx].first, waypoints[wpIdx].second);
        if (n == SegCancelled)
        {
            PathSetSnapshot(PathState::Idle, cx, cy, segments, "cancelled");
            return;
        }
        if (n == SegLost)
        {
            fail("lost local player mid-segment");
            return;
        }
        if (n == SegRejected)
        {
            // Same transient action gate as above; do not poison an A* tile.
            if (stuckTooLong())
            {
                fail("movement action gate stayed busy");
                return;
            }
            continue;
        }
        if (n == SegStalled)
        {
            // Both locally valid cardinal exits were accepted but made no headway.
            // Penalise them for this path run; full-map A* will choose a different
            // first leg when one exists, while a finite cost keeps single corridors usable.
            if (stuckTooLong())
            {
                fail("stuck — engine won't advance toward goal");
                return;
            }
            for (const auto &step : stalledRecoverySteps)
            {
                uint64_t key = PathTileKey(step.first, step.second);
                int &cost = routePenalties[key];
                cost += kPathRoutePenalty;
                if (cost > kPathRoutePenaltyCap)
                    cost = kPathRoutePenaltyCap;
                spdlog::warn("GGTB::PathWalker: alternate-route penalty tile=({},{}) "
                             "cost={} from=({},{})",
                             step.first, step.second, cost,
                             FloorTile(cx), FloorTile(cy));
            }
            Sleep(kPathReplanBackoffMs);
            if (g_pathCtl.stop.load())
            {
                PathSetSnapshot(PathState::Idle, cx, cy, segments, "cancelled");
                return;
            }
            if (!replan())
            {
                if (g_pathCtl.stop.load())
                    PathSetSnapshot(PathState::Idle, cx, cy, segments, "cancelled");
                else
                    fail(lastPlanError.c_str());
                return;
            }
        }
    }

    PathSetSnapshot(PathState::Idle, cx, cy, segments, "cancelled");
    spdlog::info("GGTB::PathWalker: cancelled after {} segments", segments);
}
} // anonymous

void StopPath()
{
    g_pathCtl.stop.store(true);
    if (g_pathCtl.thread.joinable())
        g_pathCtl.thread.join();
    // Joining only stops our worker. Cancel the engine's last accepted target as
    // well, otherwise a broker retry can begin while the old CG_MOVE is still
    // advancing for another frame or two.
    auto user = GetLocalUserPtr();
    uint8_t pending = 0;
    if (user && SafeReadByte(user + kAfterActionPendingOffset, pending) && pending)
    {
        SafeWriteByte(user + kAfterActionPendingOffset, 0);
        spdlog::info("GGTB::PathWalker: stop cleared engine pending move");
    }
    g_pathCtl.stop.store(false);
}

bool StartPathTo(float worldX, float worldY, int action, uint32_t targetId)
{
    StopPath(); // cancel + join any in-flight path first

    if (!GetLocalUserPtr())
    {
        spdlog::warn("GGTB::StartPathTo: g_pLocalUser not resolved");
        return false;
    }
    float cx = 0, cy = 0, cz = 0;
    if (!GetLocalPosition(cx, cy, cz))
        return false;
    if (!IsTileWalkable(FloorTile(cx), FloorTile(cy)))
    {
        spdlog::warn("GGTB::StartPathTo: start tile not walkable (map not ready?)");
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(g_pathCtl.mtx);
        g_pathCtl.state    = PathState::Walking;
        g_pathCtl.tgtX     = worldX;
        g_pathCtl.tgtY     = worldY;
        g_pathCtl.curX     = cx;
        g_pathCtl.curY     = cy;
        g_pathCtl.segments = 0;
        g_pathCtl.error.clear();
    }
    g_pathCtl.stop.store(false);
    g_pathCtl.thread = std::thread(PathWorkerLoop, worldX, worldY, action, targetId);
    return true;
}

PathProgress GetPathProgress()
{
    std::lock_guard<std::mutex> lk(g_pathCtl.mtx);
    PathProgress p;
    p.state    = g_pathCtl.state;
    p.targetX  = g_pathCtl.tgtX;
    p.targetY  = g_pathCtl.tgtY;
    p.curX     = g_pathCtl.curX;
    p.curY     = g_pathCtl.curY;
    p.segments = g_pathCtl.segments;
    p.error    = g_pathCtl.error;
    return p;
}

void PathWalkerShutdown()
{
    StopPath();
}

} // namespace GGTB
