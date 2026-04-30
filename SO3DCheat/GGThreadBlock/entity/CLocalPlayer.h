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

} // namespace GGTB
