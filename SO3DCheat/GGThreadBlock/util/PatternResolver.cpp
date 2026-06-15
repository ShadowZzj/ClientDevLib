#include "PatternResolver.h"
#include <Windows/util/Process/ProcessHelper.h>
#include <General/util/Crypto/Md5.h>
#include <json.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace GGTB
{

std::string PatternResolver::cachePath_;
std::string PatternResolver::targetModule_;
std::string PatternResolver::moduleMd5_;
uintptr_t PatternResolver::moduleBase_ = 0;
uintptr_t PatternResolver::moduleSize_ = 0;
std::vector<PatternEntry> PatternResolver::entries_;
std::unordered_map<std::string, size_t> PatternResolver::nameIndex_;
bool PatternResolver::initialized_ = false;

void PatternResolver::Init(HMODULE hOwnerDll, const std::string &targetModule)
{
    if (initialized_)
        return;

    char dllPath[MAX_PATH]{};
    GetModuleFileNameA(hOwnerDll, dllPath, MAX_PATH);
    cachePath_ = std::filesystem::path(dllPath).parent_path().string() +
                 "\\ggtb_pattern_cache.json";

    targetModule_ = targetModule;
    HMODULE hTarget = targetModule_.empty()
                          ? GetModuleHandleA(nullptr)
                          : GetModuleHandleA(targetModule_.c_str());
    if (!hTarget)
    {
        spdlog::error("GGTB::PatternResolver: target module '{}' not found",
                      targetModule_.empty() ? "<host exe>" : targetModule_);
        return;
    }
    moduleBase_ = reinterpret_cast<uintptr_t>(hTarget);

    zzj::Process process;
    zzj::Memory memory(process);
    char modName[MAX_PATH]{};
    GetModuleFileNameA(hTarget, modName, MAX_PATH);
    auto baseName = std::filesystem::path(modName).filename().string();
    auto modInfo  = memory.GetModuleInfo(baseName);
    moduleSize_   = modInfo ? modInfo->modBaseSize : 0;

    zzj::MD5::GetFileMD5(modName, moduleMd5_);
    spdlog::info("GGTB::PatternResolver: target={}, MD5={}, base={:x}, size={:x}",
                 baseName, moduleMd5_, moduleBase_, moduleSize_);

    RegisterAll();

    if (entries_.empty())
    {
        spdlog::info("GGTB::PatternResolver: no patterns registered (skeleton mode)");
        initialized_ = true;
        return;
    }

    if (!LoadCache())
    {
        spdlog::info("GGTB::PatternResolver: cache miss, scanning...");
        ScanAll();
        SaveCache();
    }

    for (auto &e : entries_)
    {
        if (!e.resolved)
            e.resolved = moduleBase_ + e.fallbackRVA;
        spdlog::info("  {} = {:x}{}", e.name, e.resolved,
                     (e.resolved == moduleBase_ + e.fallbackRVA) ? " (fallback)" : "");
    }

    initialized_ = true;
}

uintptr_t PatternResolver::Get(const std::string &name)
{
    auto it = nameIndex_.find(name);
    if (it == nameIndex_.end())
    {
        spdlog::error("GGTB::PatternResolver: unknown entry '{}'", name);
        return 0;
    }
    return entries_[it->second].resolved;
}

uintptr_t PatternResolver::GetModuleBase() { return moduleBase_; }
uintptr_t PatternResolver::GetModuleSize() { return moduleSize_; }

void PatternResolver::Register(const std::string &name, const std::string &pattern,
                               uintptr_t fallbackRVA, int matchIndex, int offset,
                               bool derefOperand, bool resolveRelCall)
{
    nameIndex_[name] = entries_.size();
    entries_.push_back({name, pattern, fallbackRVA, matchIndex, offset,
                        derefOperand, resolveRelCall, 0});
}

void PatternResolver::RegisterAll()
{
    // Target: SO3DPlus.exe (IDA dump: unpackd_so3d.exe), imagebase 0x400000.
    // Pattern strings left empty — fallbackRVAs are authoritative for this build.
    Register("LocalUserPtr",    "", 0xA0C0CC); // *(CLocalUser**)
    // CLocalUser::CalcStatus — every per-job branch writes [this+0x1A4] = baseSpeed.
    // NOP all 8 sites (each is `movss [reg+1A4h], xmm0` = 8 bytes) so the field
    // can be poked freely.
    Register("MoveSpeedWrite1", "", 0x345DD2);
    Register("MoveSpeedWrite2", "", 0x345E0E);
    Register("MoveSpeedWrite3", "", 0x345E3B);
    Register("MoveSpeedWrite4", "", 0x345F14);
    Register("MoveSpeedWrite5", "", 0x345F41);
    Register("MoveSpeedWrite6", "", 0x345F8C);
    Register("MoveSpeedWrite7", "", 0x345FBA);
    Register("MoveSpeedWrite8", "", 0x346229);

    // CLocalUser::TraceMove —— 每帧上限钳位（如果只 NOP CalcStatus，TraceMove 仍会
    // 把 moveSpeed 拉回 7.0/10.0）。两站共享同一段 IR：
    //   mov eax,[ebp+var_10] ; movss xmm0,[g_kMoveSpeedCap*]
    //   movss [eax+1A4], xmm0 ; mov ecx,[ebp+var_10]
    // 22 字节 pattern 在 .text 里 exactly 2 处命中 (0x3576FC, 0x357738)，+11 落到
    // 写入 movss 上 (0x357707 mounted -> 10.0, 0x357743 normal -> 7.0)。
    // fallbackRVA 已经填到 movss 本身，scan 失败也能直接 NOP 8 字节。
    Register("MoveSpeedClampMounted",
             "8B 45 F0 F3 0F 10 05 ?? ?? ?? ?? F3 0F 11 80 A4 01 00 00 8B 4D F0",
             0x357707, 0, 11);
    Register("MoveSpeedClampNormal",
             "8B 45 F0 F3 0F 10 05 ?? ?? ?? ?? F3 0F 11 80 A4 01 00 00 8B 4D F0",
             0x357743, 1, 11);

    // Mount/state refresh writes. These are outside CalcStatus/TraceMove and
    // can pull +0x1A4 back down when mounting or when vehicle state is synced:
    //   sub_A03F60: 0xA03F8E / 0xA03FB6 / 0xA03FEA
    //   sub_A04050: 0xA04CB7
    //   sub_A074E0: 0xA075FF
    Register("MoveSpeedMountStateBoost",   "", 0x603F8E);
    Register("MoveSpeedMountStateMax",     "", 0x603FB6);
    Register("MoveSpeedMountStateDefault", "", 0x603FEA);
    Register("MoveSpeedMountStatusSync",   "", 0x604CB7);
    Register("MoveSpeedMountPathRefresh",  "", 0x6075FF);

    // CLocalUser::CalcStatus per-frame writes — single NOP each.
    //   0x745C18: F3 0F 11 88 1C 2E 00 00  movss [eax+2E1Ch], xmm1   (attackSpeed)
    //   0x745CCF: F3 0F 11 88 20 2E 00 00  movss [eax+2E20h], xmm1   (skillSpeed)
    // Both are "1.0 - level/divisor" animation divisors (lower = faster).
    Register("AttackSpeedWrite", "", 0x345C18);
    Register("SkillSpeedWrite",  "", 0x345CCF);

    // Skill__GetPreTime（IDA: sub_9394F0）—— 把 SkillTable+0x174 的 preTime 字段
    // 通过 FPU 返回。两个分支：
    //   level==0：movss xmm0, [edx+0x174]   F3 0F 10 82 74 01 00 00  @ 0x53954A
    //   level>0 ：movss xmm0, [ecx+0x174]   F3 0F 10 81 74 01 00 00  @ 0x53956B
    // 两站都打成 `movss xmm0, [&s_preTimeConst]`（ModR/M 0x05 = mod=00, reg=000=xmm0,
    // r/m=101=disp32），同样 8 字节，原地替换。和 SkillSpeed 联动可让起手 + 动画都瞬完。
    // 注意：GGTB 之前的 CLocalPlayer.h 注释说 preTime 不再被读 —— 那是错的，仍然在用。
    Register("PreTimeRead1",     "", 0x53954A);  // level==0 branch (edx)
    Register("PreTimeRead2",     "", 0x53956B);  // level>0  branch (ecx)

    // Entity iteration:
    //   *(EntityManager**)(0xE0C0C8) -> mgr
    //   mgr+0x0C = singly-linked list head (around-player AOI). Each node has
    //              next-pointer at +0x2E30 and a kind field at +0x1AE8 (==5 for
    //              real player; other values mark CLocalUser/NPCs/monsters).
    //   mgr+0x08 also exposes a std::map<int, CUser*> view (used by
    //              EntityManager__FindUserById), but we don't walk it — the
    //              rb-tree races with insert/delete during scene transitions.
    // Same architecture as so3dFullCheat (old: head=+0x10, next=+0x236C, name=+0x1410);
    // offsets shifted ~+0xAC0 in this build, in line with attackSpeed (0x235C→0x2E1C).
    Register("EntityManagerPtr", "", 0xA0C0C8); // *(EntityManager**)

    // Attack range (CUser+0x35AC, int "attackRangeTiles"). CalcStatus rewrites it
    // every frame (baseline=1 -> max-clamp weapon -> add buffs), and Combat__GetAttackRangeSquared
    // squares it with a hard cap of 225 (=15^2). Same MoveSpeed pattern: NOP the
    // 3 writes in CalcStatus (different sizes per site), then poke the field.
    // Plus raise the squared-range cap to lift the 15-tile limit.
    Register("AttackRangeWrite1",  "", 0x34203F); // 10B: mov dword [eax+35ACh], 1     (baseline)
    Register("AttackRangeWrite2",  "", 0x343C94); //  6B: mov [ecx+35ACh], eax         (weapon clamp)
    Register("AttackRangeWrite3",  "", 0x343DB1); //  6B: mov [edx+35ACh], eax         (buff accum)
    // imm32 inside  cmp [ebp-4], 0E1h  -> raise to lift the 15^2 cap. Both sites must
    // be patched; they're the two branches inside Combat__GetAttackRangeSquared.
    Register("AttackRangeCapImm1", "", 0x3493C4); // 4B imm32 inside cmp at 0x7493C1
    Register("AttackRangeCapImm2", "", 0x34940F); // 4B imm32 inside cmp at 0x74940C

    // SkillTable__GetSkillRange (linear 0x939890). Returns SkillTable+0x170 —
    // the per-skill cast distance (tiles). 13 callers total: 9 in
    // Net__SendSkillPacket's distance gates, 2 in SkillTable__GetSkillRangeSq
    // (OutputDebugString "Skill GetRangeSq : %d"), 1 in sub_A89AD0 (walk
    // animation normaliser). Patching the prologue to `mov eax, imm32; ret`
    // (6B) covers all callers with a single write; slider updates just
    // rewrite imm32 at +1.
    Register("SkillRangeGetterEntry", "", 0x539890);

    // Item short-cd: rewrite the per-use cd setter inside StatTable__SetItemCdRemaining
    // (linear 0x79AACE). InventoryItem__Use's cd gate calls ItemTblEntry__GetCooldownRatio,
    // whose path-3 dividend is *itemEntry+0x28 — and itemEntry+0x28 IS the same field as
    // statTable + i*0x118 + 0xEE0 (stat entry's internal base is 0xEB8: +4=category(0xEBC),
    // +0x28=remainingCd(0xEE0)). The earlier patch at 0x756E98 only short-circuited the
    // SkillCdMgr per-category sub-check (path 2), which doesn't override path 3, so the
    // inventory cd toast stayed for the full original duration.
    //
    // Original 14 bytes at 0x79AACE:
    //   F3 0F 10 4D F4              movss xmm0, [ebp+var_C]        ; v4 = ApplyCdReductionBuff(GetCdDuration)
    //   F3 0F 11 84 01 E0 0E 00 00  movss [ecx+eax+0EE0h], xmm0    ; statEntry.remainingCd = v4
    // Replacement (14 bytes total):
    //   C7 84 01 E0 0E 00 00 <imm32_float>   mov dword [ecx+eax+0EE0h], imm32  (11B)
    //   90 90 90                              nop nop nop                       (3B padding)
    // Effect: cd MECHANISM stays intact (UI shows brief cd, server-side throttle still
    // applies) but the wait shrinks to the configured number of seconds (~0.3s default).
    Register("ItemCdSetter", "", 0x39AACE);

    // ---------- Auto-pickup ----------
    // SendPickItemPacket(int dropId) — engine wrapper, __stdcall, retn 4.
    // Linear 0x5FEC90 in unpackd_so3d.exe. Internally:
    //   v3 = LookupDropItemById(g_pItemContainer, dropId);
    //   if (!v3) return 0;
    //   if (!*(BYTE*)(v3 + 0x2E)) return 0;          // canPick gate
    //   v2 = sub_798DA0(*(DWORD*)(v3 + 4), 0,0,0);   // itemId validator
    //   if (v2 <= 0) return 0;
    //   Net__BeginSend();
    //   sub_B2C930(411011, dropId, v2, 0);           // packet 0x64583
    //   return 1;
    // Saved ECX at entry is dead — safe to call as plain stdcall regardless of ECX.
    Register("SendPickItemPacketFn", "", 0x1FEC90);
    // g_pItemContainer — *(CItemContainer**). Drop list head at container+0x6C
    // (DropItem next at +0x88, dropId@+0x0, itemId@+0x4, x/y/z@+0x14/+0x18/+0x1C,
    // canPick@+0x2E byte).
    Register("ItemContainerPtr",     "", 0xA0DDF0);
    // g_pCashItemContainer — same struct, different instance for cash bag
    Register("CashContainerPtr",    "", 0xA08160);

    // ---------- Active buffs (BuffHelper) ----------
    // BuffHelper_FindHostBuffContainer @ 0x6486C0 — __thiscall(this, hostType,
    // hostId): ecx = &g_BuffHelper, then 2 stack args (retn 8). Returns the
    // std::list container of active buff instances for that host. 0 => no buffs.
    // (Disasm @ 0x6485a9 proves ecx is the live BuffHelper `this`, NOT a dead
    // register — calling it as __stdcall makes the internal keyed lookup use a
    // garbage `this` and always return 0.)
    Register("BuffFindHostContainer", "", 0x2486C0);
    // &g_BuffHelper @ 0xE09178 — the BuffHelper singleton's address (used as
    // `this` for FindHostBuffContainer). `mov ecx, offset g_BuffHelper` at
    // 0x7C8396 confirms this is the address itself, not a pointer to deref.
    // RVA = 0xE09178 - 0x400000 = 0xA09178.
    Register("BuffHelperThis",        "", 0xA09178);
    // &g_pGameHUD @ 0xED40F0 — the in-game HUD object's address (used directly as
    // `this`, NOT a pointer to deref; `mov ecx, offset g_pGameHUD` at 0x5DAD07
    // confirms). It owns the cash-item icon row drawn by HUD_RenderCashItemBars
    // (sub_7C2230): a std::vector at this+0x25C0 of 280-byte records (rec+0 type,
    // rec+12 iconId, rec+16 Big5 name, rec+276 blink flag) holding timed cash/system
    // effect icons. RVA = 0xED40F0 - 0x400000 = 0xAD40F0.
    Register("GameHUDThis",           "", 0xAD40F0);
    // g_BuffDescTable @ 0x9E355C — base of the BuffDesc array; row = base+0xAF*id.
    // g_BuffDescCount @ 0x9E3560 — element-count ceiling (id must be < this).
    Register("BuffDescTable",         "", 0x9E355C); // *(BuffDesc**) base ptr
    Register("BuffDescCount",         "", 0x9E3560); // int count
    // g_SkillBuffMap @ 0x9C2640 — 116 entries of {int skillId, int buffTableId};
    // used to reverse-map a live buff back to the skill that applied it.
    Register("SkillBuffMap",          "", 0x9C2640);

    // ---------- Party / 组队 (classic CMessenger party roster) ----------
    // Reversed off CMessenger_RecvPartyMemberList (0x843DF0) and verified live
    // (shadowsing's party, 2026-06-08). RVA = linear - 0x400000. The self slot in
    // the roster/state table is blanked; GetPartyMembers back-fills it. See the
    // party block in entity/CLocalPlayer.h for the full layout doc.
    Register("PartyMemberCount",      "", 0xA2872C); // int, 0 = solo, max 6
    Register("PartyMemberArray",      "", 0xA28730); // 6 × 0x3B entries
    Register("PartySelfIndex",        "", 0xA2896C); // int, my slot (-1 unset)
    Register("PartySelfRole",         "", 0xA28894); // int, 0/1/2
    Register("PartyMemberStateArray", "", 0xA28898); // 6 × 7 dwords (HP@0,MaxHP@4)
    Register("PartyListValid",        "", 0xA28974); // int

    // Combat__TryUseSkill @ 0x601CA0 — high-level "cast skill" entry the engine's
    // own AutoHunt uses. Declared __fastcall(ctx, edx, skillId, targetId) but the
    // ctx/edx regs are never read; all state comes from g_pLocalUser/g_pSkillManager
    // globals. Runs the full state machine (UseSkill -> SendSkillCast -> CG_SKILL_24
    // + CG_SKILL_22). For self-buff skills pass targetId = own userId (or 0).
    Register("CombatTryUseSkill",      "", 0x201CA0);

    // ---------- Action-time movement bypass ----------
    // CLocalUser::SetAfterAction (0x7539E0) queues the "move-after-action" target
    // (writes [this+32F0/F4/F8/FC] + sets [this+2BC8]=1). It runs through 7 reject
    // gates first: external validator (sub_7494A0), global lock (dword_ED5BD0),
    // skill mode (+0x370C in {3,5,8}), stunTime (+0x2E24>0), m_bCanMove (+0x304),
    // buffer flag (+0x4B24 query 0x0D/0x0F), animation/skill ID whitelist (+0x194).
    // CLocalUser::TraceMove (0x756F10) is the per-frame consumer of that queue and
    // re-applies most of the same gates. Two patches inside SetAfterAction +
    // TraceMove are enough to let movement proceed at any time.
    //
    //   0x7539FE: jle short loc_753A22 (2B `7E 22`) — first reject gate. Overwrite
    //             5 bytes with `E9 C1 02 00 00` = jmp loc_753CC4 (success path).
    //             Skips ALL SetAfterAction rejects in one shot.
    //   0x756331: jbe loc_7563F7 (6B `0F 86 C0 00 00 00`) — Skill__GetPreTime
    //             gate in SendSkillCast. Force it to the immediate-send branch
    //             so preTime skills skip the mode=3 raise-hand/start-cast path.
    //   0x756FAA: jbe short loc_756FCA (2B `76 1E`) — TraceMove stunTime gate
    //             (+0x3468). Flip `76 -> EB` so it always jumps past the reject.
    //   0x756FCD: cmp [eax+194h], 0 (5B start of OR-chain). Overwrite with
    //             `E9 19 01 00 00` = jmp TraceMove_PostGateChain (0x7570EB),
    //             skipping every animation/skill ID equality test.
    //   0x7570FD: jbe short loc_75711D (2B `76 1E`) — TraceMove second timer gate
    //             (+0x2BCC). Flip `76 -> EB`.
    Register("ActionMoveSetAfterActionGate", "", 0x3539FE);
    Register("ActionMoveInstantCastStartup",  "", 0x356331);
    Register("ActionMoveTraceMoveGate1",     "", 0x356FAA);
    Register("ActionMoveTraceMoveOrChain",   "", 0x356FCD);
    Register("ActionMoveTraceMoveGate2",     "", 0x3570FD);

    // ---------- Fire-full-power ----------
    // SkillManager singleton — *(SkillManager**)0xED2F3C (g_pSkillManager).
    // Verified via SkillManager__GetSkillByIndex which does `*(array + 0x28*i)`
    // after a `i < count` gate (array@+0x440, count@+0x444).
    Register("SkillManagerPtr",    "", 0xAD2F3C);
    // CreatureMgr singleton — *(CreatureMgr**)0xECF658 (g_pCreatureMgr).
    // AutoTarget walks the singly-linked list head at +0x0C (next pointer at
    // creature+0x370). Same shape as EntityManager's around-player list.
    Register("CreatureMgrPtr",     "", 0xACF658);
    // Net__BeginSend — __cdecl, no args. Lazy-inits + returns the global
    // CGameClient singleton (g_pGameClient @ linear 0x18B3138). Despite the name
    // it writes no header; it just hands back the client used as ecx/this for the
    // subsequent __thiscall body writers. Linear 0xB2B580 (RVA 0x72B580).
    Register("NetBeginSend",       "", 0x72B580);
    // GameClientPtr — the CGameClient singleton pointer itself: client =
    // *(void**)0x18B3138. RVA 0x14B3138 (imagebase 0x400000). Same object
    // Net__BeginSend returns; AutoFishing reads it directly to pass as the
    // this/ecx of Net__SendPacket_Plaintext without going through Net__BeginSend.
    // Net__SendPacket_Plaintext gates on client+12 (connected) — a stale/pre-login
    // pointer just makes the send a no-op.
    Register("GameClientPtr",      "", 0x14B3138);
    // Net__SendDword — __thiscall(buf, proto, dwordValue). Linear 0xB2D240
    // (RVA 0x72D240). Used for short single-DWORD packets like 412017
    // (CG_PLAYER_REVIVE) and 411015 (CG_ITEM_USE).
    Register("NetSendDword",       "", 0x72D240);
    // Net__SendSkillPacket — __thiscall(this=netBuf, skillId, targetId).
    // Linear 0xB2F5D0 (RVA 0x72F5D0). See CLocalUser__SendSkillCast @ 0x7565E0
    // for the canonical call sequence.
    Register("NetSendSkillPacket", "", 0x72F5D0);

    // Net__SkillSendPackage — __thiscall(this=netBuf, routingTag=0x6458E, buf, size).
    // Linear 0xB2EA90 (RVA 0x72EA90). The lowest-level skill packet dispatcher;
    // every variant branch inside Net__SendSkillPacket finishes by calling
    // sub_B2EA90(411022, Src, len). Old so3dFullCheat's ThrowBomb did
    // `mov ecx,gameClient; push len; push buf; push 0x6458E; call <skillSendPackageOffset>`
    // which is exactly this function. Bomber raw-packet mode skips the variant
    // dispatch and feeds a hand-built buffer (skillId + level + bombItemId +
    // isLocalInRange + count + targetIds) straight into here.
    Register("NetSkillSendPackage", "", 0x72EA90);

    // Net__SendChatStr — __thiscall(this=netBuf, protoId, message). Linear 0xB2E920
    // (RVA 0x72E920). Builds {u32 totalLen, u32 protoId, char[] message} with 4-byte
    // alignment padding, then dispatches via Net__SendPacket_Plaintext. Used for
    // CG_PUBLIC_CHAT (411001) and other string-body protos.
    Register("NetSendChatStr", "", 0x72E920);

    // ResolveTeleportDestByName — __thiscall(this=CUIManager::GetUIContent(99),
    // "/cityname"). Linear 0xA1F800 (RVA 0x61F800). Iterates the in-game teleport
    // destination table, _stricmp(name+1, entryName) (skips the leading '/'), and
    // on match validates the record (valid flag / required level @ g_pLocalUser+6932
    // / required money @ g_pLocalUser+13464) before returning the destination id
    // (>0). Returns 0 when the name doesn't match or a gate fails. This is the
    // exact resolver the chat "/狮子城" command path (ChatCmd_Teleport @ 0x966770)
    // uses to turn a city name into the dest id sent in proto 411076.
    Register("ResolveTeleportDestByName", "", 0x61F800);

    // SendPlayerMoveSyncPacket — __thiscall(this=netBuf). Linear 0xB2DEF0
    // (RVA 0x72DEF0). Position-sync packet (proto 411000/411163) the engine emits
    // right before a teleport request (see Teleport_SendPacket @ 0x9925F0). Heavily
    // self-gated (m_bCanMove / IsDead / stance) and best-effort — the teleport path
    // discards its return value. We fire it for wire-fidelity before proto 411076.
    Register("SendPlayerMoveSync", "", 0x72DEF0);

    // Net_SendTeleportReq411076 — __thiscall(this=netBuf, destId, doSend). Linear
    // 0xB2D9E0 (RVA 0x72D9E0). The gated 411076 sender: checks netBuf valid /
    // !IsDead / map-warp-allowed, then (doSend!=0) Net__SendDword(buf, 411076,
    // destId). doSend==0 is validate-only. We send proto 411076 directly via
    // NetSendDword instead (project bypasses local gates), so this is registered
    // for reference/fallback only.
    Register("NetSendTeleportReq", "", 0x72D9E0);

    // Item__GetItemClass(int itemId) — __stdcall, 1 arg. Returns:
    //   3 = throwable bomb (backs skill 83 投掷炸弹)
    //   1 = consumable class A
    //   2 = consumable class B
    //   0 = other / not-consumable
    // Linear 0x9FCD70 (RVA 0x5FCD70). Used by the bomber-class 火力全开 branch
    // to find the bomb item in the bag without matching encrypted name strings.
    // Internally dispatches to three category-table probes (sub_9365C0 for bombs,
    // sub_9365E0 for class B, sub_936600 for class A) against ranges loaded at
    // init — safe to call from any thread; SEH-wrap at the caller just in case
    // the item-table pointer is torn during scene transitions.
    Register("ItemGetItemClass",   "", 0x1FCD70);

    // StringTableCopy(id, dst, cap) -> localized Big5 text. The client itself
    // displays profession text as StringTableCopy(sub_978880(prof)+0x198).
    Register("StringTableCopy", "", 0x1BF8E0);

    // ---------- Hardware-fingerprint spoof ----------
    // HwFp_FillBuffer — __thiscall(this=SYSTEMTIME*, outPkt). Linear 0xBCCBF0
    // (RVA 0x7CCBF0). Writes hardware fingerprint into fixed offsets of the
    // login packet buffer: OS/CPU/GPU, ComputerName, UserName, locale, MAC,
    // disk hash. Three callers (0xBC8980 / 0xBC9A70 / 0xBCA900) feed it the
    // outgoing login packet buffer. Hooking here covers all three.
    Register("HwFpFillBuffer", "", 0x7CCBF0);

    // ---------- Auto-confirm dialog ----------
    // UI_ShowMessageBox_Modal — char __thiscall(this=CUIMgr, text, dialogType,
    // ctx, timeout). Linear 0x97F7D0 (RVA 0x57F7D0). The entire game routes
    // every confirmation box through here: fills "messagetext" field, enables
    // message_ok (a3==1) / message_yes+message_no (a3==2) / silent (a3==100),
    // then runs its OWN PeekMessage loop until a button flips *(this+11274)
    // back to 0. Return value: 0 = user cancelled, non-zero = user confirmed.
    // Hooking here to short-circuit return 1 auto-picks YES for every modal
    // confirm in one shot (sell, drop, expensive purchase, guild kick, ...).
    Register("UIMessageBoxModal", "", 0x57F7D0);

    // UI_CountDialog —— i64 __thiscall(this=CUIMgr, labelFmt, initialCount,
    // maxCount, unitPrice, mode) -> chosenCount. Linear 0x96FCD0 (RVA 0x56FCD0).
    // 数量选择弹窗（购买/扔物品/卖物/邮件附件等）。游戏会跑自己的 PeekMessage
    // 循环，等用户在 +1/+10/Max/OK/Cancel 之间选完才返回。直接 short-circuit
    // 返回 a4(maxCount) ≈ 用户点了 Max 再 OK，配合 AutoConfirm 的 YesNo 跳过，
    // 就实现"扔/卖/买，全堆/全栈一键执行"。
    Register("UICountDialog", "", 0x56FCD0);

    // ---------- Box-loot auto-move-to-bag ----------
    // OnBoxLootRecv_AutoMoveToBag (IDA 0x8BDF60) 是协议 511620(箱子开出战利品)
    // 的 recv handler。它末尾有作者自带的作弊分支：开箱出战利品后自动发 411313
    // 把战利品挪进背包(target=13 基址，服务器自动选空格)。但有两道门：
    //   gate1 @ 0x8BE010 (RVA 0x4BE010): je  —— 测 g_boxCheatEnabled(byte_DFD67E)
    //   gate2 @ 0x8BE026 (RVA 0x4BE026): jne —— 测 boxUI(GetUIContent(56))+0x1208
    // gate2 的 +0x1208 只有 /openbox 期间才置 1，开箱循环结束就清 0，所以平时手动
    // 开箱这段自动入袋根本不跑。两道都 6 字节近跳转，全 NOP 掉后：任何时候箱子开出
    // 东西都会无条件自动入袋。挂在"自动确认弹窗以及物品最大"开关上一并启停。
    Register("BoxAutoMoveGate1", "", 0x4BE010); // je  byte_DFD67E gate (6B 0F 84)
    Register("BoxAutoMoveGate2", "", 0x4BE026); // jne boxUI+0x1208 gate (6B 0F 85)

    // ---------- Packet logger ----------
    // Net__SendPacket_Plaintext — __thiscall(this=CGameClient, pktObj, length).
    // Linear 0xB1CA60 (RVA 0x71CA60). This is the LOWEST plaintext send: pktObj
    // points to {u32 totalLen; u32 protocolId; ...body}, and the optional XOR
    // obfuscation (sub_5CA910) happens INSIDE this function after we've already
    // observed the unobfuscated body. Hook here for readable send logs.
    Register("NetSendPacketPlaintext", "", 0x71CA60);
    // Net__RawRecv — __thiscall(this=CGameClient, timeoutSec, timeoutUsec).
    // Linear 0xB1C750 (RVA 0x71C750). Engine recv path is ALREADY plaintext on
    // the wire (no decryption layer — verified against every dispatcher at
    // 0x68AEA0, 0xB333C0, 0xB35810, …). Each call does one j_recv() into the
    // rolling buffer at [this+0x18] with fill level at [this+0x24]; we diff
    // fill-level pre/post to capture the exact bytes that just arrived.
    Register("NetRawRecv",            "", 0x71C750);

    // ---------- Drop-item packet ----------
    // Net__SendTriple — __thiscall(this=netBuf, protocolId, arg1, arg2). Linear
    // 0xB2C790 (RVA 0x72C790). Generic 3-arg packet builder; observed in
    // sub_8FCC00 as the dispatcher for CG_ITEM_DROP (411012) with arg1=bagId+13
    // and arg2=stackCount. Also used for other fixed-shape packets (411026,
    // 412048, 412175) that add an extra guild-system hook before sending.
    // For CG_ITEM_DROP the pre-dispatch is a no-op.
    Register("NetSendTriple", "", 0x72C790);

    // ---------- Dialog-select / pickup packet ----------
    // Net__SendDialogSelect — __thiscall(this=netBuf, protocolId, a3, a4, a5).
    // Linear 0xB2C930 (RVA 0x72C930). Builds a 20-byte 3-DWORD-body packet:
    //   {u32 totalLen=20, u32 protocolId, u32 a3, u32 a4, u32 a5}
    // Confirmed shapes (411026 verified 2026-05-09 via repeated same-NPC selection;
    // see OnNpcDialogConfirm @ 0x8BBEF0 — a3 = packet-derived dialogOptionIndex,
    // a4 = cached global g_NpcInteractTargetId at *(dword_ED347C+1016)):
    //   CG_NPC_DIALOG_SELECT (411026): a3=dialogOptionIndex, a4=npcInteractTargetId, a5=1
    //   CG_PICK_ITEM         (411011): a3=dropId,            a4=validatedItemId,      a5=0
    // Sister of NetSendTriple — same caller pattern (BeginSend → mov ecx,eax →
    // call sub_B2C930) but emits one extra DWORD on the wire.
    Register("NetSendDialogSelect", "", 0x72C930);

    // Net__SendFiveDword - __thiscall(this=netBuf, protocolId, a1, a2, a3, a4, a5).
    // Generic 28-byte packet builder with five DWORD body fields. Current
    // vendor-buy implementation uses NetSkillSendPackage instead, matching the
    // actual UI path at sub_82BF50.
    Register("NetSendFiveDword", "", 0x72CD20);

    // ---------- Shop / summoned vendor helpers ----------
    // Merchant UI uses table type 17. The buy path sends proto 411020 with:
    //   {vendorId, shopIndex, count, targetBagSlot, token}
    // where shopIndex is the 0-based index in (vendorRow + 12), and targetBagSlot
    // is computed by CItemContainer::FindInventorySlotForItem (sub_798DA0).
    Register("ShopTableManager",     "", 0x76C6C0); // sub_B6C6C0(type), cdecl
    Register("ShopTableGetElem",     "", 0x76D450); // sub_B6D450(this, type, vendorId, 0)
    Register("ShopListGetAt",        "", 0x1D5460); // sub_5D5460(list, index)
    Register("ShopItemTableById",    "", 0x39BB60); // sub_79BB60(g_pItemContainer, itemId, 0)
    Register("ShopFindInventorySlot","", 0x398DA0); // sub_798DA0(g_pItemContainer, itemId, 0, 0, count)
    // Merchant UI content id 31 has vtable 0xCC2334. Slot 22 (vtable+0x58)
    // is sub_82C6F0, the real close handler: it clears merchant state and sends
    // 411456 using this+0x40 token. Always verify the vtable before calling it.
    Register("VendorUiVTable",       "", 0x8C2334);
    Register("VendorUiClose",        "", 0x42C6F0);

    // ---------- MailBox::SendMoneyMail (proto 411524, op=0) ----------
    // Linear 0x7F51E0 (RVA 0x3F51E0). __stdcall(int op, const char *recipient,
    // int64 money, const char *body). Self-contained: calls Net__BeginSend +
    // Net__SkillSendPackage(buf, 411524, payload, 77) internally. Verified by
    // capturing a live "give 123456 gold to shadowpope" mail packet and matching
    // body bytes against this layout. See CLocalPlayer.h doc.
    //
    Register("MailSendMoney", "", 0x3F51E0);

    // MailBox::SendItemMail (proto 411524, op=1). Linear 0x7F4F80 (RVA 0x3F4F80).
    // __stdcall(int op, const char *recipient, int itemBagId, int itemCount,
    // const char *body). Same self-contained pattern as SendMoneyMail.
    Register("MailSendItem", "", 0x3F4F80);

    // ---------- Block level-up gate ----------
    // CLocalPlayer::UpdateExp's level-up trigger. The function ends with:
    //   if (currentExp >= maxExp && isAlive && level < cap) {
    //       Net__BeginSend(); Net__SendDword(412016);  // CG_LEVEL_UP_CHECK
    //   }
    // The condition compiles to a `jge short loc_878BE2` (2 bytes: 7D 23) at
    // 0x878BBD that SKIPS the send block when currentExp < maxExp. Flipping
    // the opcode byte 7D -> EB makes it an unconditional `jmp short +0x23`,
    // i.e. the send block is NEVER entered regardless of EXP. The displacement
    // byte 0x23 is reused — patch is a single byte.
    //
    // Linear 0x878BBD, RVA 0x478BBD. See entity/CLocalPlayer.h doc block.
    Register("BlockLevelUpGate", "", 0x478BBD);

    // ---------- Fishing catch-handler self-close guard ----------
    // GC_OnFishingCatch_521102 @ 0x891810 (收鱼 handler) 开头有一道一致性/反作弊保护:
    // 客户端若既不在钓鱼会话态 (dword_ED3D74+0x31 == 0)、又没开钓鱼窗口
    // (dword_D67CC4 != 0x10),却收到收鱼包 521102,就 SendMessageA(主窗口, WM_CLOSE)
    // 自关 —— 这正是"纯发包中鱼→游戏闪退"的真因 (鱼仍入袋,因入袋逻辑在 WM_CLOSE 调用
    // 之后、且 WM_CLOSE 是异步)。0x891824 处 `jnz short loc_891855` (75 2F) 在钓鱼会话态
    // 时本就跳过第一处 WM_CLOSE;把 opcode 75→EB 改成无条件 jmp,纯发包流程也跳过。对正常
    // 钓鱼零副作用 (正常 +49!=0 本就 jnz 跳走)。第二处 WM_CLOSE(0x89188C)被
    // dword_D67CC4==0x10 gate,纯发包没开窗口不可达。AutoFishing 启用时打、停用时还原,
    // 单字节补丁。Linear 0x891824, RVA 0x491824。
    Register("FishingCatchGuardJnz", "", 0x491824);

    // ---------- Fishing-pose broadcast call ----------
    // GC_DispatchActorAction @ 0x94ED10 是通用 per-actor 动作广播分发(由 GC 收包
    // handler GC_OnEntityAction_07CC52 @ 0x893FC0 调用)。case 3/13 = 钓鱼动作:
    // 按 packet 里的 actorId 解析出 actor(v17, 可能就是本地玩家)后,在 0x94EFE0
    // 处 `call Actor_SetFishingPose(v17, flag)` —— Actor_SetFishingPose @ 0xA05E80
    // (调试串 "fishing animation setting") 把 actor+404=27(钓鱼抛竿 motion)。
    // 服务器把我们的 411047 抛竿回显成钓鱼动作时,v17=本地玩家,这一步就是"抛竿后
    // 进入钓鱼姿态"的来源。AutoFishing 启用时把这 5 字节 call(E8 rel32)NOP 掉,
    // 停用/析构时还原。副作用:附近真人钓鱼者经此路径的逐动作重新摆姿也会被跳过
    // (仅外观,且仅在启用期间;远端 spawn 摆姿走 sub_876A20 不受影响)。
    // Linear 0x94EFE0, RVA 0x54EFE0。
    Register("FishingPoseBroadcastCall", "", 0x54EFE0);

    // ---------- Walk-to-world-position ----------
    // CLocalUser::SetAfterAction — __thiscall(this=*g_pLocalUser, x, y, action, target).
    // Linear 0x7539E0 (RVA 0x3539E0). Queues "move-after-action" by writing target
    // XY/action/target into this+0x32F0/+0x32F4/+0x32FC/+0x32F8 and setting flag
    // this+0x2BC8 = 1; the engine's per-frame TraceMove + A* mover then walks the
    // avatar there.
    //
    // x/y are **truncated-integer world coords** (NOT bit-cast floats). Verified
    // against OnPlayerMoveClick @ 0x871D10 (`cvttss2si` before push) and
    // AutoHunt__TickBattle (`(int)*(float *)`).  The MoveTo() implementation does
    // the truncation at the call site.
    //
    // Note: SetAfterAction internally runs gates (m_bCanMove, stunTime, anim/skill ID
    // whitelist, …). The ActionMove patch @ 0x7539FE skips them in one shot, so
    // MoveTo() works in any state when ActionMove is enabled. Without it the engine
    // still accepts most idle/walk states.
    Register("SetAfterAction", "", 0x3539E0);

    // dword_DFD518 — global "after-action intent" (1 = walk-click, 3 = walk+attack).
    // OnPlayerMoveClick sets it to 1 before SetAfterAction and back to 0 after.
    // Linear 0xDFD518, RVA 0x9FD518.
    Register("AfterActionIntent", "", 0x9FD518);
    // byte_DFD4E6 — secondary flag also cleared by OnPlayerMoveClick after the
    // SetAfterAction call. Linear 0xDFD4E6, RVA 0x9FD4E6.
    Register("AfterActionFlag2", "", 0x9FD4E6);

    // ---------- Walk-and-talk-to-NPC ----------
    // EntityManager::FindCreatureById — __thiscall(this=*g_pCreatureMgr, id) -> CCreature*.
    // Linear 0x863F60 (RVA 0x463F60). Used to resolve an NPC/monster id to its live
    // CCreature pointer so we can read its world position before walking to it.
    Register("FindCreatureById", "", 0x463F60);

    // g_TargetCreatureId — global int "currently-targeted CCreature id". UI hit-tests
    // fill it when the user clicks an NPC/monster; OnTargetCreatureClick_TalkOrAttack
    // @ 0x9584A0 reads it to find the creature, then sets g_AfterActionIntent=3 and
    // calls SetAfterAction(... action=3, target=0). The action-3 success path inside
    // TraceMove later reads THIS global (NOT SetAfterAction's target arg) to dispatch
    // the right packet: NPC -> dialog open, monster -> attack. -1 means "no target".
    // Linear 0x18B2A08, RVA 0x14B2A08.
    Register("TargetCreatureId", "", 0x14B2A08);

    // ---------- NPC dialog ----------
    // Npc__LoadDialogScript(state, monsterTblId) -> bool. Linear 0x8F7FA0, RVA 0x4F7FA0.
    // The engine's CANONICAL NPC discriminator: returns nonzero iff `monsterTblId` has
    // a dialog script in npctalk.dat / quest.txt. Used by every NPC-click path to
    // distinguish NPCs from monsters. Much more reliable than heuristics on monsterTblId
    // ranges (which differ across maps).
    //
    // CALLING CONVENTION — __thiscall(state*, int monsterTblId). The `state*` MUST be
    // the dereferenced VALUE at g_NpcDialogState (RVA 0xAD347C / linear 0xED347C), NOT
    // the address of the global. IDA shows `mov ecx, ds:g_NpcDialogState; push ecx` at
    // every call site — i.e. the global at 0xED347C HOLDS a pointer to the actual state
    // object. Passing the address of the global as `this` makes the function walk the
    // dword sitting at 0xED347C+0x404 (=g_NpcDialogState's 257th DWORD) as the head of
    // the dialog-script linked list — guaranteed garbage, always returns 0, every
    // creature ends up classified as monster. See CLocalPlayer.cpp CreatureIsNpc for
    // the correct deref pattern.
    Register("NpcLoadDialogScript", "", 0x4F7FA0);

    // Npc__OpenDialogByCreatureRef(creatureMgr, &creatureIdRef, dialogState, dialogUI,
    //                              p1, p2) -> bool. Linear 0xB3A3F0, RVA 0x73A3F0.
    // Opens the NPC dialog UI immediately given a creatureId in g_TargetCreatureId. No
    // distance check — pure local UI bootstrap. Used by the engine's per-frame click
    // handler at sub_9EA090.
    Register("NpcOpenDialogByCreatureRef", "", 0x73A3F0);

    // g_NpcDialogState — dialog state object. Linear 0xED347C, RVA 0xAD347C.
    // NpcOpenDialogByCreatureRef writes [state+1016] = creature+112, then calls
    // NpcLoadDialogScript(state, monsterTblId).
    Register("NpcDialogState", "", 0xAD347C);

    // dword_ED3D10 — dialog UI controller table (npctalk.dat-derived). Linear 0xED3D10,
    // RVA 0xAD3D10. Passed as a4 to NpcOpenDialogByCreatureRef.
    Register("NpcDialogUITable", "", 0xAD3D10);

    // dword_189DEE8 — game world state pointer. Linear 0x189DEE8, RVA 0x149DEE8.
    // Passed as a5 (int, plain value) to NpcOpenDialogByCreatureRef.
    Register("NpcDialogParam1Ptr", "", 0x149DEE8);

    // byte_ED40F0 — global UI parent handle. Linear 0xED40F0, RVA 0xAD40F0.
    // Passed as a6 (pointer to byte) to NpcOpenDialogByCreatureRef.
    Register("NpcDialogParam2Ptr", "", 0xAD40F0);

    // ---------- NPC dialog option click chain ----------
    // 这两个搭配 OnNpcDialogOption_Quest 实现「真的像玩家点了选项一样」效果:
    // 直接发 411026 包的话 UI 状态不会更新(state+1044 仍指向旧选项,server 推
    // 下一菜单时引擎自己 build 不出来)。正确做法是先把选中的选项节点写到
    // state+1044,然后调 Npc__ConfirmDialogOptionLocal 让引擎自己:
    //   1) 播 SFX
    //   2) 把 dialogResult 写到 g_DialogOptionPendingResult (dword_ED3DC4)
    //   3) 在本地 build 出下一屏菜单(如果有 sub-script)
    // 最后调 OnNpcDialogOption_Quest 发包并清理 g_NpcDialogState UI 状态。

    // Npc__ConfirmDialogOptionLocal @ 0x8F9EE0. __thiscall(state, &resultOut).
    // 等于「在 UI 上点一下当前 state+1044 指向的选项」: 内部根据 state[1044]
    // 走完所有本地 state-machine 转换,然后(如果选项有 quest tag)把数值写进
    // 调用者传入的 result 指针 —— 即 dword_ED3DC4。返回 1=对话框继续打开
    // (下一菜单已构建),0=对话结束(本地关闭)。
    Register("NpcConfirmDialogOption", "", 0x4F9EE0);

    // OnNpcDialogOption_Quest @ 0x992160. __thiscall(uiThis, dialogResult, mode).
    // 这才是真正发 411026 包的入口。mode=0 = 用户点选项,mode=1 = 一些特殊
    // 快捷路径(来自 sub_967280)。
    Register("NpcSendDialogOption", "", 0x592160);

    // g_DialogOptionPendingResult — dword_ED3DC4. Npc__ConfirmDialogOptionLocal
    // 写,OnNpcDialogOption_Quest 读。我们手动模拟点击时,在写 state+1044
    // 之后先把这个清零(防上一次残留),Confirm 之后再 take 出来交给 send.
    Register("NpcDialogPendingResult", "", 0xAD3DC4);

    // ---------- Net__SendPacket16B_2DW_Tracked (revive / generic 2-DWORD packet) ----------
    // Linear 0xB2C790 / RVA 0x72C790. __thiscall(buf, proto, arg1, arg2). Same
    // packet shape as NetSendTriple but with the tracking pre-hook (logs into
    // Net__OnSendDialogStateTrack for protos 411026 / 412048 / 412175). Used by
    // many UI buttons including the revive dialog's "rebirth_ok" handler:
    //   Net__SendPacket16B_2DW_Tracked(buf, 411170, deadUserId, flag=1)
    // 411170 = CG_REBIRTH_OK: server warps player back to town. arg1 is the
    // dead user's id (we read g_pLocalUser+112 since AutoRevive fires on our
    // own death), arg2=1 means "yes revive" / 0 means "cancel".
    Register("NetSendPacket16B2DwTracked", "", 0x72C790);

    // ---------- Current map id ----------
    // g_CurMapSlot — int (NOT int*). RVA 0x967CB4 (linear 0xD67CB4).
    // Engine ref sites verified across 18+ functions (AutoMover_Update,
    // sub_5FED10, sub_658FF0, sub_65BD10, CLocalUser__SendSkillCast, ...):
    //   mov eax, ds:dword_D67CB4         ; eax = slot index VALUE
    //   mov edx, ds:dword_D6AE78[eax*4]  ; edx = g_MapIdTable[slot]
    // Helper sub_978670 also confirms: `return g_MapIdTable[slot] == 125;`
    // with `slot` passed as a plain int arg. So GetCurrentMapId() must do a
    // SINGLE deref of CurMapSlot, NOT a double-deref like an int* would need.
    Register("CurMapSlot", "", 0x967CB4);

    // g_MapIdTable — int[]. RVA 0x96AE78 (linear 0xD6AE78). Indexed by
    // g_CurMapSlot to retrieve the current map id (e.g. 7=新手村, 400=Square/主城).
    Register("MapIdTable",  "", 0x96AE78);

    // ---------- Map collision grid (用来判定 tile 是否可走) ----------
    // g_pCurMap — pointer to current map object (CMap*). RVA 0x149E070 (linear
    // 0x189E070). NULL or torn-on-scene-transition. Layout:
    //   +0x10 collisionGrid (int[width*height], 0=walkable nonzero=blocked)
    //   +0x14 width  (int)
    //   +0x18 height (int)
    // 网格按 row-major 排列：grid[x + width*y]。世界坐标转 tile 用 floor 取整
    // (引擎用 sub_AA2050)。Map__IsBlocked @ 0xA97080 是引擎的可达性查询入口,
    // A* 寻路、target-spec 检查全都funnel through 它。
    Register("CurMapPtr", "", 0x149E070);

    // ---------- Auto-trade: local exchange-window open ----------
    // 自动接受交易时复刻「点接受按钮」的本地动作。手动 accept 在
    // Trade_RequestPopup (0x721080) 里先调 Trade_OpenLocalExchangeWindow(ctrl)
    // 把请求弹窗(control 32)就地转成交易窗口,再发 412028。只发包不调它,窗口
    // 会停在请求态直到对方放物品才被动弹出 —— 这三条用来补上本地开窗那一步。
    //
    // CUIManager__GetSingleton() (__cdecl) 返回 UI 管理器单例;
    // CUIManager__GetUIContent(mgr, id) (__thiscall) 取 control(无则返回 0);
    // Trade_OpenLocalExchangeWindow(ctrl) (__thiscall) 铺出 itembox/item 子控件。
    // 调用链:Open(GetUIContent(GetSingleton(), 32))。RVA = linear - 0x400000。
    Register("TradeOpenLocalWindow",  "", 0x31E040); // sub_71E040
    Register("UIManagerGetSingleton", "", 0x5EA420); // sub_9EA420
    Register("UIManagerBeginContent", "", 0x5E9C50); // CUIManager::BeginContent(mgr, renderer, enum) retn 8; 开仓库=enum 2(内部建本地UI+发411154/411644,不能裸发包否则卡移动)
    Register("UIManagerGetUIContent", "", 0x5EA4D0); // sub_9EA4D0 (CUIManager::GetUIContent)
    Register("UIManagerCloseContent", "", 0x5E9FB0); // sub_9E9FB0(id), marks UI content close flag (+0x25)
    Register("UIContentMarkClose",    "", 0x739190); // sub_B39190(), direct content close flag setter used by CloseContent
    Register("UIManagerIsContentOpen", "", 0x5EC570); // sub_9EC570(id), true when UI content id is open
    Register("UIManagerCloseActiveContent", "", 0x5EA090); // sub_9EA090(renderer), processes marked-close UI content
    Register("RendererPtr", "", 0x149DC08); // dword_189DC08, pushed before sub_9EA090 in the UI frame path
}

void PatternResolver::ScanAll()
{
    if (!moduleBase_ || !moduleSize_)
        return;

    zzj::Process process;
    zzj::Memory memory(process);

    std::unordered_map<std::string, std::vector<uintptr_t>> matchCache;

    for (auto &entry : entries_)
    {
        if (entry.pattern.empty())
            continue;

        if (matchCache.find(entry.pattern) == matchCache.end())
        {
            auto results = memory.PatternScan(moduleBase_, moduleSize_, entry.pattern);
            std::vector<uintptr_t> addrs;
            addrs.reserve(results.size());
            for (auto &r : results)
                addrs.push_back(r.address);
            std::sort(addrs.begin(), addrs.end());
            matchCache[entry.pattern] = std::move(addrs);
        }

        auto &matches = matchCache[entry.pattern];
        if (entry.matchIndex < static_cast<int>(matches.size()))
        {
            uintptr_t addr = matches[entry.matchIndex] + entry.offset;
            if (entry.resolveRelCall)
            {
                int32_t rel = *reinterpret_cast<int32_t *>(addr);
                addr        = addr + 4 + rel;
            }
            else if (entry.derefOperand)
            {
                addr = *reinterpret_cast<uintptr_t *>(addr);
            }
            entry.resolved = addr;
            spdlog::info("GGTB::PatternResolver: {} -> {:x}", entry.name, entry.resolved);
        }
        else
        {
            spdlog::warn("GGTB::PatternResolver: {} pattern found {} matches, need index {}",
                         entry.name, matches.size(), entry.matchIndex);
        }
    }
}

bool PatternResolver::LoadCache()
{
    std::ifstream ifs(cachePath_);
    if (!ifs.is_open())
        return false;

    try
    {
        nlohmann::json j;
        ifs >> j;

        if (j.value("module_md5", "") != moduleMd5_)
        {
            spdlog::info("GGTB::PatternResolver: MD5 mismatch, will rescan");
            return false;
        }

        auto &addrs = j["addresses"];
        int loaded  = 0;
        for (auto &entry : entries_)
        {
            if (entry.pattern.empty())
            {
                entry.resolved = moduleBase_ + entry.fallbackRVA;
                ++loaded;
                continue;
            }
            if (addrs.contains(entry.name))
            {
                entry.resolved = std::stoull(addrs[entry.name].get<std::string>(),
                                             nullptr, 16);
                ++loaded;
            }
        }
        spdlog::info("GGTB::PatternResolver: loaded {} addresses from cache", loaded);
        return true;
    }
    catch (const std::exception &e)
    {
        spdlog::warn("GGTB::PatternResolver: cache parse error: {}", e.what());
        return false;
    }
}

void PatternResolver::SaveCache()
{
    nlohmann::json j;
    j["module_md5"] = moduleMd5_;

    nlohmann::json addrs = nlohmann::json::object();
    for (auto &entry : entries_)
    {
        if (entry.resolved)
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned>(entry.resolved));
            addrs[entry.name] = std::string(buf);
        }
    }
    j["addresses"] = addrs;

    std::ofstream ofs(cachePath_);
    if (ofs.is_open())
    {
        ofs << std::setw(4) << j << std::endl;
        spdlog::info("GGTB::PatternResolver: saved cache to {}", cachePath_);
    }
}

} // namespace GGTB
