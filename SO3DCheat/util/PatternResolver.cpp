#include "PatternResolver.h"
#include <Windows/util/Process/ProcessHelper.h>
#include <General/util/Crypto/Md5.h>
#include <json.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace SO3D
{

std::string PatternResolver::cachePath_;
std::string PatternResolver::exeMd5_;
uintptr_t PatternResolver::moduleBase_ = 0;
uintptr_t PatternResolver::moduleSize_ = 0;
std::vector<PatternEntry> PatternResolver::entries_;
std::unordered_map<std::string, size_t> PatternResolver::nameIndex_;
bool PatternResolver::initialized_ = false;

// ============================================================
//  Public
// ============================================================

void PatternResolver::Init(HMODULE hCheatDll)
{
    if (initialized_) return;

    char dllPath[MAX_PATH]{};
    GetModuleFileNameA(hCheatDll, dllPath, MAX_PATH);
    cachePath_ = std::filesystem::path(dllPath).parent_path().string() + "\\pattern_cache.json";

    moduleBase_ = reinterpret_cast<uintptr_t>(GetModuleHandleA("SO3D.exe"));
    if (!moduleBase_)
    {
        spdlog::error("PatternResolver: SO3D.exe not found");
        return;
    }

    zzj::Process process;
    zzj::Memory memory(process);
    auto modInfo = memory.GetModuleInfo("SO3D.exe");
    moduleSize_ = modInfo ? modInfo->modBaseSize : 0;

    char exePath[MAX_PATH]{};
    GetModuleFileNameA(reinterpret_cast<HMODULE>(moduleBase_), exePath, MAX_PATH);
    zzj::MD5::GetFileMD5(exePath, exeMd5_);
    spdlog::info("PatternResolver: SO3D.exe MD5={}, base={:x}, size={:x}",
                 exeMd5_, moduleBase_, moduleSize_);

    RegisterAll();

    if (!LoadCache())
    {
        spdlog::info("PatternResolver: cache miss, scanning...");
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
        spdlog::error("PatternResolver: unknown entry '{}'", name);
        return 0;
    }
    return entries_[it->second].resolved;
}

uintptr_t PatternResolver::GetModuleBase() { return moduleBase_; }
uintptr_t PatternResolver::GetModuleSize() { return moduleSize_; }

// ============================================================
//  Registration
// ============================================================

void PatternResolver::Register(const std::string &name, const std::string &pattern,
                               uintptr_t fallbackRVA, int matchIndex, int offset,
                               bool derefOperand, bool resolveRelCall)
{
    nameIndex_[name] = entries_.size();
    entries_.push_back({name, pattern, fallbackRVA, matchIndex, offset, derefOperand, resolveRelCall, 0});
}

void PatternResolver::RegisterAll()
{
    // --- Code patch addresses ---

    // CLocalUser::CalcStatus —— 8 个 per-job 分支写入 [this+0x1A4]=baseSpeed。
    // 每个 movss 形式不同（[edx+1A4]/[ecx+1A4]/[eax+1A4]），且都被夹在 if/else 块里，
    // 写一个能精确分辨 8 个站的 pattern 太脆弱；这里直接用 RVA fallback。
    // RVA 来自 unpackd_so3d.exe 静态分析（与 GGThreadBlock 同源）。
    Register("MoveSpeedWrite1",  "", 0x345DD2);  // movss [edx+1A4], xmm0
    Register("MoveSpeedWrite2",  "", 0x345E0E);  // movss [ecx+1A4], xmm0
    Register("MoveSpeedWrite3",  "", 0x345E3B);  // movss [eax+1A4], xmm0
    Register("MoveSpeedWrite4",  "", 0x345F14);  // movss [ecx+1A4], xmm0
    Register("MoveSpeedWrite5",  "", 0x345F41);  // movss [eax+1A4], xmm0
    Register("MoveSpeedWrite6",  "", 0x345F8C);  // movss [ecx+1A4], xmm0
    Register("MoveSpeedWrite7",  "", 0x345FBA);  // movss [eax+1A4], xmm0
    Register("MoveSpeedWrite8",  "", 0x346229);  // movss [ecx+1A4], xmm0

    // CLocalUser::TraceMove —— 每帧上限钳位（如果只 NOP CalcStatus，TraceMove 仍会
    // 把 moveSpeed 拉回 7.0/10.0）。两站共享同一段 IR：
    //   mov eax,[ebp+var_10] ; movss xmm0,[g_kMoveSpeedCap*]
    //   movss [eax+1A4], xmm0 ; mov ecx,[ebp+var_10]
    // 这个 22 字节 pattern 在整个 .text 里 exactly 2 处命中，offset+11 落到 movss 上。
    Register("MoveSpeedClampMounted",
             "8B 45 F0 F3 0F 10 05 ?? ?? ?? ?? F3 0F 11 80 A4 01 00 00 8B 4D F0",
             0x357707, 0, 11);  // if (this->mounted) speed = min(speed, 10.0f)
    Register("MoveSpeedClampNormal",
             "8B 45 F0 F3 0F 10 05 ?? ?? ?? ?? F3 0F 11 80 A4 01 00 00 8B 4D F0",
             0x357743, 1, 11);  // else            speed = min(speed, 7.0f)

    // movss [ebx+64A4h], xmm0
    Register("AttackSpeedWrite", "F3 0F 11 83 A4 64 00 00", 0x4732AB);

    // movss [ebx+64A8h], xmm0
    Register("SkillSpeedWrite",  "F3 0F 11 83 A8 64 00 00", 0x4732FF);

    // Skill__GetPreTime（IDA: sub_9394F0）—— 把 SkillTable+0x174 的 preTime 字段
    // 通过 FPU 返回。两个分支：
    //   level==0：movss xmm0, [edx+0x174]   F3 0F 10 82 74 01 00 00
    //   level>0 ：movss xmm0, [ecx+0x174]   F3 0F 10 81 74 01 00 00
    // 两站都打成 `movss xmm0, [&s_preTimeConst]`（0x05 = mod=00, reg=000=xmm0,
    // r/m=101=disp32），同样 8 字节，原地替换。注意：旧 binary 用 xmm1+offset 0x150，
    // 这版改成 xmm0+offset 0x174。
    // [edx+0x174] 在 .text 里 3 处命中，[ecx+0x174] 唯一；这里都直接走 RVA fallback。
    Register("PreTimeRead1",     "", 0x53954A);  // level==0 branch (edx)
    Register("PreTimeRead2",     "", 0x53956B);  // level>0  branch (ecx) — 用户实测站

    // test [edi+124h],1; movss; mulss [ebp+8]; jz — unique: next divss uses 64A8h (skillSpeed)
    Register("AnimFlagJz",       "F6 87 24 01 00 00 01 F3 0F 10 87 ?? ?? ?? ?? F3 0F 59 45 08 0F 84", 0x678F8C, 0, 20);

    // movups [reg+ecx*4+7054h], xmm0
    Register("PreTimeStore",     "0F 11 84 ?? 54 70 00 00", 0x4715D8);

    // subss xmm0,xmm1; movss [eax+edx+18h],xmm0 — cooldown store
    Register("CooldownDecrement","F3 0F 5C C1 F3 0F 11 44 ?? 18", 0x5DE7C5, 0, 4);

    // --- Data pointer RVAs (deref operand to get the global address) ---
    // mov eax, ds:g_pLocalPlayer; add eax, 691Ch
    Register("LocalPlayerPtr",   "A1 ?? ?? ?? ?? 05 1C 69 00 00", 0xD6E3E0, 0, 1, true);
    // mov ecx, ds:g_pSkillManager; push 145h
    Register("SkillManagerPtr",  "8B 0D ?? ?? ?? ?? 68 45 01 00 00", 0xDB2E78, 0, 2, true);
    // movss xmm0, ds:g_fGlobalTime; mov eax, [ebp+arg_4]
    Register("GlobalTime",       "F3 0F 10 05 ?? ?? ?? ?? 8B 45 0C", 0xCA4344, 0, 4, true);

    // Entity manager: mov ecx, [EM]; push edi; call; test eax,eax; jz; add eax, 3A80h
    Register("EntityManager",    "8B 0D ?? ?? ?? ?? 57 E8 ?? ?? ?? ?? 85 C0 ?? ?? 05 80 3A 00 00", 0xD6E3D4, 0, 2, true);

    // CUser vftable: push eax; mov [edi], CUser::vft; mov [edi+30h]; mov [edi+5Ch]; mov [edi+60h]
    Register("CUserVftable",     "50 C7 07 ?? ?? ?? ?? C7 47 30 ?? ?? ?? ?? C7 47 5C ?? ?? ?? ?? C7 47 60", 0xABAEE8, 0, 3, true);

    // --- GameGuard bypass patches ---

    // Patch 0: GameGuard_StaticInit — push "SealOnlineCN"; call GG_Init; push lambda; call _atexit; ret
    Register("GG_StaticInit",
             "68 ?? ?? ?? ?? E8 ?? ?? ?? ?? 68 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 C4 08 C3",
             0x4BE30);

    // Patch 1: GameGuard_RuntimeCheck — resolved via IsGameGuardAlive caller pattern:
    //   call <RuntimeCheck>; xor ecx,ecx; cmp eax,755h; setz cl; mov eax,ecx; retn
    //   offset=1 → rel32 operand; resolveRelCall computes the call target
    Register("GG_RuntimeCheck",
             "E8 ?? ?? ?? ?? 33 C9 3D 55 07 00 00 0F 94 C1 8B C1 C3",
             0x8D8BB0, 0, 1, false, true);

    // Patch 2: GameGuard_GetResult — resolved via WinMain caller pattern:
    //   call <GetResult>; cmp eax,755h; jz OK; push eax; call <ErrorBox>; add esp,4
    Register("GG_GetResult",
             "E8 ?? ?? ?? ?? 3D 55 07 00 00 74 ?? 50 E8 ?? ?? ?? ?? 83 C4 04",
             0x8D8F80, 0, 1, false, true);

    // Patch 3: GG exit jnz in game loop — mov ebx,[PeekMsg]; nop; cmp [g_bGGExit],0; jnz
    //   offset 14 = the jnz opcode byte
    Register("GG_ExitJnz",
             "8B 1D ?? ?? ?? ?? 90 83 3D ?? ?? ?? ?? 00 75",
             0x6300B7, 0, 14);

    // Patch 4: GameGuard_ErrorMessageBox — prologue + security cookie + cmp ecx,755h (unique)
    Register("GG_ErrorMsgBox",
             "55 8B EC 81 EC 04 01 00 00 ?? ?? ?? ?? ?? 33 C5 89 45 FC 8B 4D 08 81 F9 55 07 00 00",
             0x607570);

    // Patch 5: Packet_GC_GAME_GUARD2 — mov [ebp-4],0; push; push [ebp+8]; call; push; mov esi,eax; push
    //   offset -8 to reach function entry from the mov dword ptr [ebp-4],0 instruction
    Register("GG_PacketHandler",
             "C7 45 FC 00 00 00 00 50 FF 75 08 E8 ?? ?? ?? ?? FF 75 FC 8B F0 FF 35",
             0x5822B0, 0, -8);
}

// ============================================================
//  Scanning
// ============================================================

void PatternResolver::ScanAll()
{
    if (!moduleBase_ || !moduleSize_) return;

    zzj::Process process;
    zzj::Memory memory(process);

    std::unordered_map<std::string, std::vector<uintptr_t>> matchCache;

    for (auto &entry : entries_)
    {
        if (entry.pattern.empty()) continue;

        std::string key = entry.pattern;
        if (matchCache.find(key) == matchCache.end())
        {
            auto results = memory.PatternScan(moduleBase_, moduleSize_, entry.pattern);
            std::vector<uintptr_t> addrs;
            addrs.reserve(results.size());
            for (auto &r : results)
                addrs.push_back(r.address);
            std::sort(addrs.begin(), addrs.end());
            matchCache[key] = std::move(addrs);
        }

        auto &matches = matchCache[key];
        if (entry.matchIndex < static_cast<int>(matches.size()))
        {
            uintptr_t addr = matches[entry.matchIndex] + entry.offset;
            if (entry.resolveRelCall)
            {
                int32_t rel = *reinterpret_cast<int32_t *>(addr);
                addr = addr + 4 + rel;
            }
            else if (entry.derefOperand)
            {
                addr = *reinterpret_cast<uintptr_t *>(addr);
            }
            entry.resolved = addr;
            const char *tag = entry.resolveRelCall ? " relcall" : (entry.derefOperand ? " deref" : "");
            spdlog::info("PatternResolver: {} scanned -> {:x} (match {} + offset {}{})",
                         entry.name, entry.resolved, entry.matchIndex, entry.offset, tag);
        }
        else
        {
            spdlog::warn("PatternResolver: {} pattern found {} matches, need index {}",
                         entry.name, matches.size(), entry.matchIndex);
        }
    }
}

// ============================================================
//  JSON Cache
// ============================================================

bool PatternResolver::LoadCache()
{
    std::ifstream ifs(cachePath_);
    if (!ifs.is_open()) return false;

    try
    {
        nlohmann::json j;
        ifs >> j;

        if (j.value("exe_md5", "") != exeMd5_)
        {
            spdlog::info("PatternResolver: MD5 mismatch, will rescan");
            return false;
        }

        auto &addrs = j["addresses"];
        int loaded = 0;
        for (auto &entry : entries_)
        {
            if (addrs.contains(entry.name))
            {
                std::string hex = addrs[entry.name].get<std::string>();
                entry.resolved = std::stoull(hex, nullptr, 16);
                loaded++;
            }
        }
        spdlog::info("PatternResolver: loaded {} addresses from cache", loaded);
        return true;
    }
    catch (const std::exception &e)
    {
        spdlog::warn("PatternResolver: cache parse error: {}", e.what());
        return false;
    }
}

void PatternResolver::SaveCache()
{
    nlohmann::json j;
    j["exe_md5"] = exeMd5_;

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
        spdlog::info("PatternResolver: saved cache to {}", cachePath_);
    }
    else
    {
        spdlog::error("PatternResolver: failed to write {}", cachePath_);
    }
}

} // namespace SO3D
