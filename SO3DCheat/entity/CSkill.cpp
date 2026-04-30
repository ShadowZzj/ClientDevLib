#include "CSkill.h"
#include "CLocalPlayer.h"
#include "../util/PatternResolver.h"
#include <spdlog/spdlog.h>
#include <cstring>

namespace SO3D
{

// 用户要求 0.0001f —— 替换 Skill__GetPreTime 里两条 movss 的 disp32 指向这里。
// `static` 保证在 .data 段固定地址，patch 时直接 &s_preTimeConst 嵌进 disp32 即可。
static float s_preTimeConst = 0.0001f;
static BYTE  s_preTimeOrig1[kPreTimePatchSize] = {};
static BYTE  s_preTimeOrig2[kPreTimePatchSize] = {};
static bool  s_preTimePatched = false;

std::string SkillTable::GetSkillName()
{
    return std::string(skillName, strnlen(skillName, sizeof(skillName)));
}

std::string SkillTable::GetSkillDescription()
{
    return std::string(description, strnlen(description, sizeof(description)));
}

// ============================================================
//  Skill manager access
// ============================================================

uintptr_t GetSkillManagerBase()
{
    auto addr = PatternResolver::Get("SkillManagerPtr");
    if (!addr) return 0;
    auto ptr = *reinterpret_cast<uintptr_t *>(addr);
    if (ptr == 0 || ptr == 0xFFFFFFFF) return 0;
    return ptr;
}

uint32_t GetSkillCount()
{
    auto mgr = GetSkillManagerBase();
    if (!mgr) return 0;
    return *reinterpret_cast<uint32_t *>(mgr + kSkillArraySizeOffset);
}

CSkill *GetCSkill(uint32_t index)
{
    auto mgr = GetSkillManagerBase();
    if (!mgr) return nullptr;
    uint32_t count = *reinterpret_cast<uint32_t *>(mgr + kSkillArraySizeOffset);
    if (index >= count) return nullptr;
    auto arrPtr = *reinterpret_cast<uintptr_t *>(mgr + kSkillArrayPtrOffset);
    if (!arrPtr) return nullptr;
    return reinterpret_cast<CSkill *>(arrPtr + static_cast<uintptr_t>(index) * sizeof(CSkill));
}

// ============================================================
//  PreTime code patch
// ============================================================

static bool PatchOnePreTime(uintptr_t addr, BYTE *backup)
{
    DWORD oldProt = 0;
    if (!VirtualProtect(reinterpret_cast<void *>(addr), kPreTimePatchSize, PAGE_EXECUTE_READWRITE, &oldProt))
    {
        spdlog::error("VirtualProtect failed at {:x}", addr);
        return false;
    }
    memcpy(backup, reinterpret_cast<void *>(addr), kPreTimePatchSize);

    // ModR/M 0x05 = mod=00, reg=000(xmm0), r/m=101(disp32 absolute)。
    // 新 binary 的两条原指令都是 movss xmm0,[reg+0x174]（reg 字段=000=xmm0），
    // 替换后必须保持目标寄存器仍为 xmm0；旧版 0x0D（xmm1）不适用此处。
    BYTE patch[kPreTimePatchSize] = { 0xF3, 0x0F, 0x10, 0x05, 0, 0, 0, 0 };
    auto constAddr = reinterpret_cast<uintptr_t>(&s_preTimeConst);
    memcpy(&patch[4], &constAddr, 4);
    memcpy(reinterpret_cast<void *>(addr), patch, kPreTimePatchSize);

    VirtualProtect(reinterpret_cast<void *>(addr), kPreTimePatchSize, oldProt, &oldProt);
    return true;
}

static bool RestoreOne(uintptr_t addr, const BYTE *backup)
{
    DWORD oldProt = 0;
    if (!VirtualProtect(reinterpret_cast<void *>(addr), kPreTimePatchSize, PAGE_EXECUTE_READWRITE, &oldProt))
        return false;
    memcpy(reinterpret_cast<void *>(addr), backup, kPreTimePatchSize);
    VirtualProtect(reinterpret_cast<void *>(addr), kPreTimePatchSize, oldProt, &oldProt);
    return true;
}

bool PatchPreTime()
{
    if (s_preTimePatched) return true;
    auto a1 = PatternResolver::Get("PreTimeRead1");
    auto a2 = PatternResolver::Get("PreTimeRead2");
    if (!a1 || !a2) return false;

    bool ok1 = PatchOnePreTime(a1, s_preTimeOrig1);
    bool ok2 = PatchOnePreTime(a2, s_preTimeOrig2);
    s_preTimePatched = ok1 && ok2;

    if (s_preTimePatched)
        spdlog::info("PreTime patched: {:x}, {:x} -> movss xmm0,[{:x}] ({})",
            a1, a2, reinterpret_cast<uintptr_t>(&s_preTimeConst), s_preTimeConst);
    return s_preTimePatched;
}

bool RestorePreTime()
{
    if (!s_preTimePatched) return true;
    auto a1 = PatternResolver::Get("PreTimeRead1");
    auto a2 = PatternResolver::Get("PreTimeRead2");
    if (!a1 || !a2) return false;

    bool ok1 = RestoreOne(a1, s_preTimeOrig1);
    bool ok2 = RestoreOne(a2, s_preTimeOrig2);
    s_preTimePatched = !(ok1 && ok2);

    if (!s_preTimePatched)
        spdlog::info("PreTime restored");
    return !s_preTimePatched;
}

} // namespace SO3D
