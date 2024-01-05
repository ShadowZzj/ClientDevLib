#include "GameManager.h"
#include <Detours/build/include/detours.h>
#include <Windows/util/Process/ProcessHelper.h>
#include <spdlog/spdlog.h>
#include <vector>
#include <boost/locale.hpp>
#include "SpeedHack.h"
static const uintptr_t localPlayerOffset               = 0x65997c;
static const uintptr_t aroundPlayerOffset = 0x659978;
static const std::vector<unsigned int> aroundPlayerMultiLevelOffset = {0xc};
static const uintptr_t aroundPlayerNameOffset = 0x1410;
static const uintptr_t attackRangeOffset               = 0x28d4;
static const uintptr_t attackSpeedOffset               = 0x21c4;
static const uintptr_t skillRangeOffset                = 0x14c;
static const uintptr_t attackOffset                    = 0x27c8;
static const uintptr_t monsterIdOffset                 = 0x2628;
static const uintptr_t professionOffset                = 0x1448;
static const uintptr_t moneyOffset                     = 0x27c0;
static const uintptr_t positionXOffset                 = 0x3c;
static const uintptr_t positionYOffset                 = 0x40;
static const uintptr_t positionZOffset                 = 0x44;
static const uintptr_t nameOffset                      = 0x1410;
static const uintptr_t skillSpeedOffset                = 0x21c8;

static const std::string attackRangePattern      = "C7 83 D4 28 00 00 01 00 00 00";
static const std::string attackSpeedPattern      = "F3 0F 11 83 C4 21 00 00 3D";
static const std::string skillRangePattern       = "8B 80 4C 01 00 00 C3";
static const std::string itemNoCoolDownPattern   = "66 0F 6E 80 F8 02 00 00";
static const std::string skillNoPretimePattern   = "F3 0F 10 88 50 01 00 00";
static const std::string skillSpeedPattern       = "F3 0F 11 83 C8 21 00 00 83";
static const std::string moveSpeedPattern        = "F3 0F 11 83 A4 01 00 00 76 0A";
static const std::string moveSpeedUnlimitPattern = "C7 86 A4 01 00 00 00 00 E0 40";
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

    auto aroundPlayerListBeginAddress =
        memory.FindMultiPleLevelAddress((uintptr_t)moduleInfo.value().modBaseAddr + aroundPlayerOffset, aroundPlayerMultiLevelOffset);
    
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

        if (!memory.Read(nextPlayerAddress + 0x21d8,&nextPlayerAddress,sizeof(nextPlayerAddress)))
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
        mov eax, GameManager::attackRange 
        mov ecx, attackRangeOffset 
        mov[ebx + ecx], eax
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

        movss xmm0, GameManager::attackSpeed
        mov ecx, attackSpeedOffset 
        movss[ebx + ecx], xmm0
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
        push ecx
    }

    __asm
    {
        mov ecx, skillRangeOffset
        mov eax, [eax + ecx]
        cmp eax, 4 
        jl set_eax_to_4 
        mov eax,11 
        jmp ret1 
    set_eax_to_4 : 
        mov eax, 4
    }

    __asm
    {
    ret1:
        pop ecx
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
        movd xmm0,GameManager::itemCoolDown
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
    __asm
    {
        xorps xmm1,xmm1
        jmp skillSpeedRetAddress1
    }
}

int __declspec(naked) SkillSpeedHooked2()
{
    static float skillSpeed = 0.001f;
    __asm
    {
        push ecx
        movss xmm0, skillSpeed
        mov ecx,skillSpeedOffset
        movss [ebx + ecx],xmm0
        pop ecx
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
        movss [ebx + 0x1a4],xmm0
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
    //Speedhack::Detach();
    return FALSE;
}

void GameManager::SetSpeed(float speed)
{
    Speedhack::SetSpeed(speed);
}
