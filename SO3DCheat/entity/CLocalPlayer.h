#pragma once
#include "CUser.h"

namespace SO3D
{

// ============================================================
//  CLocalUser : public CUser
//
//  Allocation: 0x9D38 bytes (single instance, g_pLocalPlayer)
//  CUser base:          0x0000 ~ 0x6AFF  (sizeof(CUser) == 0x6B00)
//  CLocalUser-specific: 0x6B00 ~ 0x9D37
// ============================================================

#pragma pack(push, 4)
class CLocalUser : public CUser
{
  public:
    char pad_6B00[0x2D4];               // 0x6B00
    int32_t cooldownReductionStat;      // 0x6DD4
    char pad_6DD8[0x2F60];              // 0x6DD8 → total 0x9D38
};
#pragma pack(pop)

static_assert(offsetof(CLocalUser, cooldownReductionStat) == 0x6DD4);
static_assert(sizeof(CLocalUser) == 0x9D38);

// Patch size constants
inline constexpr size_t kAnimFlagJzSize    = 6;
inline constexpr size_t kPatchSize         = 8;

// MoveSpeed 写入站总数：8 个 CalcStatus 基础写 + 2 个 TraceMove 钳位。
// 仅 NOP CalcStatus 不够 —— TraceMove 每帧都会把 speed 拉回 7.0(普通)/10.0(骑乘)，
// 这就是为什么旧版只 NOP 2 处时 moveSpeed 上限永远是 7。
inline constexpr size_t kMoveSpeedWriteCount = 10;

// ============================================================
//  Helper functions
// ============================================================
CLocalUser *GetLocalPlayer();

bool PatchMoveSpeed();
bool RestoreMoveSpeed();
bool PatchAttackSpeed();
bool RestoreAttackSpeed();
bool PatchSkillSpeed();
bool RestoreSkillSpeed();
bool PatchAnimFlag();
bool RestoreAnimFlag();

} // namespace SO3D
