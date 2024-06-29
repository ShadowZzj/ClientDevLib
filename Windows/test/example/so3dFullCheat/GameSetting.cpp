#include "GameSetting.h"
#include "GameManager.h"
#include <General/util/File/File.h>
#include <General/util/StrUtil.h>
#include <Windows/util/Process/ProcessHelper.h>
#include <boost/filesystem.hpp>
#include <future>
#include <sstream>
#include "Messager.h"
static void PlaceHolder()
{
}
LONG NTAPI VEHhandler(struct _EXCEPTION_POINTERS *ExceptionInfo)
{
    spdlog::info("VEHHandler called");

    // 获取异常代码和发生异常的位置
    DWORD exceptionCode    = ExceptionInfo->ExceptionRecord->ExceptionCode;
    PVOID exceptionAddress = ExceptionInfo->ExceptionRecord->ExceptionAddress;

    // 打印异常代码和发生异常的位置
    spdlog::info("Exception code: 0x{:X}", exceptionCode);
    spdlog::info("Exception address: {}", exceptionAddress);
    switch (exceptionCode)
    {
    case EXCEPTION_ACCESS_VIOLATION:
        spdlog::info("Exception type: Access Violation");
        break;
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        spdlog::info("Exception type: Array Bounds Exceeded");
        break;
    case EXCEPTION_BREAKPOINT:
        spdlog::info("Exception type: Breakpoint");
        break;
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        spdlog::info("Exception type: Datatype Misalignment");
        break;
    case EXCEPTION_FLT_DENORMAL_OPERAND:
        spdlog::info("Exception type: Floating-point Denormal Operand");
        break;
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        spdlog::info("Exception type: Floating-point Divide by Zero");
        break;
    case EXCEPTION_FLT_INEXACT_RESULT:
        spdlog::info("Exception type: Floating-point Inexact Result");
        break;
    case EXCEPTION_FLT_INVALID_OPERATION:
        spdlog::info("Exception type: Floating-point Invalid Operation");
        break;
    case EXCEPTION_FLT_OVERFLOW:
        spdlog::info("Exception type: Floating-point Overflow");
        break;
    case EXCEPTION_FLT_STACK_CHECK:
        spdlog::info("Exception type: Floating-point Stack Check");
        break;
    case EXCEPTION_FLT_UNDERFLOW:
        spdlog::info("Exception type: Floating-point Underflow");
        break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        spdlog::info("Exception type: Illegal Instruction");
        break;
    case EXCEPTION_IN_PAGE_ERROR:
        spdlog::info("Exception type: In Page Error");
        break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        spdlog::info("Exception type: Integer Divide by Zero");
        break;
    case EXCEPTION_INT_OVERFLOW:
        spdlog::info("Exception type: Integer Overflow");
        break;
    case EXCEPTION_INVALID_DISPOSITION:
        spdlog::info("Exception type: Invalid Disposition");
        break;
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
        spdlog::info("Exception type: Noncontinuable Exception");
        break;
    case EXCEPTION_PRIV_INSTRUCTION:
        spdlog::info("Exception type: Privileged Instruction");
        break;
    case EXCEPTION_SINGLE_STEP:
        spdlog::info("Exception type: Single Step");
        break;
    case EXCEPTION_STACK_OVERFLOW:
        spdlog::info("Exception type: Stack Overflow");
        break;
    default:
        spdlog::info("Exception type: Unknown");
        break;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
void GameSetting::Init()
{
    ImGuiStyle &style = ImGui::GetStyle();
    auto &colors      = style.Colors;

    style.ScrollbarRounding = 0.0f;
    style.WindowRounding    = 4.0f;

    colors[ImGuiCol_ResizeGrip]        = ImColor(0, 0, 0, 0);
    colors[ImGuiCol_ResizeGripHovered] = ImColor(0, 0, 0, 0);
    colors[ImGuiCol_ResizeGripActive]  = ImColor(0, 0, 0, 0);

    colors[ImGuiCol_Button]        = ImColor(18, 18, 18, 100);
    colors[ImGuiCol_ButtonHovered] = ImColor(21, 21, 21, 100);
    colors[ImGuiCol_ButtonActive]  = ImColor(21, 21, 21, 100);

    colors[ImGuiCol_CheckMark] = ImColor(0, 189, 0, 255);

    colors[ImGuiCol_FrameBg]        = ImColor(24, 24, 24);
    colors[ImGuiCol_FrameBgHovered] = ImColor(26, 26, 26);
    colors[ImGuiCol_FrameBgActive]  = ImColor(26, 26, 26);

    // load chinese font and support 繁体中文
    ImGuiIO &io                            = ImGui::GetIO();
    boost::filesystem::path executablePath = zzj::GetDynamicLibPath(&PlaceHolder);
    boost::filesystem::path fontPath       = executablePath / "MINGLIU.TTF";
    spdlog::info("Font path: {}", fontPath.string());
}
void GameSetting::UninitImguiConfig()
{
}
static bool isTempPause = true;

bool IsCurrentGameWindowHasFocus()
{
    HWND hwnd = GetForegroundWindow();
    if (hwnd == nullptr)
    {
        return false;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0)
    {
        return false;
    }
    // get my process id
    DWORD myPid = GetCurrentProcessId();
    if (pid != myPid)
    {
        return false;
    }
    return true;
}
void AttackRange()
{
    if (ImGui::Checkbox("AttackRangeEnable", &GameManager::attackRangeEnable))
    {
        spdlog::info("AttackRangeEnable changed to {}", GameManager::attackRangeEnable);
        if (GameManager::attackRangeEnable)
        {
            gameManager.EnableAttackRange();
        }
        else
        {
            gameManager.DisableAttackRange();
        }
    }

    if (GameManager::attackRangeEnable)
    {
        // slider from 1 to 10
        ImGui::SliderInt("AttackRange", &GameManager::attackRange, 1, 10);
    }
}
void SkillRange()
{
    if (ImGui::Checkbox("SkillRangeEnable", &GameManager::skillRangeEnable))
    {
        spdlog::info("SkillRangeEnable changed to {}", GameManager::skillRangeEnable);
        if (GameManager::skillRangeEnable)
        {
            gameManager.EnableSkillRange();
        }
        else
        {
            gameManager.DisableSkillRange();
        }
    }
}
void ItemCoolDown()
{
    if (ImGui::Checkbox("ItemNoCoolDownEnable", &GameManager::itemNoCoolDownEnable))
    {
        spdlog::info("ItemNoCoolDownEnable changed to {}", GameManager::itemNoCoolDownEnable);
        if (GameManager::itemNoCoolDownEnable)
        {
            gameManager.EnableItemNoCoolDown();
        }
        else
        {
            gameManager.DisableItemNoCoolDown();
        }
    }
    if (GameManager::itemNoCoolDownEnable)
    {
        // slider from 1 to 10
        ImGui::SliderInt("ItemCoolDown", &GameManager::itemCoolDown, 1, 100);
    }
}
void SkillSpeed()
{
    if (ImGui::Checkbox("SkillSpeedEnable", &GameManager::skillSpeedEnable))
    {
        spdlog::info("SkillSpeedEnable changed to {}", GameManager::skillSpeedEnable);
        if (GameManager::skillSpeedEnable)
        {
            gameManager.EnableSkillSpeed();
        }
        else
        {
            gameManager.DisableSkillSpeed();
        }
    }
}
void SpeedHack()
{
    if (ImGui::Checkbox("SpeedHackEnable", &GameManager::speedHackEnable))
    {
        spdlog::info("SpeedHackEnable changed to {}", GameManager::speedHackEnable);
        if (GameManager::speedHackEnable)
        {
            gameManager.EnableSpeedHack();
        }
        else
        {
            gameManager.DisableSpeedHack();
        }
    }
    if (GameManager::speedHackEnable)
    {

        // slider from 1 to 10
        if (ImGui::SliderFloat("SpeedHack", &GameManager::speedHack, 1.0f, 10.0f))
        {
            gameManager.SetSpeed(GameManager::speedHack);
        }
    }
}
void FullFirePower()
{
    bool isXPressed            = GetAsyncKeyState('X');
    static auto lastTimeSwitch = std::chrono::system_clock::now();
    if (IsCurrentGameWindowHasFocus() && GetAsyncKeyState('X'))
    {
        auto now      = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTimeSwitch);
        if (duration.count() > 500)
        {
            GameManager::fireFullPowerEnabled = !GameManager::fireFullPowerEnabled;
            lastTimeSwitch                    = now;
        }
    }
    ImGui::Checkbox("FireFullPower", &GameManager::fireFullPowerEnabled);
    ImGui::InputInt("MaxCreature", &GameManager::fireFullPowerMaxCreature);
    ImGui::InputInt("Interval", &GameManager::fireFullPowerIntervalValue);
    if (!isTempPause && GameManager::fireFullPowerEnabled)
    {
        // the focus window must be the game window
        static auto lastTime = std::chrono::system_clock::now();
        auto currentTime     = std::chrono::system_clock::now();
        auto duration        = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastTime);
        if (duration.count() < GameManager::fireFullPowerIntervalValue)
            return;
        lastTime                           = currentTime;
        GameManager::CLocalUser *localUser = (GameManager::CLocalUser *)gameManager.GetLocalPlayerBase();
        if (localUser != nullptr)
        {
            auto profession = localUser->profession;
            if (profession == 6 || profession == 16) // 铁匠、爆破
            {
                gameManager.ThrowBomb();
            }
            else
            {
                spdlog::info("FireFullPower!");
                auto skills = gameManager.GetSkills();

                for (auto &skill : skills)
                {
                    if (skill.skillTable == nullptr)
                    {
                        continue;
                    }
                    if (skill.skillTable->attack == 0 || skill.skillLevel == 0 || skill.skillTable->apCost == 0 ||
                        skill.skillTable->skillRange == 0)
                        continue;

                    auto monsters = gameManager.GetMonsters(skill.skillTable->skillRange + 3);
                    std::vector<uint32_t> monsterIds;
                    for (auto &monster : monsters)
                    {
                        monsterIds.push_back(monster.creature.monsterId);
                    }
                    std::stringstream ss;
                    ss << "{";
                    for (size_t i = 0; i < monsterIds.size(); i++)
                    {
                        ss << monsterIds[i];
                        if (i != monsterIds.size() - 1)
                            ss << ", ";
                    }
                    ss << "}";
                    if (monsterIds.size() > 0)
                    {

                        spdlog::info("Ready to cast skill: {} type:{} to monsters {}", skill.skillTable->GetSkillName(),
                                     (int)skill.skillTable->skillType, ss.str());
                        gameManager.CastSkill(skill, monsters);
                    }
                }
            }
        }
    }
}

std::atomic<bool> isSelling = false;
void SellItemWrapper(uint32_t bagId)
{

    std::thread([=]() {
        if (isSelling.load())
            return;
        isSelling.store(true);
        // auto autoHuntManager = gameManager.GetAutoHuntManager();
        // if (autoHuntManager == nullptr)
        //     return;
        //
        // auto preStatus                   = autoHuntManager->status;
        // autoHuntManager->status          = GameManager::AutoHuntStatus::Stop;
        auto preAutoPickItem             = gameManager.autoPickItemEnable;
        gameManager.autoPickItemEnable   = false;
        auto preFullFirePower            = gameManager.fireFullPowerEnabled;
        gameManager.fireFullPowerEnabled = false;


        auto preDll123AutoHuntEnabledVar = gameManager.Dll123IsAutoHuntEnable();
        gameManager.Dll123SetAutoHuntEnable(false);


        Sleep(1000);
        if (gameManager.UseCashItem(GameManager::cashSellerItemName))
        {
            Sleep(500);
            gameManager.SellItem(bagId);
            Sleep(500);
            gameManager.CloseSellerGui();
        }
        gameManager.autoPickItemEnable   = preAutoPickItem;
        gameManager.fireFullPowerEnabled = preFullFirePower;
        // autoHuntManager->status          = preStatus;
        if(preDll123AutoHuntEnabledVar.has_value())
            gameManager.Dll123SetAutoHuntEnable(preDll123AutoHuntEnabledVar.value());
        isSelling.store(false);
    }).detach();
}
void SellItem()
{

    static uint32_t bagId = 24;
    ImGui::InputInt("BagId", (int *)&bagId);
    if (ImGui::Button("SellItem"))
    {
        std::lock_guard<std::mutex> guarder(GameSetting::sellerGuarderMutex);
        SellItemWrapper(bagId);
    }
    // get current time point, execute SellItem every 1 hour
    static auto lastTime = std::chrono::system_clock::now();
    ImGui::Checkbox("AutoSellItem", &GameManager::isAutoSell);
    if (!GameManager::isAutoSell)
    {
        return;
    }
    auto currentTime = std::chrono::system_clock::now();
    auto duration    = std::chrono::duration_cast<std::chrono::minutes>(currentTime - lastTime);
    int count        = 0;
    auto items       = gameManager.GetBagItems();
    for (auto &item : items)
    {
        if (item.itemTable)
            count++;
    }
    if (duration.count() >= 1 && count > 150)
    {
        std::lock_guard<std::mutex> guarder(GameSetting::sellerGuarderMutex);
        SellItemWrapper(bagId);
        lastTime = currentTime;
    }
}
void UseCashItem()
{
    static uint32_t cashId = 0;
    ImGui::InputInt("CashId", (int *)&cashId);
    if (ImGui::Button("UseCashItem"))
    {
        gameManager.UseCashItem(cashId);
    }
}
void ShowItems()
{
    if (ImGui::Button("ShowItems"))
    {
        auto items = gameManager.GetBagItems();
        spdlog::info("Items size: {}", items.size());
        for (auto &item : items)
        {
            if (item.itemTable == nullptr)
            {
                continue;
            }
            spdlog::info("ItemId {}, hpheal {}", item.itemTable->itemId, item.itemTable->hpHeal);
            spdlog::info("ItemName: {}, Description: {}", item.itemTable->GetItemName(),
                         item.itemTable->GetItemDescription());
        }

        auto cashItems = gameManager.GetCashItems();
        spdlog::info("CashItems size: {}", cashItems.size());
        for (auto &item : cashItems)
        {
            if (item.itemTable == nullptr)
            {
                continue;
            }
            spdlog::info("CashItemName: {}, Description: {}", item.itemTable->GetItemName(),
                         item.itemTable->GetItemDescription());
        }
    }
}
void ShowSkill()
{
    if (ImGui::Button("ShowSkill"))
    {
        auto skills = gameManager.GetSkills();
        spdlog::info("Skills size: {}", skills.size());
        for (auto &skill : skills)
        {
            if (skill.skillTable == nullptr)
            {
                continue;
            }
            spdlog::info("Skill id: {} Skill Level: {}  cooldown: {} SkillName:{}", skill.skillId, skill.skillLevel,
                         skill.skillTable->coolDown, skill.skillTable->GetSkillName());
            // spdlog::info("SkillName: {}", skill.skillTable->GetSkillName());
        }
    }
}
void ShowMonsters()
{
    if (ImGui::Button("ShowMonsters"))
    {
        auto monsters = gameManager.GetCreatures();
        spdlog::info("Monsters size: {}", monsters.size());

        for (auto &monster : monsters)
        {
            if (monster.creature.monsterStatTablePtr == nullptr)
            {
                continue;
            }
            if (!monster.creature.monsterStatTablePtr->IsMonster())
            {
                spdlog::info(
                    "CreatureName: {} CCreature:{:x}, MonsterTable:{:x} id: {}  index: {}  leftHealth: {} type:{}",
                    monster.creature.monsterStatTablePtr->GetName(), monster.address,
                    (uintptr_t)monster.creature.monsterStatTablePtr, monster.creature.monsterId,
                    monster.creature.monsterStatTablePtr->index, monster.creature.health,
                    monster.creature.monsterStatTablePtr->type);
                continue;
            }
            spdlog::info("MonsterName: {} CCreature:{:x}, MonsterTable:{:x} id: {}  index: {}  leftHealth: {} type:{}",
                         monster.creature.monsterStatTablePtr->GetName(), monster.address,
                         (uintptr_t)monster.creature.monsterStatTablePtr, monster.creature.monsterId,
                         monster.creature.monsterStatTablePtr->index, monster.creature.health,
                         monster.creature.monsterStatTablePtr->type);
        }
    }
}
void ShowDropItem()
{
    if (ImGui::Button("ShowDropItems"))
    {
        auto itemContainer = gameManager.GetItemContainer();
        if (itemContainer != nullptr && itemContainer->dropItemPtr)
        {
            for (GameManager::DropItem *item = itemContainer->dropItemPtr; item != nullptr; item = item->next)
            {
                if (item->itemTablePtr == nullptr)
                {
                    continue;
                }
                spdlog::info("DropItemName: {} dropId:{:x} itemId:{:x} Pos {},{},{}", item->itemTablePtr->GetItemName(),
                             item->dropId, item->itemId, item->x, item->y, item->z);
            }
        }
    }
}
#include <regex>
void AutoPickup()
{
    ImGui::Checkbox("AutoPickItem", &GameManager::autoPickItemEnable);
    static std::chrono::system_clock::time_point lastTime = std::chrono::system_clock::now();
    if (gameManager.autoPickItemEnable)
    {
        GameManager::CLocalUser *localPlayer = (GameManager::CLocalUser *)gameManager.GetLocalPlayerBase();
        if (localPlayer)
        {
            // execute every 0.5 second
            auto currentTime = std::chrono::system_clock::now();
            auto duration    = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastTime);
            if (duration.count() >= 500)
            {
                std::vector<std::string> filter;
                if (gameManager.config.find("PickItemFilter") != gameManager.config.end())
                    filter = gameManager.config["PickItemFilter"];
                lastTime           = currentTime;
                auto itemContainer = gameManager.GetItemContainer();
                if (itemContainer != nullptr && itemContainer->dropItemPtr)
                {
                    int maxPickUpCount = 5;
                    for (GameManager::DropItem *item = itemContainer->dropItemPtr; item != nullptr; item = item->next)
                    {
                        if (item->itemTablePtr == nullptr)
                        {
                            continue;
                        }
                        if (!item->canPick)
                            continue;
                        std::string itemName = item->itemTablePtr->GetItemName();
                        bool shouldContinue  = false;
                        for (auto &f : filter)
                        {
                            std::regex reg(f);
                            if (std::regex_search(itemName, reg))
                            {
                                shouldContinue = true;
                                break;
                            }
                        }
                        if (shouldContinue)
                            continue;

                        auto distance = sqrt(pow(item->x - localPlayer->x, 2) + pow(item->y - localPlayer->y, 2) +
                                             pow(item->z - localPlayer->z, 2));
                        if (distance < 4)
                        {
                            spdlog::info("PickItemName: {} dropId:{:x} itemId:{:x} distance:{} playerPos {},{},{} "
                                         "itemPos {},{},{}",
                                         itemName, item->dropId, item->itemId, distance, localPlayer->x, localPlayer->y,
                                         localPlayer->z, item->x, item->y, item->z);
                            if (maxPickUpCount == 0)
                                return;
                            gameManager.PickItem(item->dropId);
                            maxPickUpCount--;
                        }
                    }
                }
            }
        }
    }
}
void DropItem()
{
    static uint32_t bagid = 0;
    static uint32_t count = 0;
    ImGui::InputInt("BagId", (int *)&bagid);
    ImGui::InputInt("Count", (int *)&count);

    if (ImGui::Button("DropItem"))
    {
        gameManager.DropItemFunc(bagid, count);
    }
}
void MakeBomb()
{
    static bool enabled = false;
    auto skills         = gameManager.GetSkills();
    if (skills.size() == 0)
        return;
    ImGui::Checkbox("MakeBomb", &enabled);
    if (ImGui::Button("MakeBomb") || enabled)
    {
        gameManager.CastSkill(skills[0x145], {});
        auto items = gameManager.GetBagItems();
        for (auto &item : items)
        {
            if (item.itemTable == nullptr)
            {
                continue;
            }
            if (item.itemTable->GetItemName() == u8"原子彈")
            {
                gameManager.DropItemFunc(item.bagId - 0xd, 300);
            }
        }
    }
    static std::chrono::system_clock::time_point lastTime = std::chrono::system_clock::now();
    auto currentTime                                      = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastTime);
    if (duration.count() < 10)
    {
        return;
    }
    lastTime                        = currentTime;
    GameManager::CLocalUser *player = (GameManager::CLocalUser *)gameManager.GetLocalPlayerBase();
    if (player && player->profession == 16)
    {
        auto items = gameManager.GetBagItems();
        int bombId = -1;
        int count  = 0;
        for (size_t i = 0; i < items.size(); i++)
        {
            auto item = items[i];
            if (item.itemTable == nullptr)
                continue;
            if (item.itemTable->GetItemName().find("\xE5\x8E\x9F\xE5\xAD\x90\xE5\xBD\x88") !=
                std::string::npos) // 原子弹
            {
                bombId = i;
                count  = item.count;
                break;
            }
        }
        if (bombId == -1)
        {
            gameManager.CastSkill(skills[0x145], {});
            spdlog::info("no bomb to throw");
            return;
        }
        spdlog::info("bombCount: {0}", count);
        if (count < 150)
            gameManager.CastSkill(skills[0x145], {});
    }
}
void BuyItemWrapper(std::string itemName)
{
    auto autoHuntManager = gameManager.GetAutoHuntManager();
    if (autoHuntManager == nullptr)
        return;

    auto preStatus                   = autoHuntManager->status;
    autoHuntManager->status          = GameManager::AutoHuntStatus::Stop;
    auto preAutoPickItem             = gameManager.autoPickItemEnable;
    gameManager.autoPickItemEnable   = false;
    auto preFullFirePower            = gameManager.fireFullPowerEnabled;
    gameManager.fireFullPowerEnabled = false;
    std::thread([=]() {
        Sleep(1000);
        std::lock_guard<std::mutex> guarder(GameSetting::sellerGuarderMutex);
        if (gameManager.UseCashItem(GameManager::cashSellerItemName))
        {
            spdlog::info("Ready to buy item {}", itemName);
            Sleep(500);
            int buyIndex = -1;
            for (int i = 0; i < GameManager::remoteSellerItemList.size(); i++)
            {
                if (GameManager::remoteSellerItemList[i] == itemName)
                {
                    buyIndex = i;
                    break;
                }
            }
            if (buyIndex == -1)
            {
                spdlog::info("BuyItem: {} not found", itemName);
                return;
            }
            GameManager::BuyItemPacket packet;
            packet.sellerItemIndex = buyIndex;
            packet.count           = 300;
            gameManager.BuyItem(packet);
            Sleep(1000);
            gameManager.CloseSellerGui();
        }
        gameManager.autoPickItemEnable   = preAutoPickItem;
        gameManager.fireFullPowerEnabled = preFullFirePower;
        autoHuntManager->status          = preStatus;
    }).detach();
}
void BuyItem()
{
    try
    {
        static uint32_t sellerIndex = 0;
        static uint32_t count       = 0;
        ImGui::InputInt("SellerIndex", (int *)&sellerIndex);
        ImGui::InputInt("Count", (int *)&count);
        if (ImGui::Button("BuyItem"))
        {
            GameManager::BuyItemPacket packet;
            packet.sellerItemIndex = sellerIndex;
            packet.count           = count;
            if (gameManager.UseCashItem(GameManager::cashSellerItemName))
            {
                gameManager.BuyItem(packet);
                gameManager.CloseSellerGui();
            }
        }

        if (gameManager.config.find("roleConfig") == gameManager.config.end())
        {
            return;
        }
        GameManager::CLocalUser *localPlayer = (GameManager::CLocalUser *)gameManager.GetLocalPlayerBase();
        if (localPlayer == nullptr)
        {
            return;
        }
        std::string name = localPlayer->GetName();
        if (gameManager.config["roleConfig"].find(name) == gameManager.config["roleConfig"].end())
        {
            return;
        }

        auto roleConfig = gameManager.config["roleConfig"][name];
        if (roleConfig.find("buyItem") == roleConfig.end())
        {
            return;
        }

        roleConfig           = roleConfig["buyItem"];
        int interval         = roleConfig["interval"];
        auto itemList        = roleConfig["list"];
        static auto lastTime = std::chrono::system_clock::now();
        auto currentTime     = std::chrono::system_clock::now();
        auto duration        = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastTime);
        if (duration.count() < interval)
        {
            return;
        }
        lastTime = currentTime;
        for (auto &item : itemList)
        {
            std::string itemName = item["name"];
            uint32_t threshold   = item["threshold"];
            auto items           = gameManager.GetBagItems();
            uint32_t bagCount    = 0;
            for (auto &bagItem : items)
            {
                if (bagItem.itemTable == nullptr)
                {
                    continue;
                }
                if (bagItem.itemTable->GetItemName() == itemName)
                {
                    bagCount += bagItem.count;
                }
            }
            if (bagCount < threshold)
            {
                BuyItemWrapper(itemName);
                return;
            }
        }
    }
    catch (const std::exception &ex)
    {
        spdlog::info("BuyItem exception {}", ex.what());
    }
}
void DeliverLetter()
{
    if (ImGui::Button("DeliverLetter-BlueEyes"))
    {
        GameManager::DeliverLetterPacket packet;
        GameManager::NPCManager::DeliverLetter(packet);
    }
    if (ImGui::Button("DeliverLetter-LittleEngine"))
    {
        GameManager::DeliverLetterPacket packet;
        packet.pad1 = 0x6b9;
        GameManager::NPCManager::DeliverLetter(packet);
    }
}
#include <spdlog/sinks/rotating_file_sink.h>
void InitLog(const std::string &name)
{
    if (name.empty())
    {
        return;
    }
    boost::filesystem::path logPath = zzj::GetDynamicLibPath(InitLog);
    logPath /= "log";
    if (!boost::filesystem::exists(logPath))
    {
        boost::filesystem::create_directory(logPath);
    }
    logPath /= name + ".log";
    std::string logPathStr = logPath.string();
    logPathStr             = zzj::str::w2ansi(zzj::str::utf82w(logPathStr));
    auto file_logger       = spdlog::rotating_logger_mt(name, logPathStr, 10 * 1024 * 1024, 3);
    spdlog::set_default_logger(file_logger);
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::info);
}

void SaveLoginUserName(const std::string &userName)
{
    boost::filesystem::path currentPath = zzj::GetDynamicLibPath(SaveLoginUserName);
    auto loginConfigPath                = currentPath / "loginUserName.json";
    auto currentProcessId               = GetCurrentProcessId();
    if (!boost::filesystem::exists(loginConfigPath))
    {
        nlohmann::json j;
        std::ofstream o(loginConfigPath.string());
        o << j.dump(4);
        o.close();
    }
    nlohmann::json j;
    std::ifstream i(loginConfigPath.string());
    i >> j;
    i.close();
    j[userName] = currentProcessId;
    std::ofstream o(loginConfigPath.string());
    o << j.dump(4);
    o.close();
}
void GameSetting::AutoHuntHandler()
{
    if (ImGui::Button("AutoHunt"))
    {
        auto preAutoHuntEnableVar = gameManager.Dll123IsAutoHuntEnable();
        if(preAutoHuntEnableVar.has_value())
            gameManager.Dll123SetAutoHuntEnable(!preAutoHuntEnableVar.value());
    }

    if(ImGui::Button("SaveAutoHuntPos"))
    {
        GameManager::CLocalUser *localPlayer = (GameManager::CLocalUser *)gameManager.GetLocalPlayerBase();
        if (localPlayer)
        {
            int x = localPlayer->intX;
            int z = localPlayer->intZ;

            roleConfig["AutoHuntPosX"] = x;
            roleConfig["AutoHuntPosZ"] = z;
            boost::filesystem::path currentPath = zzj::GetDynamicLibPath(InitLog);
            currentPath /= localPlayer->GetName() + ".json";
            std::string fileName = currentPath.string();
            fileName             = zzj::str::w2ansi(zzj::str::utf82w(fileName));
            std::ofstream o(fileName);
            o << std::setw(4) << roleConfig << std::endl;
        }
    }

    ImGui::Checkbox("AutohuntFirmPositionEnable", &GameManager::autohuntFirmPositionEnable);
    if (GameManager::autohuntFirmPositionEnable)
    {
        if (roleConfig.find("AutoHuntPosX") != roleConfig.end() && roleConfig.find("AutoHuntPosZ") != roleConfig.end())
        {
            auto preAutohuntEnableVar = gameManager.Dll123IsAutoHuntEnable();
            if(preAutohuntEnableVar.has_value() && preAutohuntEnableVar.value())
            {
                static auto lastTime = std::chrono::system_clock::now();
                auto currentTime     = std::chrono::system_clock::now();
                auto duration        = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastTime);
                if (duration.count() < 10)
                {
                    return;
                }
                gameManager.Dll123SetAutoHuntPos(roleConfig["AutoHuntPosX"], roleConfig["AutoHuntPosZ"]);
                lastTime = currentTime;
            }

        }
        else
            ImGui::Text("AutoHuntPosX or AutoHuntPosZ not found");
    }
}
void GameSetting::AttackSpeed()
{
    if (ImGui::Checkbox("AttackSpeedEnable", &GameManager::attackSpeedEnable))
    {
        spdlog::info("AttackSpeedEnable changed to {}", GameManager::attackSpeedEnable);
        if (GameManager::attackSpeedEnable)
        {
            gameManager.EnableAttackSpeed();
        }
        else
        {
            gameManager.DisableAttackSpeed();
        }
    }
    if (roleConfig.find("AttackSpeedEnable") != roleConfig.end())
    {
        auto preAttackSpeedEnable      = GameManager::attackSpeedEnable;
        GameManager::attackSpeedEnable = roleConfig["AttackSpeedEnable"];
        if (!preAttackSpeedEnable)
        {
            gameManager.EnableAttackSpeed();
        }

        if (roleConfig.find("AttackSpeedValue") != roleConfig.end())
        {
            GameManager::attackSpeed = roleConfig["AttackSpeedValue"];
        }

        if (GameManager::speedHackEnable)
            GameManager::attackSpeed = 1.0f;
    }

    if (GameManager::attackSpeedEnable)
    {
        // slider from 0.0f to 1.0f
        ImGui::SliderFloat("AttackSpeed", &GameManager::attackSpeed, 0.0f, 1.0f);
    }
}
void GameSetting::MoveSpeedHandler()
{
    if (ImGui::Checkbox("MoveSpeedEnable", &GameManager::moveSpeedEnable))
    {
        spdlog::info("MoveSpeedEnable changed to {}", GameManager::moveSpeedEnable);
        if (GameManager::moveSpeedEnable)
        {
            gameManager.EnableMoveSpeed();
        }
        else
        {
            gameManager.DisableMoveSpeed();
        }
    }
    float maxMoveSpeed = 25.0f;
    ImGui::SliderFloat("MoveSpeed", &GameManager::moveSpeed, 1.0f, maxMoveSpeed);
    if (GameManager::speedHackEnable)
    {
        float currentMoveSpeed = GameManager::moveSpeed;
        if (GameManager::moveSpeedEnable)
        {
            currentMoveSpeed = currentMoveSpeed * GameManager::speedHack;
            if (currentMoveSpeed > maxMoveSpeed)
            {
                GameManager::moveSpeed = maxMoveSpeed / GameManager::speedHack;
            }
        }
        // slider from 1 to 10
    }
    else if (roleConfig.find("MoveSpeedEnable") != roleConfig.end())
    {
        auto preMoveSpeedEnable      = GameManager::moveSpeedEnable;
        GameManager::moveSpeedEnable = roleConfig["MoveSpeedEnable"];
        if (!preMoveSpeedEnable)
        {
            gameManager.EnableMoveSpeed();
        }

        if (roleConfig.find("MoveSpeedValue") != roleConfig.end())
        {
            GameManager::moveSpeed = roleConfig["MoveSpeedValue"];
        }
    }
}
void OpenBoxHandler()
{
    if (ImGui::Button("OpenBox"))
        gameManager.OpenSandBox();
}
void AutoLoginHandler()
{
    static std::thread autoLoginThread([]() {
        while (true)
        {
            gameManager.AutoLoginHandler();
            Sleep(1000);
        }
    });
}
boost::filesystem::path GameSetting::GetRoleRunningEnviromentPath(const std::string &name)
{
    boost::filesystem::path currentDllPath          = zzj::GetDynamicLibPath(AutoLoginHandler);
    boost::filesystem::path runningEnviromentFolder = currentDllPath / "RunningEnviroment";
    if (!boost::filesystem::exists(runningEnviromentFolder))
    {
        boost::filesystem::create_directory(runningEnviromentFolder);
    }

    boost::filesystem::path rolePath = runningEnviromentFolder / (name + ".json");
    if (!boost::filesystem::exists(rolePath))
    {
        nlohmann::json j;
        std::ofstream o(rolePath.string());
        o << j.dump(4);
        o.close();
    }

    return rolePath;
}
void GameSetting::GetReward()
{
    static auto lastTime = std::chrono::system_clock::now();
    auto currentTime     = std::chrono::system_clock::now();
    auto duration        = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastTime);
    if (duration.count() < 30)
    {
        return;
    }
    lastTime = currentTime;
    std::string currentTimeString;
    // convert currentTime to string
    std::time_t currentTimeT  = std::chrono::system_clock::to_time_t(currentTime);
    std::tm currentTimeStruct = *std::localtime(&currentTimeT);
    std::stringstream ss;
    ss << std::put_time(&currentTimeStruct, "%Y-%m-%d %H:%M:%S");
    currentTimeString                = ss.str();


    boost::filesystem::path rolePath = GetRoleRunningEnviromentPath(roleName);
    nlohmann::json j;
    std::ifstream i(rolePath.string());
    i >> j;
    i.close();

    if (!j.contains("RewardInfo"))
        j["RewardInfo"] = "";
    
    bool shouldGetReward = false;
    std::string lastTimeStr = j["RewardInfo"];
    if(lastTimeStr.empty())
        shouldGetReward = true;
    else
    {
        std::tm lastTimeStruct = {};
        std::istringstream ss2(lastTimeStr);
        ss2 >> std::get_time(&lastTimeStruct, "%Y-%m-%d %H:%M:%S");
        auto lastTimePoint = std::chrono::system_clock::from_time_t(std::mktime(&lastTimeStruct));
        auto duration2     = std::chrono::duration_cast<std::chrono::hours>(currentTime - lastTimePoint);
        if (duration2.count() > 6)
        {
            shouldGetReward = true;
        }
    }

    if(shouldGetReward)
    {
        std::thread td([j,rolePath,currentTimeString]() {
            nlohmann::json js = j;
            Sleep(10000);
            gameManager.OpenRewardAccessGui();
            Sleep(1000);
            auto rewardInfo = gameManager.GetRewardInfo(GameManager::GUIIndex::RewardAccess);
            spdlog::info("RewardInfoSize {}", rewardInfo.size());
            for (int i = 0; i < rewardInfo.size(); i++)
            {
                auto info = rewardInfo[i];
                spdlog::info("RewardInfo: {}", (int)info.status);
                if (info.status == GameManager::SingleRewardInfo::Status::CanGet)
                {
                    gameManager.GetRewardAccessReward(i + 1);
                }
            }
            Sleep(1000);
            gameManager.CloseRewardAccessGui();

            Sleep(1000);

            gameManager.OpenRewardAttenceGui();
            Sleep(1000);
            auto rewardAttdenceInfo = gameManager.GetRewardInfo(GameManager::GUIIndex::RewardAttence);
            spdlog::info("RewardAttdenceInfo Size  {}", rewardAttdenceInfo.size());
            for (int i = 0; i < rewardAttdenceInfo.size(); i++)
            {
                auto info = rewardAttdenceInfo[i];
                spdlog::info("RewardAttdenceInfo: {}", (int)info.status);
                if (info.status == GameManager::SingleRewardInfo::Status::CanGet)
                {
                    gameManager.GetRewardAttenceReward(i + 1);
                }
            }
            Sleep(1000);
            gameManager.CloseRewardAttenceGui();
            js["RewardInfo"]  = currentTimeString;
            std::ofstream o(rolePath.string());
            o << js.dump(4);
            o.close();
        });
        td.detach();
    }
}
void GameSetting::CashItemHandler()
{
    try
    {
        static auto lastTime = std::chrono::system_clock::now();
        auto currentTime     = std::chrono::system_clock::now();
        auto duration        = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastTime);
        if (duration.count() < 10)
        {
            return;
        }
        lastTime = currentTime;
        if (roleConfig.find("CashItem") != roleConfig.end())
        {
            boost::filesystem::path rolePath = GetRoleRunningEnviromentPath(roleName);
            nlohmann::json j;
            std::ifstream i(rolePath.string());
            i >> j;
            i.close();

            auto cashItem = roleConfig["CashItem"];
            std::string currentTimeString;
            // convert currentTime to string
            std::time_t currentTimeT  = std::chrono::system_clock::to_time_t(currentTime);
            std::tm currentTimeStruct = *std::localtime(&currentTimeT);
            std::stringstream ss;
            ss << std::put_time(&currentTimeStruct, "%Y-%m-%d %H:%M:%S");
            currentTimeString = ss.str();

            for (auto &cashItemInfo : cashItem)
            {
                std::string itemName = cashItemInfo["name"];
                int intervalSecond   = cashItemInfo["interval"];

                if (!j.contains("CashItemInfo"))
                    j["CashItemInfo"] = nlohmann::json::object();
                if (!j["CashItemInfo"].contains(itemName))
                {
                    gameManager.UseCashItem(itemName);
                    Sleep(100);
                    
                    j["CashItemInfo"][itemName] = currentTimeString;
                }
                else
                {
                    std::string lastTimeString = j["CashItemInfo"][itemName];
                    std::tm lastTime           = {};
                    std::istringstream ss(lastTimeString);
                    ss >> std::get_time(&lastTime, "%Y-%m-%d %H:%M:%S");
                    auto lastTimePoint = std::chrono::system_clock::from_time_t(std::mktime(&lastTime));
                    auto duration      = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastTimePoint);
                    if (duration.count() > intervalSecond)
                    {
                        gameManager.UseCashItem(itemName);
                        Sleep(100);
                        j["CashItemInfo"][itemName] = currentTimeString;
                    }
                }
            }

            std::ofstream o(rolePath.string());
            o << j.dump(4);
            o.close();
        }
    }
    catch (const std::exception &ex)
    {
        spdlog::error("CashItemHandler error with {}", ex.what());
    }
    catch (...)
    {
        spdlog::error("CashItemHandler error with unknown error");
    }
}
void Test()
{
    if (ImGui::Button("Test"))
    {
        auto currentGearItemVar = gameManager.GetCurrentGearItem();
        if (currentGearItemVar.has_value())
        {
			auto currentGearItem = currentGearItemVar.value();
			spdlog::info("Current Gear item {} Gear level {} ", currentGearItem.itemTable->GetItemName(),currentGearItem.gearLevel);
            for (auto gearInfo : currentGearItem.gearInfo)
            {
                spdlog::info("Gear Type: {} Gear value: {} Gear every {}", (int)gearInfo.type, gearInfo.value, gearInfo.every);
            }
		}
    }

    if (ImGui::Button("ChangeGear"))
    {
        gameManager.ChangeGear();
    }
        
}

void GameSetting::AutoGear()
{
    static bool autoGearEnable = false;
    ImGui::Checkbox("AutoGear", &autoGearEnable);

    static bool isThreadRunning = false;
    static std::mutex roleConfigMutex;
    static nlohmann::json funcRoleConfig;
    {
        std::lock_guard<std::mutex> guarder(roleConfigMutex);
        funcRoleConfig = roleConfig;
    }
    if (!isThreadRunning)
    {
        std::thread gearChaningThread([]() {
            do
            {
                if (!autoGearEnable)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    continue;
                }

                nlohmann::json roleConfig;
                {
                    std::lock_guard<std::mutex> guarder(roleConfigMutex);
                    roleConfig = funcRoleConfig;
                }
                try
                {
                    
                    static bool lastTimeChanged = false;
                    static std::vector<GameManager::GearInfo> lastGearInfo;
                    auto gearGoal   = roleConfig["GearGoal"];
                    int targetLevel = gearGoal["level"];
                    int targetCount       = gearGoal["count"];
                    auto goals      = gearGoal["goals"];

                    auto currentGearItemVar = gameManager.GetCurrentGearItem();
                    if (!currentGearItemVar.has_value())
                    {
                        lastTimeChanged = false;
                        lastGearInfo.clear();
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        continue;
                    }
                    auto currentGearItem = currentGearItemVar.value();

                    bool shouldChangeGear = false;
                    if (currentGearItem.gearLevel < targetLevel)
                    {
                        gameManager.ChangeGear();
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        continue;
                    }

                    if (lastTimeChanged && !lastGearInfo.empty())
                    {
						bool isSame = true;
                        for (int i = 0; i < lastGearInfo.size(); i++)
                        {
                            if (lastGearInfo[i] != currentGearItem.gearInfo[i])
                            {
								isSame = false;
								break;
							}
						}
                        if (isSame)
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds(50));
                            continue;
                        }
					}
					
                    if (!shouldChangeGear)
                    {
                        int satisfyCount = 0;
                        for (auto gearInfo : currentGearItem.gearInfo)
                        {
                            spdlog::info("Gear Type: {} Gear value: {} Gear every {}", (int)gearInfo.type,
                                         gearInfo.value, gearInfo.every);
                            bool isSatify = false;
                            for (auto &goal : goals)
                            {
                                int goalType    = goal["type"];
                                int goalValue   = goal["value"];
                                bool everyFound = goal.find("every") != goal.end();

                                bool condition1 = goalType == (int)gearInfo.type && goalValue <= gearInfo.value;

                                bool condition2 = false;
                                if (everyFound)
                                {
                                    int goalEvery = goal["every"];
                                    condition2    = gearInfo.every <= goalEvery;
                                }
                                else
                                    condition2 = true;

                                if (condition1 && condition2)
                                {
                                    isSatify = true;
                                    break;
                                }
                            }

                            if (isSatify)
                                satisfyCount++;
                        }
                        shouldChangeGear = ! (satisfyCount >= targetCount);
                    }

                    lastGearInfo.clear();
                    if (shouldChangeGear)
                    {
                        lastTimeChanged = true;
                        for (auto gearInfo : currentGearItem.gearInfo)
                        {
                            lastGearInfo.push_back(gearInfo);
                        }
                        gameManager.ChangeGear();
                    }
                    else
                        lastTimeChanged = false;
                }
                catch (const std::exception &ex)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    spdlog::error("AutoGear error with {}", ex.what());
                }
                catch (...)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    spdlog::error("AutoGear error with unknown error");
                }
            } while (1);
            });
        gearChaningThread.detach();
        isThreadRunning = true;
    }



}
void DeliverThing()
{
    if (ImGui::Button("DeliverTeeth"))
    {
        std::thread test([=]() {
            gameManager.DeliverTask(0x1d8e, 0x4ab0);
            Sleep(100);
            gameManager.DeliverTask(0x1d9b, 0x4ab0);
        });
        test.detach();
    }

    if (ImGui::Button("DeliverTree"))
    {
        std::thread test([=]() {
            gameManager.DeliverTask(0x1d94, 0x4ab0);
            Sleep(100);
            gameManager.DeliverTask(0x1d9e, 0x4ab0);
        });
        test.detach();
    }
    if (ImGui::Button("DeliverEngine"))
    {
        std::thread test([=]() {
            gameManager.DeliverTask(0x1d90, 0x4ab0);
            Sleep(100);
            gameManager.DeliverTask(0x1d9c, 0x4ab0);
        });
        test.detach();
    }
    if (ImGui::Button("DeliverHairCard"))
    {
        std::thread test([=]() {
            gameManager.DeliverTask(0x1d8c, 0x4ab0);
            Sleep(100);
            gameManager.DeliverTask(0x1d9a, 0x4ab0);
        });
        test.detach();
    }
}
void TeleportHandler()
{
    if (ImGui::Button("Teleport-Fire"))
    {
        std::thread test([=]() { gameManager.DeliverTask(0x272b, 0x4aac); });
        test.detach();
    }
    if (ImGui::Button("Teleport-Wood"))
    {
        std::thread test([=]() { gameManager.DeliverTask(0x272d, 0x4aac); });
        test.detach();
    }
    if (ImGui::Button("Teleport-Sand"))
    {
        std::thread test([=]() { gameManager.DeliverTask(0x272f, 0x4aac); });
        test.detach();
    }
}
void CheckLocalPlayer()
{
    int maxWaitSecond = 300;
    while (true)
    {
        Sleep(1000);
        GameManager::CLocalUser *localPlayer = (GameManager::CLocalUser *)gameManager.GetLocalPlayerBase();
        if (localPlayer != nullptr && localPlayer->GetName() != "")
        {
            break;
        }

        maxWaitSecond--;
        if (maxWaitSecond <= 0)
        {
            spdlog::error("CheckLocalPlayer no player");
            exit(0);
        }
    }
}
void GameSetting::RoleConfigLoader(const std::string &name)
{
    static auto lastTime = std::chrono::system_clock::now();
    auto currentTime     = std::chrono::system_clock::now();
    auto duration        = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastTime);
    if (duration.count() >= 10)
    {
        lastTime = currentTime;
        LoadRoleConfig(name);
    }
}
void CalculatorHookHandler()
{
    static bool enabled = false;
    if (ImGui::Checkbox("CalculatorMax", &enabled))
    {
        if (enabled)
            gameManager.EnableMaxCalculator();
        else
            gameManager.DisableMaxCalculator();
    }
}
void MessagerHandler()
{
    static auto lastTime = std::chrono::system_clock::now();
    auto currentTime     = std::chrono::system_clock::now();
    auto duration        = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastTime);
    if (duration.count() < 60)
    {
        return;
    }
    Messager messager;
    messager.PostInfo();

    lastTime = currentTime;
}
void GameSetting::Render(bool &open)
    {
    ImGui::SetNextWindowBgAlpha(0.2f);
    ImGui::Begin("SealCheat", &open);
    // run once
    static bool isInit = false;
    if (!isInit)
    {
        isInit = true;
        gameManager.EnablePopupWindowHook();
        gameManager.EnableCameraDistance();
        std::thread checkLocalPlayer(CheckLocalPlayer);
        checkLocalPlayer.detach();
        std::thread cardHandler([](){
            Messager messager;
            messager.CardHandler();
        });
        cardHandler.detach();
    }
    ImGui::Checkbox("HookSend", &GameManager::hookSendEnable);
    
    // AutoLoginHandler();
    GameManager::CLocalUser *localPlayer = (GameManager::CLocalUser *)gameManager.GetLocalPlayerBase();
    if (localPlayer == nullptr || localPlayer->GetName() == "")
    {
        ImGui::Text("Please login game first");
        ImGui::End();
        return;
    }
    std::string playerName  = localPlayer->GetName();
    static bool roleLogInit = false;
    if (!roleLogInit)
    {
        roleLogInit = true;
        InitLog(playerName);
        LoadRoleConfig(playerName);
        std::string loginUserName = localPlayer->loginUserName;
        SaveLoginUserName(loginUserName);
        gameManager.HookSendAndRecv();
        roleName = playerName;
        //AddVectoredExceptionHandler(1, (PVECTORED_EXCEPTION_HANDLER)VEHhandler);
    }
    if (ImGui::Button("SaveConfig"))
    {
        SaveRoleConfig(playerName);
    }
    try
    {
        // reload config every 10 seconds
        static auto lastTime = std::chrono::system_clock::now();
        auto currentTime     = std::chrono::system_clock::now();
        auto duration        = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastTime);
        if (duration.count() >= 10)
        {
            lastTime = currentTime;
            gameManager.RefreshConfig();
        }
    }
    catch (const std::exception &ex)
    {
        spdlog::error("ReloadConfig error with {}", ex.what());
    }

    static auto result = std::async(std::launch::async, []() {
        Sleep(3000);
        return 0;
    });

    if (result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    {
        ImGui::Text("Cheat Loading ......");
        ImGui::End();
        return;
    }

    ImGui::Checkbox("TempPause", &gameManager.tempPauseEnable);
    if (gameManager.tempPauseEnable)
    {
		ImGui::End();
		return;
	}
    static bool isAutoSwitch = true;
    ImGui::Checkbox("isAutoSwitch", &isAutoSwitch);
    auto aroundPlayers = gameManager.GetAroundPlayers();

    std::vector<std::string> namesAlert;
    if (gameManager.config.find("nameAlert") != gameManager.config.end())
        namesAlert = gameManager.config["nameAlert"];

    std::string localPlayerPosPrint =
        fmt::format("localPlayer pos {:.2f},{:.2f},{:.2f} hp:{} mp:{}", localPlayer->x, localPlayer->y, localPlayer->z,
                    localPlayer->GetCurrentHP(), localPlayer->GetCurrentMP());
    ImGui::Text(localPlayerPosPrint.c_str());   
    std::string moneyString = fmt::format("money: {}w", localPlayer->money / 10000);
    ImGui::Text(moneyString.c_str());
    for (auto &player : aroundPlayers)
    {
        std::string name = player.GetName();
        if (std::find(namesAlert.begin(), namesAlert.end(), name) != namesAlert.end())
        {
            std::string textStr = fmt::format(
                "{} found!   position {:.2f},{:.2f},{:.2f} distance {:.2f}", name, player.x, player.y, player.z,
                sqrt(pow(player.x - localPlayer->x, 2) + pow(player.y - localPlayer->y, 2) +
                     pow(player.z - localPlayer->z, 2)));
            ImGui::Text(textStr.c_str());
        }
    }

    // iterate thourgh all the key in config["roleConfig"], if the aroundPlayers not has the name, then add it
    std::vector<GameManager::CUser> aroundPlayersExceptMine = aroundPlayers;
    aroundPlayersExceptMine.erase(std::remove_if(aroundPlayersExceptMine.begin(), aroundPlayersExceptMine.end(),
                                                 [&](GameManager::CUser &player) {
                                                     std::string playerName = player.GetName();
                                                     for (auto &[name, roleConfig] :
                                                          gameManager.config["roleConfig"].items())
                                                     {
                                                         if (playerName == name)
                                                             return true;
                                                     }
                                                     return false;
                                                 }),
                                  aroundPlayersExceptMine.end());

    if (isAutoSwitch)
    {
        if (aroundPlayersExceptMine.size() > 0 && !isTempPause)
        {
            End();
            GameManager::skillAutoCastEnable = false;
            // GameManager::autoPickItemEnable  = false;
            isTempPause = true;
        }
        else if (aroundPlayersExceptMine.size() == 0 && isTempPause)
        {
            GameManager::attackRangeEnable   = true;
            GameManager::skillRangeEnable    = true;
            GameManager::skillSpeedEnable    = true;
            GameManager::speedHackEnable     = true;
            GameManager::skillAutoCastEnable = true;
            // GameManager::autoPickItemEnable   = true;
            isTempPause = false;
            gameManager.EnableAttackRange();
            gameManager.EnableSkillRange();
            gameManager.EnableSkillSpeed();
            gameManager.EnableSpeedHack();
        }
    }

    ImGui::Text("Around Players: %d", aroundPlayers.size());
    Test();
    AutoGear();
    MessagerHandler();
    RoleConfigLoader(playerName);
    CalculatorHookHandler();
    DeliverThing();
    OpenBoxHandler();
    CashItemHandler();
    AutoHuntHandler();
    GetReward();
    AttackRange();
    AttackSpeed();
    SkillRange();
    ItemCoolDown();
    SkillSpeed();
    MoveSpeedHandler();
    SpeedHack();
    FullFirePower();
    SellItem();
    UseCashItem();
    // ShowItems();
    // ShowSkill();
    // ShowMonsters();
    // ShowDropItem();
    AutoPickup();
    // DropItem();
    BuyItem();
    DeliverLetter();
    MakeBomb();
    ImGui::End();
}

void GameSetting::LoadRoleConfig(const std::string &name)
{
    try
    {
        boost::filesystem::path currentPath = zzj::GetDynamicLibPath(InitLog);
        currentPath /= name + ".json";

        std::string fileName = currentPath.string();
        fileName             = zzj::str::w2ansi(zzj::str::utf82w(fileName));
        std::ifstream i(fileName);
        i >> roleConfig;

        static auto firefullPowerOnceHandler = [=]() {
            if (roleConfig.find("FullFirePower") != roleConfig.end())
            {
                GameManager::fireFullPowerEnabled = roleConfig["FullFirePower"];
            }
            if (roleConfig.find("FullFirePowerVal") != roleConfig.end())
            {
                GameManager::fireFullPowerIntervalValue = roleConfig["FullFirePowerVal"];
            }
            return 0;
            }();
        if (roleConfig.find("ItemNoCoolDown") != roleConfig.end())
        {
            auto preEnable                    = GameManager::itemNoCoolDownEnable;
            GameManager::itemNoCoolDownEnable = roleConfig["ItemNoCoolDown"];
            if (preEnable == false && GameManager::itemNoCoolDownEnable)
                gameManager.EnableItemNoCoolDown();
            else if (preEnable == true && !GameManager::itemNoCoolDownEnable)
                gameManager.DisableItemNoCoolDown();
        }
        if (roleConfig.find("CoolDownValue") != roleConfig.end())
        {
            GameManager::itemCoolDown = roleConfig["CoolDownValue"];
        }
        if (roleConfig.find("SpeedHackValue") != roleConfig.end())
        {
            GameManager::speedHack = roleConfig["SpeedHackValue"];
        }
        if (roleConfig.find("AutoSell") != roleConfig.end())
        {
            GameManager::isAutoSell = roleConfig["AutoSell"];
        }
        if (roleConfig.find("AutohuntFirmPositionEnable") != roleConfig.end())
        {
            GameManager::autohuntFirmPositionEnable = roleConfig["AutohuntFirmPositionEnable"];
        }
    }
    catch (const std::exception &ex)
    {
        spdlog::error("LoadRoleConfig error {}", ex.what());
    }
    catch (...)
    {
        spdlog::error("LoadRoleConfig error");
    }
}

void GameSetting::SaveRoleConfig(const std::string &name)
{
    boost::filesystem::path currentPath = zzj::GetDynamicLibPath(InitLog);
    currentPath /= name + ".json";

    std::string fileName                     = currentPath.string();
    fileName                                 = zzj::str::w2ansi(zzj::str::utf82w(fileName));
    roleConfig["FullFirePower"]    = GameManager::fireFullPowerEnabled;
    roleConfig["FullFirePowerVal"] = GameManager::fireFullPowerIntervalValue;
    roleConfig["ItemNoCoolDown"]   = GameManager::itemNoCoolDownEnable;
    roleConfig["CoolDownValue"]    = GameManager::itemCoolDown;
    roleConfig["SpeedHackValue"]   = GameManager::speedHack;
    roleConfig["AutoSell"]         = GameManager::isAutoSell;
    roleConfig["AutohuntFirmPositionEnable"] = GameManager::autohuntFirmPositionEnable;
    std::ofstream o(fileName);
    o << std::setw(4) << roleConfig << std::endl;
}

void GameSetting::End()
{
    if (GameManager::attackRangeEnable)
    {
        GameManager::attackRangeEnable = false;
        GameManager gameManager;
        gameManager.DisableAttackRange();
    }
    if (GameManager::attackSpeedEnable)
    {
        GameManager::attackSpeedEnable = false;
        GameManager gameManager;
        gameManager.DisableAttackSpeed();
    }

    if (GameManager::skillRangeEnable)
    {
        GameManager::skillRangeEnable = false;
        GameManager gameManager;
        gameManager.DisableSkillRange();
    }

    if (GameManager::skillSpeedEnable)
    {
        GameManager::skillSpeedEnable = false;
        GameManager gameManager;
        gameManager.DisableSkillSpeed();
    }

    if (GameManager::speedHackEnable)
    {
        GameManager::speedHackEnable = false;
        GameManager gameManager;
        gameManager.DisableSpeedHack();
    }
}
