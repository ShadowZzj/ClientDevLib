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
    //   0x756FAA: jbe short loc_756FCA (2B `76 1E`) — TraceMove stunTime gate
    //             (+0x3468). Flip `76 -> EB` so it always jumps past the reject.
    //   0x756FCD: cmp [eax+194h], 0 (5B start of OR-chain). Overwrite with
    //             `E9 19 01 00 00` = jmp TraceMove_PostGateChain (0x7570EB),
    //             skipping every animation/skill ID equality test.
    //   0x7570FD: jbe short loc_75711D (2B `76 1E`) — TraceMove second timer gate
    //             (+0x2BCC). Flip `76 -> EB`.
    Register("ActionMoveSetAfterActionGate", "", 0x3539FE);
    Register("ActionMoveTraceMoveGate1",     "", 0x356FAA);
    Register("ActionMoveTraceMoveOrChain",   "", 0x356FCD);
    Register("ActionMoveTraceMoveGate2",     "", 0x3570FD);
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
