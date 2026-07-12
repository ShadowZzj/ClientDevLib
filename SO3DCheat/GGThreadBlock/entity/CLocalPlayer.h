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

// ---------- Move-while-acting (split: 普攻移动 / 技能移动) ----------
//
// SetAfterAction (0x7539E0) is the click-to-move queue. After the stun gate it
// discriminates by SkillMode (this+0x370C): values {3,5,8} = a skill is in
// progress, and the move is rejected at "gate 2" (loc_753A54). A normal attack /
// combo runs with SkillMode==0 (CLocalUser__UpdateAttackAction resets it to 0),
// so it FALLS THROUGH gate 2 to loc_753A80 and is rejected later by the generic
// ready-factor / m_bCanMove / animation gates. Because the engine already routes
// skill vs attack into two different reject paths, the split is independent with
// NO leak between the two toggles:
//
//   SkillMoveGate  @ 0x753A54: skill-mode reject block, only reached when
//                              370C∈{3,5,8}. A 5-byte `E9 rel32` redirects it to a
//                              code cave (VirtualAlloc'd at runtime) that branches
//                              on comboState (this+0x2BDC): ==0 (plain skill) jumps
//                              to 0x753CC4 (success) so move-while-cast works; >0
//                              runs the ORIGINAL reject. NOTE: a 连续技/combo runs in
//                              attack-state (370C==0), so it goes through AttackMove
//                              gate, NOT here — this cave's >0 branch is a near-dead
//                              edge case. The actual anti-burst is at the shared
//                              TraceMove recovery gates (see TraceMoveGate1/2 below),
//                              not here. 0x2BDC is the combo encoding the HUD's
//                              ComboNum indicator draws from (written only by the
//                              combo subsystem, never by skill-cast).
//   AttackMoveGate @ 0x753A80: the non-skill continue path (370C∉{3,5,8}). 5-byte
//                              `E9 3F 02 00 00` = jmp 0x753CC4 (success) — lets a
//                              queued move through for normal attack/combo (and
//                              idle) ONLY.
//
// Both jump to the same success block (0x753CC4), which re-reads x/y from the ebp
// frame, so jumping there from either site is frame-safe. The stun gate
// (0x7539FE) must NOT jump to success wholesale (as the old combined ActionMove
// did) — that skips gate 2 and leaks skill-move into the attack toggle. Instead
// it is a SHARED site flipped to FALL THROUGH to gate 2 (jle -> jmp short, same
// 0x22 disp), so a stunned move still runs the skill/attack discrimination and
// stays leak-free (see StunGate below).
//
// Anti-burst lives in the shared TraceMove recovery gates, not the SetAfterAction
// split above. A combo advances via 123.dll driving ExecuteComboSkill (0x97D200),
// whose throttle is `+0x3468 <= 0` (per-cast recovery). TraceMove gates movement on
// that same +0x3468 (and +0x2BCC); a blanket bypass of those gates let a forced
// move cancel the recovery so the next cast fired immediately -> burst -> DC. That
// bypass is shared by BOTH toggles, which is why both bursted. The fix is to make
// the two TraceMove recovery gates comboState-conditional (see TraceMoveGate1/2).
//
// The 6 sites below are SHARED and refcounted (patch when either toggle is on,
// restore when both off). The TraceMove/MoveSync/Status21 ones are execution-path
// gates that only ever advance a move SetAfterAction already QUEUED; StunGate is
// the SetAfterAction entry gate but only FALLS THROUGH to gate 2 (never to
// success). Either way an un-queued move never executes and gate 2 still
// discriminates, so sharing them does NOT leak.
//   StunGate         @ 0x7539FE: flip `7E -> EB` (jle -> jmp short, keep 0x22 disp)
//                                so a stunned click falls through to gate 2 instead
//                                of rejecting — lets you queue a move while stunned
//                                without skipping the skill/attack discrimination.
//   TraceMoveGate1   @ 0x756FAA: comiss+jbe on the per-frame stunTime/recovery
//                                (+0x3468). Redirected (9-byte E9 hook at 0x756FA3,
//                                overwriting comiss+jbe) to a combo-gate cave: honor
//                                the recovery while a combo chain is live
//                                (comboState +0x2BDC > 0), bypass it otherwise. This
//                                is what actually stops 123.dll's auto-combo from
//                                bursting — a blanket `76->EB` here let movement
//                                cancel the per-cast recovery and both 普攻/技能移动
//                                flooded the server. See CLocalPlayer.cpp.
//   TraceMoveOrChain @ 0x756FCD: 5-byte `E9 19 01 00 00` jmp 0x7570EB, skip the
//                                animation/skill-ID OR-chain.
//   TraceMoveGate2   @ 0x7570FD: comiss+jbe on the +0x2BCC per-frame timer. Same
//                                9-byte combo-gate cave redirect (hook at 0x7570F6)
//                                as TraceMoveGate1, so during a combo TraceMove
//                                behaves exactly like vanilla (no forced move).
//   MoveSyncGate     @ 0xB2DF81: flip `7F -> EB` on the m_bCanMove (+0x304) gate in
//                                SendPlayerMoveSyncPacket — keeps the SERVER move-sync
//                                packet flowing while m_bCanMove<=0 (else rubber-band).
//                                Needed for snipingmode buff(426)/狙击姿态 and any &8
//                                BitFlag stance that does --m_bCanMove on add.
//   Status21Gate     @ 0x757135: flip `7E -> EB` on the inner status-21 (定身/移动锁,
//                                set by snipingmode skill 357 / buff 426) gate in
//                                TraceMove; else a queued click-move never advances.
inline constexpr size_t kAttackMoveGateSize         = 5; // jmp near (replaces mov eax, g_pLocalUser)
inline constexpr size_t kSkillMoveGateSize          = 5; // jmp near (replaces mov eax,[ebp-4]; mov ecx,..)
inline constexpr size_t kSharedMoveStunGateSize     = 1; // jle short -> jmp short (fall through to gate 2)
inline constexpr size_t kSharedMoveTraceGate1Size   = 9; // comiss+jbe -> E9 rel32 to combo-gate cave + 4 NOP
inline constexpr size_t kSharedMoveTraceOrChainSize = 5; // cmp imm32 -> jmp near
inline constexpr size_t kSharedMoveTraceGate2Size   = 9; // comiss+jbe -> E9 rel32 to combo-gate cave + 4 NOP
inline constexpr size_t kSharedMoveSyncGateSize     = 1; // jg short  -> jmp short
inline constexpr size_t kSharedMoveStatus21GateSize = 1; // jle short -> jmp short

bool PatchAttackMove();
bool RestoreAttackMove();
bool PatchSkillMove();
bool RestoreSkillMove();

// ---------- No-Shift player attack ----------
//
// WorldClick__DispatchTargetOrGround calls CLocalUser__CheckPkAttackEligible before
// setting the player target and queuing SetAfterAction(action=8). The live 123.dll
// replaces the DIK_LSHIFT read at 0x9586A4 with a VM-protected jump and, when Shift
// is up, can leave the predicate before it reaches the visible branch at 0x9586AE.
// Patch that hook entry with a direct jump to the original Shift-success block at
// 0x9586CB. All map/PK/target gates before the input read and the level >= 30 gates
// after the success block remain intact; no keyboard state is synthesized.
inline constexpr size_t    kNoShiftAttackEntryPatchSize = 5;    // E9 rel32
inline constexpr uintptr_t kNoShiftAttackAcceptOffset   = 0x27; // 0x9586CB - 0x9586A4

bool PatchNoShiftAttack();
bool RestoreNoShiftAttack();

// ---------- Ignore crowd-control (眩晕/沉默 bypass) ----------
//
// 客户端在三个动作入口用 CUser__GetAbnormalStatusValue(uid, idx) 查异常状态来
// 拦截操作。idx 18=眩晕(stun), 19=沉默(silence)。本补丁翻 4 个分支让这些 gate
// 失效(服务端仍校验 — client bypass, server validates):
//
//   ItemStunGate     @ 0x9A4865: CLocalUser__OnUseItem 的唯一管制门。
//                                `7E 05` (jle 用道具) -> `EB 05` (jmp): 眩晕也能吃药。
//   CastStunGate     @ 0x9A6410: OnSkillShortcutKey 状态 18(眩晕) gate。
//                                `7F 1A` (jg 退出) -> `90 90` (nop): 不再因眩晕退出,
//                                落到沉默检查。
//   CastSilenceGate  @ 0x9A642A: OnSkillShortcutKey 状态 19(沉默) gate。
//                                `7E 05` (jle 继续) -> `EB 05` (jmp): 沉默也放行。
//   CastStunTimeGate @ 0x9A646E: OnSkillShortcutKey 的 +0x3468 stunTime 浮点门
//                                (眩晕另设的倒计时,沉默不置位)。`76 05` (jbe 继续)
//                                -> `EB 05` (jmp): 眩晕计时未清零也放行。
//
// 只翻这 4 个 CC 相关分支 —— +0x5F88/IsDead/+0x2FAC/+0x3010 等门保持原样,死亡/
// 未就绪等仍正常拦截。状态查询本身不动,UI 上的眩晕/沉默图标照常显示(更隐蔽)。
inline constexpr size_t kIgnoreCCItemStunGateSize     = 1; // jle short -> jmp short
inline constexpr size_t kIgnoreCCCastStunGateSize     = 2; // jg short  -> nop nop
inline constexpr size_t kIgnoreCCCastSilenceGateSize  = 1; // jle short -> jmp short
inline constexpr size_t kIgnoreCCCastStunTimeGateSize = 1; // jbe short -> jmp short

bool PatchIgnoreCC();
bool RestoreIgnoreCC();

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

// ---------- Visit nearby street stalls (打开附近玩家的个人摆摊) ----------
//
// IDA 实证 2026-06-25 (unpackd_so3d.exe):点开别人摊位 = CG 411042
// (CG_SendVisitStreetStall_411042 @ 0xB2B650)。摊主信息存在该 CUser 上(本地/远程
// 同一 CUser 布局,见 CUser 构造 0x9F8310 与 OpenStreetStall_Type2 0x9BFE90):
//   CUser+0x2DEC(11756) byte != 0  -> 该玩家正在摆个人摊(开摊置 1,收摊/构造清 0)
//   CUser+0x2DED(11757) 32B 字符串 -> 店铺名(账号编码 Big5,与开摊 411040 的 32B 名同字段)
//   CUser+0x2E14(11796) dword      -> 摊位类型(个人摊 0/2/3)
//   CUser+0x70(112)     dword      -> userId(411042 包体首字段,= kUserSelfIdOffset)
// 打开走 faithful 点击路径(sub_9941A0 @ 0x994693),不是裸发包,故不卡角色移动:
//   client = NetBeginSend();
//   CG_SendVisitStreetStall_411042(client, userId, stallType, 0);  // 写 [len28][411042][userId][16×0]
//   GetUIContent(GetSingleton(), 28) 的 +0x38 写入 userId;  // 28=买摊窗口,记住被访摊主
// 服务器随后回 511555/511589/511595 填充并弹出买摊窗口。
inline constexpr uintptr_t kUserStallFlagOffset           = 11756; // 0x2DEC byte: 正在摆摊
inline constexpr uintptr_t kUserStallNameOffset           = 11757; // 0x2DED 32B: 店铺名 (Big5)
inline constexpr uintptr_t kUserStallTypeOffset           = 11796; // 0x2E14 dword: 摊位类型
inline constexpr size_t    kUserStallNameMaxLen           = 32;
inline constexpr int       kStallBuyContentId             = 28;    // GetUIContent(mgr, 28) = 买摊窗口
inline constexpr uintptr_t kStallBuyContentOwnerIdOffset  = 0x38;  // content28+0x38 = 被访摊主 userId
inline constexpr int       kProtocolVisitStreetStall      = 411042;
inline constexpr int       kProtocolStreetStallWarp       = 411597; // CG_REQ_STREETSTALL_WARP {x,y}

struct StallPlayer
{
    std::string name;      // 摊主角色名 (UTF-8)
    std::string stallName; // 店铺名 (UTF-8)
    uint32_t    userId   = 0;
    uint32_t    stallType = 0;
    float       distance = 0.0f;
    float       x = 0, y = 0, z = 0;
};

// 走 EntityManager AOI 单链表,筛出正在摆个人摊的玩家(CUser+0x2DEC!=0),
// 按距离升序返回 maxDistance 内的摊主。SEH 包裹,跨线程读安全。
std::vector<StallPlayer> GetNearbyStallPlayers(float maxDistance);

// 打开指定 userId 的个人摊位(faithful 点击路径,见上)。成功(发包成功)返回 true。
// stallType 取摊主 CUser+0x2E14;调用方一般直接用 GetNearbyStallPlayers 给出的值。
bool OpenStreetStall(uint32_t userId, uint32_t stallType);

// 传送到地图格坐标 (x, y) —— CG 411597。复刻引擎街摊「传送到摊主」发包
// (CStreetStallSearch::SendWarpToDealer @ 0x9CCA00):拼 [len16][411597][x][y]
// 经最底层明文发送 Net__SendPacket_Plaintext 发出。x/y 是整数地图格坐标(非世界浮点
// 坐标,后者是 float 且布局 X,Z,Y)。发包成功返回 true。
bool WarpToCoordinate(int x, int y);

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

enum class DropItemLookupState : uint32_t
{
    Unavailable,
    Missing,
    Present,
};

// SEH-safe walk of CItemContainer's drop list. Returns drops within
// `maxDistance` of the local player (or all drops if maxDistance <= 0),
// sorted ascending by distance. Filters out canPick==0 entries by default
// (controlled by includeUnpickable).
std::vector<DropItemInfo> GetNearbyDropItems(float maxDistance,
                                             bool  includeUnpickable = false,
                                             bool *ready = nullptr);

// Exact engine lookup by dropId. Missing is authoritative; Unavailable means
// the container/function/local position could not be read and must not be
// interpreted as a successful pickup.
DropItemLookupState QueryDropItemById(uint32_t dropId, DropItemInfo &out);

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
// The creature `type` LIVES INSIDE StatTable, NOT on CCreature itself:
//   *(StatTable + 0xDC) = type
// i.e.  *(_DWORD *)(*(_DWORD *)(cre + 0x368) + 0xDC). Live table24 dumping
// shows that this is an AI/behavior kind, not a complete monster-vs-object enum.
// NPC/talkables use kind 3, while many combat kinds exist beyond kMonsterTypes.
inline constexpr uintptr_t kCreaturePosXOffset      = 0x3C;
inline constexpr uintptr_t kCreaturePosZOffset      = 0x40; // vertical
inline constexpr uintptr_t kCreaturePosYOffset      = 0x44; // ground-plane Y
// Queued/click position (int) used by Creature_GetPosXZ -> SetAfterAction
inline constexpr uintptr_t kCreatureQueuedXOffset   = 0x19C;
inline constexpr uintptr_t kCreatureQueuedYOffset   = 0x1A0;
inline constexpr uintptr_t kCreatureIdOffset        = 0x70;
inline constexpr uintptr_t kMonsterLevelOffset      = 0xA0;
// Stable monster.edt/table24 row index from the 0x114-byte stat-table entry.
inline constexpr uintptr_t kStatTableIndexOffset    = 0x04;
// type is at *(statTable + 0xDC), not cre+0xDC. Read via the StatTable indirection.
inline constexpr uintptr_t kStatTableTypeOffset     = 0xDC;
// Creature__MatchesTargetSpec reads this as a signed int and, with AutoHunt's
// default exclude mask, rejects the row when the value is >0. Live table24 data
// confirms that ordinary NPC/protected rows use positive values here.
inline constexpr uintptr_t kStatTableAutoTargetExcludeOffset = 0xF0;
// Legacy runtime data-key at statTable+0xEC. This is NOT the table24 row index
// at +0x04 and must not be used for the BuffKeeper denylist.
inline constexpr uintptr_t kStatTableMonsterTblIdOffset = 236;
// Localized creature name at statTable+8, char[100], Big5 encoded, null-terminated.
// Verified against MonsterTblEntry loader sub_B77EB0 which `memcpy_s_41(entry+8, 0x64, ...)`
// from the parsed monster.edt row. Same row position as `name` field — every
// alive CCreature (NPC or monster) carries its display name here.
inline constexpr uintptr_t kStatTableNameOffset     = 0x8;
inline constexpr size_t    kStatTableNameMaxLen     = 100;
// Full/template HP lives on StatTable (int64 at statTable+0xA8), NOT on
// CCreature. Verified in CMsg_OnCrtDamage @ 0x94F120: the engine's "event
// monster back to full HP?" check compares the live curHP int64 at
// creature+0x320 against *(int64*)(statTable+0xA8) via its lo/hi halves
// (statTable+168 / +172). That value is the max HP loaded from the monster
// template, so we surface it as maxHp. Best-effort; 0 when the entry is torn.
inline constexpr uintptr_t kStatTableMaxHpOffset    = 0xA8;
// Combat fields from the 0x114-byte MonsterStatTable layout. Live table24
// records for gift boxes, treasure chests, trees, seals, eggs and other
// non-combat objectives use 0/1 sentinels across attack/hit/miss. BuffKeeper
// requires at least one real combat signal (>1) so those rows fail closed.
inline constexpr uintptr_t kStatTableAttackOffset   = 0xBC;
inline constexpr uintptr_t kStatTableHitRateOffset  = 0xC8;
inline constexpr uintptr_t kStatTableMissRateOffset = 0xCC;
// Creature type @ *(statTable + 0xDC). NPCs and teleport points are kind 3.
// kMonsterTypes is retained only for legacy attack-selection consumers; it is
// not complete enough for the buff-keeper's ordinary-enemy gate.
inline constexpr uint32_t  kCreatureTypeTalkable    = 3;    // NPC / teleport / interactable
inline constexpr uint32_t  kCreatureTypeTree        = 0xA;  // gatherable resource
// Attackable-monster type values, empirical from the old cheat's IsMonster()
// and live captures. Type 37 was confirmed on the Sage Tower Another BOSS.
// Kept as a set so new live-confirmed monster types can be added explicitly.
inline constexpr uint32_t  kMonsterTypes[]          = {0, 1, 37, 38, 60, 69, 71, 73};
inline bool IsMonsterType(uint32_t type)
{
    for (uint32_t v : kMonsterTypes)
        if (type == v)
            return true;
    return false;
}

// Confirmed non-ordinary rows which otherwise look combat-capable. These are
// task components, a disguised NPC, or test/placeholder templates in the
// current table24 dump. Keep this purpose-specific and fail closed for them.
inline constexpr bool IsBuffGateExcludedTableIndex(uint32_t tableIndex)
{
    return tableIndex == 344 ||
           (tableIndex >= 2150 && tableIndex <= 2155) || // quest target: 奧客的命根
           (tableIndex >= 2159 && tableIndex <= 2161) || // quest target: 黛安娜之眼
           (tableIndex >= 2165 && tableIndex <= 2167) || // quest target: 黛安娜之心
           (tableIndex >= 2183 && tableIndex <= 2185) || // quest target: 石碑之火
           (tableIndex >= 3235 && tableIndex <= 3237) || // AI test rows
           (tableIndex >= 3629 && tableIndex <= 3631) || // disguised rental-office NPC
           (tableIndex >= 6532 && tableIndex <= 6534) || // experience test monsters
           (tableIndex >= 6665 && tableIndex <= 6670) || // generic test rows
           (tableIndex >= 7411 && tableIndex <= 7413);   // DummyForDroptest
}

// Conservative, purpose-specific classification for BuffKeeper. The list is
// derived from the current live table24 dump and intentionally includes only
// kinds whose F0-eligible, combat-capable rows are ordinary monsters/BOSSes.
// Mixed/objective kinds, sentinel-stat objects, unknown test kinds, NPCs and
// resources fail closed. This does NOT drive attack target selection.
inline constexpr bool IsBuffGateMonster(uint32_t tableIndex, uint32_t type,
                                        int32_t autoTargetExclude,
                                        uint32_t attack, uint32_t hitRate,
                                        uint32_t missRate)
{
    if (autoTargetExclude > 0 || IsBuffGateExcludedTableIndex(tableIndex) ||
        (attack <= 1 && hitRate <= 1 && missRate <= 1))
        return false;

    switch (type)
    {
    case 0:
    case 1:
    case 2:
    case 11:
    case 13:
    case 15:
    case 19:
    case 21:
    case 22:
    case 26:
    case 33:
    case 37:
    case 38:
    case 39:
    case 40:
    case 41:
    case 42:
    case 43:
    case 44:
    case 46:
    case 47:
    case 48:
    case 49:
    case 50:
    case 56:
    case 57:
    case 59:
    case 60:
    case 61:
    case 62:
    case 63:
    case 64:
    case 65:
    case 66:
    case 67:
    case 69:
    case 70:
    case 71:
    case 72:
    case 73:
    case 75:
    case 76:
    case 77:
    case 79:
        return true;
    default:
        return false;
    }
}

static_assert(IsBuffGateMonster(1, 0, 0, 11, 34, 2));
static_assert(IsBuffGateMonster(6325, 37, 0, 100, 100, 100));
static_assert(!IsBuffGateMonster(2150, 1, 0, 10, 480, 320)); // quest component
static_assert(!IsBuffGateMonster(1, kCreatureTypeTalkable, 0, 100, 100, 100));
static_assert(!IsBuffGateMonster(1, 23, 0, 100, 100, 100)); // mixed objective kind
static_assert(!IsBuffGateMonster(1, 35, 0, 100, 100, 100)); // minigame targets
static_assert(!IsBuffGateMonster(1, 68, 0, 100, 100, 100)); // BOSS components
static_assert(!IsBuffGateMonster(4149, 0, 0, 1, 1, 1));    // gift box sentinel
static_assert(!IsBuffGateMonster(5531, 51, 0, 100, 100, 100)); // bomb mechanic
static_assert(!IsBuffGateMonster(6325, 37, 1, 100, 100, 100)); // native F0 exclusion
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
    uint32_t    monsterId;
    uint32_t    kind;
    uint32_t    level;
    float       distance;
    float       x, y, z;
    int64_t     hp;
    uintptr_t   addr;  // CCreature* at snapshot time; recheck state/hp before use
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
inline constexpr uintptr_t kUserSkillModeOffset       = 0x370C;

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

// 分解宝石 (CG_SendDismantleGem, proto 411571) —— 自动分解模块用。
// wireBagId = BagItemInfo::bagId (item+0x00,已是 wire 格式 slotIndex+13,不要再 +13)。
// count = 本次分解数量,调用方自行 clamp。复用 NetSendTriple(411571, wireBagId, count):
// 游戏原生分解 (idb sub_71D300) 发的就是 [16][411571][(slot&0x7FFF)+13][count],字节完全一致。
// SEH-wrapped。
bool SendDecomposeGem(uint32_t wireBagId, uint32_t count);

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

// Reads HP without conflating a valid zero (dead) with an unavailable local
// player during login, map transitions, or client teardown.
bool TryGetLocalHp(int64_t &hp);
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

// Returns true iff HP was read successfully and is exactly zero.
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
//   state[263] (+0x41C /1052)  in mode==2: head of multi-choice linked list.
//                              Each node:
//                                +0x04 (char[]) Big5 option label
//                                +0x188 (392) next ptr (0 = end)
//                                +0x18C (396) target script tag
//   state[264] (+0x420 /1056)  confirm-wrapper list used while building mode 1.
//
// Walk order — UI labels for selectable rows in mode==2 are written into
// "select%02d" UI controls starting at i=0; we send back option_index =
// position in the walked list (matches the engine's own send path in
// OnNpcDialogOption_Quest).
inline constexpr uintptr_t kDialogStateModeOffset           = 0x3F4;
inline constexpr uintptr_t kDialogStateMonsterTblIdOffset   = 0x008;
inline constexpr uintptr_t kDialogStateNpcInteractIdOffset  = 0x3F8;
inline constexpr uintptr_t kDialogStateSelectedScriptOffset = 0x410;
inline constexpr uintptr_t kDialogStateConfirmEntryOffset   = 0x414;
inline constexpr uintptr_t kDialogStateChoiceHeadOffset     = 0x41C;
inline constexpr uintptr_t kDialogStateWrapperHeadOffset    = 0x420;
inline constexpr uintptr_t kDialogOptionTextOffset          = 0x004; // Big5
inline constexpr uintptr_t kDialogOptionNextOffset          = 0x188; // 392
inline constexpr uintptr_t kDialogOptionTagOffset           = 0x18C; // scriptEntry*
// Native CG_NPC_DIALOG_SELECT metadata:
// option+396 -> scriptEntry, scriptEntry+12 -> scriptCtx.
inline constexpr uintptr_t kDialogScriptEntryCtxOffset      = 0x00C;
inline constexpr uintptr_t kDialogScriptCtxOptOffset        = 0x14C; // scriptCtx+332
inline constexpr uintptr_t kDialogScriptCtxWarpKeyOffset    = 0x188; // scriptCtx+392
inline constexpr int       kDialogMaxOptions                = 64;
inline constexpr size_t    kDialogOptionTextMaxLen          = 256;
inline constexpr size_t    kDialogBodyTextMaxLen            = 1024;

enum class DialogMode : uint32_t
{
    Closed   = 0,
    Confirm  = 1, // body text only; option list empty; "OK" advances
    Choice   = 2, // body + options; UI must render and let user pick
};

struct DialogOption
{
    uint32_t    index;       // 0-based visible option index
    std::string text;        // UTF-8, converted from Big5
    uint32_t    tag;         // option+396, engine-internal scriptEntry*
    uint32_t    opt;         // scriptCtx+332; 0 means no server opt on this step
    uint32_t    warpKey;     // scriptCtx+392
    uint32_t    warpTableId; // table 11 element+8; Another entry is 564
};

struct DialogAdvanceResult
{
    bool        ok;
    bool        retryable;
    bool        found;
    bool        advanced;
    uint32_t    optionIndex;
    uint32_t    warpKey;
    uint32_t    warpTableId;
    std::string error;
};

struct NpcWheelDialogResult
{
    bool        ok;
    bool        retryable;
    bool        callbackInvoked;
    bool        alreadyOpen;
    uint32_t    foregroundUiId;
    uint32_t    npcId;
    std::string error;
};

struct NativeDropPickupResult
{
    bool        ok = false;
    bool        alreadyGone = false;
    bool        actionQueued = false;
    bool        executionAmbiguous = false;
    uint32_t    dropId = 0;
    uint32_t    itemId = 0;
    float       distance = 0.0f;
    std::string error;
};

struct DialogSnapshot
{
    bool                       open;             // foreground UI 10 and mode != 0
    DialogMode                 mode;
    uint32_t                   npcInteractId;    // state+1016
    uint32_t                   monsterTblId;     // state+8
    std::string                body;             // UTF-8 (empty for Choice mode in
                                                 //        this engine — option labels
                                                 //        carry the prompt)
    std::vector<DialogOption>  options;          // [] in Closed mode
};

// Reads g_NpcDialogState snapshot. SEH-wrapped; returns {open=false} unless the
// ordinary NPC dialog is the foreground UI or when any state read fails.
DialogSnapshot GetDialogSnapshot();

// Installs the two native UI hooks used by broker-driven NPC interaction:
// GameLoop is the game thread rendezvous for the radial interaction wheel, and
// CUIManager__UpdateAutoQuestDialog is intercepted for exactly one requested
// Next/choice click so the surrounding GameLoop keeps its normal validation and
// CG_NPC_DIALOG_SELECT path. Call after PatternResolver stage 2 and before the
// RemoteControl pipe starts accepting commands.
bool InstallNativeUiBridge();
void StopNativeUiBridge();
bool UninstallNativeUiBridge();
void RequestNativeUiBridgeStopNoWait();
bool IsNativeUiBridgeReady();
bool IsNativeDropPickupReady();

// Mirrors the real world-drop click on GameLoop: select g_TargetDropItemId,
// retain intent=4, then queue SetAfterAction(x,y,4,0). The engine itself walks
// and sends the pickup packet at range. Callers must confirm exact dropId
// disappearance; actionQueued only reports SetAfterAction's immediate result.
NativeDropPickupResult SelectDropForPickupOnGameThread(
    uint32_t dropId, uint32_t expectedItemId, uint32_t expectedMapId,
    float maxDistance);

// The special five-button interaction wheel used by 越過次元的修道士 is UI 88.
// This invokes its real `exchange_ok` (對話) callback once on GameLoop after
// revalidating the dynamic creature id and wheel target. The callback itself
// transitions to ordinary NPC dialog UI 10.
NpcWheelDialogResult OpenNpcDialogFromInteractionWheel(uint32_t npcId);

// Sends `dialogOption` through the same per-mode callback used by the real UI.
// Mode 1 calls Npc__ConfirmDialogPage; mode 2 calls Npc__ChooseDialogOption.
// The callback is injected from CUIManager__UpdateAutoQuestDialog on GameLoop,
// so its actual return value and pending script pointer continue through the
// game's own validation/send/cleanup code. No direct 411026 is emitted here.
bool SelectDialogOption(uint32_t dialogOption);

// Advances exactly one local dialog step toward targetOpt. Confirm mode uses
// the engine's current state+0x414 entry; Choice mode recursively selects the
// unique visible branch whose script subtree contains targetOpt. It never
// sends a non-target server opt. When targetOpt is visible it returns found
// without clicking it.
DialogAdvanceResult AdvanceDialogTowardOpt(uint32_t npcId, uint32_t targetOpt,
                                           bool allowAdvance = true);

// Final critical transaction used by dungeonEntry. Revalidates npc/opt/warp at
// the target choice, preserves the engine's retained pending script while it
// drains confirm-only child pages, and succeeds only after the surrounding
// GameLoop has emitted the exact npc/opt/sub packet. It never chooses a child
// branch or permits a different positive opt.
bool SelectDialogOptionChecked(uint32_t npcId, uint32_t targetOpt,
                               uint32_t dialogOption, uint32_t expectedWarpTableId);

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

// ---------- Daily rewards: 在线奖励(Access) / 签到奖励(Attendance) ----------
//
// IDA 实证 2026-06-16 (unpackd_so3d.exe):
//   两个奖励弹窗都是预建 UIContent。在线=content 62、签到=content 61(GetUIContent(mgr,id) 取)。
//   开窗走游戏自身 __stdcall 开窗 call(先建本地 UI 状态再发 CG 开窗包,faithful 到点按钮,
//   不卡角色移动):RewardAccessOpen 发 412562、RewardAttendanceOpen 发 412560。
//   服务器收到开窗包后回流奖励列表,填充弹窗的两段 entry 表:
//     page0 std::vector<Entry*> begin@dlg+0x44 end@dlg+0x48
//     page1 std::vector<Entry*> begin@dlg+0x50 end@dlg+0x54
//   Entry(0x1C): +0x00 按钮控件 +0x04 物品槽控件 +0x08 status(0 未解锁/1 可领/2 已领)
//                +0x0C accessType(page) +0x10 id(index)
//   领取直发(跳过 OnClick 的确认弹窗 3002/3003):
//     在线 RewardAccessEntry_SendClaim(entry) __thiscall,发 412563 body{accessType,id}(8B);
//           每个 status==1 的 entry 都要单独领一次(按时长分档,逐档可领)。
//     签到 RewardAttendance_SendClaim() 无参,发 412561,一次领掉今天那档。
//   关窗:UIContentMarkClose(dlg) + CUIManager::CloseActiveContent(mgr, renderer)(复用仓库关窗范式)。
//   注:在线奖励按累计在线时长逐档解锁(status 在时长不够时一直是 0),单次登录+短等待只能领到
//       已解锁档;签到当天即可领。所以 broker 侧需在会话内周期重试。
inline constexpr int kRewardAccessContentId     = 62; // 在线奖励 UIContent id
inline constexpr int kRewardAttendanceContentId = 61; // 签到奖励 UIContent id
inline constexpr uintptr_t kRewardEntryStatusOffset = 0x08; // entry+0x08: 0 锁/1 可领/2 已领
inline constexpr uintptr_t kRewardDlgPage0BeginOffset = 0x44; // dlg+0x44: page0 vec begin
inline constexpr uintptr_t kRewardDlgPage0EndOffset   = 0x48; // dlg+0x48: page0 vec end
inline constexpr uintptr_t kRewardDlgPage1BeginOffset = 0x50; // dlg+0x50: page1 vec begin
inline constexpr uintptr_t kRewardDlgPage1EndOffset   = 0x54; // dlg+0x54: page1 vec end
// dlg+0x38(byte): 该奖励是否有可领档(任一 entry status==1)。服务器登录即主动推送完整列表包
// (在线 0x7CE76 / 签到 0x7CE74)会填条目并算出此位 + 驱动 HUD 图标闪烁(闹钟/红心),领取/解锁后
// 的更新包也实时刷新它。+0x39(byte)是 open-flag(图标点击置位,开窗后清),登录时为 0 故只点灯
// 不开窗 —— 所以此位登录即有效、无需开窗即可读。详见 IDA Recalc*HasClaimable* / Receive_FullList*。
inline constexpr uintptr_t kRewardDlgHasClaimableOffset = 0x38;
inline constexpr int kProtocolRewardAccessOpen      = 412562;
inline constexpr int kProtocolRewardAttendanceOpen  = 412560;
inline constexpr int kProtocolRewardAccessClaim     = 412563;
inline constexpr int kProtocolRewardAttendanceClaim = 412561;

// 一次领取的统计结果。claimable=本次发现的 status==1(已对其下发领取);claimed=本次发现的
// 已领(status==2);locked=未解锁(status==0)。day-done 判定靠 broker:claimable==0 &&
// locked==0 && signin 已领。ok 表示至少成功打开并读到弹窗。
struct RewardClaimResult
{
    bool ok = false;
    int onlineClaimed = 0;
    int onlineClaimable = 0;
    int onlineLocked = 0;
    int signinClaimed = 0;
    int signinClaimable = 0;
    int signinLocked = 0;
};

// 打开在线/签到奖励弹窗,领取当前所有可领档位,再关窗。返回各档统计。
// 在命令线程(pipe)上同步执行:开窗后轮询等待服务器回流填充 entry 表再读。
// doOnline/doSignin 选择只领某一种还是都领(默认都领);未选的那种统计字段保持 0。
RewardClaimResult ClaimDailyRewards(bool doOnline = true, bool doSignin = true);

// 只读探测「现在是否有可领奖励」——不开窗、不发包,仅读 GetUIContent(62/61)+0x38 那个 HUD
// 图标闪烁位(见 kRewardDlgHasClaimableOffset)。content 对象登录后即由服务器推送填好,故可在
// 角色上线后随时轮询;只有 +0x38==1 时才值得真正 ClaimDailyRewards 开窗去领。
// resolved=false 表示 content 还没取到(UI 未就绪 / 刚上线),此时 claimable 不可信,应稍后重试。
struct RewardPeekResult
{
    bool ok = false;              // UI manager 取到 = true
    bool onlineResolved = false;  // content 62 取到
    bool signinResolved = false;  // content 61 取到
    bool onlineClaimable = false; // content62+0x38
    bool signinClaimable = false; // content61+0x38
};
RewardPeekResult PeekRewardClaimable();

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

// ---------- 技能宝石合成 (Compose / 三合一) ----------
//
// 合成界面 = unpackd_so3d Compose_* 系列(IDA 已命名)。三合一规则:3 个同等级宝石产
// 出 1 个高一级宝石(N→G→DG→XG→SG)。
//
// 发包:proto 411606 (0x647D6),20 字节帧 {u32 len=20, u32 411606, w0, w1, w2}。每个
//   wi = 背包 slotIndex(0..191) + 13(同丢弃/用品的 wire 槽编码)。三个槽可以是同一堆叠
//   (抓包实测 9F 9F 9F),也可以是 3 个不同 itemId 的同等级宝石(引擎 Compose_AutoFillByGrade
//   自身就跨 itemId 同级混合)。走 Net__SendPacket20B_3DW_NoTrack(0xB2C810),无 dialog
//   状态副作用 —— 不要用 NetSendDialogSelect(它有 client+133 dialog 闸 + 状态追踪)。
//
// 等级识别(引擎表驱动,不写死 itemId):
//   宝石判定: itemTable(=*(item+0x20)) 的 +0x110(subtype) ∈ {72,73,90} (Compose_IsSkillGem)。
//   等级键:   gradeKey = GetTableElem(55, *(itemTable+0x454))[+0x14];不可再合成(顶级)返回 4。
//   同 gradeKey 的宝石可互相凑 3 合 1(即使 itemId 不同)—— 引擎自身的分组依据。
inline constexpr int       kProtocolCompose      = 411606; // 0x647D6
inline constexpr uint32_t  kComposeWireSlotBase  = 13;     // wire = slotIndex + 13
inline constexpr uintptr_t kItemRecipeFieldOffset = 0x454; // itemTable+0x454: table55 索引
inline constexpr int       kSealTableTypeGemGrade = 55;    // GetTableElem(55, recipeId)
inline constexpr uintptr_t kGemGradeTableElemOffset = 0x14; // elem+0x14 = gradeKey
inline constexpr int       kComposeNoGrade       = 4;      // gradeKey==4: 顶级/不可合成

// 背包里一类(同 gradeKey)可合成宝石的聚合视图。slotIndices 是该组所有宝石所在的
// 背包 arrayIndex 列表(按出现顺序;同一可堆叠槽会按其 count 展开成多份,合成按"件"计)。
struct ComposeGemGroup
{
    uint32_t                gradeKey;     // GetTableElem(55,...) 出的等级键(同键可混合)
    uint32_t                sampleItemId; // 该组第一个宝石的 itemId(展示用)
    std::string             sampleName;   // 该组第一个宝石的 UTF-8 名(含 (N)/(G)... 标记)
    uint32_t                totalCount;   // 该组宝石总件数(跨槽求和)
    std::vector<uint32_t>   wireSlots;    // 每"件"一个 wire 槽(slotIndex+13);len==totalCount
};

// 扫整背包(192 格)聚合出所有"可合成"(gradeKey!=4)的技能宝石组。SEH 安全。每槽按
// count 展开(可堆叠宝石一槽多件)。totalCount<3 的组也返回(让上层显示),由调用方决定
// 是否够合。
std::vector<ComposeGemGroup> ListComposeGemGroups();

// 直接发一次三合一:body=[slotA+13, slotB+13, slotC+13]。三个 slot 是背包 arrayIndex
// (0..191);可以全相同(同堆叠)或不同(同等级混合)。只负责发包,不等回包、不校验
// 数量(由上层 broker 在两次合成之间重读背包确认)。资源未解析/SEH 返回 false。
bool ComposeSendRaw(uint32_t slotA, uint32_t slotB, uint32_t slotC, std::string *errOut = nullptr);

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

// ---------- 组队列表浏览 / 自动组队 (Party board, CMessenger op 0x44xx) ----------
//
// IDA 实证 2026-06-23 (unpackd_so3d.exe / shadowdance):点开「组队搜索」窗口会发两包,
// 服务端回流整页队伍列表,填进 CUIManager::GetUIContent(mgr, 17) 那个 party-board content。
//   开列表: CMessenger_SendOpenPartyList(g_pMessenger)  -> op 0x4400, wire 06 00 00 44 00 00
//   取某页: CMessenger_SendRequestPartyListPage(g_pMessenger, page) -> op 0x4402, +<page:u16>
//   开窗:  CUIManager::BeginContent(mgr, 17, renderer) —— 走本地 UI call(内部调
//          PartyWindow_Open 建本地 content 并发 0x4402(page1)),不裸发包,不卡移动。
//   加入:  CMessenger_SendJoinPartyById(partyId) -> op 0x4413, body {u32 2, u32 partyId}
//   关窗:  UIContentMarkClose(content) + CUIManager::CloseActiveContent(mgr, renderer)
//
// content(=GetUIContent(17))行布局,每页 8 行,stride 0x40,行 i 基址 = content+0x38+i*0x40:
//   +0x38(56) u32 partyId  +0x3C(60) u16 distribution  +0x3E(62) u16 gender
//   +0x40(64) u16 curMembers  +0x42(66) u16 maxMembers  +0x44(68) u16 mapId
//   +0x46(70) char partyName[33] (Big5)  +0x67(103) char leaderName[17] (Big5, 队长/创建人)
// content 翻页字段: +0x346(838) u16 curPage, +0x348(840) u16 maxPage。partyId==0 = 空行。
// distribution 取值待实测(疑似 0/1/2 = 自由/平均/随机),先原样透出由 web 解释。
inline constexpr int       kPartyBoardContentEnum   = 17;
inline constexpr size_t    kPartyBoardRowsPerPage   = 8;
inline constexpr uintptr_t kPartyBoardRowBase       = 0x38; // content+56
inline constexpr uintptr_t kPartyBoardRowStride     = 0x40; // 64
inline constexpr uintptr_t kPartyBoardOffPartyId    = 0x00; // +56
inline constexpr uintptr_t kPartyBoardOffDistrib    = 0x04; // +60 u16
inline constexpr uintptr_t kPartyBoardOffCurMembers = 0x08; // +64 u16
inline constexpr uintptr_t kPartyBoardOffMaxMembers = 0x0A; // +66 u16
inline constexpr uintptr_t kPartyBoardOffMapId      = 0x0C; // +68 u16
inline constexpr uintptr_t kPartyBoardOffName       = 0x0E; // +70 char[33]
inline constexpr size_t    kPartyBoardNameLen       = 33;
inline constexpr uintptr_t kPartyBoardOffLeader     = 0x2F; // +103 char[17]
inline constexpr size_t    kPartyBoardLeaderLen     = 17;
inline constexpr uintptr_t kPartyBoardOffCurPage    = 0x346; // content+838 u16
inline constexpr uintptr_t kPartyBoardOffMaxPage    = 0x348; // content+840 u16

struct PartyBoardEntry
{
    uint32_t    partyId      = 0;
    uint16_t    distribution = 0; // 分配规则(原始值,web 翻译)
    uint16_t    curMembers   = 0;
    uint16_t    maxMembers   = 0;
    uint16_t    mapId        = 0;
    std::string partyName;        // UTF-8
    std::string leaderName;       // UTF-8 (创建人/队长)
};

struct PartyBoardSnapshot
{
    bool                         ok      = false; // content 取到且已回流
    int                          curPage = 0;
    int                          maxPage = 0;
    std::vector<PartyBoardEntry> entries;
};

// 打开组队列表(BeginContent 17 本地开窗 + 发 0x4400/0x4402),轮询等服务端回流,把
// 所有页抓全合并返回,然后关窗。命令线程同步执行(内部有 ~秒级轮询)。失败 ok=false。
PartyBoardSnapshot FetchPartyBoard();

// 直接按 partyId 发加入包(op 0x4413)。只发包不开窗。
bool SendJoinPartyById(uint32_t partyId);

// 抓列表 -> 找 leaderName(创建人,UTF-8,精确匹配)对应队伍 -> 发加入包。命中并发包
// 返回 true,outJoinedId 填命中的 partyId;未命中 false + outErr 说明。
bool JoinPartyByLeaderName(const std::string &leaderNameUtf8, uint32_t *outJoinedId,
                           std::string *outErr);

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
// stunTime, animation lock, skill mode, …). 普攻移动 bypasses the non-skill
// path (370C==0 @ 0x753A80) and 技能移动 the skill path (370C∈{3,5,8} @
// 0x753A54) — with both ON, MoveTo works in any action state. Without them,
// idle/walking states still succeed; mid-attack/cast may reject. UI tells the
// user; this layer doesn't auto-enable the patch.
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

// ---------- Teleport (instant warp to a ground point) ----------
//
// SetAfterAction's success path (0x753CC4) writes the move target into these
// four fields and arms the per-frame mover with the pending flag:
//   +0x32F0 targetX (int)   +0x32F4 targetY (int)
//   +0x32FC action          +0x32F8 targetId
//   +0x2BC8 pending = 1  -> TraceMove (0x756F10) interpolates visible pos toward target
// Teleport short-circuits the interpolation: it writes the VISIBLE position
// (+0x3C worldX / +0x44 worldY) straight to the destination, mirrors the target
// fields to the same point, clears the pending flag so TraceMove treats the move
// as already complete, then fires SendPlayerMoveSyncPacket(411000) to push the
// absolute coords to the server. Movement is ground-plane only — Z (+0x40) is
// left for the engine to re-derive from terrain on the next frame.
inline constexpr uintptr_t kAfterActionTargetXOffset = 0x32F0; // int
inline constexpr uintptr_t kAfterActionTargetYOffset = 0x32F4; // int
inline constexpr uintptr_t kAfterActionPendingOffset = 0x2BC8; // byte (1 = move queued)

// Instantly relocates the local player to (worldX, worldY) and syncs the new
// position to the server. Returns true if the position was written (the sync
// packet is best-effort — the server still validates the jump distance, so an
// over-long teleport may be rejected/rubber-banded server-side). SEH-wrapped.
// MUST be called on the game's main / D3D9-present thread.
bool Teleport(float worldX, float worldY);


// ---------- Walk-and-talk-to-NPC ----------
//
// "Click a creature and let the game decide what to do" — mirrors
// WorldClick__DispatchTargetOrGround @ 0x9941A0: write g_TargetCreatureId and
// queue action=5 for a talkable NPC or action=3 for a combat creature. The game
// loop opens the NPC dialog or starts attacking after interaction distance is
// reached. Never call lower-level Npc__OpenDialogByCreatureRef from a
// broker/worker thread.
//
// MoveTo() vs TalkOrAttack()
// - MoveTo: pure "go to (x,y)", no target reference
// - TalkOrAttack: takes a creatureId; the game loop dispatches dialog vs combat.
//
// Returns true if the engine accepted the request (the appropriate path was
// dispatched). SEH-wrapped to survive torn g_pLocalUser/creature pointer.
//
// MUST be called on the game's main / D3D9-present thread, like MoveTo.
bool TalkOrAttack(uint32_t creatureId);

// Broker-safe wrapper: schedules TalkOrAttack on the real GameLoop thread and
// waits for that frame to finish. Use this from RemoteControl handlers.
bool TalkOrAttackOnGameThread(uint32_t creatureId, std::string *error = nullptr);

struct MonsterTargetSelectionResult
{
    uint32_t creatureId = 0;
    uint32_t creatureType = 0;
    int64_t hp = 0;
    bool targetSelected = false;
    bool attackQueued = false;
};

// Monster-only combat path used by workflow combat nodes. It selects the
// creature and queues action=3 without routing through NPC interaction logic.
bool SelectMonsterForAttack(uint32_t creatureId, MonsterTargetSelectionResult &result,
                            std::string *error = nullptr);

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

// Snapshot wrapper around the engine's CreatureMgr list — every alive entry
// with a creature id (NPCs, monsters, trees, teleports). `isNpc` drives the
// talk-vs-attack affordance; legacy `attackable` remains the existing attack
// whitelist verdict. `buffGateMonster` is the separate conservative ordinary-
// enemy verdict used only by BuffKeeper. Sorted by distance.
struct NearbyNpc
{
    uint32_t    creatureId;
    uint32_t    kind;         // raw AI/behavior kind @ statTable+0xDC
    uint32_t    monsterTblId; // legacy statTable+0xEC data-key, for UI/debug only
    bool        isNpc;        // type==3 (Talkable) — talk affordance (对话 vs 攻击)
    bool        attackable;   // legacy IsMonsterType(type), kept for existing attack consumers
    bool        buffGateMonster; // conservative ordinary enemy for BuffKeeper only
    uint32_t    level;
    float       distance;
    float       x, y, z;
    int64_t     hp;
    int64_t     maxHp;        // full/template HP from statTable+0xA8; 0 if unread (many NPCs).
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

// ---------- Grid A* path walker (自写网格 A*, DLL worker 线程) ----------
//
// 背景:引擎原生点击移动(OnPlayerMoveClick @ 0x871D10 -> SetAfterAction @
// 0x7539E0 -> TraceMove @ 0x756F10)只存「单个」目标 +0x32F0/+0x32F4,pending
// +0x2BC8=1。TraceMove 每 tile 用 CalcDirToAfterActionTarget(@0x748E70,纯直线
// 算 8 方向,无绕障)朝那个固定目标推进一格,撞到第一个阻挡 tile 就清 pending 停
// 下。所以一次喂一个远目标,人常常走两步卡墙不动 —— 这正是「自动寻路只 call 一次
// 远坐标就没用」的根因。
//
// PathWalker 在 DLL 内起一条 worker 线程,自己在碰撞表(Map__IsBlocked @ 0xA97080,
// 经 TileBlocked 读)上跑网格 A* 算出绕障路径,不依赖引擎 navmesh:
//   1) PathPlanGrid:8 邻域 A*,octile 启发,拒绝对角穿墙角。先在起点/终点外扩
//      48 格的 bbox 内搜索；找不到时自动扩到 96、192，最后在 maxCells 允许时
//      搜全图。流程只给最终目标，不需要为迷宫手填途经点。
//   2) PathSmooth:对 A* 折线做 string-pull(用 RaycastFurthestWalkable 直线可达性
//      合并),并把每段限制在 24 tile 内；这些中间点由算法生成,流程不用手填。
//   3) 逐 waypoint MoveTo + 轮询。SetAfterAction 返回 0 只按「动作闸暂忙」退避
//      重试,不触发 A*；只有命令已受理但持续没有位移才算路线段失败。
//   4) 真路线段失败时惩罚本次失败 tile 并在预算允许时搜全图,自动选择另一条路；
//      30 秒内任意 tile 位移都会刷新 watchdog,不会再靠累计 replan 次数误判失败。
//
// 这是真的绕障 A*,凸障碍、U 型凹墙和需要大回环的迷宫都能绕；只有全图碰撞网格
// 确认真不可达,或整图超过安全预算时才放弃。
//
// 线程:worker 调 MoveTo/GetLocalPosition/读碰撞表,全部 SEH 包过,和现有 moveTo 命令
// 一样跑在非主线程,issue 速率很温和(每段秒级)。StartPathTo 会先停掉上一条路径再起
// 新的。DLL 卸载路径必须调 PathWalkerShutdown() 把 worker join 掉,否则卸载时
// worker 还在跑引擎代码会崩。
enum class PathState
{
    Idle,
    Walking,
    Arrived,
    Failed
};

struct PathProgress
{
    PathState   state;
    float       targetX, targetY; // 最终目标(固定)
    float       curX, curY;       // 最近一次读到的玩家坐标
    int         segments;         // 已发出的中途段数
    std::string error;            // Failed 时的原因(UTF-8)
};

// 启动寻路走到 (worldX, worldY)。action/targetId 只在「最后一段」生效(action=3 +
// targetId 用于走到后接战;中途段一律 action=1 纯走)。会先取消上一条路径。返回
// false 仅当 g_pLocalUser 未就绪 / 起点 tile 不可走(切图未就绪)。
bool StartPathTo(float worldX, float worldY, int action = 1, uint32_t targetId = 0);

// 取消当前寻路(置 stop 并 join worker)。幂等;无路径在跑时是空操作。
void StopPath();

// 读当前寻路进度快照(mutex 保护)。
PathProgress GetPathProgress();

// DLL 退出路径调用:停 worker 并 join。幂等。
void PathWalkerShutdown();

// 读 g_TargetCreatureId — 当前选中的怪物 / NPC ID。0 表示未选,-1 在引擎里也
// 偶尔被写入表示 "无目标",用 0 等价处理。
uint32_t GetTargetCreatureId();

// 通过 EntityManager::FindCreatureById 反查 CCreature*。返回 0 表示找不到。
// 成功的话填入怪物坐标 (x, y, z) 和其它常用字段。SEH-wrapped。
struct CreatureSnapshot
{
    uintptr_t addr;
    uint32_t  id;
    uint32_t  kind;       // raw statTable+0xDC type; 3=NPC, kMonsterTypes=monster
    int64_t   hp;
    float     x, y, z;
};
bool LookupCreatureById(uint32_t creatureId, CreatureSnapshot &out);

} // namespace GGTB
