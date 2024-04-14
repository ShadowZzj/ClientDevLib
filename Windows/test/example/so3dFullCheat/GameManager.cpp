#include "GameManager.h"
#include "SpeedHack.h"
#include <Detours/build/include/detours.h>
#include <Windows/util/Process/ProcessHelper.h>
#include <boost/locale.hpp>
#include <spdlog/spdlog.h>
#include <vector>

// 搜索攻击力变换，然后找[???+0xoffset]，然后ce搜???，一级指针就是
static const uintptr_t localPlayerOffset = 0x95a784;
//// localPlayer offset
static const uintptr_t positionXOffset   = 0x3c;
static const uintptr_t positionYOffset   = 0x40;
static const uintptr_t positionZOffset   = 0x44;
static const uintptr_t moneyOffset       = 0x29b8;
static const uintptr_t attackOffset      = 0x29c0;
static const uintptr_t attackRangeOffset = 0x2acc;
static const uintptr_t attackSpeedOffset = 0x235c;
static const uintptr_t skillSpeedOffset  = 0x2360;
static const uintptr_t moveSpeedOffset   = 0x1a4;
//// end
// player是一个链表，每次看见新玩家，链表头都会变成新玩家，所以用另一个号，来回卡视野，搜索变动和不变，找到链表头，然后查看谁访问了这个地址，找到基地址
static const uintptr_t aroundPlayerOffset                           = 0x95a780;
static const uintptr_t aroundPlayerNameOffset                       = 0x1410;
static const std::vector<unsigned int> aroundPlayerMultiLevelOffset = {0x10};
static const uintptr_t nextPlayerPointerOffset                      = 0x236c;

// ItemTable offset
static const uintptr_t itemCoolDownOffset = 0x2f8;
// end
// SkillTable offset
static const uintptr_t skillRangeOffset   = 0x14c;
static const uintptr_t skillPretimeOffset = 0x150;
// end
static const std::string attackRangePattern = "89 81 cc 2a 00 00";
static const std::string attackSpeedPattern = "F3 0F 11 88 5c 23 00 00";
static const std::string moveSpeedPattern = "F3 0F 11 81 A4 01 00 00 8B 95 68 FB FF FF F3 0F 10 82 A4 01 00 00 0F 2F 05 60";
static const std::string moveSpeedUnlimitPattern = "F3 0F 11 80 A4 01 00 00";
static const std::string itemNoCoolDownPattern   = "F3 0F 2A 81 F8 02 00 00";
static const std::string skillRangePattern       = "8B 81 4C 01 00 00";
static const std::string skillNoPretimePattern = "F3 0F 10 81 50 01 00 00";
static const std::string skillSpeedPattern     = "F3 0F 11 88 60 23 00 00 F3 0F 2A 85 EC";


GameManager::GameManager()
{
}

GameManager::~GameManager()
{
}
std::vector<std::string> GameManager::GetAroundPlayersName()
{
    std::vector<std::string> aroundPlayersName;
    zzj::Process process;
    zzj::Memory memory(process);
    auto moduleInfo = memory.GetModuleInfo("SO3DPlus1.exe");
    if (!moduleInfo.has_value())
    {
        spdlog::error("moduleInfo is null");
        return {};
    }

    auto aroundPlayerListBeginAddress = memory.FindMultiPleLevelAddress(
        (uintptr_t)moduleInfo.value().modBaseAddr + aroundPlayerOffset, aroundPlayerMultiLevelOffset);

    if (aroundPlayerListBeginAddress == NULL)
    {
        return {};
    }

    auto nextPlayerAddress = aroundPlayerListBeginAddress;
    while (true)
    {
        auto nameVec = memory.ReadUntilZero(nextPlayerAddress + aroundPlayerNameOffset);
        std::string nameString(nameVec.begin(), nameVec.end());
        nameString = boost::locale::conv::to_utf<char>(nameString, "Big5");
        aroundPlayersName.push_back(nameString);

        if (!memory.Read(nextPlayerAddress + nextPlayerPointerOffset, &nextPlayerAddress, sizeof(nextPlayerAddress)))
            break;

        if (nextPlayerAddress == NULL)
            break;
    }
    return aroundPlayersName;
}

uintptr_t GetPatternMatchResult(const std::string &pattern)
{
    zzj::Process process;
    zzj::Memory memory(process);
    auto moduleInfo = memory.GetModuleInfo("SO3DPlus1.exe");
    auto matches =
        memory.PatternScan((uintptr_t)moduleInfo.value().modBaseAddr, moduleInfo.value().modBaseSize, pattern);
    if (matches.empty())
    {
        spdlog::error("matches is empty");
        return NULL;
    }

    auto match = matches[0];
    return match.address;
}

uintptr_t DetourAndGetRetAddress(uintptr_t &address, void *hooked)
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    PDETOUR_TRAMPOLINE trampoline = nullptr;
    // DetourAttachEx usage
    LONG result = DetourAttachEx(&(PVOID &)address, (PVOID)hooked, &trampoline, nullptr, nullptr);
    if (result != NO_ERROR)
    {
        spdlog::error("DetourAttachEx failed: {0}", result);
        return NULL;
    }
    DetourTransactionCommit();
    return (uintptr_t)trampoline->pbRemain;
}
static uintptr_t attackRangeHookAddress = NULL;
static void *attackRangeRetAddress;
int __declspec(naked) AttackRangeHooked()
{
    __asm
    {
        pushad
    }

    __asm
    {
        mov eax, GameManager::attackRange mov ebx, attackRangeOffset mov[ecx + ebx], eax
    }

    __asm
    {
        popad
        jmp attackRangeRetAddress
    }
}
bool GameManager::EnableAttackRange()
{
    attackRangeHookAddress = GetPatternMatchResult(attackRangePattern);
    if (attackRangeHookAddress == NULL)
    {
        spdlog::error("attackRangeHookAddress is null");
        return false;
    }
    spdlog::info("attackRangeHookAddress: {0:x}", attackRangeHookAddress);

    attackRangeRetAddress = (void *)DetourAndGetRetAddress(attackRangeHookAddress, &AttackRangeHooked);
    return true;
}

bool GameManager::DisableAttackRange()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&(PVOID &)attackRangeHookAddress, &AttackRangeHooked);
    DetourTransactionCommit();
    return true;
}

static uintptr_t attackSpeedHookAddress = NULL;
static void *attackSpeedRetAddress;
int __declspec(naked) AttackSpeedHooked()
{
    __asm
    {
        pushad
    }

    __asm
    {

        movss xmm0, GameManager::attackSpeed mov ecx, attackSpeedOffset movss[ebx + ecx], xmm0
    }

    __asm
    {
        popad
        jmp attackSpeedRetAddress
    }
}
bool GameManager::EnableAttackSpeed()
{
    attackSpeedHookAddress = GetPatternMatchResult(attackSpeedPattern);
    if (attackSpeedHookAddress == NULL)
    {
        spdlog::error("attackSpeedHookAddress is null");
        return false;
    }
    spdlog::info("attackSpeedHookAddress: {0:x}", attackSpeedHookAddress);

    attackSpeedRetAddress = (void *)DetourAndGetRetAddress(attackSpeedHookAddress, &AttackSpeedHooked);
    return true;
}

bool GameManager::DisableAttackSpeed()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&(PVOID &)attackSpeedHookAddress, &AttackSpeedHooked);
    DetourTransactionCommit();
    return true;
}

static uintptr_t skillRangeHookAddress = NULL;
static void *skillRangeRetAddress;
int __declspec(naked) SkillRangeHooked()
{
    __asm
    {
        push ebx
    }

    __asm
    {
        mov ebx, skillRangeOffset mov eax, [ecx + ebx] cmp eax, 4 jl set_eax_to_4 mov eax,
            11 jmp ret1 set_eax_to_4 : mov eax, 4
    }

    __asm
    {
    ret1:
        pop ebx
        jmp skillRangeRetAddress
    }
}
bool GameManager::EnableSkillRange()
{
    skillRangeHookAddress = GetPatternMatchResult(skillRangePattern);
    if (skillRangeHookAddress == NULL)
    {
        spdlog::error("skillRangeHookAddress is null");
        return false;
    }
    spdlog::info("skillRangeHookAddress: {0:x}", skillRangeHookAddress);

    skillRangeRetAddress = (void *)DetourAndGetRetAddress(skillRangeHookAddress, &SkillRangeHooked);
    return true;
}

bool GameManager::DisableSkillRange()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&(PVOID &)skillRangeHookAddress, &SkillRangeHooked);
    DetourTransactionCommit();
    return true;
}

static uintptr_t itemNoCoolDownHookAddress = NULL;
static void *itemNoCoolDownRetAddress;
int __declspec(naked) ItemNoCoolDownHooked()
{
    __asm
    {
        mov [ecx+itemCoolDownOffset],GameManager::itemCoolDown
        cvtsi2ss xmm0,[ecx+itemCoolDownOffset]
        jmp itemNoCoolDownRetAddress
    }
}

bool GameManager::EnableItemNoCoolDown()
{
    itemNoCoolDownHookAddress = GetPatternMatchResult(itemNoCoolDownPattern);
    if (itemNoCoolDownHookAddress == NULL)
    {
        spdlog::error("itemNoCoolDownHookAddress is null");
        return false;
    }
    spdlog::info("itemNoCoolDownHookAddress: {0:x}", itemNoCoolDownHookAddress);

    itemNoCoolDownRetAddress = (void *)DetourAndGetRetAddress(itemNoCoolDownHookAddress, &ItemNoCoolDownHooked);
    return true;
}

bool GameManager::DisableItemNoCoolDown()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&(PVOID &)itemNoCoolDownHookAddress, &ItemNoCoolDownHooked);
    DetourTransactionCommit();
    return true;
}

static uintptr_t skillSpeedHookAddress1 = NULL;
static void *skillSpeedRetAddress1;
static uintptr_t skillSpeedHookAddress2 = NULL;
static void *skillSpeedRetAddress2;

int __declspec(naked) SkillSpeedHooked1()
{
    static float pretime = 0.0001f;
    __asm
    {
        movss xmm0, pretime
        movss [edx + skillPretimeOffset],xmm0
        jmp skillSpeedRetAddress1
    }
}

int __declspec(naked) SkillSpeedHooked2()
{
    static float skillSpeed = 0.001f;
    __asm
    {
        movss xmm1, skillSpeed
        movss [eax + skillSpeedOffset],xmm1
        jmp skillSpeedRetAddress2
    }
}
bool GameManager::EnableSkillSpeed()
{
    skillSpeedHookAddress1 = GetPatternMatchResult(skillNoPretimePattern);
    if (skillSpeedHookAddress1 == NULL)
    {
        spdlog::error("skillSpeedHookAddress1 is null");
        return false;
    }
    spdlog::info("skillSpeedHookAddress1: {0:x}", skillSpeedHookAddress1);

    skillSpeedRetAddress1 = (void *)DetourAndGetRetAddress(skillSpeedHookAddress1, &SkillSpeedHooked1);

    skillSpeedHookAddress2 = GetPatternMatchResult(skillSpeedPattern);
    if (skillSpeedHookAddress2 == NULL)
    {
        spdlog::error("skillSpeedHookAddress2 is null");
        return false;
    }
    spdlog::info("skillSpeedHookAddress2: {0:x}", skillSpeedHookAddress2);

    skillSpeedRetAddress2 = (void *)DetourAndGetRetAddress(skillSpeedHookAddress2, &SkillSpeedHooked2);
    return true;
}

bool GameManager::DisableSkillSpeed()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&(PVOID &)skillSpeedHookAddress1, &SkillSpeedHooked1);
    DetourDetach(&(PVOID &)skillSpeedHookAddress2, &SkillSpeedHooked2);
    DetourTransactionCommit();
    return true;
}

static uintptr_t moveSpeedHookAddress = NULL;
static void *moveSpeedRetAddress;

static uintptr_t moveSpeedUnlimitHookAddress = NULL;
static void *moveSpeedUnlimitRetAddress;
int __declspec(naked) MoveSpeedHooked()
{
    __asm
    {
        movss xmm0,GameManager::moveSpeed
        movss [ecx + moveSpeedOffset],xmm0
        jmp moveSpeedRetAddress
    }
}

int __declspec(naked) MoveSpeedUnlimitHooked()
{
    __asm
    {
        jmp moveSpeedUnlimitRetAddress
    }
}

bool GameManager::EnableMoveSpeed()
{
    moveSpeedHookAddress = GetPatternMatchResult(moveSpeedPattern);
    if (moveSpeedHookAddress == NULL)
    {
        spdlog::error("moveSpeedHookAddress is null");
        return false;
    }
    spdlog::info("moveSpeedHookAddress: {0:x}", moveSpeedHookAddress);

    moveSpeedRetAddress = (void *)DetourAndGetRetAddress(moveSpeedHookAddress, &MoveSpeedHooked);

    moveSpeedUnlimitHookAddress = GetPatternMatchResult(moveSpeedUnlimitPattern);
    if (moveSpeedUnlimitHookAddress == NULL)
    {
        spdlog::error("moveSpeedUnlimitHookAddress is null");
        return false;
    }
    spdlog::info("moveSpeedUnlimitHookAddress: {0:x}", moveSpeedUnlimitHookAddress);

    moveSpeedUnlimitRetAddress = (void *)DetourAndGetRetAddress(moveSpeedUnlimitHookAddress, &MoveSpeedUnlimitHooked);
    return true;
}

bool GameManager::DisableMoveSpeed()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&(PVOID &)moveSpeedHookAddress, &MoveSpeedHooked);
    DetourDetach(&(PVOID &)moveSpeedUnlimitHookAddress, &MoveSpeedUnlimitHooked);
    DetourTransactionCommit();
    return true;
}

bool GameManager::EnableSpeedHack()
{
    static bool isEnabled = false;
    if (!isEnabled)
    {
        Speedhack::Setup();
        isEnabled = true;
    }
    Speedhack::SetSpeed(GameManager::speedHack);
    return TRUE;
}
bool GameManager::DisableSpeedHack()
{
    Speedhack::SetSpeed(1.0f);
    // Speedhack::Detach();
    return FALSE;
}

void GameManager::SetSpeed(float speed)
{
    Speedhack::SetSpeed(speed);
}
