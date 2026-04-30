#include "CLocalPlayer.h"
#include "../util/PatternResolver.h"
#include <spdlog/spdlog.h>
#include <cstring>

namespace SO3D
{

// 8 CalcStatus 写 + 2 TraceMove 钳位，统一 8 字节窗口（两类都是 movss [reg+1A4],xmm0）。
static const char *kMoveSpeedKeys[kMoveSpeedWriteCount] = {
    "MoveSpeedWrite1", "MoveSpeedWrite2", "MoveSpeedWrite3", "MoveSpeedWrite4",
    "MoveSpeedWrite5", "MoveSpeedWrite6", "MoveSpeedWrite7", "MoveSpeedWrite8",
    "MoveSpeedClampMounted", "MoveSpeedClampNormal",
};
static BYTE s_moveSpeedOrig[kMoveSpeedWriteCount][kPatchSize] = {};
static BYTE s_attackSpeedOrig[kPatchSize]  = {};
static BYTE s_skillSpeedOrig[kPatchSize]   = {};
static BYTE s_animFlagOrig[kAnimFlagJzSize] = {};

static bool s_moveSpeedPatched   = false;
static bool s_attackSpeedPatched = false;
static bool s_skillSpeedPatched  = false;
static bool s_animFlagPatched    = false;

// ============================================================
//  Base helpers
// ============================================================

CLocalUser *GetLocalPlayer()
{
    auto addr = PatternResolver::Get("LocalPlayerPtr");
    if (!addr) return nullptr;
    auto ptr = *reinterpret_cast<uintptr_t *>(addr);
    if (ptr == 0 || ptr == 0xFFFFFFFF) return nullptr;
    return reinterpret_cast<CLocalUser *>(ptr);
}

// ============================================================
//  NOP / Restore helpers
// ============================================================

static bool NopBytes(uintptr_t addr, size_t len, BYTE *backup)
{
    DWORD oldProt = 0;
    if (!VirtualProtect(reinterpret_cast<void *>(addr), len, PAGE_EXECUTE_READWRITE, &oldProt))
    {
        spdlog::error("VirtualProtect failed at {:x}", addr);
        return false;
    }
    memcpy(backup, reinterpret_cast<void *>(addr), len);
    memset(reinterpret_cast<void *>(addr), 0x90, len);
    VirtualProtect(reinterpret_cast<void *>(addr), len, oldProt, &oldProt);
    return true;
}

static bool RestoreBytes(uintptr_t addr, size_t len, const BYTE *backup)
{
    DWORD oldProt = 0;
    if (!VirtualProtect(reinterpret_cast<void *>(addr), len, PAGE_EXECUTE_READWRITE, &oldProt))
        return false;
    memcpy(reinterpret_cast<void *>(addr), backup, len);
    VirtualProtect(reinterpret_cast<void *>(addr), len, oldProt, &oldProt);
    return true;
}

// ============================================================
//  Patch / Restore
// ============================================================

bool PatchMoveSpeed()
{
    if (s_moveSpeedPatched) return true;

    bool allOk = true;
    for (size_t i = 0; i < kMoveSpeedWriteCount; ++i)
    {
        auto addr = PatternResolver::Get(kMoveSpeedKeys[i]);
        if (!addr)
        {
            spdlog::error("MoveSpeed: {} unresolved", kMoveSpeedKeys[i]);
            allOk = false;
            continue;
        }
        if (NopBytes(addr, kPatchSize, s_moveSpeedOrig[i]))
            spdlog::info("MoveSpeed NOP {} @ {:x}", kMoveSpeedKeys[i], addr);
        else
            allOk = false;
    }
    s_moveSpeedPatched = allOk;
    return allOk;
}

bool RestoreMoveSpeed()
{
    if (!s_moveSpeedPatched) return true;

    bool allOk = true;
    for (size_t i = 0; i < kMoveSpeedWriteCount; ++i)
    {
        auto addr = PatternResolver::Get(kMoveSpeedKeys[i]);
        if (!addr)
        {
            allOk = false;
            continue;
        }
        if (!RestoreBytes(addr, kPatchSize, s_moveSpeedOrig[i]))
            allOk = false;
    }
    if (allOk)
    {
        s_moveSpeedPatched = false;
        spdlog::info("MoveSpeed restored");
    }
    return allOk;
}

bool PatchAttackSpeed()
{
    if (s_attackSpeedPatched) return true;
    auto addr = PatternResolver::Get("AttackSpeedWrite");
    if (!addr) return false;
    s_attackSpeedPatched = NopBytes(addr, kPatchSize, s_attackSpeedOrig);
    if (s_attackSpeedPatched)
        spdlog::info("AttackSpeed patched (NOP {:x})", addr);
    return s_attackSpeedPatched;
}

bool RestoreAttackSpeed()
{
    if (!s_attackSpeedPatched) return true;
    auto addr = PatternResolver::Get("AttackSpeedWrite");
    if (!addr) return false;
    s_attackSpeedPatched = !RestoreBytes(addr, kPatchSize, s_attackSpeedOrig);
    if (!s_attackSpeedPatched)
        spdlog::info("AttackSpeed restored");
    return !s_attackSpeedPatched;
}

bool PatchSkillSpeed()
{
    if (s_skillSpeedPatched) return true;
    auto addr = PatternResolver::Get("SkillSpeedWrite");
    if (!addr) return false;
    s_skillSpeedPatched = NopBytes(addr, kPatchSize, s_skillSpeedOrig);
    if (s_skillSpeedPatched)
        spdlog::info("SkillSpeed patched (NOP {:x})", addr);
    return s_skillSpeedPatched;
}

bool RestoreSkillSpeed()
{
    if (!s_skillSpeedPatched) return true;
    auto addr = PatternResolver::Get("SkillSpeedWrite");
    if (!addr) return false;
    s_skillSpeedPatched = !RestoreBytes(addr, kPatchSize, s_skillSpeedOrig);
    if (!s_skillSpeedPatched)
        spdlog::info("SkillSpeed restored");
    return !s_skillSpeedPatched;
}

bool PatchAnimFlag()
{
    if (s_animFlagPatched) return true;
    auto addr = PatternResolver::Get("AnimFlagJz");
    if (!addr) return false;
    s_animFlagPatched = NopBytes(addr, kAnimFlagJzSize, s_animFlagOrig);
    if (s_animFlagPatched)
        spdlog::info("AnimFlag patched (NOP jz at {:x}, {} bytes)", addr, kAnimFlagJzSize);
    return s_animFlagPatched;
}

bool RestoreAnimFlag()
{
    if (!s_animFlagPatched) return true;
    auto addr = PatternResolver::Get("AnimFlagJz");
    if (!addr) return false;
    s_animFlagPatched = !RestoreBytes(addr, kAnimFlagJzSize, s_animFlagOrig);
    if (!s_animFlagPatched)
        spdlog::info("AnimFlag restored");
    return !s_animFlagPatched;
}

} // namespace SO3D
