#include "GameSetting.h"
#include "GameManager.h"
#include <General/util/File/File.h>
#include <General/util/StrUtil.h>
#include <Windows/util/Process/ProcessHelper.h>
#include <boost/filesystem.hpp>
#include <future>
#include <sstream>
static void PlaceHolder()
{
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
void AttackSpeed()
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

    if (GameManager::attackSpeedEnable)
    {
        // slider from 0.0f to 1.0f
        ImGui::SliderFloat("AttackSpeed", &GameManager::attackSpeed, 0.0f, 1.0f);
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
void MoveSpeed()
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

    if (GameManager::moveSpeedEnable)
    {
        float currentMoveSpeed = GameManager::moveSpeed;
        if (GameManager::speedHackEnable)
        {
            currentMoveSpeed = currentMoveSpeed * GameManager::speedHack;
            if (currentMoveSpeed > 10.0f)
            {
                GameManager::moveSpeed = 10.0f / GameManager::speedHack;
            }
        }
        // slider from 1 to 10
        ImGui::SliderFloat("MoveSpeed", &GameManager::moveSpeed, 1.0f, 10.0f);
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
    bool isXPressed = GetAsyncKeyState('X');
    ImGui::Checkbox("FireFullPower", &GameManager::fireFullPowerEnabled);
    ImGui::InputInt("Interval", &GameManager::fireFullPowerIntervalValue);
    if (!isTempPause && ((IsCurrentGameWindowHasFocus() && isXPressed) || GameManager::fireFullPowerEnabled))
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
void SellItemWrapper(uint32_t bagId)
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
    auto dll123BaseAddr = GameManager::GetModuleBaseAddress("123.dll");
    zzj::Process thisProcess;
    zzj::Memory memory(thisProcess);
    bool preDll123AutoHuntEnabled = false;
    if (dll123BaseAddr != NULL)
    {
        memory.Read(dll123BaseAddr + 0x185284, &preDll123AutoHuntEnabled, 1);
        memory.Write(dll123BaseAddr + 0x185284, {0});
    }
    
    std::thread([=]() {
        zzj::Process thisProcess;
        zzj::Memory memory(thisProcess);
        Sleep(1000);
        if (gameManager.UseCashItem(GameManager::cashSellerItemName))
        {
            Sleep(1000);
            gameManager.SellItem(bagId);
            Sleep(1000);
            gameManager.CloseSellerGui();
        }
        gameManager.autoPickItemEnable   = preAutoPickItem;
        gameManager.fireFullPowerEnabled = preFullFirePower;
        autoHuntManager->status          = preStatus;
        memory.Write(dll123BaseAddr + 0x185284, {preDll123AutoHuntEnabled});
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
    auto duration    = std::chrono::duration_cast<std::chrono::hours>(currentTime - lastTime);
    if (duration.count() >= 1)
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
                        for(auto& f: filter)
                        {
                            std::regex reg(f);
                            if(std::regex_search(itemName, reg))
                            {
                                shouldContinue = true;
                                break;
                            }
                        }
                        if(shouldContinue)
                            continue;

                        auto distance =
                            sqrt(pow(item->x - localPlayer->x, 2) + pow(item->y - localPlayer->y, 2) +
                                             pow(item->z - localPlayer->z, 2));
                        if (distance < 4)
                        {
                            spdlog::info("PickItemName: {} dropId:{:x} itemId:{:x} distance:{} playerPos {},{},{} "
                                         "itemPos {},{},{}",
                                         itemName, item->dropId, item->itemId, distance,
                                         localPlayer->x, localPlayer->y, localPlayer->z, item->x, item->y, item->z);
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
void GetReward()
{
    static auto lastTime = std::chrono::system_clock::now();
    static bool firstTime = true;
    auto currentTime     = std::chrono::system_clock::now();
    auto duration        = std::chrono::duration_cast<std::chrono::hours>(currentTime - lastTime);


    if (ImGui::Button("GetReward") || firstTime || duration.count() > 6)
    {
        lastTime = currentTime;
        bool firstTimeTemp = firstTime;
        std::thread td([firstTimeTemp]() {
            if (firstTimeTemp)
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
                     gameManager.GetRewardAccessReward(i+1);
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
        });
        td.detach();
    }
    firstTime = false;

}
void SaveLoginUserName(const std::string& userName)
{
    boost::filesystem::path currentPath = zzj::GetDynamicLibPath(SaveLoginUserName);
    auto loginConfigPath = currentPath / "loginUserName.json";
    auto currentProcessId = GetCurrentProcessId();
    if(!boost::filesystem::exists(loginConfigPath))
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
void AutoHuntHandler()
{
    if (ImGui::Button("AutoHunt"))
    {
        zzj::Process thisProcess;
        zzj::Memory memory(thisProcess);
        auto dll123BaseAddr           = GameManager::GetModuleBaseAddress("123.dll");
        bool preDll123AutoHuntEnabled = false;
        if (dll123BaseAddr != NULL)
        {
            memory.Read(dll123BaseAddr + 0x185284, &preDll123AutoHuntEnabled, 1);
            memory.Write(dll123BaseAddr + 0x185284, {!preDll123AutoHuntEnabled});
        }
    }
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
        InitLog("default");
        gameManager.EnablePopupWindowHook();
        gameManager.HookSendAndRecv();
    }
    ImGui::Checkbox("HookSend", &GameManager::hookSendEnable);
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
    std::vector<GameManager::CLocalUser> aroundPlayersExceptMine = aroundPlayers;
    aroundPlayersExceptMine.erase(std::remove_if(aroundPlayersExceptMine.begin(), aroundPlayersExceptMine.end(),
                                                 [&](GameManager::CLocalUser &player) {
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
            GameManager::attackSpeedEnable   = true;
            GameManager::skillRangeEnable    = true;
            GameManager::skillSpeedEnable    = true;
            GameManager::moveSpeedEnable     = true;
            GameManager::speedHackEnable     = true;
            GameManager::skillAutoCastEnable = true;
            // GameManager::autoPickItemEnable   = true;
            isTempPause = false;
            gameManager.EnableMoveSpeed();
            gameManager.EnableAttackSpeed();
            gameManager.EnableAttackRange();
            gameManager.EnableSkillRange();
            gameManager.EnableSkillSpeed();
            gameManager.EnableSpeedHack();
        }
    }
    ImGui::Text("Around Players: %d", aroundPlayers.size());
    AutoHuntHandler();
    GetReward();
    AttackRange();
    AttackSpeed();
    SkillRange();
    ItemCoolDown();
    SkillSpeed();
    MoveSpeed();
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

        if (roleConfig.find("FullFirePower") != roleConfig.end())
        {
            GameManager::fireFullPowerEnabled = roleConfig["FullFirePower"];
        }
        if (roleConfig.find("FullFirePowerVal") != roleConfig.end())
        {
            GameManager::fireFullPowerIntervalValue = roleConfig["FullFirePowerVal"];
        }
        if (roleConfig.find("ItemNoCoolDown") != roleConfig.end())
        {
            GameManager::itemNoCoolDownEnable = roleConfig["ItemNoCoolDown"];
            if (GameManager::itemNoCoolDownEnable)
                gameManager.EnableItemNoCoolDown();
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
    roleConfig["FullFirePower"]    = GameManager::fireFullPowerEnabled;
    roleConfig["FullFirePowerVal"] = GameManager::fireFullPowerIntervalValue;
    roleConfig["ItemNoCoolDown"]   = GameManager::itemNoCoolDownEnable;
    roleConfig["CoolDownValue"]    = GameManager::itemCoolDown;
    roleConfig["SpeedHackValue"]   = GameManager::speedHack;
    roleConfig["AutoSell"]         = GameManager::isAutoSell;
    std::ofstream o(currentPath.string());
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

    if (GameManager::moveSpeedEnable)
    {
        GameManager::moveSpeedEnable = false;
        GameManager gameManager;
        gameManager.DisableMoveSpeed();
    }

    if (GameManager::speedHackEnable)
    {
        GameManager::speedHackEnable = false;
        GameManager gameManager;
        gameManager.DisableSpeedHack();
    }
}
