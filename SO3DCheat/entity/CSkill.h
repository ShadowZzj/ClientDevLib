#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>

namespace SO3D
{

// Skill array offsets (from g_pSkillManager base)
inline constexpr uintptr_t kSkillArrayPtrOffset  = 0x440;
inline constexpr uintptr_t kSkillArraySizeOffset = 0x444;

// ============================================================
//  SkillTable (0x288 bytes, offsets unchanged from old version)
//  Verified via IDA:
//    +0x14C: 0x9DCF32 mov eax,[eax+14Ch]  (skillRange)
//    +0x158: 0x9DC8D1 movss xmm1,[esi+158h] (coolDown)
//    +0x160: 0x9DCEA7 mov eax,[eax+160h]  (attack)
// ============================================================

#pragma pack(push, 4)
class SkillTable
{
  public:
    enum class SkillType : uint32_t
    {
        Self         = 1,
        SingleAttack = 2,
        RangeAttack  = 3,
    };

    uint32_t vtable;            // 0x0000
    uint32_t skillId;           // 0x0004
    uint32_t skillId2;          // 0x0008
    char skillName[256];        // 0x000C  GBK encoded
    char pad_010C[4];           // 0x010C
    SkillType skillType;        // 0x0110
    char pad_0114[40];          // 0x0114
    uint32_t apCost;            // 0x013C
    char pad_0140[8];           // 0x0140
    int32_t skillCoverRange;    // 0x0148
    int32_t skillRange;         // 0x014C
    float preTime;              // 0x0150  (new version: server-sent, this is reference only)
    char pad_0154[4];           // 0x0154
    float coolDown;             // 0x0158
    char pad_015C[4];           // 0x015C
    uint32_t attack;            // 0x0160
    char pad_0164[12];          // 0x0164
    char description[60];       // 0x0170
    char pad_01AC[220];         // 0x01AC

  public:
    std::string GetSkillName();
    std::string GetSkillDescription();
};
static_assert(sizeof(SkillTable) == 0x288);

// ============================================================
//  CSkill (0x28 bytes)
//  Verified via IDA:
//    +0x04: 0x9DC854 mov ecx,[edi+4]  (skillId, used as table index)
//    +0x08: 0x9DC81B cmp [edi+8],0    (flag for regular vs enhanced)
//    +0x18: 0x9DE7B6 movss xmm0,[base+18h] (leftCoolDown)
//    +0x1C: 0x9DD555 check != 0       (isValid)
//    +0x20: 0x9DCE90 cmp [ecx+20h],0  (skillLevel)
//    +0x24: 0x9DCEA4 mov eax,[ecx+24h] (skillTable ptr)
// ============================================================

class CSkill
{
  public:
    uintptr_t vtable;           // 0x0000
    uint32_t skillId;           // 0x0004
    uint32_t flag;              // 0x0008  0=regular, !0=enhanced
    char pad_000C[12];          // 0x000C
    float leftCoolDown;         // 0x0018  decremented by deltaTime each frame
    uint32_t isValid;           // 0x001C  non-zero = has skill
    uint32_t skillLevel;        // 0x0020
    SkillTable *skillTable;     // 0x0024
};
static_assert(sizeof(CSkill) == 0x28);
#pragma pack(pop)

// PreTime patch size (movss instruction = 8 bytes)
inline constexpr size_t    kPreTimePatchSize  = 8;

// ============================================================
//  Helper functions
// ============================================================

uintptr_t GetSkillManagerBase();

uint32_t GetSkillCount();
CSkill  *GetCSkill(uint32_t index);

bool PatchPreTime();
bool RestorePreTime();

} // namespace SO3D
