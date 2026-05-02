#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace GGTB
{

// CUser-derived field offsets (matches SO3DPlus.exe / unpackd_so3d.exe layout)
inline constexpr uintptr_t kPositionXOffset      = 0x3C;
inline constexpr uintptr_t kPositionYOffset      = 0x40;
inline constexpr uintptr_t kPositionZOffset      = 0x44;
inline constexpr uintptr_t kMoveSpeedOffset      = 0x1A4;
inline constexpr uintptr_t kNameOffset           = 0x1AC0; // char[~16], null-terminated
inline constexpr size_t    kNameMaxLen           = 0x20;
// 8 per-job CalcStatus writes + 2 TraceMove per-frame clamps (mounted/normal).
// All 10 entries get the same 8-byte NOP — they're either `movss [reg+1A4],xmm0`
// (CalcStatus) or `movss [eax+1A4],xmm0` (TraceMove); same instruction shape, same length.
inline constexpr size_t    kMoveSpeedWriteCount  = 10;
inline constexpr size_t    kMoveSpeedPatchSize   = 8; // movss [reg+1A4h], xmm0

// attackSpeed / skillSpeed: animation divisors (lower value = faster animation).
// SkillTable.preTime (+0x174 in this build — note the offset shift from old +0x150)
// IS still read by Skill__GetPreTime; relying on skillSpeed alone leaves the cast
// startup intact. PatchPreTime() rewrites the two `movss xmm0,[reg+0x174]` reads
// to point at &s_preTimeConst (=0.0001f) so startup + animation both shrink.
inline constexpr uintptr_t kAttackSpeedOffset    = 0x2E1C;
inline constexpr uintptr_t kSkillSpeedOffset     = 0x2E20;
inline constexpr size_t    kAttackSpeedPatchSize = 8; // movss [eax+2E1Ch], xmm1
inline constexpr size_t    kSkillSpeedPatchSize  = 8; // movss [eax+2E20h], xmm1
inline constexpr size_t    kPreTimePatchSize     = 8; // movss xmm0, [reg+0x174]

// Attack range (CUser+0x35AC, int "attackRangeTiles"). Same MoveSpeed pattern:
// CalcStatus rewrites it every frame (baseline=1 -> max-clamp weapon -> add buffs),
// so we NOP the 3 writes and poke the field directly. Combat__GetAttackRangeSquared
// then squares it with a hard cap of 225 (=15^2) -- raise that cap at the imm32
// inside both `cmp imm32 0E1h` sites to allow ranges > 15 tiles.
inline constexpr uintptr_t kAttackRangeOffset           = 0x35AC;
inline constexpr size_t    kAttackRangeWrite1PatchSize  = 10; // mov dword [eax+35ACh], 1
inline constexpr size_t    kAttackRangeWrite2PatchSize  = 6;  // mov [ecx+35ACh], eax
inline constexpr size_t    kAttackRangeWrite3PatchSize  = 6;  // mov [edx+35ACh], eax
inline constexpr size_t    kAttackRangeCapImmSize       = 4;  // imm32 inside `cmp imm32`
inline constexpr uint32_t  kAttackRangeCapDefault       = 4096; // 64 tiles squared, generous

// Item short-cd: replaces a 14-byte sequence inside StatTable__SetItemCdRemaining
// (linear 0x79AACE) with `mov dword [ecx+eax+0EE0h], imm32` + 3 NOPs. This writes
// a small fixed cd-remaining value to the stat entry that ItemTblEntry__GetCooldownRatio
// path-3 actually reads (itemEntry+0x28 == statTable+i*0x118+0xEE0).
//
// Original 14 bytes:
//   F3 0F 10 4D F4              movss xmm0, [ebp+var_C]      ; v4 = ApplyCdReductionBuff(GetCdDuration)
//   F3 0F 11 84 01 E0 0E 00 00  movss [ecx+eax+0EE0h], xmm0  ; statEntry.remainingCd = v4
// Replacement (14 bytes):
//   C7 84 01 E0 0E 00 00 <imm32 float>  mov dword [ecx+eax+0EE0h], imm32   (11B)
//   90 90 90                             nop nop nop                       (3B)
//
// Why not the SkillCdMgr setter at 0x756E98 (the previous attempt): that's
// GetCooldownRatio's path 2, which short-circuits independently. Path 3 still
// reads itemEntry+0x28 unchanged and gates inventory item-use for the full
// original duration regardless of any path-2 patch.
inline constexpr size_t    kItemShortCdPatchSize = 14;

uintptr_t GetLocalUserPtr();
float    *GetMoveSpeedPtr();
float    *GetAttackSpeedPtr();
float    *GetSkillSpeedPtr();

bool PatchMoveSpeed();
bool RestoreMoveSpeed();

bool PatchAttackSpeed();
bool RestoreAttackSpeed();

bool PatchSkillSpeed();
bool RestoreSkillSpeed();

bool PatchPreTime();
bool RestorePreTime();

bool PatchItemShortCD(float seconds);
bool RestoreItemShortCD();

int *GetAttackRangePtr();
bool PatchAttackRange();
bool RestoreAttackRange();
bool PatchAttackRangeCap(uint32_t newCap);
bool RestoreAttackRangeCap();

// ---------- Action-time movement bypass ----------
//
// Removes the "can't move while attacking / casting" lock by patching 4 sites:
//
//   SetAfterActionGate  @ 0x7539FE: 5-byte `E9 C1 02 00 00` overwrites the first
//                                   `jle short loc_753A22` and unconditionally
//                                   jumps to the success path at 0x753CC4. Skips
//                                   every reject gate inside SetAfterAction.
//   TraceMoveGate1      @ 0x756FAA: flip `76 -> EB` so the per-frame stunTime
//                                   check (+0x3468) is always skipped.
//   TraceMoveOrChain    @ 0x756FCD: 5-byte `E9 19 01 00 00` jumps to
//                                   TraceMove_PostGateChain (0x7570EB), skipping
//                                   the long animation/skill ID OR-chain.
//   TraceMoveGate2      @ 0x7570FD: flip `76 -> EB` so the +0x2BCC per-frame
//                                   timer check is always skipped.
inline constexpr size_t kActionMoveSetAfterActionGateSize = 5; // jle short -> jmp near
inline constexpr size_t kActionMoveTraceMoveGate1Size     = 1; // jbe short -> jmp short
inline constexpr size_t kActionMoveTraceMoveOrChainSize   = 5; // cmp imm32 -> jmp near
inline constexpr size_t kActionMoveTraceMoveGate2Size     = 1; // jbe short -> jmp short

bool PatchActionMove();
bool RestoreActionMove();

// ---------- Entity iteration (remote players nearby) ----------
//
// EntityManager exposes two views of the same player set:
//   +0x08 = std::map<int, CUser*>           (used by FindUserById)
//   +0x0C = singly-linked list head, CUser* (used by FindUserByPartialName)
//
// We walk the linked list — it's curated for around-player AOI (only entries
// the client cares about), is much shallower than the rb-tree, and doesn't
// race with red-black-tree rotations during scene transitions. so3dFullCheat's
// old build used the same pattern (head=+0x10, next=+0x236C, name=+0x1410);
// new build offsets shifted by ~+0xAC0 in line with attackSpeed (0x235C→0x2E1C).
inline constexpr uintptr_t kUserMgrListHeadOffset = 0x0C;
inline constexpr uintptr_t kUserNextOffset        = 0x2E30;
inline constexpr uintptr_t kUserKindOffset        = 0x1AE8;
inline constexpr uint32_t  kUserKindPlayer        = 5;

struct NearbyPlayer
{
    std::string name;
    float       distance;
    float       x, y, z;
};

bool        GetLocalPosition(float &x, float &y, float &z);
std::string GetLocalPlayerName();

// Walks the EntityManager's around-player linked list, filters by
// kind==kUserKindPlayer (so CLocalUser / monsters / NPCs are skipped), and
// returns entries within `maxDistance` of the local player. Wrapped in SEH to
// survive races with list mutation.
std::vector<NearbyPlayer> GetAroundPlayers(const std::string &localName,
                                           float              maxDistance);

// ---------- Drop-item iteration (auto-pickup) ----------
//
// Layout verified across LookupDropItemById, the DropItem allocator
// (sub_7978A0), and the engine's own auto-hunt loot handler (sub_6093E0).
//   ItemContainer + 0x6C = head of singly-linked DropItem list (CItemContainer)
//   DropItem layout (alloc size 0x118):
//     +0x00 dropId (uint32, primary key for SendPickItemPacket)
//     +0x04 itemId (uint32, item table id)
//     +0x14/0x18/0x1C worldX/Y/Z (float)
//     +0x2E canPick (uint8 — 1 if free to grab, 0 if owned by another player)
//     +0x88 next (DropItem*)
inline constexpr uintptr_t kDropContainerHeadOffset = 0x6C;
inline constexpr uintptr_t kDropIdOffset            = 0x00;
inline constexpr uintptr_t kDropItemIdOffset        = 0x04;
inline constexpr uintptr_t kDropPosXOffset          = 0x14;
inline constexpr uintptr_t kDropPosYOffset          = 0x18;
inline constexpr uintptr_t kDropPosZOffset          = 0x1C;
inline constexpr uintptr_t kDropCanPickOffset       = 0x2E;
inline constexpr uintptr_t kDropNextOffset          = 0x88;

struct DropItemInfo
{
    uint32_t dropId;
    uint32_t itemId;
    float    distance;
    float    x, y, z;
    bool     canPick;
};

// SEH-safe walk of CItemContainer's drop list. Returns drops within
// `maxDistance` of the local player (or all drops if maxDistance <= 0),
// sorted ascending by distance. Filters out canPick==0 entries by default
// (controlled by includeUnpickable).
std::vector<DropItemInfo> GetNearbyDropItems(float maxDistance,
                                             bool  includeUnpickable = false);

// Fires SendPickItemPacket(dropId) on the engine helper. SEH-wrapped because
// engine state may transiently invalidate the container or the drop. Returns
// the engine's own return value (1 = packet sent, 0 = rejected upstream).
int SendPickItem(uint32_t dropId);

} // namespace GGTB
