#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace GGTB
{

// CUser-derived field offsets (matches SO3DPlus.exe / unpackd_so3d.exe layout)
// IMPORTANT — engine's world-coord layout is X, Z, Y (NOT X, Y, Z):
//   +0x3C = worldX
//   +0x40 = worldZ (vertical / terrain height)
//   +0x44 = worldY
// Verified against CLocalUser::TraceMove @ 0x756F10 which uses v11[15] (+0x3C)
// and v11[17] (+0x44) as the ground-plane components, and AutoHunt__TickBattle
// which feeds (creature+60, creature+68) into SetAfterAction as (x, y).
// OnPlayerMoveClick passes UI (worldX, worldY) which become target.X@+0x32F0
// and target.Y@+0x32F4. Do NOT swap +0x40 and +0x44.
inline constexpr uintptr_t kPositionXOffset      = 0x3C;
inline constexpr uintptr_t kPositionZOffset      = 0x40; // vertical
inline constexpr uintptr_t kPositionYOffset      = 0x44; // ground-plane Y
inline constexpr uintptr_t kMoveSpeedOffset      = 0x1A4;
inline constexpr uintptr_t kNameOffset           = 0x1AC0; // char[~16], null-terminated
inline constexpr size_t    kNameMaxLen           = 0x20;
// 8 per-job CalcStatus writes + 2 TraceMove per-frame clamps (mounted/normal)
// + 5 mount/state refresh writes. All entries get the same 8-byte NOP: each is
// a `movss [reg+1A4],xmm0`-style write to CLocalUser.moveSpeed.
inline constexpr size_t    kMoveSpeedWriteCount  = 15;
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

// Live-tunes the constant the patched movss reads from (see s_preTimeConst in
// the .cpp). Safe to call regardless of patch state — the patched code only
// reaches this address when PatchPreTime is active; otherwise it's just a
// data write nobody reads. No need to re-patch on change.
void  SetPreTimeConst(float v);
float GetPreTimeConst();

bool PatchItemShortCD(float seconds);
bool RestoreItemShortCD();

int *GetAttackRangePtr();
bool PatchAttackRange();
bool RestoreAttackRange();
bool PatchAttackRangeCap(uint32_t newCap);
bool RestoreAttackRangeCap();

// ---------- Skill cast range (SkillTable+0x170 via SkillTable__GetSkillRange) ----------
//
// SkillTable__GetSkillRange @ 0x939890 returns SkillTable+0x170 (tiles). It's
// the ONLY reader of that field used by the engine's distance gates:
//   - Net__SendSkillPacket has 9 call sites that reject the cast if
//     (targetDist^2 > range^2).
//   - SkillTable__GetSkillRangeSq wraps it for squared-distance comparisons
//     (OutputDebugStringA "Skill GetRangeSq : %d" is the confirm tag).
//   - sub_A89AD0 uses it to normalise walk animation against skill range.
//
// Reading the field without patching (FireFullPower's in-range filter) is
// enough for auto-cast scoring, but manual casts still go through the engine
// gate — so anything beyond the skill's configured tile range silently drops.
//
// Patch strategy: overwrite the function prologue with `mov eax, imm32; ret`
// (6 bytes). Covers all 13 callers at once. Updating the slider only rewrites
// the imm32 at +1 (no need to re-enable).
inline constexpr size_t kSkillRangePatchSize = 6; // B8 XX XX XX XX C3

bool PatchSkillRange(uint32_t tiles);
bool RestoreSkillRange();

// ---------- Action-time movement bypass ----------
//
// Removes the "can't move while attacking / casting" lock by patching 5 sites:
//
//   SetAfterActionGate  @ 0x7539FE: 5-byte `E9 C1 02 00 00` overwrites the first
//                                   `jle short loc_753A22` and unconditionally
//                                   jumps to the success path at 0x753CC4. Skips
//                                   every reject gate inside SetAfterAction.
//   InstantCastStartup  @ 0x756331: 6-byte `E9 C1 00 00 00 90` overwrites
//                                   `jbe loc_7563F7` and always jumps to the
//                                   immediate-send branch. Skips the preTime
//                                   mode=3 raise-hand/start-cast branch.
//   TraceMoveGate1      @ 0x756FAA: flip `76 -> EB` so the per-frame stunTime
//                                   check (+0x3468) is always skipped.
//   TraceMoveOrChain    @ 0x756FCD: 5-byte `E9 19 01 00 00` jumps to
//                                   TraceMove_PostGateChain (0x7570EB), skipping
//                                   the long animation/skill ID OR-chain.
//   TraceMoveGate2      @ 0x7570FD: flip `76 -> EB` so the +0x2BCC per-frame
//                                   timer check is always skipped.
inline constexpr size_t kActionMoveSetAfterActionGateSize = 5; // jle short -> jmp near
inline constexpr size_t kActionMoveInstantCastStartupSize  = 6; // jbe near -> jmp near + nop
inline constexpr size_t kActionMoveTraceMoveGate1Size     = 1; // jbe short -> jmp short
inline constexpr size_t kActionMoveTraceMoveOrChainSize   = 5; // cmp imm32 -> jmp near
inline constexpr size_t kActionMoveTraceMoveGate2Size     = 1; // jbe short -> jmp short

bool PatchActionMove();
bool RestoreActionMove();

// ---------- Block level-up (CLocalPlayer__UpdateExp send-gate flip) ----------
//
// CLocalPlayer__UpdateExp @ 0x878760 ends with a guard: when current EXP
// reaches the per-level cap (and player is alive, level < max), it calls
// Net__BeginSend + Net__SendDword(412016) — that's CG_LEVEL_UP_CHECK, the
// client asking the server to bump level. Server replies GC_LEVEL_UP_SUCC
// carrying the authoritative new level; the client never sets level on its
// own. Suppress the request and the character pins at the current level
// indefinitely while EXP keeps accumulating server-side.
//
// Site: 0x878BBD — `7D 23` (jge short loc_878BE2). Original semantics:
// "if (currentExp < maxExp) skip the send block." Patch: `7D -> EB`
// (jge -> jmp short). Same +0x23 displacement byte, so we always jump over
// the send pair. UI text / sound / spdlog ahead of the gate are untouched.
// One-byte rewrite — minimum footprint.
inline constexpr size_t kBlockLevelUpPatchSize = 1; // jge short -> jmp short

bool PatchBlockLevelUp();
bool RestoreBlockLevelUp();

// Sends the same CG_LEVEL_UP_CHECK packet emitted by CLocalPlayer::UpdateExp
// when EXP reaches the level cap. IDA shows the engine pushes 0x64970 twice:
// Net__SendDword(buf, 412016, 412016).
bool RequestLevelUpCheck(int payload = 412016);

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
    uint32_t    profession;
    std::string professionName;
    float       distance;
    float       x, y, z;
    // CUser 当前 HP(明文 int32 @+0x1B00)。来自 CUser__GetCurrentHP(0x9FC340):
    // 远程玩家 +0x1B00 存明文,只有本地玩家自己那条是 XOR 加密的。-1=未读到。
    // 比组队状态表(g_PartyMemberStateArray)的 HP 实时,组队 buff 守护据此剔除死人。
    int32_t     hp = -1;
};

bool        GetLocalPosition(float &x, float &y, float &z);
std::string GetLocalPlayerName();
std::string GetProfessionName(uint32_t profession);

// Walks the EntityManager's around-player linked list, filters by
// kind==kUserKindPlayer (so CLocalUser / monsters / NPCs are skipped), and
// returns entries within `maxDistance` of the local player. Wrapped in SEH to
// survive races with list mutation.
std::vector<NearbyPlayer> GetAroundPlayers(const std::string &localName,
                                           float              maxDistance);

// 反查 around-player AOI list 中 userId 对应的角色名 (UTF-8)。
// 公屏聊天 511004 包带 senderUserId 但不带名字,引擎自己也是 EntityManager
// 反查的。AOI 范围之外(野外/不同区)拿不到,返回空字符串。
std::string LookupAroundPlayerNameById(uint32_t userId);
bool LookupAroundPlayerById(uint32_t userId, NearbyPlayer &out);

// ---------- Drop-item iteration (auto-pickup) ----------
//
// Layout verified across LookupDropItemById, the DropItem allocator
// (sub_7978A0), and the engine's own auto-hunt loot handler (sub_6093E0).
//   ItemContainer + 0x6C = head of singly-linked DropItem list (CItemContainer)
//   DropItem layout (alloc size 0x118):
//     +0x00 dropId (uint32, primary key for SendPickItemPacket)
//     +0x04 itemId (uint32, item table id)
//     +0x14 worldX, +0x18 vertical, +0x1C worldY (float) — SAME field order as
//       CLocalUser (+0x3C worldX / +0x40 vertical / +0x44 worldY).
//     +0x2E canPick (uint8 — 1 if free to grab, 0 if owned by another player)
//     +0x88 next (DropItem*)
// GetLocalPosition returns the tuple (worldX, worldY, vertical), so the drop
// Y/Z offsets below are mapped to MATCH that tuple: kDropPosYOffset=worldY(0x1C),
// kDropPosZOffset=vertical(0x18). If these two are swapped the distance filter
// pairs drop-vertical against player-worldY and inflates every distance by the
// ground-plane magnitude (~580u here), so GetNearbyDropItems rejects every drop
// and auto-pickup silently reports drops=0.
inline constexpr uintptr_t kDropContainerHeadOffset = 0x6C;
inline constexpr uintptr_t kDropIdOffset            = 0x00;
inline constexpr uintptr_t kDropItemIdOffset        = 0x04;
inline constexpr uintptr_t kDropPosXOffset          = 0x14; // worldX   -> player +0x3C
inline constexpr uintptr_t kDropPosYOffset          = 0x1C; // worldY   -> player +0x44
inline constexpr uintptr_t kDropPosZOffset          = 0x18; // vertical -> player +0x40
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

// ---------- Fire-full-power (auto-cast all skills on nearby monsters) ----------
//
// SkillManager (singleton via *(SkillManager**)0xAD2F3C) holds the local
// player's learned skills as a SPARSE ARRAY INDEXED BY skillId — verified
// against SkillManager__GetSkillByIndex @ 0x93B1E0 which does
// `*(mgr+0x440) + 40*skillId` after a `skillId < *(mgr+0x444)` gate, AND
// against SkillManager__CanCastSkill @ 0x938840 which calls it with raw skill
// IDs (83, 119, 255 etc.).
//   SkillManager + 0x440 = CSkill *array  (DEREFERENCED — pointer-to-array)
//   SkillManager + 0x444 = element count (uint32) = MAX possible skillId + 1
//                          (typically 1000+, NOT learned-skill-count!)
//
// CSkill layout (sizeof=0x28, verified against CanCastSkill + Skill__GetSkillTable):
//   +0x00 vtable
//   +0x04 skillId       (uint32)         — primary key for SendSkillPacket
//   +0x18 leftCoolDown  (float seconds)  — engine subtracts dt every frame
//   +0x1C learnedFlag   (uint32)         — engine's own "is learned" gate
//                                          (CanCastSkill returns 0 if zero).
//                                          THIS is the canonical "did the
//                                          player learn this slot" signal —
//                                          NOT skillLevel or skillId. Use
//                                          this when filtering snapshots.
//   +0x20 skillLevel    (uint32)         — cached current level. Compared
//                                          by Skill__GetSkillTable against
//                                          requested level for cache hit.
//   +0x24 skillTable*   (SkillTable*)    — cached per-level table; returned
//                                          by Skill__GetSkillTable on a hit
//                                          (a2==cachedLevel || a2==-1).
//
// SkillTable (sizeof=0x288 from old build; per-level row inside the global
// skill DB). New SO3D build shifted offsets relative to so3dFullCheat by
// non-uniform deltas — DON'T trust the +0x24 uniform shift theory:
//   +0x004 skillId
// SkillTable+0x168 is NOT a single/AOE distinguisher — it's a coarse category:
//   1 = Attack       (covers BOTH single-target and AOE attack skills)
//   2 = Interact     (trade / duel-request / chat — these also share variant 1
//                     packet shape, which sends to SendSkillPacket's hardcoded
//                     branches and disconnects on unexpected IDs)
//   0, 4, 8, ...     = passive / mastery / mount / summon / movement — never
//                      consumed as a cast packet
// Verified against real game logs: every learned skill that actually deals
// damage (亂擊/全力一擊/狂暴亂舞/弱點精準/昏眩射擊 …) reports type==1; only
// interaction-class skills (交易/申請對戰) report type==2; all passive and
// toggle skills (狙擊模式/速戰之力/遠程火力/殺氣/霸氣 …) report type==0.
//
// The single vs AOE distinction lives in kSkillTablePacketVariantOffset (+0x110):
//   variant 2 = single-target packet shape
//   variant 3 = AOE list packet shape
// So the correct "is this a castable combat skill" filter is:
//     type == Attack (1)  AND  variant ∈ {2, 3}
//
// UseSkill @ 0x754EF0 confirms: the main attack dispatch arm is entered iff
// `*(skillTable+0x168) == 1`; the else-if at 0x755adf handles type==2 by calling
// CLocalUser__SetSkillMode(1) (interact/target-pick mode), not a cast.
//   +0x16C skillCoverRange (int32, AOE radius)
//                                          — verified via SkillTable__GetSkillCoverRange
//                                          @ 0x9390F0; caller CLocalUser__UseSkill
//                                          @ 0x755975 passes it to sub_B2B150
//                                          which enumerates nearby UserList
//                                          entries around the caster position.
//   +0x170 skillRange      (int32, cast distance, 1 tile == ~50 world units)
//                                          — verified via SkillTable__GetSkillRange
//                                          @ 0x939890; the wrapper at 0x939910
//                                          emits OutputDebugString
//                                          "Skill GetRangeSq : %d" after squaring
//                                          the returned value, used by
//                                          Net__SendSkillPacket distance gates.
//   +0x174 preTime         (float)        — already used by PreTime patch
//   +0x17C coolDown        (float)
//   +0x184 attack          (uint32)
struct SkillTable;

enum class SkillType : uint32_t
{
    Attack   = 1, // covers BOTH single-target AND AOE attack skills; the
                  // single/AOE split is encoded in packetVariant (+0x110),
                  // NOT here. All skills that actually deal damage report 1.
    Interact = 2, // trade / duel-request / chat — non-combat interaction.
                  // NOT a cast packet; engine puts local player into
                  // SetSkillMode(1) target-pick state.
    // Anything else (0, 4, 8, ...) = passive / mastery / mount / summon /
    // movement — not castable as an attack packet. Treat as skip.
};

inline constexpr uintptr_t kSkillTableTypeOffset       = 0x168; // SkillType
inline constexpr uintptr_t kSkillTableCoverRangeOffset = 0x16C; // int32 AOE radius
inline constexpr uintptr_t kSkillTableRangeOffset      = 0x170; // int32 cast distance

// SkillTable+0x0C = char[] skill name, Big5-encoded (game is KR/TW build).
// Verified via SkillTable Init function sub_92F550 @ 0x9328A8:
//   mov  eax, [ebp+var_3B04]   ; SkillTable*
//   add  eax, 0Ch              ; -> name char[]
//   push eax                   ; "%s" arg
//   push offset "SkillName : %s, SkillNumber : %d"
//   call ds:wsprintfA
// 64 bytes is a generous upper bound — observed names in this build are
// typically <= 16 Big5 chars (~32 bytes); the read is SEH-wrapped and
// truncates at first NUL anyway.
inline constexpr uintptr_t kSkillTableNameOffset = 0x0C;
inline constexpr size_t    kSkillNameMaxLen      = 64;

// SkillTable+0x110 = packet variant (uint32) — DIFFERENT from SkillType.
// Net__SendSkillPacket @ 0xB2F5D0 dispatches its packet shape on this field
// (NOT on +0x168). Variants 1, 5, 6, 7 contain HARDCODED skillId branches
// (variant 1: skillId 109 -> pet packet 411097, 1/2 -> world-pos, 78 -> couple,
// 101 -> chant; variant 5: 127; variant 6: 65/114/78; variant 7: 55/56). When
// the engine sees an unexpected skillId on those variants it sends a packet
// the server immediately disconnects on. Variants 2 and 3 are clean
// (single target / AOE list) — those are the only ones we whitelist for
// FireFullPower. Read CastSkill's dispatch in CLocalPlayer.cpp for details.
inline constexpr uintptr_t kSkillTablePacketVariantOffset = 0x110;

struct CSkill
{
    uint32_t     vtable;
    uint32_t     skillId;     // +0x04 — also the array index (sparse-by-id)
    uint8_t      pad08[0x18 - 0x08];
    float        leftCoolDown; // +0x18
    uint32_t     learnedFlag;  // +0x1C — engine's CanCastSkill "is learned" gate
    uint32_t     skillLevel;   // +0x20 — cached current level
    SkillTable * skillTable;   // +0x24 — cached level-N row, NULL if not populated
};
static_assert(sizeof(CSkill) == 0x28, "CSkill must be 0x28 bytes");

// Snapshot wrapper — pairs the engine's CSkill memory mirror with a name read
// from SkillTable+0x0C and converted Big5 -> UTF-8 (so ImGui u8"" rendering
// shows it correctly without a custom font). Worker thread reads/converts the
// name once per tick while it has the snapshot under its mutex; UI thread
// just displays the string. Keeps name lookups off the UI thread and avoids
// dereferencing skillTable pointers each frame.
struct LearnedSkillInfo
{
    CSkill      skill;
    std::string name; // UTF-8, may be empty if read failed or table unpopulated
};

inline constexpr uintptr_t kSkillArrayOffset  = 0x440;  // *(mgr+0x440) -> CSkill*
inline constexpr uintptr_t kSkillCountOffset  = 0x444;  // max skillId+1, NOT learned-count
inline constexpr size_t    kSkillStructSize   = 0x28;
// Sanity ceiling for the sparse array length. Real values seen ~ a few thousand,
// but we bound to keep a torn count from melting the loop. 8192 covers any
// plausible engine extension while still detecting `count = 0xCCCCCCCC` garbage.
inline constexpr uint32_t  kSkillMaxArrayLen  = 8192;

// Unit note on skill range vs target distance.
// Engine uses the SAME unit scale for SkillTable+0x170 (skillRange) and for
// creature/player coord deltas. Verified in CLocalUser__UseSkill @ 0x7556c4
// and Combat__GetAttackRangeSquared @ 0x7493C1 — both do raw `tile^2 vs
// (dx^2+dy^2)` with no multiplier (AttackRange cap 225 = 15^2 = a 15-tile
// radius, not 15 world units times 50). Our `m.distance` over CCreature
// +0x3C float positions lives in the same numeric space (observed distances
// ~10 for creatures ~10 tiles away), so skillRange compares directly against
// distance with no conversion. An earlier build multiplied by 50 here which
// effectively disabled the gate — do NOT reintroduce that multiplier.

// CCreature (monster) layout — verified across AutoTarget__EnumerateCreatures
// (the engine's own around-creature linked-list walker) and Creature__IsAlive:
//   +0x3C/0x40/0x44 worldX/Z/Y (float) — SAME X/Z/Y layout as CUser:
//                                         +0x3C = X, +0x40 = Z (vertical),
//                                         +0x44 = Y. Do NOT use X/Y/Z order.
//                                         These are the **animated/visual** pos
//                                         (mover updates them each frame).
//   +0x19C/+0x1A0   queued worldX/Y (int) — the **logical** position used by
//                                         click-to-target. Creature_GetPosXZ
//                                         (0x682B80) reads exactly these two
//                                         and that's what OnTargetCreatureClick
//                                         feeds into SetAfterAction. For static
//                                         NPCs they equal +0x3C/+0x44; for
//                                         moving monsters they're the last
//                                         server-sync'd cell (used for path
//                                         calculations so we don't chase a
//                                         half-frame-old animation pos).
//   +0x70           creatureId (uint32) — primary key for SendSkillPacket(target)
//   +0xA0           level (uint32)      — only populated for monsters
//   +0x194          state (uint32)      — ==4 means dead (Creature__IsAlive)
//   +0x320          hp (int64)          — Creature__IsAlive returns hp > 0
//   +0x368          statTable (ptr)     — null for half-initialised entries
//   +0x370          next (CCreature*)   — singly-linked list pointer
//
// kind LIVES INSIDE StatTable, NOT on CCreature itself:
//   *(StatTable + 0xDC) = kind   (7 = NPC, 8 = pet, other = monster)
// i.e.  *(_DWORD *)(*(_DWORD *)(cre + 0x368) + 0xDC). Verified against
// Creature__MatchesTargetSpec @ 0x5FCE40 which is what AutoTarget uses for its
// own kind filter. An earlier version of this header had `cre + 0xDC` and
// silently broke NPC detection — cre+0xDC reads as 0 for every entry, so the
// kind!=7/!=8 filters always passed (monsters worked by luck, NPCs never showed).
inline constexpr uintptr_t kCreaturePosXOffset      = 0x3C;
inline constexpr uintptr_t kCreaturePosZOffset      = 0x40; // vertical
inline constexpr uintptr_t kCreaturePosYOffset      = 0x44; // ground-plane Y
// Queued/click position (int) used by Creature_GetPosXZ -> SetAfterAction
inline constexpr uintptr_t kCreatureQueuedXOffset   = 0x19C;
inline constexpr uintptr_t kCreatureQueuedYOffset   = 0x1A0;
inline constexpr uintptr_t kCreatureIdOffset        = 0x70;
inline constexpr uintptr_t kMonsterLevelOffset      = 0xA0;
// kind is at *(statTable + 0xDC), not cre+0xDC. Read via the StatTable indirection.
inline constexpr uintptr_t kStatTableKindOffset     = 0xDC;
// MonsterTblId (NPC vs monster data-key) at *(statTable + 236).
inline constexpr uintptr_t kStatTableMonsterTblIdOffset = 236;
// Localized creature name at statTable+8, char[100], Big5 encoded, null-terminated.
// Verified against MonsterTblEntry loader sub_B77EB0 which `memcpy_s_41(entry+8, 0x64, ...)`
// from the parsed monster.edt row. Same row position as `name` field — every
// alive CCreature (NPC or monster) carries its display name here.
inline constexpr uintptr_t kStatTableNameOffset     = 0x8;
inline constexpr size_t    kStatTableNameMaxLen     = 100;
inline constexpr uint32_t  kMonsterKindNpc          = 7;
inline constexpr uint32_t  kMonsterKindPet          = 8;
inline constexpr uintptr_t kCreatureStateOffset     = 0x194;
inline constexpr uint32_t  kCreatureStateDead       = 4;
inline constexpr uintptr_t kCreatureHpOffset        = 0x320;
inline constexpr uintptr_t kCreatureStatTableOffset = 0x368;
inline constexpr uintptr_t kCreatureNextOffset      = 0x370;

// CreatureMgr singleton: *(CreatureMgr**)0xACF658, head pointer at +0x0C
// (mirrors EntityManager's around-player list shape).
inline constexpr uintptr_t kCreatureMgrListHeadOffset = 0x0C;

struct NearbyMonster
{
    uint32_t  monsterId;
    uint32_t  kind;
    uint32_t  level;
    float     distance;
    float     x, y, z;
    int64_t   hp;
    uintptr_t addr; // CCreature* at snapshot time; recheck state/hp before use
                    // — engine can kill or despawn the entry between the
                    // snapshot walk and the send, especially when other
                    // packets in the same tick land the killing blow.
};

// Snapshots SkillManager's learned-skill array. SEH-wrapped per element so a
// torn entry doesn't kill the whole snapshot. Returns only entries with
// learnedFlag != 0 (engine's own CanCastSkill gate). Each entry's `name` is
// read from SkillTable+0x0C (Big5) and converted to UTF-8 in this call so
// the UI thread doesn't have to dereference engine pointers.
std::vector<LearnedSkillInfo> GetLearnedSkills();

// SEH-safe walk of CreatureMgr's around-player linked list. Filters out
// kind 7/8 (NPCs and pets), dead creatures, and (when maxDistance > 0)
// anything farther than maxDistance from the local player. Sorted ascending
// by distance.
std::vector<NearbyMonster> GetNearbyMonsters(float maxDistance);

// Same walk, but distance is measured from an arbitrary world point. Used by
// AOE scoring (packet variant 3): for each candidate primary target, count
// how many other monsters fall inside skillCoverRange — pick the primary with
// the best AOE coverage. Sorted ascending by distance from (cx,cy,cz).
std::vector<NearbyMonster> GetMonstersAround(float cx, float cy, float cz,
                                             float maxDistance);

// Variant-aware skill dispatch. The "is combat skill" check is:
//   skillType != Attack (1)        -> 0 (skip; interact / passive / mount /
//                                       summon etc. — server doesn't accept
//                                       these as cast packets and spamming
//                                       them is the fastest way to get caught)
//   packet variant ∉ {2, 3}        -> 0 (skip; variants 1/5/6/7 contain
//                                       hardcoded skillId branches that send
//                                       unexpected packet shapes and the
//                                       server disconnects on unexpected IDs
//                                       — see kSkillTablePacketVariantOffset).
//   variant == 2 (single-target)   -> 1 packet, closest in-range target
//   variant == 3 (AOE list)        -> 1 packet, target with the most
//                                       neighbours inside skillCoverRange
//                                       (engine resolves AOE itself; current
//                                       build's Net__SendSkillPacket is a
//                                       2-arg wrapper that can't carry a
//                                       multi-target list).
//
// Note: skillType alone used to be (wrongly) treated as single/AOE
// distinguisher in an older build — verified via live logs that every real
// attack skill reports type==1 regardless of single/AOE, and the split lives
// in packetVariant. Do not revert to a SingleAttack/RangeAttack split on
// skillType.
//
// Caller passes the full snapshot of nearby monsters (e.g. from
// GetNearbyMonsters(uiDistance)); CastSkill reads the skill's own skillRange
// and filters to in-range targets internally, so the module layer doesn't
// need to dereference SkillTable fields. Every cast attempt — including
// rejects — is logged with skillId/type/variant/target/distance so we can
// diagnose the which-skill-disconnected-us question without a debugger.
//
// dryRun=true: log the cast decision but DON'T call Net__SendSkillPacket.
// Use this to validate the filter chain against a real game session without
// risking a disconnect.
//
// Returns the number of packets actually sent (0 or 1) so the caller can
// keep a per-tick cap. dryRun=true always returns 0.
int CastSkill(const LearnedSkillInfo           &skill,
              const std::vector<NearbyMonster> &allMonsters,
              bool                              dryRun = false);

// Fires the engine's own packet builder pair:
//   buf = Net__BeginSend()                  ; cdecl, returns global send buffer
//   Net__SendSkillPacket(buf, skillId, targetId) ; thiscall — see RE notes in
//                                                   the .cpp implementation
// Bypasses CLocalUser__SendSkillCast's local gates (range, animation lock,
// stun, etc.) — the server still validates, but the client never spins on a
// rejected condition. SEH-wrapped because both functions deref a global
// network state that can be torn during scene transitions.
bool CastSkillOnMonster(uint32_t skillId, uint32_t monsterId);

// Casts a skill through the engine's own high-level entry (Combat__TryUseSkill
// @ 0x601CA0) — the same path the in-game AutoHunt uses. Unlike
// CastSkillOnMonster (which only fires the raw CG_SKILL_22 effect packet), this
// runs the full state machine: cooldown/learned checks, CG_SKILL_24 startup +
// CG_SKILL_22 effect, target plumbing. Works for self-buff skills (type==Self),
// single-target, and AOE alike — the engine picks the packet shape.
//
// targetId: creature/user id for targeted skills; pass 0 (or own userId) for
// self-buffs. For a "cast this buff skill on myself when the buff is missing"
// feature, 0 is correct — the engine resolves self-target internally.
//
// checkCanCast=true (default) pre-gates on SkillManager__CanCastSkill (skill
// learned + off cooldown) so we don't spam TryUseSkill while on cooldown.
//
// Returns true if the engine accepted the cast (packets sent). SEH-wrapped.
bool CastSkillById(uint32_t skillId, uint32_t targetId = 0, bool checkCanCast = true);


// ---------- Bomber-class bomb-throwing (火力全开 爆破师分支) ----------
//
// CLocalUser.profession (uint32). Verified against AutoHunt__ChooseNextAction
// @ 0x5FD1C0 which reads `*(g_pLocalUser + 6904) == 16` to decide "is bomber,
// try MakeBomb". Old so3dFullCheat had this at +0x1448 (name field +0x1410);
// in this build name shifted to +0x1AC0 and profession rides the same +0x6B0
// delta to +0x1AF8.
//   profession == 6  = 铁匠 (Blacksmith, legacy)
//   profession == 16 = 爆破 (Bomber)
inline constexpr uintptr_t kUserProfessionOffset = 0x1AF8;
inline constexpr uint32_t  kProfessionBomber     = 16;
inline constexpr uint32_t  kProfessionBlacksmith = 6;

// CLocalUser.selectedBagId — written by the engine's AutoHunt logic before
// firing skill 83 (throw-bomb). Net__SendSkillPacket reads the same field and
// calls `StatTable__GetItemTblEntryById(container, *(localUser + 0x3578))`;
// that function iterates item slots comparing the argument against item+0x00
// (the bag-slot unique id, NOT the game template itemId at item+0x04). If the
// lookup returns NULL, SendSkillPacket aborts via SetSkillMode(0) and nothing
// hits the wire. Verified in AutoHunt__FindAutoUseItemId @ 0x5FD920 which
// returns `*(container + 0xEB8 + i*0x118 + 0)` = item+0x00 = bagId.
// MUST be set to a valid bomb's bagId (item+0x00) before each skill-83 cast.
inline constexpr uintptr_t kUserSelectedItemIdOffset = 0x3578;

// Skill IDs used by the bomber branch — both verified in IDA:
//   83   = 投掷炸弹 (ThrowBomb). Variant 6 (item-skill) in SkillTable+0x110;
//          Net__SendSkillPacket has a dedicated branch for it. Requires
//          localUser + 0x3578 = bombItemId (see comment above).
//   325  = 制作炸弹 (MakeBomb). Regular cast packet; the bomber uses this to
//          replenish 原子彈 when the bag is empty or stack < 150.
//          (0x145 in the old so3dFullCheat headers.)
inline constexpr uint32_t kSkillIdThrowBomb = 83;
inline constexpr uint32_t kSkillIdMakeBomb  = 325;

// CItemContainer bag layout (verified in AutoHunt__FindAutoUseItemId @ 0x5FD920
// which iterates the same array):
//   container + 0xEB8 + i * 0x118 = Item[i] base   (stride 280 bytes)
//   Item +0x00  bagId        (uint32)
//   Item +0x04  itemId       (uint32) — indexes ItemTblEntry via
//                                       StatTable__GetItemTblEntryById
//   Item +0x10  count        (uint32) — stack size
//   Item +0x20  itemTable    (pointer) — NULL for empty slots
// Old so3dFullCheat had items at container+0xAA0 with the same internal
// layout; the +0x418 delta matches the bag-prefix grow-out in the new build.
inline constexpr uintptr_t kItemArrayBaseOffset  = 0xEB8;
inline constexpr uintptr_t kItemStride           = 0x118; // 280 bytes
inline constexpr size_t    kItemFullSlotCount    = 192;   // full iteration
inline constexpr uintptr_t kItemBagIdOffset      = 0x00;
inline constexpr uintptr_t kItemItemIdOffset     = 0x04;
inline constexpr uintptr_t kItemCountOffset      = 0x10;
inline constexpr uintptr_t kItemItemTableOffset  = 0x20;

struct BagBombInfo
{
    uint32_t bagId;     // item+0x00 raw value. SetSelectedItemId (localUser+0x3578)
                        // 用这个,FireFullPower RawPacket 模式的 wire bombItemId 也用
                        // 这个 — 实测可用,不要改。
    uint32_t slotIndex; // bag 循环 index (0..kItemFullSlotCount-1)。drop / use 类
                        // 协议的 wire 格式按"slot 位置"编码,需要这个而不是 item+0x00。
                        // 老项目 ThrowBomb 用 `slotIndex + 0xD` 作为 wire 字段,
                        // DropAllBombsInBag 等下层走类似 +13 编码也走这条。
    uint32_t itemId;    // 游戏模板 itemId (GetItemClass 用)
    uint32_t count;     // stack size
};

// Reads the local player's profession (CLocalUser + 0x1AF8). Returns 0 if the
// pointer hasn't resolved. SEH-wrapped — the field is safe during normal play
// but torn during scene transitions.
uint32_t GetProfession();

// Finds a throwable bomb in the bag. The old so3dFullCheat matched item names
// (u8"原子彈" / u8"黑色自製炸藥"); the new build's strings are encrypted in
// the client, so we use Item__GetItemClass(itemId) == 3 as the bomb predicate
// — same classification the engine's own AutoHunt uses to decide that skill 83
// can consume this item. Returns false with out_info cleared if nothing found.
bool FindBombInBag(BagBombInfo &out_info);

// Sum of all bomb stacks (GetItemClass==3) in the bag.
uint32_t GetTotalBombCount();

// Writes localUser + 0x3578 = itemId so that CLocalUser__SendSkillCast accepts
// the subsequent throw-bomb packet. Returns true on success. SEH-wrapped.
bool SetSelectedItemId(uint32_t itemId);

// Full bomber-class cast: looks up the bomb, pins localUser+0x3578, and fires
// Net__SendSkillPacket(83, monsterId). If no bomb is found, instead fires
// Net__SendSkillPacket(325, 0) to craft one. Returns the number of packets
// actually sent (0 or 1); dryRun logs the decision without firing.
//
// Takes the full NearbyMonster (not just monsterId) so it can re-verify
// state/hp against the live engine pointer before sending — earlier casts
// in the same tick may already have killed it, and the snapshot from
// GetNearbyMonsters is from tick-start. Mirrors the recheck CastSkill does.
//
// This is the drop-in replacement for the normal attack rotation when
// profession == 16 (爆破) — it's what the game itself chooses inside
// AutoHunt__ChooseNextAction for bomber users.
int CastBombOnMonster(const NearbyMonster &target, bool dryRun = false);

// Fires Net__SendSkillPacket(325, 0) unconditionally — used when we want to
// top up the bomb stack without throwing. Returns true on packet send.
bool CastMakeBomb(bool dryRun = false);

// ---------- Raw-packet bomber path (mirrors old so3dFullCheat ThrowBomb) ----------
//
// Old so3dFullCheat built a UINT32[6+N] buffer in C++ and called the raw
// dispatcher (skillSendPackageOffset + push 0x6458E) directly, bypassing
// Net__SendSkillPacket entirely. The new build's equivalent dispatcher is
// `Net__SkillSendPackage` @ 0xB2EA90 — same __thiscall(this=netBuf,
// routingTag=0x6458E, buf, size) signature, registered as PatternResolver
// "NetSkillSendPackage".
//
// Wire layout (verified against Net__SendSkillPacket's skill-83 branch
// @ 0xb2fbe7..0xb2fe48):
//   [0]      skillId       = 83
//   [1]      skillLevel    = 1 (or table-derived)
//   [2]      bombItemId    = bagId (RAW, what we already write to localUser+0x3578)
//   [3]      isLocalInRange = 0 if local outside blast, 1 if inside
//   [4]      targetCount   = 1 + neighbor count
//   [5]      primary monster id
//   [6+i]    neighbor monster id
// size = 4 * (5 + targetCount) bytes
//
// AOE neighbor selection uses kBomberNeighborTileRadius (5 tiles) — copied
// verbatim from the old project's hardcoded `distanceThreshold = 5`. The
// engine path uses SkillTable+0x16C (skillCoverRange) which the server
// re-resolves anyway, but raw mode pre-fills the list itself so the server
// gets a hint without us needing to read the SkillTable.
//
// Same overall behavior as CastBombOnMonster (auto-MakeBomb fallback,
// localUser+0x3578 pin, dryRun support, liveness recheck). The ONLY
// difference is which call we end up making after the prep work.
inline constexpr float kBomberNeighborTileRadius = 5.0f;

int CastBombOnMonsterRaw(const NearbyMonster              &target,
                         const std::vector<NearbyMonster> &allMonsters,
                         bool                              dryRun = false);

// Classification helper that mirrors the engine's Item__GetItemClass.
// Returns 3 if the item is a throwable bomb, 1/2 for other consumables, 0
// otherwise. Internally calls Item__GetItemClass through PatternResolver.
int GetItemClass(uint32_t itemId);

// ---------- Drop-item packet (CG_ITEM_DROP / 411012) ----------
//
// The engine's own item-drop path (sub_8FCC00 @ 0x8FCC00) ends in:
//   Net__BeginSend();
//   sub_B2C790(netBuf, 411012, bagId + 13, count);
// where bagId = the slot's item+0x00 uniqueSlotId (same value we now write to
// localUser+0x3578 for skill 83). The `+13` is the protocol-level slot
// encoding. sub_B2C790 is a generic 3-arg thiscall dispatcher (__thiscall).
//
// DropItem fires one packet per stack; the server replies with
// GC_ITEM_DROP_SUCC / GC_ITEM_DROP_FAIL to confirm. No client-side range
// gate — drops land at the local player's feet.
//
// DropAllBombsInBag walks the bag (same loop as FindBombInBag), and for every
// item where Item__GetItemClass(itemId) == 3 and itemTable != NULL, fires the
// drop packet with the slot's full count. Returns the number of drop packets
// actually sent. SEH-wrapped around each bag read + each send.
int DropAllBombsInBag();

// 丢弃单个 bag slot —— web 远控用。wireBagId = item+0x00,即 GetBagItems
// 返回的 BagItemInfo::bagId,已经是 wire 格式 (slotIndex+13),不要再 +13。
// count > 0;若 > stack 实际数量,服务端会拒,所以调用方传背包里看到的 count。
// 不做 GetItemClass 过滤 (任何可丢的格子都能丢)。SEH-wrapped。
bool DropBagItem(uint32_t wireBagId, uint32_t count);

// ---------- Local player wallet (CLocalUser+0x3498, int64) ----------
//
// Verified in trade UI sub_71F9D0 @ 0x71F9D0: the "myMoney" field is rendered
// from `*(g_pLocalUser + 13464)` (low dword) and `*(g_pLocalUser + 13468)`
// (high dword), i.e. an int64 at +0x3498. Same offset feeds the mailbox
// (sub_7F73E0 path that branches on `*(__int64 *)(v55+64) > 0` after copying
// the player wallet into the mailbox UI struct).
inline constexpr uintptr_t kLocalMoneyOffset = 0x3498;

// CUser::GetCurrentHP stores HP at +0x1B00, XOR-encrypted for local user
// with runtime key at g_HpXorKey (0xD67CE0).
inline constexpr uintptr_t kLocalUserHpOffset = 0x1B00;
inline constexpr uintptr_t kHpXorKeyAddr      = 0xD67CE0;

// Reads the local player's gold/money. Returns 0 if g_pLocalUser hasn't
// resolved yet. SEH-wrapped — torn during scene transitions.
int64_t GetLocalMoney();

int64_t GetLocalHp();

// CLocalUser identifier offset — g_pLocalUser + 112. Engine's UI_OnReviveDialogButton
// passes this exact field to Net__SendPacket16B_2DW_Tracked(411170, id, 1) when
// the user clicks "rebirth_ok" on the death dialog. Same value as the userId
// the server sends in the on-died notification.
inline constexpr uintptr_t kUserSelfIdOffset = 112;

// Returns the local player's userId (g_pLocalUser+112), or 0 if not resolved.
// SEH-wrapped.
uint32_t GetLocalUserId();

// ---------- 登录凭据 (账号/密码,明文) ----------
//
// IDA 实证 (Login__BuildAndSendLoginPacket @ 0x7DC3A0):登录界面把 id_input/pass_input
// 控件文本 memmove 进 localUser,登录后保留供重连。两者都是定长 16B 明文(ASCII),
// 不足补 0。账号另有全局副本 g_szLoginAccount(0xDFD66C)。随后 DES 加密构造 CL_LOGIN。
// 私服里游戏账号密码与泡点网店是同一套,所以可直接拿来做泡点购买,免去手填密码。
// 偏移即 IDA memmove 目标(Login__BuildAndSendLoginPacket @0x7DC629/0x7DC6B1):账号写到
// +13064、密码写到 +13227,各 16B。注意账号字段首字节可能是 \0(id_input 控件 +128 源缓冲
// 带前导 \0,CE 实测账号 = "\0gongyu9011213"),所以读取时要跳过前导 \0,不能改偏移成 +13065。
inline constexpr uintptr_t kLoginAccountOffset  = 13064; // *LocalUserPtr + 0x3318
inline constexpr uintptr_t kLoginPasswordOffset = 13227; // *LocalUserPtr + 0x33AB(明文,CE 实测)
inline constexpr size_t    kLoginFieldSize      = 16;

struct LoginCredentials
{
    std::string account;
    std::string password;
};

// 读 localUser 里的明文账号/密码。SEH 安全。失败(localUser 未就绪/账号空)返回 false。
bool GetLoginCredentials(LoginCredentials &out);

// Returns true iff the local player is currently dead (HP <= 0). Cheap;
// equivalent to GetLocalHp() <= 0. Web UI uses this to enable/disable the
// Revive button.
bool IsLocalDead();

// ---------- Current map id ----------
//
// The engine maintains a 2-step indirection for "what map am I on":
//   1) `g_pCurMapSlot` (RVA 0x967CB4) HOLDS A POINTER to an int (the map slot).
//   2) `g_MapIdTable[]` (RVA 0x96AE78) is an int[] indexed by that slot value.
// Putting them together: `mapId = g_MapIdTable[*g_pCurMapSlot]`.
//
// Seen values in this build (verified by reading at known locations):
//   7   = 新手村 (rookie village)
//   400 = Square / 主城 (the "town" everyone gets warped to on revive)
//   500..504 = guild
//   202..209 = elimdun / hunja tower
//   Specific dungeons are 1-2 / 9 / 20 / 30 / 33 / 39-41 / 59-65 / etc.
// Returns 0 if the indirection couldn't be resolved (scene transition tearing).
uint32_t GetCurrentMapId();

// ---------- AutoRevive / death warp ----------
//
// 411170 = CG_REBIRTH_OK. Same packet the revive dialog button fires when
// user clicks "rebirth_ok". Server response = warps player back to town and
// resets HP. Returns false on pattern miss / SEH / not-dead reject.
//
// ---------- AutoRevive / death warp ----------
//
// 412017 = CG_PLAYER_REVIVE. Net__SendDword(buf, 412017, 1) — 这就是死亡弹框
// 后引擎里 sub_609C70 (RequestRevive) 调的那条 wire,跟玩家手动点「复活」一致。
// payload=1 表示「就地/默认复活」;有的版本 payload=2 = 回主城/lobby revive。
// 默认我们也用 1,跟玩家手动点 OK 一致(确切是去哪个地方由服务端决定)。
// 411170 是老版本 rebirth_ok 那条路径,这版抓的实际 wire 是 412017 所以走它。
//
// safetyCheck is kept for remote-command compatibility only; ReviveToTown
// always sends and does not gate on local HP/dead state.
//
// reviveMode: payload dword written by Net__SendDword. 1=默认(已验证)。
// 其它值留给后续调整。
bool ReviveToTown(bool safetyCheck = true, int reviveMode = 1);

// ---------- Auto-trade (CExchange trade flow) ----------
//
// 交易流程的三条 CG 包(同意/锁定/确认)都是单 DWORD body 包,和 CG_PLAYER_REVIVE
// 同形,所以全部走 Net__SendDword(buf, proto, value):
//   412028 ACCEPT  : value = requesterEntityId  (来自 GC 0x07CC62 的 body[0])
//   412029 DECLINE : value = requesterEntityId
//   412033 LOCK    : value = 412033  (body 回显 proto id)
//   412035 CONFIRM : value = 412035
//   412036 CANCEL  : value = 412036
// wire 字节实测自 shadowpope(接受方)的 sendlog:
//   412028 -> 0C000000 7C490600 C9000000   (len12, proto412028, body=requesterId)
//   412033 -> 0C000000 81490600 81490600   (proto412033, body=412033)
//   412035 -> 0C000000 83490600 83490600   (proto412035, body=412035)
// 因果链(shadowpope=接受方): RECV 0x07CC62(body=C9) -> SEND 412028(body=C9);
//   RECV 0x07D050(对方锁定) -> SEND 412033(锁定) -> SEND 412035(确认)。确认只 gate
//   在本地双方锁定态上,锁定到确认之间没有单独的 peer-confirm recv。
// 全部经共享的 CallNetSendDwordSEH SEH 包,返回 true 表示已发送。
bool SendTradeAccept(uint32_t requesterId);
bool SendTradeDecline(uint32_t requesterId);
bool SendTradeLock();
bool SendTradeConfirm();
bool SendTradeCancel();

// 自动接受时本地开交易窗口。手动点「接受」按钮 = Trade_OpenLocalExchangeWindow
// (sub_71E040) + 发 412028;只发包不开窗,本地窗口会停在请求态直到对方放物品。
// 取 CExchange control 32(CUIManager__GetUIContent(GetSingleton(),32))调引擎开窗。
// 返回 false 表示 control 32 还没建好(派发竞态)或 pattern 未解析 —— 调用方重试。
bool OpenLocalTradeWindow();

// (移除: 之前的 SendDelegate / CMessenger 路径不对。自动委托跳图实际上是
// CG_NPC_DIALOG_SELECT (411026),wire = 14 字节,(npcId, dialogOption, sub=1)
// 三个数。直接复用现有 SendDialogSelect 即可,见下方。抓到的包验证:
//   2026-05-22 23:44:15 | proto=411026 opt=10245 npc=19811 sub=1
//   14 00 00 00 92 45 06 00 05 28 00 00 63 4D 00 00 01 00 00 00
// → opt=0x2805=10245, npc=0x4D63=19811。AutoRevive 脚本直接发这条就能让
// 服务器把人送到挂机地图。)

// ---------- MailBox::SendMoneyMail (proto 411524, op=0) ----------
//
// Engine helper at 0x7F51E0 — __stdcall(int op, const char *recipient,
// int64_t money, const char *body). Internally allocates a 256-byte zeroed
// payload, packs it into a 77-byte (0x4D) body, calls Net__BeginSend() and
// Net__SkillSendPackage(buf, 411524, payload, 77) — i.e. it's self-contained,
// don't call BeginSend ourselves. Verified by capturing the live "give 123456
// gold to shadowpope" mail packet and matching the bytes against this layout:
//   [0..3]   = 3            (constant header field)
//   [4..19]  = recipient    (16 bytes, no NUL forced -- pre-zeroed)
//   [20..23] = op           (0 for money mail)
//   [24..27] = money lo
//   [28..31] = money hi
//   [32..]   = body string  (no NUL forced)
// IDA's auto-prototype splits the int64 into (char Src, int a4) -- ignore that,
// it's pushed as 8 bytes by __stdcall on x86 and our typedef matches. 
//
// Recipient must be 1..15 ASCII chars (engine truncates with strlen, so a
// longer string would clobber the op field). SEH-wrapped at the caller.
//
// Returns true on packet send (1 from the engine), false on pattern-miss /
// guard reject / SEH catch.
bool SendMoneyMail(const char *recipient, int64_t money, const char *body = "");

// ---------- Public chat (CG_PUBLIC_CHAT / proto 411001) ----------
//
// Net__SendChatStr @ 0xB2E920 — __thiscall(this=netBuf, protoId, message).
// Builds {u32 totalLen, u32 protoId=411001, char[] message} with 4-byte
// alignment padding, then dispatches via Net__SendPacket_Plaintext.
// Call sequence: Net__BeginSend() -> NetSendChatStr(buf, 411001, msg).
bool SendPublicChat(const char *message);
//
// Net__SendDialogSelect @ 0xB2C930 — __thiscall(this=netBuf, proto, a3, a4, a5).
// Wire layout: {u32 totalLen=20, u32 proto=411026, u32 dialogOpt, u32 npcId, u32 sub=1}.
//
// IMPORTANT — engine arg order is (proto, dialogOpt, npcId, sub), NOT
// (proto, npcId, dialogOpt, sub). Verified 2026-05-09 by selecting the same NPC
// twice on the live server: DWORD3 (a3) varied across selections (the option
// index), DWORD4 (a4) was constant (= NPC interact target ID). See
// OnNpcDialogConfirm @ 0x8BBEF0 — a3 = packet-deserialised v10 (option),
// a4 = cached g_NpcInteractTargetId at *(dword_ED347C+1016).
//
// This wrapper keeps a friendly (npcId, dialogOption) API and swaps internally.
//
// Sister of NetSendTriple (used for CG_ITEM_DROP) but emits one extra DWORD.
// SEH-wrapped. Returns true on send. `sub` is the trailing sub-action DWORD
// (a5); defaults to 1 (the normal "confirm option" value). The 同步 feature
// passes through the master's captured sub so a mirrored select is byte-identical.
bool SendDialogSelect(uint32_t npcId, uint32_t dialogOption, uint32_t sub = 1);

// ---------- NPC dialog state snapshot (web UI) ----------
//
// g_NpcDialogState (RVA 0xAD347C / linear 0xED347C) — VALUE-AT-ADDR is a
// pointer to the dialog state struct. Engine internal layout (verified via
// IDA on Npc__BuildDialogChoiceList @ 0x8F9780):
//
//   state[253] (+0x3F4 / 1012) mode:
//                  0 = closed / no dialog showing
//                  1 = single-confirm (just "OK" — show body text only)
//                  2 = multi-choice menu (show option list)
//   state[ 2 ] (+0x008)        current monsterTblId (set by LoadDialogScript)
//   state[254] (+0x3F8 /1016)  npcInteractTargetId (creature+112 at OpenDialog)
//                              — this is the a4 we feed to SendDialogSelect.
//   state[261] (+0x414 /1044)  in mode==1: ptr to current confirm-entry.
//                              ((char*)entry + 4) = body text (Big5)
//   state[263] (+0x418 /1052)  in mode==2: head of multi-choice linked list.
//                              Each node:
//                                +0x04 (char[]) Big5 option label
//                                +0x188 (392) next ptr (0 = end)
//                                +0x18C (396) target script tag
//   state[264] (+0x41C /1056)  in mode==2 with a "wrapper" parent: single-
//                              option ctrl head; same node layout.
//
// Walk order — UI labels for selectable rows in mode==2 are written into
// "select%02d" UI controls starting at i=0; we send back option_index =
// position in the walked list (matches the engine's own send path in
// OnNpcDialogOption_Quest).
inline constexpr uintptr_t kDialogStateModeOffset           = 0x3F4;
inline constexpr uintptr_t kDialogStateMonsterTblIdOffset   = 0x008;
inline constexpr uintptr_t kDialogStateNpcInteractIdOffset  = 0x3F8;
inline constexpr uintptr_t kDialogStateConfirmEntryOffset   = 0x414;
inline constexpr uintptr_t kDialogStateChoiceHeadOffset     = 0x418;
inline constexpr uintptr_t kDialogStateWrapperHeadOffset    = 0x41C;
inline constexpr uintptr_t kDialogOptionTextOffset          = 0x004; // Big5
inline constexpr uintptr_t kDialogOptionNextOffset          = 0x188; // 392
inline constexpr uintptr_t kDialogOptionTagOffset           = 0x18C; // 396 — scriptEntry*
// opt = *(*(option_node+396) + 12) + 332 — 真正发给服务器的 dialogOption。
// 链路: option_node +396 -> scriptEntry, scriptEntry +12 -> scriptCtx,
//        scriptCtx +332 -> opt (DWORD)。验证: OnNpcDialogOption_Quest 把
//        v10 = *(*(dword_ED3DC4+12)+332) 作为 Net__SendDialogSelect 的 a3。
//        抓包 opt=10216 来源即此。
inline constexpr uintptr_t kDialogScriptEntryCtxOffset      = 0x00C; // scriptEntry +12
inline constexpr uintptr_t kDialogScriptCtxOptOffset        = 0x14C; // scriptCtx  +332
inline constexpr int       kDialogMaxOptions                = 64;    // safety
inline constexpr size_t    kDialogOptionTextMaxLen          = 256;   // Big5 char[]
inline constexpr size_t    kDialogBodyTextMaxLen            = 1024;  // Big5 char[]

enum class DialogMode : uint32_t
{
    Closed   = 0,
    Confirm  = 1, // body text only; option list empty; "OK" advances
    Choice   = 2, // body + options; UI must render and let user pick
};

struct DialogOption
{
    uint32_t    index;     // 0-based; this is what we send back as `dialogOption`
    std::string text;      // UTF-8, converted from Big5 (engine label)
    uint32_t    tag;       // option+396 — engine-internal script branch tag (diagnostic)
    uint32_t    opt;       // *(*(option+396)+12)+332 — 真正发给服务器的 opt 数值
                           // (Net__SendDialogSelect 的 a3, 抓包里 opt=10216 即此)
                           // 0 表示无 quest tag / 读取失败 — 多见于纯本地子菜单。
};

struct DialogSnapshot
{
    bool                       open;             // mode != 0
    DialogMode                 mode;
    uint32_t                   npcInteractId;    // state+1016
    uint32_t                   monsterTblId;     // state+8
    std::string                body;             // UTF-8 (empty for Choice mode in
                                                 //        this engine — option labels
                                                 //        carry the prompt)
    std::vector<DialogOption>  options;          // [] in Closed mode
};

// Reads g_NpcDialogState snapshot. SEH-wrapped; returns {open=false} on any
// read failure (state pointer null, layout torn, etc).
DialogSnapshot GetDialogSnapshot();

// Sends `dialogOption` THROUGH THE ENGINE'S OWN CLICK PATH so client UI state
// stays in sync. Directly calling Net__SendDialogSelect bypasses local state
// updates (state+1044 stays on the old option, sub-menu rebuild fails). What
// this does instead:
//   1) Walks the option linked list (state+1052 / state+1056) to the nth node
//   2) Writes state+1044 = that node (mimics "cursor on this option")
//   3) Calls Npc__ConfirmDialogOptionLocal — engine writes branch tag to
//      g_DialogOptionPendingResult, and either descends into a sub-menu
//      locally OR signals "ready to send"
//   4) Calls OnNpcDialogOption_Quest — sends 411026 (when there's a tag) and
//      cleans up g_NpcDialogState (clears highlight sprite, etc.)
// Returns false only on pattern-resolve failure or torn state.
bool SelectDialogOption(uint32_t dialogOption);

// Convenience for mode==1 single-confirm dialogs — same as
// SelectDialogOption(0) because the engine pre-points state+1044 at the
// sole entry on confirm-mode menus.
bool ConfirmDialog();

// ---------- Bag inventory snapshot (web remote) ----------

// ItemTable (the per-template row pointed to by item+0x20) layout excerpt:
//   +0x0C  char[] itemName (Big5, null-terminated)
inline constexpr uintptr_t kItemTableNameOffset = 0x0C;

struct BagItemInfo
{
    uint32_t    bagId;      // item+0x00, unique slot key
    uint32_t    slotIndex;  // 0..kItemFullSlotCount-1
    uint32_t    itemId;     // game template id
    uint32_t    count;      // stack size
    std::string name;       // UTF-8, converted from itemTable+0x0C (Big5)
};

// SEH-safe full-capacity walk of the local player's bag. Returns all occupied
// slots (itemTable != NULL && itemId != 0). Sorted by slotIndex ascending.
std::vector<BagItemInfo> GetBagItems();

// ---------- Cash bag (商城背包) ----------
//
// Cash items live in a separate array inside the same ItemContainer object,
// accessed via GetItemContainerInstance() (dword at RVA 0xA08160).
//   cashItem[i] = *(CashContainerPtr) + kCashArrayBaseOffset + i * kItemStride
// Item structure is identical to regular bag (bagId@+0x00, itemId@+0x04, etc).
// Wire protocol for "use cash item": NetSendTriple(buf, 411156, slotIndex+13, 0).
// Verified via sub_653990 case 39 branch and packet capture 2026-05-17.
inline constexpr uintptr_t kCashArrayBaseOffset = 0x48;
inline constexpr size_t    kCashSlotCount       = 80;
inline constexpr int       kProtocolUseCashItem = 411156;
inline constexpr int       kCashSlotWireBase    = 13; // wire = slotIndex + 13

// ---------- Summoned vendor shop ----------
//
// IDA verified 2026-06-11:
//   open  : NetSendDword(buf, 411455, token)
//   buy   : Net__SkillSendPackage(buf, 411020,
//           {vendorId, shopIndex, count, targetBagSlot, token}, 20)
//   close : NetSendDword(buf, 411456, token)
// 412067 in the same capture is an unrelated pet "enjoy" command, not vendor UI.
inline constexpr int      kProtocolVendorBuy   = 411020;
inline constexpr int      kProtocolVendorOpen  = 411455;
inline constexpr int      kProtocolVendorClose = 411456;
inline constexpr uint32_t kVendorSummonToken   = 0xFECD2408u;
inline constexpr uint32_t kVendorDefaultId     = 2;
inline constexpr uint32_t kVendorMaxShopItems  = 30;

struct VendorShopItem
{
    uint32_t shopIndex; // 0-based index in the vendor's type-17 shop list.
    uint32_t itemId;
    uint32_t unitPrice;
    std::string name;
};

struct VendorBuyResult
{
    uint32_t vendorId;
    uint32_t shopIndex;
    uint32_t itemId;
    uint32_t count;
    uint32_t targetSlot;
    uint32_t token;
};

std::vector<BagItemInfo> GetCashBagItems();
bool UseCashItem(uint32_t slotIndex);
std::vector<VendorShopItem> GetVendorShopItems(uint32_t vendorId = kVendorDefaultId);
bool SendVendorOpen(uint32_t token = kVendorSummonToken);
bool SendVendorBuy(uint32_t vendorId, uint32_t shopIndex, uint32_t count,
                   uint32_t targetSlot, uint32_t token = kVendorSummonToken);
bool SendVendorBuyShopItem(uint32_t itemId, uint32_t shopIndex, uint32_t count,
                           uint32_t vendorId = kVendorDefaultId,
                           uint32_t token = kVendorSummonToken,
                           const std::unordered_set<uint32_t> *avoidTargetSlots = nullptr,
                           VendorBuyResult *out = nullptr);
bool SendVendorBuyItem(uint32_t itemId, uint32_t count,
                       uint32_t vendorId = kVendorDefaultId,
                       uint32_t token = kVendorSummonToken,
                       const std::unordered_set<uint32_t> *avoidTargetSlots = nullptr,
                       VendorBuyResult *out = nullptr);
bool CloseVendorWindowLocal(bool *packetSentByUiHandler = nullptr);
bool SendVendorClose(uint32_t token = kVendorSummonToken);

// ---------- Account-shared bank (账号共享仓库) ----------
//
// 抓包 + IDA 实证 2026-06-14 (so3dplus.exe / shadowdance):泡点网店购买的物品进账号共享
// 仓库,需开仓库后把物品搬进 cash 背包才用得上。
//   开仓库: CUIManager::BeginContent(mgr, renderer, 2) —— 走游戏本地 UI call(IDA 实证
//           UICmdDispatcher__OnButton case 0x63D)。内部会建立本地仓库 UI content 并发
//           411154[1]+411644[self]。注意:**不能**直接裸发这两个包 —— 实测裸发后服务端
//           把角色置成"仓库交互中",但本地没建 UI content,移动被本地 gate 住,得手动开关
//           一次 bank 才恢复。走 BeginContent 本地状态正确,不卡。
//   仓库内容: 服务端回 recv proto 511320 整桶,由 NetLog 解析成 BankEntry 快照
//   关仓库: UIContentMarkClose(content) + CUIManager::CloseActiveContent(mgr, renderer)
//           (复用 vendor 关窗范式),搬完关掉恢复移动
//   搬到 cash: NetSkillSendPackage(411155, {destCashWireSlot(slot+13), bankUniqueId, 0, count}, 16)
//             (与摊贩购买 411020 同一发送器,只是 body 长度不同)
inline constexpr int kProtocolBankMove   = 411155;
inline constexpr int kBankContentEnum    = 2; // BeginContent 的 content enum:账号共享仓库

struct BankItemInfo
{
    uint32_t uid;    // 唯一实例 id (411155 搬运引用这个)
    uint32_t itemId; // 模板 itemId
    uint32_t count;  // 真实件数 (packed+1,按可堆叠口径)
};

// 调游戏本地 call 打开账号共享仓库(BeginContent enum 2)。会建本地 UI 并发开仓库包。
// 内容随后由 511320 回流,用 GetBankItems 读。失败返回 false。
bool SendBankOpen();
// 关闭账号共享仓库 UI(标记 content 关闭 + CloseActiveContent),搬完恢复角色移动。
bool SendBankClose();
// 取最近一次 511320 整桶解析出的仓库快照(转发 NetLog::GetBankSnapshot)。
std::vector<BankItemInfo> GetBankItems();
// 把仓库唯一实例 uid 的 count 件搬进 cash 背包。自动挑目标格:优先已有同 itemId 的格(堆叠),
// 否则首个空格;满则失败。返回选中的 cash slotIndex(0..79),失败返回 -1。
int SendBankMoveToCash(uint32_t uid, uint32_t itemId, uint32_t count);

// ---------- Magic Spring / 发条 (CMagicSpringOption) ----------
//
// IDA 实证 2026-06-14 (unpackd_so3d.exe):
//   发送(洗发条) proto 411590 = CG_REQ_SPRING_OPT_ASSIGN。WashClockwork_Send_411590
//   @0x7F1F90 的 body=3×u32: [发条所在背包(0=普通/1=cash), 目标装备 wire 槽(=arrayIndex+13),
//   发条物品 wire 槽(=spring item+0x00)]。服务器据 (bag, slot) 定位并消耗发条,发条种类
//   由该槽物品的 subtype(item+0x110) 决定。复用 NetSendDialogSelect(0x72C930) 这个
//   proto-generic 的 20-byte/3-dword 发送器(门控与 NetSendTriple 同,在线时无害)。
//
//   结果回包 proto 511132 = GC_RES_SPRING_OPT_ASSIGN, body=8×u32:
//   [resultCode(0=成功), grade, id1,id2,id3, val1,val2,val3]。成功后 sub_7A6130 把
//   [grade, id1,val1, id2,val2, id3,val3] (7×u32) qmemcpy 到 **item+0xB0**,所以洗完
//   直接读 item+0xB0 即可拿到新状态(无需挂 recv 钩子)。grade: sub_79C780 = *(item+0xB0)。
//
//   属性 id(1..23) → 字符串表 id 映射在 sub_7F2520(属性名渲染器)的 switch 里,经
//   StringTableCopy(0x1BF8E0=sub_5BF8E0) 取 Big5 名;偶数高 id 是百分比(+N%)与复合属性。
//   三种发条 subtype: 0=實習生(65) 1=高手(66) 2=武爾坎努斯(67);可交易与不可交易版同 subtype。
inline constexpr uintptr_t kSpringBlockOffset      = 0xB0; // item+0xB0: [grade,id1,v1,id2,v2,id3,v3]
inline constexpr uintptr_t kItemSubtypeOffset      = 0x110; // *itemTable*(=*(item+0x20))+0x110: subtype (发条 65/66/67)
inline constexpr int       kProtocolWashSpring     = 411590;
inline constexpr int       kSpringSubtypeBase      = 65;   // type 0/1/2 -> subtype 65/66/67
inline constexpr uint32_t  kSpringWireSlotBase     = 13;   // wire slot = arrayIndex + 13
inline constexpr size_t    kSpringGradeCount       = 5;    // N/G/DG/XG/SG (grade 0..4)

struct SpringAttr
{
    uint32_t id;     // 1..23 (0 = 空槽)
    int32_t  value;
};

struct SpringState
{
    bool       valid;     // 装备存在且能读出发条块
    uint32_t   grade;     // 0=N,1=G,2=DG,3=XG,4=SG
    SpringAttr attrs[3];  // 固定三条 (id 可能为 0 表示该槽未启用)
};

// 读取普通背包某 arrayIndex(0..191) 上装备的发条状态 (item+0xB0)。SEH 安全。
SpringState ReadSpringState(uint32_t equipSlotIndex);

// 用 springType(0/1/2) 的发条洗 equipSlotIndex 上的装备。自动在普通/cash 背包里
// 找对应 subtype 的发条算出 wire 槽并发 411590。找不到发条 / 资源未解析返回 false,
// errOut(可选) 填原因。
bool WashSpring(uint32_t equipSlotIndex, int springType, std::string *errOut = nullptr);

// 属性 id(1..23) → UTF-8 中文名 (经游戏字符串表)。isPercentOut(可选) 标记是否 +N%。
// 未知 id 返回空串。
std::string GetSpringAttrName(uint32_t attrId, bool *isPercentOut = nullptr);

// ---------- Active buffs (BuffHelper) ----------
//
// All buffs are owned by a single engine singleton, BuffHelper (g_BuffHelper @
// linear 0xE09178). Per host (player / creature) it keeps a std::list of active
// buff instances, retrieved by (hostType, hostId). For the local player:
//   hostType = 0, hostId = GetLocalUserId()  (g_pLocalUser + 112)
//
// Container lookup + list walk (verified against BuffHelper_ForEachHostBuff
// @ 0x648580 disassembly — an intrusive circular list whose stored pointers
// point AT the link field, not the node base):
//   container = FindHostBuffContainer(0, hostId)        -- NULL/empty => no buffs
//   end       = container + 0x59  (sentinel link-slot; also the head pointer)
//   firstLink = *(container + 0x59)                     -- ==end or 0 => empty
//   buffInst  = firstLink - 0x43                        -- node base
//   advance   : nextLink = *(buffInst + 0x43)
//               stop when nextLink == end ; else buffInst = nextLink - 0x43
//   (the +0x43/+0x47 fields are the two list links embedded in each buffInst.)
//
// Reproducing the engine's keyed container lookup (an STL rb-tree keyed by the
// (hostType,hostId) pair) from C++ is fragile, so we resolve the container
// pointer by calling the engine's own FindHostBuffContainer (BuffHelper_
// FindHostBuffContainer @ 0x6486C0, __stdcall(hostType, hostId) — operates on
// the BuffHelper global internally, no `this` needed) through a function
// pointer (PatternResolver "BuffFindHostContainer"), then walk the intrusive
// list ourselves. The list link offsets are stable across the build.
//
// buffInst layout (offsets read by BuildBuffIconList / OnBuffmgrCallback):
//   +0x10 buffId        (uint16)
//   +0x12 duration      (uint32) -- total, GC "duration=%ld" in OnBuffmgrCallback
//   +0x37 endTimestamp  (int64)  -- remaining = endTimestamp - now
//   +0x33 boolActive    (uint32) -- only meaningful for flag 0x100000 buffs
//   +0x3F buffDesc*      -> BuffDesc row (see below)
//
// BuffDesc row = g_BuffDescTable + 0xAF * buffId (engine sub_58D630). Fields:
//   +0x02 category (uint8)  -- 1/2/5 = normal (skill) buff, 4/0 = cash/system
//   +0x05 name     (char[], Big5)
//   +0x79 flags    (uint32) -- 0x80000 => endTimestamp/duration are in SECONDS
//                              (time()), otherwise MILLISECONDS (GetTickCount).
//                              0x100000 => bool-style buff (uses boolActive, no
//                              countdown).
//   +0x7F iconGroup (int16) -- -1 means not rendered
//
// "Now" base: GetTickCount() for ms buffs, time(NULL) for sec buffs (matches
// BuildBuffIconList's `v15=GetTickCount` / `v20=time(0)` split). remainingMs is
// normalised to milliseconds (-1 / 0 => no countdown / expired).
inline constexpr uintptr_t kBuffInstIdOffset       = 0x10; // uint16
inline constexpr uintptr_t kBuffInstDurationOffset = 0x12; // uint32
inline constexpr uintptr_t kBuffInstBoolOffset     = 0x33; // uint32
inline constexpr uintptr_t kBuffInstEndTsOffset    = 0x37; // int64
inline constexpr uintptr_t kBuffInstDescOffset     = 0x3F; // BuffDesc*
inline constexpr uintptr_t kBuffContainerSentinelOffset = 0x59; // sentinel link-slot / head ptr
inline constexpr uintptr_t kBuffNodeLinkOffset          = 0x43; // node = linkValue - 0x43
inline constexpr size_t    kBuffMaxListWalk             = 256;  // torn-list safety cap

inline constexpr uintptr_t kBuffDescCategoryOffset  = 0x02; // uint8
inline constexpr uintptr_t kBuffDescNameOffset      = 0x05; // Big5 char[]
inline constexpr uintptr_t kBuffDescFlagsOffset     = 0x79; // uint32
inline constexpr uintptr_t kBuffDescIconGroupOffset = 0x7F; // int16
inline constexpr size_t    kBuffDescStride          = 0xAF; // 175
inline constexpr size_t    kBuffNameMaxLen          = 64;

inline constexpr uint32_t  kBuffFlagSeconds  = 0x80000;  // duration in sec not ms
inline constexpr uint32_t  kBuffFlagBool     = 0x100000; // bool buff, no countdown

// g_SkillBuffMap (RVA 0x9C2640): skill->buff table, 116 entries of
// {int skillId, int buffTableId}. We use it to answer "which skill produced
// this buff". The pairing is skillId -> buffTableId; the buffTableId is the
// per-skill effect row id, which DoSkillBuff resolves to a runtime buffId. In
// this build the buffTableId equals the runtime buffId for skill buffs, so a
// direct reverse scan (entry.buffTableId == liveBuffId) yields the skill id.
// Returns 0 if no skill maps to this buff (server/system/cash buffs).
inline constexpr size_t kSkillBuffMapCount = 0x74; // 116 entries

// Category classification mirroring the two engine render bars
// (RenderNormalBuffBar {1,2,5} vs RenderCashBuffBar {4,0}).
enum class BuffKind : uint32_t
{
    Normal = 0, // category 1/2/5 — skill / status buffs (top bar, has countdown)
    Cash   = 1, // category 4/0 — cash-shop / system buffs (second bar)
};

struct ActiveBuff
{
    uint32_t    buffId;       // buffInst+0x10
    uint32_t    category;     // buffDesc+0x02 (raw)
    BuffKind    kind;         // Normal vs Cash (derived from category)
    std::string name;         // UTF-8, from buffDesc+0x05 (Big5)
    uint32_t    duration;     // buffInst+0x12 (total)
    int64_t     remainingMs;  // normalised remaining time in ms; -1 = no countdown
    uint32_t    skillId;      // reverse-mapped via g_SkillBuffMap; 0 if none
    bool        isBoolBuff;   // flag 0x100000 (toggle-style, no timer)
    uintptr_t   instAddr;     // buffInst base (diagnostics)
};

// Returns the buffId that a given skill applies, by scanning g_SkillBuffMap.
// 0 if the skill is not a skill-buff. Mirrors engine sub_B93040 semantics.
uint32_t GetSkillBuffId(uint32_t skillId);

// Reverse of the above: given a runtime buffId, returns the skillId that
// produces it (0 if none / system buff). Used to annotate ActiveBuff.skillId.
uint32_t GetBuffSourceSkillId(uint32_t buffId);

// SEH-safe snapshot of the local player's active buffs. Walks BuffHelper's
// per-host buff list via the engine's FindHostBuffContainer, reads each
// buffInst + its BuffDesc, computes remaining time, and reverse-maps the source
// skill. `kindFilter` < 0 returns all; 0 = Normal only; 1 = Cash only.
// Sorted by remainingMs ascending (永久/无倒计时 buff sorted last).
std::vector<ActiveBuff> GetActiveBuffs(int kindFilter = -1);

// Convenience: active buffs of a specific host (player or creature) by id.
// hostType 0 = player (UserList), other host types match the engine's
// FindHostBuffContainer keying. Mainly for nearby-player/creature buff display.
std::vector<ActiveBuff> GetHostBuffs(uint32_t hostId, int hostType = 0,
                                     int kindFilter = -1);

// ---------- Cash-item effect icons (g_pGameHUD) ----------
//
// The cash-buff icons the user sees are NOT all owned by BuffHelper. The
// BuffHelper container only holds a single category-{4,0} buff (e.g.
// premiumselling). The rest are timed "active cash item effect" icons owned by
// the in-game HUD object g_pGameHUD (linear 0xED40F0, PatternResolver
// "GameHUDThis"). HUD_RenderCashItemBars (sub_7C2230) draws them from a
// std::vector at GameHUD+0x25C0 (begin ptr @ +0x25C0, end ptr @ +0x25C4),
// 280-byte records. Record layout (from producer sub_7C5BB0 / sub_7C59E0):
//   +0x00 typeId   (uint32) -- cash-item effect type (special value 7)
//   +0x0C iconId   (uint32) -- HUD icon-table key (sub_A368E0 group lookup)
//   +0x10 name     (char[], Big5) -- label text
//   +0x114 blink   (uint8)  -- 1 = expiring (alpha-blinked)
// count = (end - begin) / 280.
//
// We surface these as ActiveBuff entries with kind=Cash so queryBuffs returns
// the full cash list. They have no buffId/duration in the BuffDesc sense, so
// buffId=0, remainingMs=-1, and the iconId is carried in `skillId` purely as a
// diagnostic. (A second std::map at GameHUD+0x27F4 also feeds the bar, but it is
// an rb-tree that is fragile to walk; left out unless entries are still missing.)
inline constexpr uintptr_t kHudCashVecBeginOffset = 0x25C0; // begin ptr
inline constexpr uintptr_t kHudCashVecEndOffset   = 0x25C4; // end ptr
inline constexpr size_t    kHudCashRecStride      = 280;
inline constexpr uintptr_t kHudCashRecTypeOffset  = 0x00;  // uint32
inline constexpr uintptr_t kHudCashRecIconOffset  = 0x0C;  // uint32
inline constexpr uintptr_t kHudCashRecNameOffset  = 0x10;  // Big5 char[]
inline constexpr uintptr_t kHudCashRecBlinkOffset = 0x114; // uint8 (276)
inline constexpr size_t    kHudCashMaxRecords     = 64;    // sanity cap

// SEH-safe snapshot of the HUD cash-item effect icons (vector @ GameHUD+0x25C0).
// Returned as ActiveBuff entries (kind=Cash, buffId=0, name from rec+0x10).
std::vector<ActiveBuff> GetCashItemIcons();

// ---------- Party / 组队 (classic CMessenger party) ----------
//
// The party roster lives in data-section globals (registered in PatternResolver;
// RVA = linear - 0x400000). Fully reversed off CMessenger_RecvPartyMemberList
// (0x843DF0) and verified live against shadowsing's party (2026-06-08):
//   g_PartyMemberCount   (RVA 0xA2872C) int — 0 = solo, max 6
//   g_PartyMemberArray   (RVA 0xA28730) 6 × 0x3B entries
//   g_PartySelfIndex     (RVA 0xA2896C) int — my slot, -1 = unset
//   g_PartySelfRole      (RVA 0xA28894) int — 0=none 1=leader(idx0) 2=member
//   g_PartyMemberStateArray (RVA 0xA28898) 6 × 7 dwords; state[0]=HP state[1]=MaxHP
//   g_PartyListValid     (RVA 0xA28974) int
// Member entry (stride 0x3B=59): +0x00 name char[17] (Big5, engine strncmp 0x11),
// +0x19 int online/valid gate (>0 = slot in use), +0x31 int member userId.
//
// NOTE — the SELF slot in the array + state table reads back as -1/blank: the
// engine renders the local player's own HP/name from the player object, not
// from this roster. GetPartyMembers() therefore back-fills the self member from
// GetLocalUserId()/GetLocalPlayerName()/GetLocalHp().
inline constexpr size_t    kPartyMaxMembers        = 6;
inline constexpr uintptr_t kPartyEntryStride       = 0x3B;
inline constexpr uintptr_t kPartyEntryNameOffset   = 0x00;
inline constexpr size_t    kPartyEntryNameMaxLen   = 17;  // engine strncmp 0x11
inline constexpr uintptr_t kPartyEntryOnlineOffset = 0x19;
inline constexpr uintptr_t kPartyEntryIdOffset     = 0x31;
inline constexpr uintptr_t kPartyStateStride       = 0x1C; // 7 dwords / member
inline constexpr uintptr_t kPartyStateHpOffset     = 0x00; // state[0]
inline constexpr uintptr_t kPartyStateMaxHpOffset  = 0x04; // state[1]

struct PartyMember
{
    int                     index;    // slot in the engine roster array
    std::string             name;     // UTF-8 (from Big5)
    uint32_t                userId;   // entry+0x31 (self back-filled)
    bool                    isSelf;   // index == g_PartySelfIndex
    bool                    online;   // entry+0x19 > 0
    int32_t                 hp;       // state[0]; -1 = unknown
    int32_t                 maxHp;    // state[1]; -1 = unknown
    // nearby = userId resolves in the around-player AOI list (self always true).
    // The AOI list only holds entities loaded around the local player, so
    // nearby=false means the teammate is too far / on a different map and a
    // remote buff cast wouldn't reach them. distance is world units (~50/tile),
    // -1 when unknown (not in AOI).
    bool                    nearby;
    float                   distance;
    std::vector<ActiveBuff> buffs;    // GetHostBuffs(userId, isSelf?0:2)
};

struct PartySnapshot
{
    bool                     inParty;   // g_PartyMemberCount > 0
    int                      role;      // g_PartySelfRole
    int                      selfIndex; // g_PartySelfIndex
    std::vector<PartyMember> members;
};

// SEH-safe snapshot of the local player's party. When includeBuffs is true each
// member's `buffs` is filled via GetHostBuffs (self = hostType 0, teammates =
// hostType 2 — the same key the engine's own party-UI buff-icon renderer uses).
// Returns inParty=false / empty members when solo.
PartySnapshot GetPartyMembers(bool includeBuffs = true);

// ---------- MailBox::SendItemMail (proto 411524, op=1) ----------
//
// Engine helper at 0x7F4F80 — __stdcall(int op, const char *recipient,
// int itemBagId, int itemCount, const char *body). Same self-contained
// pattern as SendMoneyMail: zeroes a 256-byte buf, packs fields, calls
// Net__BeginSend + Net__SkillSendPackage(buf, 411524, payload, 0x4D).
//
// itemBagId is the item's bag-slot unique id (item+0x00, NOT the game
// template itemId). itemCount is how many to send from that stack.
// Recipient 1..15 ASCII chars. SEH-wrapped.
bool SendItemMail(const char *recipient, uint32_t itemBagId,
                  uint32_t itemCount, const char *body = "");

// ---------- Walk-to-world-position (CLocalUser::SetAfterAction) ----------
//
// Drives the engine's own pathfinding queue. SetAfterAction (linear 0x7539E0)
// writes (x, y, action, targetId) into the local-player struct (+0x32F0..+0x32FC)
// and sets the "move-after-action pending" flag at +0x2BC8 = 1; per-frame
// TraceMove + the A* mover consume that queue and walk the avatar to (x, y).
// See OnPlayerMoveClick @ 0x871D10 for the canonical call shape.
//
// IMPORTANT — XY are world coordinates in the same scale as CUser+0x3C/0x40
// (the values you'd read with GetLocalPosition). The engine stores them as
// **truncated integers** at this+32F0/32F4 — verified against OnPlayerMoveClick
// @ 0x871D10 which uses `cvttss2si` (float->int truncation) before passing
// args, and AutoHunt__TickBattle which uses `(int)*(float *)…` (plain C cast).
// So 12345.6f is passed as int 12345. Bit-casting the float was the BUG that
// made earlier MoveTo calls effectively walk toward integer 1.18 billion.
//
// action: 1 = pure walk (move there and stop)
//         3 = walk + attack (used by AutoHunt when chasing a target)
// targetId: 0 for pure walk; creature/user id when action=3.
//
// Gates: SetAfterAction runs through ~7 reject conditions (m_bCanMove,
// stunTime, animation lock, skill mode, …). The ActionMoveModule patch
// @ 0x7539FE skips them all — when that module is ON, MoveTo works in any
// state. Without the patch, idle/walking states still succeed; mid-cast may
// reject. UI tells the user; this layer doesn't auto-enable the patch.
//
// Returns true if the engine accepted the request (SetAfterAction returned
// non-zero / fell through). SEH-wrapped to survive torn g_pLocalUser during
// scene transitions.
//
// MUST be called on the game's main / D3D9-present thread. RemoteControl IO
// thread is fine because broker handlers run on the same IO thread where
// AutoMail / SendMoneyMail already exercise engine packet helpers — those
// run engine code from non-main threads without issue.
bool MoveTo(float worldX, float worldY, int action = 1, uint32_t targetId = 0);

// ---------- Walk-and-talk-to-NPC ----------
//
// "Click a creature and let the game decide what to do" — the same behavior as
// the user clicking the in-world creature with the mouse, which is what
// `OnTargetCreatureClick_TalkOrAttack` @ 0x9584A0 does, but ALWAYS treating the
// target as a fight target. For NPCs, that path is wrong: it sends an attack
// packet and the server despawns/refuses the NPC.
//
// The correct two-path implementation, drawn from the engine's per-frame click
// dispatcher at sub_9EA090 + Npc__OpenDialogByCreatureRef @ 0xB3A3F0:
//
//   - NPC discriminator: call `Npc__LoadDialogScript(state, monsterTblId)`.
//     Nonzero return means "this creature has a dialog script" — i.e. it's a
//     real NPC. This is the engine's OWN test (used in 4+ places) — much more
//     reliable than the >=50000 monsterTblId heuristic which doesn't hold up
//     across maps (you have NPCs at 1080, 1781, etc).
//
//   - If NPC:
//       1) MoveTo(npcX, npcY, action=1) — walk close (need to be in range)
//       2) g_TargetCreatureId = npcId
//       3) Npc__OpenDialogByCreatureRef(mgr, &g_TargetCreatureId,
//                                       g_NpcDialogState, dialogUITable, p1, p2)
//      The engine itself only opens the dialog AFTER arrival in its sub_9EA090
//      path, but we cheat by calling the opener directly — for in-range NPCs
//      this works immediately; for too-far NPCs the dialog still opens but the
//      server will reject any subsequent action until in range. Acceptable.
//
//   - If monster:
//       Mirrors OnTargetCreatureClick_TalkOrAttack: write g_TargetCreatureId,
//       g_AfterActionIntent=3, then SetAfterAction(action=3, target=0). The
//       engine walks the player to the monster and starts attacking.
//
// MoveTo() vs TalkOrAttack()
// - MoveTo: pure "go to (x,y)", no target reference
// - TalkOrAttack: takes a creatureId; auto-dispatches NPC dialog vs combat.
//
// Returns true if the engine accepted the request (the appropriate path was
// dispatched). SEH-wrapped to survive torn g_pLocalUser/creature pointer.
//
// MUST be called on the game's main / D3D9-present thread, like MoveTo.
bool TalkOrAttack(uint32_t creatureId);

// ---------- City teleport ("/狮子城" chat command, proto 411076) ----------
//
// Typing "/<cityName>" in the chat box warps the player to that city. The
// command is CLIENT-parsed: the chat dispatcher (ChatCmd_Dispatch @ 0x969E30)
// falls through to ChatCmd_Teleport @ 0x966770, which:
//   1) destId = ResolveTeleportDestByName(CUIManager::GetUIContent(99),
//              "/cityName")  — looks the name up in the teleport table and
//              returns the destination id (e.g. 狮子城 -> 202), 0 if no match
//              / level / money / valid gate fails (resolver @ 0xA1F800).
//   2) Teleport_Execute @ 0x995020 -> Teleport_SendPacket @ 0x9925F0 emits a
//              position-sync packet (411000) then the warp request
//              Net__SendDword(buf, 411076, destId).
//
// We replicate that on the wire WITHOUT needing the chat-window object:
//   - ResolveTeleportDestId resolves a UTF-8 city name (converted to Big5 — the
//     table is Big5, game is TW build) to its dest id via the UIManager(99)
//     teleport table. A leading '/' is added if absent (the resolver compares
//     name+1, skipping the slash).
//   - SendTeleportToDest fires SendPlayerMoveSync (best-effort, like the engine)
//     then Net__SendDword(411076, destId). Local death/map gates are bypassed
//     (project philosophy — the server still validates), so a warp that the
//     in-game button would grey out is simply rejected server-side instead of
//     spinning.
//
// All SEH-wrapped. ResolveTeleportDestId returns 0 on no-match / gate-fail /
// torn UI table; SendTeleportToDest/TeleportByCityName return false on failure.
//
// MUST be called on the game's main / D3D9-present thread (engine packet path).
int  ResolveTeleportDestId(const char *cityNameUtf8);
bool SendTeleportToDest(int destId);
bool TeleportByCityName(const char *cityNameUtf8);

// Snapshot wrapper around the engine's CreatureMgr list, filtered to entries
// the user could "talk to" — kind != Pet, alive, has a creature id. Includes
// both NPCs AND monsters because the engine doesn't distinguish them
// structurally (both kind==3 in this build) — the discriminator is the
// data-table id at statTable+236 (MonsterTblId). NPC templates are usually in
// [50000, 99999] in this game's data files; UI uses that range to label/filter.
// Sorted ascending by distance.
struct NearbyNpc
{
    uint32_t    creatureId;
    uint32_t    kind;         // 0=special, 3=NPC OR monster (see hasDialog), 7/8=pet (filtered)
    uint32_t    monsterTblId; // statTable+236 — data-table row id (template). Used as the
                              // key into Npc__LoadDialogScript to determine NPC vs monster.
    bool        hasDialog;    // true iff engine's Npc__LoadDialogScript returns nonzero
                              // for this template. THE canonical NPC discriminator.
    uint32_t    level;
    float       distance;
    float       x, y, z;
    int64_t     hp;
    std::string name;         // UTF-8, converted from statTable+8 (Big5). Empty on read failure.
    uintptr_t   addr;         // CCreature*, for diagnostics only
};

std::vector<NearbyNpc> GetNearbyNpcs(float maxDistance);

// ---------- Map collision / pathfind helpers (auto-flee) ----------
//
// 引擎的可达性判定全 funnel through Map__IsBlocked @ 0xA97080。它接收世界
// 坐标对应的整 tile 索引,从 g_pCurMap+0x10 处的 collisionGrid (int[width*height])
// 里取值；返回 0 = 可走,1 = 不可走/越界/地图未加载。world float 转 tile
// 用 floor 取整(引擎是 sub_AA2050,我们用 std::floor 直接走)。
//
// 自动退避用法:
//   1. 选一个理想退避方向上的远点 (远离怪物的反方向)
//   2. 调 RaycastFurthestWalkable -- 它沿射线 1-tile 步进,返回起点 -> 远点
//      之间最远那个仍然可走的 tile,保证返回值是合法可走点
//   3. 把结果 (tx, ty) 灌进 MoveTo(tx, ty, action=1)
//
// IsTileWalkable 是兜底单点查询,主要用于校验/调试。
//
// 全部 SEH 包过 -- g_pCurMap 在切图时会瞬时撕裂,即便单帧读不到也只算 "暂时
// 不知道",不应炸 worker 线程。
bool IsTileWalkable(int tileX, int tileY);

// 沿 (sx, sy) -> (dx, dy) 这条射线步进,返回最远仍可走的 tile。结果一定是
// 可走的(若起点本身被阻挡,会返回 dst 让上层判失败)。outX/outY 为 tile 整数。
// dist 为期望的射线长度(tile);算法内部在 tile 整数空间走,精度足够。
bool RaycastFurthestWalkable(int sx, int sy, int dx, int dy,
                             int &outX, int &outY);

// 读 g_TargetCreatureId — 当前选中的怪物 / NPC ID。0 表示未选,-1 在引擎里也
// 偶尔被写入表示 "无目标",用 0 等价处理。
uint32_t GetTargetCreatureId();

// 通过 EntityManager::FindCreatureById 反查 CCreature*。返回 0 表示找不到。
// 成功的话填入怪物坐标 (x, y, z) 和其它常用字段。SEH-wrapped。
struct CreatureSnapshot
{
    uintptr_t addr;
    uint32_t  id;
    uint32_t  kind;       // statTable+0xDC. 7=NPC, 8=pet, 其他=monster
    int64_t   hp;
    float     x, y, z;
};
bool LookupCreatureById(uint32_t creatureId, CreatureSnapshot &out);

} // namespace GGTB
