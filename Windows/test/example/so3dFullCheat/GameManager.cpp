#include <Ws2spi.h>
#include "GameManager.h"
#include "SpeedHack.h"
#include <Detours/build/include/detours.h>
#include <General/util/StrUtil.h>
#include <Windows/util/Process/ProcessHelper.h>
#include <boost/locale.hpp>
#include <cstddef>
#include <spdlog/spdlog.h>
#include <vector>
#include <json.hpp>
#include <fstream>
#include <boost/filesystem.hpp>
#include <General/util/File/File.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
GameManager gameManager;
static void PlaceHolder()
{
}
void GameManager::RefreshConfig()
{
    boost::filesystem::path currentPath = zzj::GetDynamicLibPath(PlaceHolder);
    currentPath /= "config.json";
    std::ifstream i(currentPath.string());
    i >> config;
}
void GameManager::SaveConfig()
{
    boost::filesystem::path currentPath = zzj::GetDynamicLibPath(PlaceHolder);
    currentPath /= "config.json";
    std::ofstream o(currentPath.string());
    o << std::setw(4) << config << std::endl;
}
GameManager::GameManager()
{
    RefreshConfig();
}

GameManager::~GameManager()
{
}

void GameManager::SellItem(unsigned int beginBagId)
{
    auto bagItems = GetBagItems();
    for (unsigned int i = beginBagId; i < bagItems.size(); i++)
    {
        auto item = bagItems[i];
        if (item.itemTable == nullptr)
            continue;
        if (item.itemTable->sellMoney == 0)
            continue;
        std::string itemName = item.itemTable->GetItemName();
        std::vector<std::string> filterList = config["sellItemWhiteList"];

        bool isWhiteList = false;
        for (const auto& filter : filterList)
        {
            if (itemName.find(filter) != std::string::npos)
            {
                isWhiteList = true;
                break;
            }
		}
        if (isWhiteList)
        {
            spdlog::info("Item is in whitelist: {0}", itemName);
            continue;
        }
        SellItem(i, item.count);
        Sleep(100);
    }
}
uintptr_t GameManager::GetModuleBaseAddress(const std::string &moduleName)
{
    static std::mutex mutex;
    static std::map<std::string, uintptr_t> moduleBaseAddressMap;
    std::lock_guard<std::mutex> lock(mutex);
    if (moduleBaseAddressMap.find(moduleName) != moduleBaseAddressMap.end())
    {
        return moduleBaseAddressMap[moduleName];
    }
    zzj::Process process;
    zzj::Memory memory(process);
    auto moduleInfo = memory.GetModuleInfo(moduleName);
    if (!moduleInfo.has_value())
    {
        spdlog::error("moduleInfo is null");
        return NULL;
    }
    moduleBaseAddressMap[moduleName] = (uintptr_t)moduleInfo.value().modBaseAddr;
    return (uintptr_t)moduleInfo.value().modBaseAddr;
}
int GetFirstNullPos(const std::vector<GameManager::Item> items)
{
    int ret = -1;
    for (int bagPos = 0; bagPos < items.size(); bagPos++)
    {
        auto item = items[bagPos];
        if (item.itemTable == nullptr)
        {
            ret = bagPos;
            return ret;
        }
    }
    return ret;
}
void GameManager::OpenSandBoxImp(const std::vector<GameManager::Item>& items,int bagPos)
{
    zzj::Process process;
    zzj::Memory memory(process);
    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return;
    }
    uintptr_t gameClient = NULL;
    auto res             = memory.Read(baseAddr + gameClientOffset, &gameClient, sizeof(gameClient));
    if (!res)
    {
        spdlog::error("gameClient is null");
        return;
    }
    auto callAddr = baseAddr + rawSendPackageOffset;

    char openBoxPacket[0xc]     = {0};
    openBoxPacket[0]            = 0xc;
    *(DWORD *)&openBoxPacket[4] = 0x646af;
    *(DWORD *)&openBoxPacket[8] = bagPos + 0xd;
    __asm
        {
	    pushad

	    mov ecx,gameClient
        push 0xc
        lea eax, openBoxPacket
        push eax
	    call callAddr

        popad
        }
    spdlog::info("OpenBox first step done");
    Sleep(200);

    //int firstEmptyBagPos = GetFirstNullPos(items);
    //if (firstEmptyBagPos == -1)
    //{
	//	spdlog::info("OpenBox GetFirstNullPos return -1");
	//	return;
	//}
   //char getItemPacket[0x14]{0};
   //getItemPacket[0]              = 0x14;
   //*(DWORD *)&getItemPacket[4]   = 0x646b1;
   //*(DWORD *)&getItemPacket[8]   = bagPos + 0xd;
   //*(DWORD *)&getItemPacket[0xc] = 0xD + firstEmptyBagPos;
   //
   //__asm
   //{
	//    pushad
   //
	//    mov ecx,gameClient
   //    push 0x14
   //    lea eax, getItemPacket
   //    push eax
	//    call callAddr
   //
   //    popad
   //}
   //spdlog::info("OpenBox second step done");
}
void GameManager::OpenSandBox()
{
    std::thread t([this]() { 

        do
        {

            auto items                          = GetBagItems();
            std::vector<std::string> filterList = {u8"必中一般寶箱",       u8"必中銀寶箱",
                                                   u8"必中金寶箱",         u8"必中一般寶箱（開啟）",
                                                   u8"必中銀寶箱（開啟）", u8"必中金寶箱（開啟）"};
            int targetPos                       = -1;
            for (int bagPos = 0; bagPos < items.size(); bagPos++)
            {
                auto item = items[bagPos];
                if (item.itemTable == nullptr)
                    continue;
                std::string itemName = item.itemTable->GetItemName();
                if (std::find(filterList.begin(), filterList.end(), itemName) == filterList.end())
                {
                    spdlog::info("Item is not in filterlist: {0}", itemName);
                    continue;
                }
                targetPos = bagPos;
                break;
            }

            if (targetPos == -1)
            {
                spdlog::info("OpenSandBox done!");
                break;
            }
            spdlog::info("OpenSandBox targetPos: {:x}", targetPos);
            OpenSandBoxImp(items, targetPos);
            Sleep(20000);

        } while (true);
	});
    t.detach();
}

size_t GetModuleSize(const std::string &moduleName)
{
    zzj::Process process;
    zzj::Memory memory(process);
    auto moduleInfo = memory.GetModuleInfo(moduleName);
    if (!moduleInfo.has_value())
    {
        spdlog::error("moduleInfo is null");
        return NULL;
    }
    return moduleInfo.value().modBaseSize;
}
void GameManager::SellItem(unsigned int bagId, unsigned int count)
{
    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return;
    }

    auto sellItemFuncAddr = baseAddr + sellItemFuncAddressOffset;
    int param[3];
    param[0] = bagId + 13;
    param[1] = count;
    // param[2] = 0x4f41;//狮子城摊贩
    param[2] = 0xfecd2408;

    __asm
    {
        pushad
        push 0xC
        lea eax, param
        push eax
        push 0x6458D
        call sellItemFuncAddr
        popad
    }
}
void GameManager::BuyItem(GameManager::BuyItemPacket packet)
{
    zzj::Process process;
    zzj::Memory memory(process);
    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return;
    }
    uintptr_t gameClient = NULL;
    auto res             = memory.Read(baseAddr + gameClientOffset, &gameClient, sizeof(gameClient));
    if (!res)
    {
        spdlog::error("gameClient is null");
        return;
    }

    auto callAddr = baseAddr + sellItemFuncAddressOffset;
    size_t sizehh = sizeof(packet);
    __asm
    {
        pushad

        mov ecx, gameClient
        push sizehh
        lea eax, packet
        push eax
        push 0x6458C
        call callAddr

        popad
    }
}
void GameManager::AutoLoginHandler()
{

    zzj::Process process;
    zzj::Memory memory(process);

    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return;
    }
    static bool isRoleChoose = false;
    int currentGameUIStatus;
    memory.Read(baseAddr + 0x8b8cc4, &currentGameUIStatus, sizeof(currentGameUIStatus));
    switch (currentGameUIStatus)
    {
    case 2:
        ChooseRole(1);
        isRoleChoose = true;
        Sleep(5000);
        break;
    case 9:
        ChooseServer(1);
        Sleep(5000);
        break;
    case 1:
        Login("zhangyitong1", "960430");
        Sleep(5000);
        break;
    default:
        break;
    }
}
void GameManager::Login(const std::string &userName, const std::string &password)
{
    zzj::Process process;
    zzj::Memory memory(process);

    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return;
    }
    CLocalUser *localUser = (CLocalUser *)GetLocalPlayerBase();
    if (localUser == nullptr)
    {
		spdlog::error("localUser is null");
		return;
	}
    uintptr_t arg0             = NULL;
    memory.Read(baseAddr + mapFindValueFirstArgOffset, &arg0, sizeof(arg0));

    auto mapFindValueFuncAddr = baseAddr + mapFindValueFunctionOffset;
    uintptr_t userNameAddress = NULL;
    uintptr_t passwordAddress  = NULL;

    const char* id_input = "id_input";
    const char* pass_input = "pass_input";
    __asm
    {
        mov ecx,arg0
        push id_input
        call mapFindValueFuncAddr
        add eax,0x68
        mov userNameAddress, eax

        mov ecx,arg0
        push pass_input
        call mapFindValueFuncAddr
        add eax,0x68
        mov passwordAddress, eax
    }

    memcpy((void*)userNameAddress, userName.c_str(), userName.size());
    //trailing zero
    ((char*)userNameAddress)[userName.size()] = 0;
    memcpy((void*)passwordAddress, password.c_str(), password.size());
    //trailing zero
	((char*)passwordAddress)[password.size()] = 0;

    auto funcAddr = baseAddr + loginFunctionOffset;
    __asm
    {
        push 1
        push 0
        call funcAddr
    }
}
void GameManager::ChooseServer(int channel)
{
    zzj::Process process;
    zzj::Memory memory(process);

    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return;
    }

    uintptr_t loginClient = NULL;
    auto res             = memory.Read(baseAddr + loginClientOffset, &loginClient, sizeof(loginClient));
    if (!res)
    {
		spdlog::error("loginClient is null");
		return;
	}
    auto funcAddr = baseAddr + generalSendPackageOffset;
    __asm
    {
        pushad
		push channel
		push 2
		push 0x1b19b
		mov ecx,loginClient
		call funcAddr
		popad
	
    }
    int server = 2;
    memory.Write(baseAddr + gChannelOffset, &channel, sizeof(channel));
    memory.Write(baseAddr + gServerOffset, &server, sizeof(server));
    server = 1;
    memory.Write(baseAddr + gServerMinus1Offset, &server, sizeof(server));

}
void GameManager::ChooseRole(int index)
{
    zzj::Process process;
    zzj::Memory memory(process);

    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return;
    }
    uintptr_t loginClient = NULL;
    auto res              = memory.Read(baseAddr + loginClientOffset, &loginClient, sizeof(loginClient));
    if (!res)
    {
        spdlog::error("loginClient is null");
        return;
    }

    static char buffer[0x18] = {0};
    RtlZeroMemory(buffer, sizeof(buffer));

    int server;
    int channel;
    int serverMinus1;
    memory.Read(baseAddr + gServerOffset, &server, sizeof(server));
    memory.Read(baseAddr + gChannelOffset, &channel, sizeof(channel));
    memory.Read(baseAddr + gServerMinus1Offset, &serverMinus1, sizeof(serverMinus1));
    *(DWORD *)&buffer[0] = server;
    *(DWORD *)&buffer[4] = channel;

    char tmp[0x10] = {0};
    memory.Read(baseAddr + gRolesBeginOffset + 0x2810*(index-1), tmp, sizeof(tmp));
    memcpy(buffer + 8, tmp, sizeof(tmp));

    spdlog::info("ChooseRole: server {} channel {} name {}", server, channel, tmp);

    int tmpVal                             = 1;
    memory.Write((uintptr_t)0xd5cfa4,&tmpVal,sizeof(tmpVal));
    auto funcAddr = baseAddr + skillSendPackageOffset;
    __asm
    {
        pushad
		mov ecx, loginClient
		push 0x18
		lea eax, buffer
		push eax
		push 0x1b19e
		call funcAddr
		popad
    }

    tmpVal = serverMinus1 - 1;
    memory.Write(0xd4c75c, &tmpVal, sizeof(tmpVal));

    tmpVal = channel - 1;
    memory.Write(0xd4c760, &tmpVal, sizeof(tmpVal));

    tmpVal = 1;
    memory.Write(0xd5cdf0, &tmpVal, sizeof(tmpVal));
}
void GameManager::OpenRewardAccessGui()
{
    zzj::Process process;
    zzj::Memory memory(process);

    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return;
    }
    auto funcAddr = baseAddr + 0x32bdf0;
    __asm
    {
        push 1
        call funcAddr
    }
}

GameManager::CMenuContainerEx *GameManager::GetMenuContainer(GUIIndex index)
{
    zzj::Process process;
    zzj::Memory memory(process);
    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return {};
    }
    auto callAddr  = baseAddr + FindGuiWithIndexFuncOffset;
    auto ecxVal    = baseAddr + guiIndexerOffset;
    int guiIndex   = index;
    GUIStruct *gui = nullptr;
    __asm
    {
        pushad
        mov edx,ecxVal
        mov ecx, [edx]
        lea ebx, guiIndex
        push ebx
        call callAddr
        mov gui, eax
        popad
    }

    if (gui == nullptr)
    {
        spdlog::error("gui is null");
        return {};
    }
    spdlog::info("gui: {0:x}", (uintptr_t)gui);
    GUIStruct *iter        = gui;
    CMenuContainerEx *menu = iter->menu;
    return menu;
}

std::vector<GameManager::SingleRewardInfo> GameManager::GetRewardInfo(GUIIndex rewardGuiType)
{
    std::vector<SingleRewardInfo> ret;
    CMenuContainerEx *menu = GetMenuContainer(rewardGuiType);

    //find reward menu
    if (rewardGuiType == GUIIndex::RewardAccess)
    {
        CRewardAccessDialog *rewardMenu = (CRewardAccessDialog *)menu;
        if (rewardMenu->rewardInfoTable)
        {
            auto rewardInfoTable = rewardMenu->rewardInfoTable;
            auto itemCount       = sizeof(rewardInfoTable->rewardInfo) / sizeof(rewardInfoTable->rewardInfo[0]);
            for (int i = 0; i < itemCount; i++)
            {
                auto rewardInfo = rewardInfoTable->rewardInfo[i];
                if (rewardInfo != nullptr)
                    ret.push_back(*rewardInfo);
            }
        }
    }
    else if (rewardGuiType == GUIIndex::RewardAttence)
    {

        CRewardAttenceDialog *rewardMenu = (CRewardAttenceDialog *)menu;
        if (rewardMenu->rewardInfoTable)
        {
            auto rewardInfoTable = rewardMenu->rewardInfoTable;
            auto itemCount       = sizeof(rewardInfoTable->rewardInfo) / sizeof(rewardInfoTable->rewardInfo[0]);
            for (int i = 0; i < itemCount; i++)
            {
                auto rewardInfo = rewardInfoTable->rewardInfo[i];
                if (rewardInfo != nullptr)
                    ret.push_back(*rewardInfo);
            }
        }
    }
    return ret;
}
void GameManager::GetRewardAccessReward(int index)
{
    zzj::Process process;
    zzj::Memory memory(process);

    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return;
    }
    auto funcAddr = baseAddr + 0x4b6800;
    std::string indexString = "time_";
    indexString += std::to_string(index);
    const char* indexChar = indexString.c_str();
    auto rewardMenu       = (CRewardAccessDialog *)GetMenuContainer(GUIIndex::RewardAccess);
    __asm
    {
		pushad
		push 0
        push 0
        push 0
        push indexChar
        mov ecx, rewardMenu
		call funcAddr
		popad
	}
}
void GameManager::CloseRewardAccessGui()
{
    zzj::Process process;
    zzj::Memory memory(process);

    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return;
    }
    auto funcAddr           = baseAddr + 0x4b6800;
    std::string exitString = "exit";
    const char *indexChar   = exitString.c_str();
    auto rewardMenu         = (CRewardAccessDialog *)GetMenuContainer(GUIIndex::RewardAccess);
    __asm
    {
		pushad
		push 0
        push 0
        push 0
        push indexChar
        mov ecx, rewardMenu
		call funcAddr
		popad
    }
}
void GameManager::OpenRewardAttenceGui()
{
    zzj::Process process;
    zzj::Memory memory(process);

    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return;
    }
    auto funcAddr = baseAddr + 0x32bfc0;
    __asm
    {
        push 1
        call funcAddr
    }
}

void GameManager::CloseRewardAttenceGui()
{
    zzj::Process process;
    zzj::Memory memory(process);

    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return;
    }
    auto funcAddr          = baseAddr + 0x4b83c0;
    std::string exitString = "exit";
    const char *indexChar  = exitString.c_str();
    auto rewardMenu        = (CRewardAccessDialog *)GetMenuContainer(GUIIndex::RewardAttence);
    __asm
    {
		pushad
		push 0
        push 0
        push 0
        push indexChar
        mov ecx, rewardMenu
		call funcAddr
		popad
    }
}
std::optional<bool> GameManager::Dll123IsAutoHuntEnable()
{
    auto dll123BaseAddr              = GameManager::GetModuleBaseAddress("123.dll");
    if (dll123BaseAddr == NULL)
        return {};
    zzj::Process thisProcess;
    zzj::Memory memory(thisProcess);
    bool preDll123AutoHuntEnabled = false;
    memory.Read(dll123BaseAddr + 0x184248, &preDll123AutoHuntEnabled, 1);
    return preDll123AutoHuntEnabled;
}
void GameManager::Dll123SetAutoHuntEnable(bool enable)
{
    auto dll123BaseAddr = GameManager::GetModuleBaseAddress("123.dll");
    if (dll123BaseAddr == NULL)
        return;
    zzj::Process thisProcess;
    zzj::Memory memory(thisProcess);
    memory.Write(dll123BaseAddr + 0x184248, &enable, 1);

}
void GameManager::Dll123SetAutoHuntPos(int x, int z)
{
    auto dll123BaseAddr = GameManager::GetModuleBaseAddress("123.dll");
    if (dll123BaseAddr == NULL)
        return;
    zzj::Process thisProcess;
    zzj::Memory memory(thisProcess);

    uintptr_t autoHuntPosPointer = NULL;
    memory.Read(dll123BaseAddr + 0x184240, &autoHuntPosPointer, sizeof(autoHuntPosPointer));
    if (autoHuntPosPointer == NULL)
        return;

    memory.Write(autoHuntPosPointer, &x, sizeof(x));
    memory.Write(autoHuntPosPointer + 4, &z, sizeof(z));
    
}
void GameManager::GetRewardAttenceReward(int index)
{
    zzj::Process process;
    zzj::Memory memory(process);

    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return;
    }
    auto funcAddr           = baseAddr + 0x4b83c0;
    std::string indexString = "day_";
    indexString += std::to_string(index);
    const char *indexChar = indexString.c_str();
    auto rewardMenu       = (CRewardAccessDialog *)GetMenuContainer(GUIIndex::RewardAttence);
    __asm
    {
		pushad
		push 0
        push 0
        push 0
        push indexChar
        mov ecx, rewardMenu
		call funcAddr
		popad
    }
}

bool GameManager::UseCashItem(unsigned int bagId)
{
    zzj::Process process;
    zzj::Memory memory(process);

    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return false;
    }

    auto useCashItemFuncAddr       = baseAddr + useCashItemFuncOffset;
    uintptr_t cashItemTableAddress = NULL;
    memory.Read(baseAddr + cashItemTableOffset, &cashItemTableAddress, sizeof(cashItemTableAddress));

    size_t _bagId = bagId + 13;
    __asm
    {
        pushad
        push 0
        push _bagId
        push 0x64614
        mov ecx, cashItemTableAddress
		call useCashItemFuncAddr
        popad
    }
    return true;
}
bool GameManager::UseCashItem(std::string itemName)
{
    int sellerCashItemIndex = -1;
    auto cashItems          = GetCashItems();
    for (unsigned int i = 0; i < cashItems.size(); i++)
    {
        auto cashItem = cashItems[i];
        if (cashItem.itemTable == nullptr)
            continue;
        spdlog::info("cashItem: {0}", cashItem.itemTable->GetItemName());
        if (cashItem.itemTable->GetItemName().find(itemName) !=
            std::string::npos)
        {
            sellerCashItemIndex = i;
            break;
        }
    }

    if (sellerCashItemIndex == -1)
    {
        spdlog::info("no seller cash item");
        return false;
    }

    return UseCashItem(sellerCashItemIndex);
}

std::vector<std::string> GameManager::GetAroundPlayersName()
{
    std::vector<std::string> aroundPlayersName;
    zzj::Process process;
    zzj::Memory memory(process);
    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return {};
    }
    auto aroundPlayerListBeginAddress =
        memory.FindMultiPleLevelAddress(baseAddr + aroundPlayerOffset, aroundPlayerMultiLevelOffset);

    if (aroundPlayerListBeginAddress == NULL)
    {
        return {};
    }

    auto nextPlayerAddress = aroundPlayerListBeginAddress;
    while (true)
    {
        auto nameVec = memory.ReadUntilZero(nextPlayerAddress + aroundPlayerNameOffset);
        
        std::string nameString(nameVec.begin(), nameVec.end());
        nameString = nameString.substr(0, nameString.size() - 1);
        nameString = boost::locale::conv::to_utf<char>(nameString, "Big5");
        aroundPlayersName.push_back(nameString);

        if (!memory.Read(nextPlayerAddress + nextPlayerPointerOffset, &nextPlayerAddress, sizeof(nextPlayerAddress)))
            break;

        if (nextPlayerAddress == NULL)
            break;
    }
    return aroundPlayersName;
}
void GameManager::CastSkill(CSkill skill, std::vector<CreatureWithAddress> creatures)
{
    zzj::Process process;
    zzj::Memory memory(process);
    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return;
    }
    uintptr_t gameClient = NULL;
    auto res             = memory.Read(baseAddr + gameClientOffset, &gameClient, sizeof(gameClient));
    if (!res)
    {
        spdlog::error("gameClient is null");
        return;
    }
    if(skill.skillTable == nullptr)
    {
        spdlog::error("skillTable is null");
        return;
    }
    if(skill.leftCoolDown > 0.1)
    {
        return;
    }

    spdlog::info("skill type switch");
    if (skill.skillId == 0x145)// 制作炸弹
    {
        auto callAddr = baseAddr + generalSendPackageOffset;
        __asm
        {
            pushad
            push 1
            push skill.skillId
            push 0x6458E
            mov ecx,gameClient
            call callAddr
            popad
        }
    }
    else if (skill.skillTable->skillType == SkillTable::SkillType::SingleAttack)
    {
        spdlog::info("SingleAttack handler");
        auto callAddr = baseAddr + skillSendPackageOffset;

        for (auto creature : creatures)
        {
            auto monsterId = creature.creature.monsterId;
            UINT32 param[3] = {skill.skillId, 0x14, monsterId};
            size_t sizehh   = sizeof(param);
            __asm
            {
			pushad

			
			mov ecx,gameClient
			push sizehh
			lea eax, param
			push eax
			push 0x6458E
			call callAddr

			popad
            }
        }
    }
    else if (skill.skillTable->skillType == SkillTable::SkillType::RangeAttack)
    {
        spdlog::info("RangeAttack handler");
        auto callAddr = baseAddr + skillSendPackageOffset;

        spdlog::info("RangeAttack for start");
        for (size_t i = 0; i < creatures.size(); i++)
        {
            spdlog::info("RangeAttack {}", i);
            auto currentCreature = creatures[i];
            auto neighbors = GetMonsters(currentCreature.creature, skill.skillTable->skillCoverRange + 3);
            spdlog::info("neighbors size1: {0}", neighbors.size());
            neighbors.erase(std::remove_if(neighbors.begin(), neighbors.end(),
                                           [currentCreature](const CreatureWithAddress &_creature) { 
                    if (currentCreature.creature.monsterId == _creature.creature.monsterId)
                        return true; 
                    return false;
            }), neighbors.end());
            spdlog::info("neighbors size2: {0}", neighbors.size());
            UINT32 *param = new UINT32[4 + neighbors.size()];
            size_t sizehh = 4 * (4 + neighbors.size());
            param[0]      = skill.skillId;
            param[1]      = skill.skillLevel;
            param[2]      = 1 + neighbors.size();
            param[3]      = currentCreature.creature.monsterId;
            for (int i = 0; i < neighbors.size(); i++)
            {
                param[4 + i] = neighbors[i].creature.monsterId;
            }
            spdlog::info("Ready to call");
            __asm
            {
	    		pushad
    
	    		mov ecx,gameClient
	    		push sizehh
	    		mov eax, param
	    		push eax
	    		push 0x6458E
	    		call callAddr
    
	    		popad
            }
    
            delete[] param;
        }
    }
}
bool GameManager::HasMonsterSelected()
{
    zzj::Process process;
    zzj::Memory memory(process);
    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return false;
    }

    auto callAddr = baseAddr + 0x6a2d60;
    int monsterId = -1;
    auto res      = memory.Read(baseAddr + monsterIdOffset, &monsterId, sizeof(monsterId));
    return monsterId != -1;
}
std::string GameManager::CItemTable::GetItemName()
{
    std::string big5Str(itemName);
    if (big5Str.empty())
        return "(null)";
    return boost::locale::conv::to_utf<char>(big5Str, "Big5");
}
std::string GameManager::CItemTable::GetItemDescription()
{
    std::string big5Str(description);
    if (big5Str.empty())
        return "(null)";
    return boost::locale::conv::to_utf<char>(big5Str, "Big5");
}

std::vector<GameManager::CUser> GameManager::GetAroundPlayers()
{
    std::vector<CUser> ret;
    std::vector<std::string> aroundPlayersName;
    zzj::Process process;
    zzj::Memory memory(process);
    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return {};
    }
    auto aroundPlayerListBeginAddress =
        memory.FindMultiPleLevelAddress(baseAddr + aroundPlayerOffset, aroundPlayerMultiLevelOffset);

    if (aroundPlayerListBeginAddress == NULL)
    {
        return {};
    }

    auto nextPlayerAddress = aroundPlayerListBeginAddress;
    while (true)
    {
        ret.push_back(*(CUser *)nextPlayerAddress);
        if (!memory.Read(nextPlayerAddress + nextPlayerPointerOffset, &nextPlayerAddress, sizeof(nextPlayerAddress)))
            break;

        if (nextPlayerAddress == NULL)
            break;
    }
    return ret;

}
std::vector<GameManager::Item> GameManager::GetBagItems()
{
    try
    {
        auto localPlayerPointer = GetLocalPlayerBase();
        if (localPlayerPointer == NULL)
        {
            spdlog::error("localPlayerPointer is null");
            return {};
        }

        std::vector<std::string> items;
        zzj::Process process;
        zzj::Memory memory(process);
        auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
        if (baseAddr == NULL)
        {
            return {};
        }
        uintptr_t itemContainerAddr;
        auto res = memory.Read(baseAddr + itemContainterOffset, &itemContainerAddr, sizeof(itemContainerAddr));
        if (!res)
        {
            spdlog::error("itemContainerAddr is null");
            return {};
        }
        uintptr_t firstItemAddr = itemContainerAddr + firstItemOffset;
        std::vector<Item> itemVec;
        for (int i = 0; i < itemFullCount; i++)
        {
            Item item;
            res = memory.Read(firstItemAddr + i * itemStructSize, &item, sizeof(item));
            if (!res)
            {
                spdlog::error("item is null");
                return {};
            }
            itemVec.push_back(item);
        }
        return itemVec;
    }
    catch (const std::exception &e)
    {
        spdlog::error("GetBagItems error: {0}", e.what());
        return {};
    }
    catch (...)
    {
        spdlog::error("GetBagItems error");
        return {};
    }
}

int GameManager::GetItemCount(const std::string &name)
{
    auto items = GetBagItems();
    int ret    = 0;
    for (auto& item : items)
    {
        if (!item.itemTable)
        {
            continue;
        }

        std::string itemName = item.itemTable->GetItemName();
        if (name == itemName)
            ret += item.count;
    }
    return ret;
}

std::vector<GameManager::Item> GameManager::GetCashItems()
{
    try
    {
        auto localPlayerPointer = GetLocalPlayerBase();
        if (localPlayerPointer == NULL)
        {
            spdlog::error("localPlayerPointer is null");
            return {};
        }

        std::vector<std::string> items;
        zzj::Process process;
        zzj::Memory memory(process);
        auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
        if (baseAddr == NULL)
        {
            return {};
        }
        uintptr_t itemContainerAddr;
        auto res = memory.Read(baseAddr + cashInvenOffset, &itemContainerAddr, sizeof(itemContainerAddr));
        if (!res)
        {
            spdlog::error("cashContainerAddr is null");
            return {};
        }
        uintptr_t firstItemAddr = itemContainerAddr + firstCashItemOffset;
        std::vector<Item> itemVec;
        for (int i = 0; i < cashInvenSize; i++)
        {
            Item item;
            res = memory.Read(firstItemAddr + i * itemStructSize, &item, sizeof(item));
            if (!res)
            {
                spdlog::error("item is null");
                return {};
            }
            itemVec.push_back(item);
        }
        return itemVec;
    }
    catch (const std::exception &e)
    {
        spdlog::error("GetBagItems error: {0}", e.what());
        return {};
    }
    catch (...)
    {
        spdlog::error("GetBagItems error");
        return {};
    }
}

std::vector<GameManager::CSkill> GameManager::GetSkills()
{
    try
    {
        auto localPlayerPointer = GetLocalPlayerBase();
        if (localPlayerPointer == NULL)
        {
            spdlog::error("localPlayerPointer is null");
            return {};
        }

        std::vector<std::string> items;
        zzj::Process process;
        zzj::Memory memory(process);
        auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
        if (baseAddr == NULL)
        {
            return {};
        }
        uintptr_t skillTableBeginAddr;
        auto res = memory.Read(baseAddr + skillIndexBase, &skillTableBeginAddr, sizeof(skillTableBeginAddr));
        if (!res)
        {
            spdlog::error("skillIndexAddr is null");
            return {};
        }
        uintptr_t firstSkillAddr = skillTableBeginAddr + skillArrayPointerOffset;
        res                      = memory.Read(firstSkillAddr, &firstSkillAddr, sizeof(firstSkillAddr));
        if (!res)
        {
            spdlog::error("firstSkillAddr is null");
            return {};
        }
        UINT32 skillCount = 0;
        res               = memory.Read(skillTableBeginAddr + skillArraySizeOffset, &skillCount, sizeof(skillCount));
        if (!res)
        {
            spdlog::error("skillCount is null");
            return {};
        }
        std::vector<CSkill> skillVec;
        for (int i = 0; i < skillCount; i++)
        {
            CSkill skill;
            res = memory.Read(firstSkillAddr + i * sizeof(CSkill), &skill, sizeof(skill));
            if (!res)
            {
                spdlog::error("skill is null");
                return {};
            }
            skillVec.push_back(skill);
        }
        return skillVec;
    }
    catch (const std::exception &e)
    {
        spdlog::error("GetSkills error: {0}", e.what());
        return {};
    }
    catch (...)
    {
        spdlog::error("GetSkills error");
        return {};
    }
}

void GameManager::ThrowBomb()
{
    zzj::Process process;
    zzj::Memory memory(process);
    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return;
    }

    auto callAddr = baseAddr + skillSendPackageOffset;

    uintptr_t gameClient = NULL;
    auto res             = memory.Read(baseAddr + gameClientOffset, &gameClient, sizeof(gameClient));
    if (!res)
    {
        spdlog::error("gameClient is null");
        return;
    }

    auto localPlayerPointer = GetLocalPlayerBase();
    if (localPlayerPointer == NULL)
    {
        spdlog::error("localPlayerPointer is null");
        return;
    }
    auto items = GetBagItems();
    int bombId = -1;
    int count  = 0;
    for (size_t i = 0; i < items.size(); i++)
    {
        auto item = items[i];
        if (item.itemTable == nullptr)
            continue;
        if (item.itemTable->GetItemName().find("\xE5\x8E\x9F\xE5\xAD\x90\xE5\xBD\x88") != std::string::npos) // 原子弹
        {
            bombId = i;
            count  = item.count;
            break;
        }
        if (item.itemTable->GetItemName().find(u8"黑色自製炸藥") != std::string::npos)
        {
            bombId = i;
            count  = item.count;
            break;
        }
    }

    auto skills = GetSkills();
    if (skills.size() == 0)
        return;
    if (bombId == -1)
    {
        CastSkill(skills[0x145], {});
        spdlog::info("no bomb to throw");
        return;
    }
    static auto lastCastBomb = std::chrono::system_clock::now();
    auto now                 = std::chrono::system_clock::now();
    auto duration            = std::chrono::duration_cast<std::chrono::seconds>(now - lastCastBomb);
    if (duration.count() > 1)
    {
        CastSkill(skills[0x145], {});
        lastCastBomb = now;
    }
    bombId += 0xD;
    auto creatures = GetMonsters(12);
    if (creatures.empty())
    {
        spdlog::info("no monster to throw bomb");
        return;
    }

    uint32_t distanceThreshold = 5;
    for (size_t i = 0; i < creatures.size(); i++)
    {
        if (i >= fireFullPowerMaxCreature)
            continue;
        auto currentCreature = creatures[i];
        if (currentCreature.creature.health <= 0)
            continue;
        //spdlog::info("Ready to throw bomb to {}   {}", currentCreature.creature.monsterStatTablePtr->GetName(),
        //             currentCreature.creature.monsterId);
        std::vector<CreatureWithAddress> neighbors;
        //
        for (size_t j = 0; j < creatures.size(); j++)
        {
            if (i == j)
                continue;
            auto otherCreature = creatures[j];
            if (otherCreature.creature.health <= 0)
				continue;
            if (currentCreature.creature.GetDistance(otherCreature.creature) < distanceThreshold)
            {
                neighbors.push_back(otherCreature);
            }
        }

        //spdlog::info("Throw bomb with {} neighbors", neighbors.size());
        UINT32 *param = new UINT32[6 + neighbors.size()];
        size_t sizehh = 4 * (6 + neighbors.size());
        param[0]      = 0x53;
        param[1]      = 1;
        param[2]      = bombId;
        param[3]      = neighbors.size() > 0 ? 0 : 1;
        param[4]      = 1 + neighbors.size();
        param[5]      = currentCreature.creature.monsterId;
        for (int i = 0; i < neighbors.size(); i++)
        {
            param[6 + i] = neighbors[i].creature.monsterId;
        }
        __asm
        {
			pushad

			mov ecx,gameClient
			push sizehh
			mov eax, param
			push eax
			push 0x6458E
			call callAddr

			popad
        }

        delete[] param;
    }
}

void GameManager::DropItemFunc(unsigned int bagId, unsigned int count)
{
    zzj::Process process;
    zzj::Memory memory(process);
    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return;
    }
    uintptr_t gameClient = NULL;
    auto res             = memory.Read(baseAddr + gameClientOffset, &gameClient, sizeof(gameClient));
    if (!res)
    {
        spdlog::error("gameClient is null");
        return;
    }

    auto dropItemFuncAddr = baseAddr + generalSendPackageOffset;
    auto _bagId           = bagId + 0xd;
    __asm
    {
        pushad
		mov ecx,gameClient
        push count
        push _bagId
        push 0x64584
		call dropItemFuncAddr
        popad
    }
}

std::vector<GameManager::CreatureWithAddress> GameManager::GetCreatures()
{
    try
    {
        auto localPlayerPointer = GetLocalPlayerBase();
        if (localPlayerPointer == NULL)
        {
            spdlog::error("localPlayerPointer is null");
            return {};
        }

        std::vector<std::string> items;
        zzj::Process process;
        zzj::Memory memory(process);
        auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
        if (baseAddr == NULL)
        {
            return {};
        }
        uintptr_t creatureListHead =
            memory.FindMultiPleLevelAddress(baseAddr + creatureBaseOffset, creatureMultiLevelOffset);
        if (creatureListHead == NULL)
        {
            spdlog::error("creatureListHead is null");
            return {};
        }
        std::vector<CreatureWithAddress> monsterVec;
        for (CCreature *monster = (CCreature *)creatureListHead; monster != nullptr;
             monster            = monster->nextMonsterPointer)
        {
            if (monster == nullptr)
                break;
            CreatureWithAddress monsterWithAddress;
            monsterWithAddress.address  = (uintptr_t)monster;
            monsterWithAddress.creature = *monster;
            monsterVec.push_back(monsterWithAddress);
        }
        return monsterVec;
    }
    catch (const std::exception &e)
    {
        spdlog::error("GetCreatures error: {0}", e.what());
        return {};
    }
    catch (...)
    {
        spdlog::error("GetCreatures error");
        return {};
    }
}
void GameManager::PickItem(unsigned int dropId)
{
    zzj::Process process;
    zzj::Memory memory(process);
    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return;
    }

    auto pickItemFuncAddr = baseAddr + pickItemSendPackageOffset;
    uintptr_t gameClient  = NULL;
    auto res              = memory.Read(baseAddr + gameClientOffset, &gameClient, sizeof(gameClient));
    if (!res)
    {
        spdlog::error("gameClient is null");
        return;
    }

    __asm
    {
        pushad
        mov ecx,gameClient
        push 0
        push 0x1b
        push dropId
        push 0x64583
        call pickItemFuncAddr
        popad
    }
}
GameManager::CItemContainer *GameManager::GetItemContainer()
{
    auto localPlayerPointer = GetLocalPlayerBase();
    if (localPlayerPointer == NULL)
    {
        spdlog::error("localPlayerPointer is null");
        return {};
    }

    std::vector<std::string> items;
    zzj::Process process;
    zzj::Memory memory(process);
    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return {};
    }
    uintptr_t itemContainerAddr;
    auto res = memory.Read(baseAddr + itemContainterOffset, &itemContainerAddr, sizeof(itemContainerAddr));
    if (!res)
    {
        spdlog::error("itemContainerAddr is null");
        return {};
    }
    return (GameManager::CItemContainer *)itemContainerAddr;
}
std::optional<GameManager::Item> GameManager::GetCurrentGearItem()
{
    auto gearMenu = GetMenuContainer(GUIIndex::CMagicSpringOption);
    if (gearMenu == nullptr)
    {
		spdlog::error("gearMenu is null");
		return {};
	}

    spdlog::info("GearMenu Address {:x}", (uintptr_t)gearMenu);
    if (gearMenu->guiMenu)
    {
        if (gearMenu->guiMenu->data)
        {
            auto hasItem = gearMenu->guiMenu->data->hasItem;
            if (hasItem == 0)
            {
                spdlog::info("no item");
                return {};
            }

            auto bagPos = gearMenu->guiMenu->data->bagABSPos;
            auto items = GetBagItems();
            return items[bagPos];
		}
	}
    return {};
}
void GameManager::ChangeGear()
{
    zzj::Process process;
    zzj::Memory memory(process);
    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return;
    }

    auto callFunc = useGearCallOffset + baseAddr;
    auto gearMenu = GetMenuContainer(GUIIndex::CMagicSpringOption);
    if (gearMenu == nullptr)
    {
        spdlog::error("gearMenu is null");
        return;
    }
    __asm
    {
        mov ecx,gearMenu
        call callFunc
    }
    return;
}
std::vector<GameManager::CreatureWithAddress> GameManager::GetMonsters(uint32_t range)
{
    auto localPlayerPointer = GetLocalPlayerBase();
    if (localPlayerPointer == NULL)
    {
        spdlog::error("localPlayerPointer is null");
        return {};
    }
    return GetMonsters(*(CCreature *)localPlayerPointer, range);
}
std::vector<GameManager::CreatureWithAddress> GameManager::GetMonsters(CCreature creature, uint32_t range)
{
    auto creatures = GetCreatures();
    // erase the creature that is not monster or too far from player
    creatures.erase(std::remove_if(creatures.begin(), creatures.end(),
                                   [creature, range](const CreatureWithAddress &_creature) {
                                       if (_creature.creature.monsterStatTablePtr == nullptr)
                                           return true;
                                       if (!_creature.creature.monsterStatTablePtr->IsMonster())
                                           return true;
                                       if (_creature.creature.GetDistance(creature) > range)
                                           return true;
                                       return false;
                                   }),
                    creatures.end());

    return creatures;
}

uintptr_t GetPatternMatchResult(const std::string &pattern)
{
    zzj::Process process;
    zzj::Memory memory(process);

    if (gameManager.config.find("baseAddress") != gameManager.config.end())
    {
        nlohmann::json patternBaseAddr = gameManager.config["baseAddress"];
        if (patternBaseAddr.find(pattern) != patternBaseAddr.end())
            return patternBaseAddr[pattern].get<uint32_t>();
    }
    else
        gameManager.config["baseAddress"] = nlohmann::json::object();

    auto baseAddr = GameManager::GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return {};
    }
    auto moduleSize = GetModuleSize("SO3DPlus.exe");
    auto matches    = memory.PatternScan(baseAddr, moduleSize, pattern);
    if (matches.empty())
    {
        spdlog::error("matches is empty");
        return NULL;
    }

    auto match = matches[0];
    gameManager.config["baseAddress"][pattern] = (uint32_t)match.address;
    //save to file
    gameManager.SaveConfig();
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
        mov ebx, GameManager::attackRangeOffset
        mov[ecx + ebx], eax
    }

    __asm
    {
        popad
        jmp attackRangeRetAddress
    }
}
uintptr_t GameManager::GetLocalPlayerBase()
{
    zzj::Process process;
    zzj::Memory memory(process);
    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return {};
    }

    uintptr_t localPlayerAddr = 0;
    auto res                  = memory.Read(baseAddr + localPlayerOffset, &localPlayerAddr, sizeof(localPlayerAddr));
    return localPlayerAddr;
}
bool GameManager::EnableAttackRange()
{
    if (attackRangeHookAddress == NULL)
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

        movss xmm1, GameManager::attackSpeed
        mov ebx, GameManager::attackSpeedOffset
        movss[eax + ebx], xmm1
    }

    __asm
    {
        popad
        jmp attackSpeedRetAddress
    }
}
bool GameManager::EnableAttackSpeed()
{
    if (attackSpeedHookAddress == NULL)
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
        mov ebx, GameManager::skillRangeOffset
        mov eax, [ecx + ebx]
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
        pop ebx
        jmp skillRangeRetAddress
    }
}
bool GameManager::EnableSkillRange()
{
    if (skillRangeHookAddress == NULL)
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
        pushad
        mov ebx,GameManager::itemCoolDownOffset
        mov eax, GameManager::itemCoolDown
        mov [ecx+ebx],eax
        cvtsi2ss xmm0,[ecx+ebx]
        popad
        jmp itemNoCoolDownRetAddress
    }
}

bool GameManager::EnableItemNoCoolDown()
{
    if (itemNoCoolDownHookAddress == NULL)
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
        push ebx
        movss xmm0, pretime
        mov ebx,GameManager::skillPretimeOffset
        movss [edx + ebx],xmm0
        pop ebx
        jmp skillSpeedRetAddress1
    }
}

int __declspec(naked) SkillSpeedHooked2()
{
    static float skillSpeed = 0.001f;
    __asm
    {
        push ebx
        movss xmm1, skillSpeed
        mov ebx,GameManager::skillSpeedOffset
        movss [eax + ebx],xmm1
        pop ebx
        jmp skillSpeedRetAddress2
    }
}

static uintptr_t skillCooldownHookAddress = NULL;
static void *skillCooldownRetAddress;
constexpr uintptr_t skillCoolDownOffset = offsetof(GameManager::SkillTable, coolDown);
float beforeCoolDown;
int __declspec(naked) SkillCooDownCalculateHooked()
{
    __asm
    {
        push eax
        push ebx
        push edx
        push ecx

        mov eax,skillCoolDownOffset
        mov ebx,[edx + eax]
        mov beforeCoolDown,ebx
        lea edx,GameManager::speedHackEnable
        movzx ecx,byte ptr [edx]
        test ecx,ecx
        je retP
        movss xmm1,beforeCoolDown
        mulss xmm1,GameManager::speedHack
        movss beforeCoolDown,xmm1
    }
    __asm
    {
    retP:
        pop ecx
        pop edx
        pop ebx
        pop eax
        movss xmm1, beforeCoolDown
        jmp skillCooldownRetAddress
    }
}

static uintptr_t skillModeHookAddress = NULL;
static void *skillModeRetAddress;

int __declspec(naked) SkillModeHooked()
{
    __asm
    {
		cmp ecx,5
        jne original
        mov ecx,0

    original:
        mov [eax + 0x2c2c],ecx
		jmp skillModeRetAddress
	}
}

static uintptr_t animationModeHookAddress = NULL;
static void *animationModeRetAddress;

int __declspec(naked) animationModeHooked()
{
    __asm
    {
		jmp animationModeRetAddress
    }
}

bool GameManager::EnableSkillSpeed()
{
    if (skillSpeedHookAddress1 == NULL)
        skillSpeedHookAddress1 = GetPatternMatchResult(skillNoPretimePattern);

    if (skillSpeedHookAddress1 == NULL)
    {
        spdlog::error("skillSpeedHookAddress1 is null");
        return false;
    }
    spdlog::info("skillSpeedHookAddress1: {0:x}", skillSpeedHookAddress1);

    skillSpeedRetAddress1 = (void *)DetourAndGetRetAddress(skillSpeedHookAddress1, &SkillSpeedHooked1);

    if (skillSpeedHookAddress2 == NULL)
        skillSpeedHookAddress2 = GetPatternMatchResult(skillSpeedPattern);
    if (skillSpeedHookAddress2 == NULL)
    {
        spdlog::error("skillSpeedHookAddress2 is null");
        return false;
    }
    spdlog::info("skillSpeedHookAddress2: {0:x}", skillSpeedHookAddress2);

    skillSpeedRetAddress2 = (void *)DetourAndGetRetAddress(skillSpeedHookAddress2, &SkillSpeedHooked2);

    if (skillCooldownHookAddress == NULL)
        skillCooldownHookAddress = GetPatternMatchResult(skillCoolDownCalculatePattern);
    if (skillCooldownHookAddress == NULL)
    {
        spdlog::error("skillCooldownHookAddress is null");
        return false;
    }

    spdlog::info("skillCooldownHookAddress: {0:x}", skillCooldownHookAddress);
    skillCooldownRetAddress = (void *)DetourAndGetRetAddress(skillCooldownHookAddress, &SkillCooDownCalculateHooked);

    if (skillModeHookAddress == NULL)
       skillModeHookAddress = GetPatternMatchResult(skillModeChangePattern);

    if (skillModeHookAddress == NULL)
    {
        spdlog::error("skillModeHookAddress is null");
        return false;
    }

    spdlog::info("skillModeHookAddress: {0:x}", skillModeHookAddress);
    skillModeRetAddress = (void *)DetourAndGetRetAddress(skillModeHookAddress, &SkillModeHooked);

    if (animationModeHookAddress == NULL)
        animationModeHookAddress = GetPatternMatchResult(animationModeChangePattern);

    if (animationModeHookAddress == NULL)
    {
        spdlog::error("animationModeHookAddress is null");
        return false;
    }

    spdlog::info("animationModeHookAddress: {0:x}", animationModeHookAddress);
    animationModeRetAddress = (void *)DetourAndGetRetAddress(animationModeHookAddress, &animationModeHooked);
    return true;
}

bool GameManager::DisableSkillSpeed()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&(PVOID &)skillSpeedHookAddress1, &SkillSpeedHooked1);
    DetourDetach(&(PVOID &)skillSpeedHookAddress2, &SkillSpeedHooked2);
    DetourDetach(&(PVOID &)skillCooldownHookAddress, &SkillCooDownCalculateHooked);
    DetourDetach(&(PVOID &)skillModeHookAddress, &SkillModeHooked);
    DetourDetach(&(PVOID &)animationModeHookAddress, &animationModeHooked);
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
        pushad
        movss xmm0,GameManager::moveSpeed
        mov eax,GameManager::moveSpeedOffset
        movss [ecx + eax],xmm0
        popad
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
    if (moveSpeedHookAddress == NULL)
        moveSpeedHookAddress = GetPatternMatchResult(moveSpeedPattern);
    if (moveSpeedHookAddress == NULL)
    {
        spdlog::error("moveSpeedHookAddress is null");
        return false;
    }
    spdlog::info("moveSpeedHookAddress: {0:x}", moveSpeedHookAddress);

    moveSpeedRetAddress = (void *)DetourAndGetRetAddress(moveSpeedHookAddress, &MoveSpeedHooked);
    zzj::Process process;
    zzj::Memory memory(process);
    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return nullptr;
    }

    //moveSpeedUnlimitHookAddress = baseAddr + moveSpeedUnlimitOffset;
    //spdlog::info("moveSpeedUnlimitHookAddress: {0:x}", moveSpeedUnlimitHookAddress);
    //
    //moveSpeedUnlimitRetAddress = (void *)DetourAndGetRetAddress(moveSpeedUnlimitHookAddress, &MoveSpeedUnlimitHooked);
    float maxVal               = 22.0f;
    memory.Write(baseAddr + moveSpeedLimitValueOffset, &maxVal, sizeof(maxVal));
    return true;
}

GameManager::CMenuContainerEx *GameManager::GetCurrentOpenGuiMenu()
{
    zzj::Process process;
    zzj::Memory memory(process);
    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return nullptr;
    }
    CMenuContainerEx *menuContainer = (CMenuContainerEx *)memory.FindMultiPleLevelAddress(baseAddr + guiIndexerOffset, guiMenuMultilevelOffset);
    if (menuContainer == nullptr)
	{
		spdlog::error("menuContainer is null");
		return nullptr;
	}
    return menuContainer;
}


GameManager::AutoHuntManager *GameManager::GetAutoHuntManager()
{
    zzj::Process process;
    zzj::Memory memory(process);
    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return nullptr;
    }
    return (AutoHuntManager *)(baseAddr + autoHuntBaseAddr);
}
bool(__fastcall *popUpWindowHookAddress)(void *thisPointer, void *edx, const char *popMessage, int popType, int a4,
                                         float a5) = NULL;
bool __fastcall PopUpWindowHandler(void *thisPointer, void *edx, const char *popMessage, int popType,
                                   int a4, float a5)
{
    std::string message = popMessage;
    if (message.empty())
        return true;
    message = boost::locale::conv::to_utf<char>(message, "Big5");
    spdlog::info("PopUpWindowHandler: {0}", message);
    if(message.find(u8"與伺服器連線中斷") != std::string::npos)
    {
        ExitProcess(0);
    }
    if (message.find(u8"角色在副本任務內死亡的話") != std::string::npos)
        return popUpWindowHookAddress(thisPointer, edx, popMessage, popType, a4, a5);
    return true;
}
bool GameManager::EnablePopupWindowHook()
{
    if (popUpWindowHookAddress == NULL){
        zzj::Process process;
        zzj::Memory memory(process);
        auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
        if (baseAddr == NULL)
        {
            return false;
        }
        popUpWindowHookAddress = decltype(popUpWindowHookAddress)(popupWindowHandlerFuncOffset + baseAddr);
    }
    if (popUpWindowHookAddress == NULL)
    {
        spdlog::error("popUpWindowHookAddress is null");
        return false;
    }
    spdlog::info("popUpWindowHookAddress: {0:x}", (uintptr_t)popUpWindowHookAddress);

    auto ret = (void *)DetourAndGetRetAddress((uintptr_t&)popUpWindowHookAddress, &PopUpWindowHandler);
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

static uintptr_t cameraDistanceHookAddress = NULL;
static void *cameraDistanceRetAddress;

int __declspec(naked) CameraDistanceHooked()
{
    __asm
    {
        jmp cameraDistanceRetAddress
    }
}
bool GameManager::EnableCameraDistance()
{
    zzj::Process process;
    zzj::Memory memory(process);
    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return false;
    }
    //cameraDistanceHookAddress = baseAddr + cameraDistanceHookFunctionOffset;
    //
    //cameraDistanceRetAddress = (void *)DetourAndGetRetAddress(cameraDistanceHookAddress, &CameraDistanceHooked);

    float maxValue = 50.0f;
    memory.Write(baseAddr + camearDistanceMaxValueOffset, &maxValue, sizeof(maxValue));
    return true;
}

bool GameManager::DisableCameraDistance()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&(PVOID &)cameraDistanceHookAddress, &CameraDistanceHooked);
    DetourTransactionCommit();
    return true;
}


void GameManager::CloseSellerGui()
{
    std::thread t1([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        zzj::Process process;
        zzj::Memory memory(process);
        auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
        if (baseAddr == NULL)
        {
            return;
        }

        CMenuContainerEx *menuContainer = GetMenuContainer(GUIIndex::Seller);
        if (menuContainer == nullptr)
        {
            spdlog::error("menuContainer is null");
            return;
        }
        spdlog::info("currentMenuContainerAddr {:x}", (uintptr_t)menuContainer);
        uintptr_t vTableVA = baseAddr + CMerchantVirtualTableOffset;
        if (!menuContainer->isClosed)
        {
            spdlog::info("close seller gui");
            menuContainer->isClosed = true;
        }
    });
    t1.detach();
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

std::string GameManager::SkillTable::GetSkillName()
{
    std::string big5Str(skillName);
    if (big5Str.empty())
        return "(null)";
    return boost::locale::conv::to_utf<char>(big5Str, "Big5");
}

std::string GameManager::SkillTable::GetSkillDescription()
{
    std::string big5Str(description);
    if (big5Str.empty())
        return "(null)";
    return boost::locale::conv::to_utf<char>(big5Str, "Big5");
}

std::string GameManager::MonsterStatTable::GetName()
{
    std::string big5Str(name);
    if (big5Str.empty())
        return "(null)";
    return boost::locale::conv::to_utf<char>(big5Str, "Big5");
}

bool GameManager::MonsterStatTable::IsMonster()
{
    std::vector<int> monsterType;
    if (gameManager.config.find("monsterType") != gameManager.config.end())
    {
        monsterType = gameManager.config["monsterType"].get <std::vector<int>>();
    }
    if (std::find(monsterType.begin(), monsterType.end(), type) != monsterType.end()){
        return TRUE;
    }

    auto retVal = type == CreatureType::Monster0 || type == CreatureType::Monster1 || type == CreatureType::Monster2 ||
                  type == CreatureType::Monster3 || type == CreatureType::Monster4 || type == CreatureType::Monster5 ||
                  type == CreatureType::Monster6;
    if (!retVal)
        spdlog::info("Monster name {}, type {}", GetName(), type);
    return retVal;
}


std::string GameManager::CLocalUser::GetName()
{
    std::string big5Str(name);
    if (big5Str.empty())
        return "";
    return boost::locale::conv::to_utf<char>(big5Str, "Big5");
}
uint32_t GameManager::CLocalUser::GetXorEncryptVal()
{
    zzj::Process process;
    zzj::Memory memory(process);
    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return 0;
    }

    uint32_t xorVal = 0;
    memory.Read(baseAddr + xorValOffset, &xorVal, sizeof(xorVal));
    return xorVal;
}

uint32_t GameManager::CLocalUser::GetCurrentHP()
{
    return currentHP ^ GetXorEncryptVal();
}

uint32_t GameManager::CLocalUser::GetMaxHP()
{
    return maxHP ^ GetXorEncryptVal();
}

uint32_t GameManager::CLocalUser::GetCurrentMP()
{
    return currentMP ^ GetXorEncryptVal();
}

uint32_t GameManager::CLocalUser::GetMaxMP()
{
    return maxMP ^ GetXorEncryptVal();
}

void GameManager::NPCManager::InteractWithNpc(uint32_t npcId)
{
    static GameManager gameManager;
    zzj::Process process;
	zzj::Memory memory(process);
	auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
	if (baseAddr == NULL)
	{
		return;
	}

	uintptr_t gameClient = NULL;
	auto res             = memory.Read(baseAddr + gameClientOffset, &gameClient, sizeof(gameClient));
	if (!res)
	{
		spdlog::error("gameClient is null");
		return;
	}

    CLocalUser *localPlayer = (CLocalUser *)gameManager.GetLocalPlayerBase();
    GameManager::NPCInteract packet;
    packet.localPlayerX = localPlayer->intX;
    packet.localPlayerZ = localPlayer->intZ;
    packet.npcId = npcId;

    auto callAddr = baseAddr + rawSendPackageOffset;

    __asm
	{
		pushad

		mov ecx,gameClient
        push packet.len
        lea eax, packet
        push eax
		call callAddr

        popad
	}

}

void GameManager::NPCManager::DeliverLetter(GameManager::DeliverLetterPacket packet)
{
    InteractWithNpc(letterNpcId);
    zzj::Process process;
    zzj::Memory memory(process);
    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return;
    }

    uintptr_t gameClient = NULL;
    auto res             = memory.Read(baseAddr + gameClientOffset, &gameClient, sizeof(gameClient));
    if (!res)
    {
        spdlog::error("gameClient is null");
        return;
    }

	auto callAddr = baseAddr + rawSendPackageOffset;
	__asm
	{
		pushad

        push packet.len
        lea eax, packet
        push eax
		mov ecx,gameClient
		call callAddr

		popad
	}
}
//send address
int (__stdcall *sendOriginAddress)(
 SOCKET     s,
 const char *buf,
 int        len,
 int        flags
) = nullptr;

//recv address
int (__stdcall *recvOriginAddress)(
 SOCKET s,
 char   *buf,
 int    len,
 int    flags
) = nullptr;

int (__stdcall *wspSendOriginAddress)(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesSent,
                                     DWORD dwFlags, LPWSAOVERLAPPED lpOverlapped,
                                     LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine, LPWSATHREADID lpThreadId,
                                     LPINT lpErrno) = nullptr;
int __stdcall WSPSendHooked(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesSent,
                                     DWORD dwFlags, LPWSAOVERLAPPED lpOverlapped,
                                     LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine, LPWSATHREADID lpThreadId,
                                     LPINT lpErrno)
{
    //get remote ip and port from socket
    sockaddr_in addr;
    spdlog::info("WSPSend called");
    int len = sizeof(addr);
    if (getpeername(s, (sockaddr *)&addr, &len) == 0)
    {
        spdlog::info("please holder");
        spdlog::info("send to {0}:{1}", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
    }
    else
        spdlog::error("Get peer name failed: {0}", WSAGetLastError());
    return wspSendOriginAddress(s, lpBuffers, dwBufferCount, lpNumberOfBytesSent, dwFlags, lpOverlapped, lpCompletionRoutine, lpThreadId, lpErrno);
}
int __stdcall SendHooked(
 SOCKET     s,
 const char *buf,
 int        len,
 int        flags
)
{
    sockaddr_in addr;
    int addrLen = sizeof(addr);

    // 使用getpeername获取对方的地址信息
    if (getpeername(s, (sockaddr *)&addr, &addrLen) == 0)
    {
        char ipStr[INET_ADDRSTRLEN];

        // 将二进制的IP地址转换为字符串形式
        auto ret = inet_ntop(AF_INET, &addr.sin_addr, ipStr, sizeof(ipStr));
        if (ret == NULL)
        {
            spdlog::error("inet_ntop failed: {0}", WSAGetLastError());
        }
        else
        {
            int port = ntohs(addr.sin_port);
            spdlog::info("Send on socket {}:{} called ", std::string(ipStr), port);
        }
    }
    else
    {
        spdlog::info("Send on socket {} called", (uintptr_t)s);
    }
    auto ret = sendOriginAddress(s, buf, len, flags);
    spdlog::info("Send len {}", ret);
    return ret;
}

std::mutex mtx;
static auto lastTimeRecv = std::chrono::system_clock::now();
typedef int(WINAPI *RECV)(SOCKET, char *, int, int);
RECV TrueRecv = NULL;
void RecvListerner()
{
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10000));
        {
            auto currentTime = std::chrono::system_clock::now();
            std::lock_guard<std::mutex> lock(mtx);
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastTimeRecv);
            if (duration.count() > 180)
            {
                spdlog::error("recv timeout");
                ExitProcess(0);
            }
        }
    }
}
int __stdcall RecvHooked(
 SOCKET s,
 char   *buf,
 int    len,
 int    flags
)
{
    sockaddr_in addr;
    int addrLen = sizeof(addr);
    int port    = -1;
    // 使用getpeername获取对方的地址信息
    if (getpeername(s, (sockaddr *)&addr, &addrLen) == 0)
    {
        char ipStr[INET_ADDRSTRLEN];

        // 将二进制的IP地址转换为字符串形式
        auto ret = inet_ntop(AF_INET, &addr.sin_addr, ipStr, sizeof(ipStr));
        if (ret == NULL)
        {
			spdlog::error("inet_ntop failed: {0}", WSAGetLastError());
		}
        else
        {
            port = ntohs(addr.sin_port);
            spdlog::info("Recv on socket {}:{} called ", std::string(ipStr), port);
        }
    }
    else
    {
        spdlog::info("Recv on socket {} called", (uintptr_t)s);
    }
    int ret = recvOriginAddress(s, buf, len, flags);
    if (ret > 0 && ret != 6 && port > 1800 && port < 1900)
    {
        auto currentTime = std::chrono::system_clock::now();
        std::lock_guard<std::mutex> lock(mtx);
        lastTimeRecv = currentTime;
    }
    return ret; 
}

#include <pybind11/pybind11.h>
#include <pybind11/embed.h>
#include <pybind11/stl.h> 
#include <regex>
namespace py = pybind11;

std::vector<byte> CallPackageFilter(void* buffer, size_t len)
{
    static py::scoped_interpreter guard{};
    try
    {
        std::string module_name             = "testing";
        boost::filesystem::path currentPath = zzj::GetDynamicLibPath(CallPackageFilter);
        auto module_path                    = currentPath / "packetfilter.py";
        py::module importlib                = py::module_::import("importlib.util");
        py::object spec   = importlib.attr("spec_from_file_location")(module_name, module_path.string());
        py::object module = importlib.attr("module_from_spec")(spec);
        spec.attr("loader").attr("exec_module")(module);

        std::vector<BYTE> packet((BYTE *)buffer, (BYTE *)buffer + len);
        auto pythonRet = module.attr("PacketSniffer")(packet);
        // 检查返回值是否为bytes或bytearray，并转换为std::vector<byte>
        if (py::isinstance<py::bytes>(pythonRet) || py::isinstance<py::bytearray>(pythonRet))
        {
            std::vector<byte> result = pythonRet.cast<std::vector<uint8_t>>();
            std::string packetRet;
            for (auto &byte : result)
            {
                packetRet += fmt::format("{:02x} ", byte);
            }
            spdlog::info("ModifiedPacket: {0}", packetRet);
            return result;
        }
    }
    catch (py::error_already_set &e)
    {
        spdlog::error("Python error: {0}", e.what());
    }
    catch (const std::exception &e)
    {
        spdlog::error("C++ error: {0}", e.what());
    }
    catch (...)
    {
        spdlog::error("Unknown error");
    }
    return {};
}

int(__fastcall *plainSendPackage)(void *pGameClient, void *edx, void *buffer, size_t len) = nullptr;
int __fastcall PlainSendHooked(void *pGameClient, void *edx, void *buffer, size_t len)
{
    static auto init = []() { 
        boost::filesystem::path currentPath = zzj::GetDynamicLibPath(CallPackageFilter);
        currentPath /= "pythonlib";
        Py_SetPythonHome(currentPath.wstring().c_str());
        return 0; 
        }();
    if (GameManager::hookSendEnable)
    {
        auto modifiedPackage = CallPackageFilter(buffer, len); 
        spdlog::info("original len :{}",len);
        if (modifiedPackage.size() > 0)
        {
            spdlog::info("send package modified");
            static byte *buf         = nullptr;
            if (buf == nullptr)
                buf = (byte*)malloc(0x100);

            ZeroMemory(buf, 0x100);
            memcpy(buf, modifiedPackage.data(), modifiedPackage.size());
            std::string packetRet;
            for (int i = 0; i < len;i++)
            {
                packetRet += fmt::format("{:02x} ", buf[i]);
            }
            spdlog::info("Buf string {}", packetRet);
            return plainSendPackage(pGameClient, edx, buf, len);
		}

    }
	return plainSendPackage(pGameClient, edx, buffer, len);
}
#include <imagehlp.h>
#pragma comment(lib, "Imagehlp.lib")

FARPROC* GetProcAddressFromIAT(HMODULE hModule, const char* szDllName, const char* szProcName) {
    ULONG ulSize = 0;
    PIMAGE_IMPORT_DESCRIPTOR pImportDesc = (PIMAGE_IMPORT_DESCRIPTOR)ImageDirectoryEntryToData(
        hModule, TRUE, IMAGE_DIRECTORY_ENTRY_IMPORT, &ulSize);
    
    if (pImportDesc == NULL)
    {
        spdlog::error("pImportDesc NULL");
        return NULL;
    }

    while (pImportDesc->Name) {
        const char* szCurrentDllName = (const char*)((PBYTE)hModule + pImportDesc->Name);
        spdlog::info("Handle dll {}", szCurrentDllName);
        if (_stricmp(szCurrentDllName, szDllName) == 0) {
            spdlog::info("find {}", szDllName);
            PIMAGE_THUNK_DATA pThunk = (PIMAGE_THUNK_DATA)((PBYTE)hModule + pImportDesc->FirstThunk);
            while (pThunk->u1.Function) {
                FARPROC* pfnAddress = (FARPROC*)&pThunk->u1.Function;
                if (*pfnAddress == GetProcAddress(GetModuleHandleA(szDllName), szProcName)) {
                    return pfnAddress;
                }
                pThunk++;
            }
        }
        pImportDesc++;
    }
    return NULL;
}

void GameManager::HookSendAndRecv()
{
    auto ws2_32 = LoadLibrary(L"ws2_32.dll");
    if (ws2_32 == NULL)
    {
        spdlog::error("LoadLibrary failed: {0}", GetLastError());
        return;
    }

    sendOriginAddress = decltype(sendOriginAddress)(GetProcAddress(ws2_32, "send"));
    if (sendOriginAddress == NULL)
    {
        spdlog::error("GetProcAddress failed: {0}", GetLastError());
        return;
    }
    spdlog::info("sendOriginAddress: {0:x}", (uintptr_t)sendOriginAddress);

    spdlog::info("recvOriginAddress: {0:x}", (uintptr_t)recvOriginAddress);
    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return;
    }
     
    plainSendPackage = decltype(plainSendPackage)(baseAddr + rawSendPackageOffset);
    spdlog::info("plainSendPackage: {0:x}", (uintptr_t)plainSendPackage);
    //auto mswsock = LoadLibrary(L"mswsock.dll");
    //wspSendOriginAddress = decltype(wspSendOriginAddress)(GetProcAddress(mswsock, "_WSPSend@36"));

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID &)sendOriginAddress, SendHooked);

    auto base123      = GetModuleBaseAddress("123.dll");
    if (base123 != NULL)
    {
        recvOriginAddress = decltype(recvOriginAddress)(base123 + 0x4cea7);
        DetourAttach(&(PVOID &)recvOriginAddress, RecvHooked);
        std::thread t1(RecvListerner);
        t1.detach();
    }

    DetourAttach(&(PVOID &)plainSendPackage, PlainSendHooked);
    //DetourAttach(&(PVOID &)wspSendOriginAddress, WSPSendHooked);
    DetourTransactionCommit();

}

std::string GameManager::CUser::GetName()
{
    std::string big5Str(name);
    if (big5Str.empty())
        return "(null)";
    return boost::locale::conv::to_utf<char>(big5Str, "Big5");
}
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")

ULONG ( WINAPI *getAdapterInfoOriginal)(
PIP_ADAPTER_INFO AdapterInfo,
PULONG           SizePointer
) = nullptr;
ULONG WINAPI GetAdapterInfoHooked(
PIP_ADAPTER_INFO AdapterInfo,
PULONG           SizePointer
)
{
    ULONG ret = getAdapterInfoOriginal(AdapterInfo, SizePointer);
    if(ret != ERROR_SUCCESS)
    {
        return ret;
    }

    PIP_ADAPTER_INFO pAdapterInfo = AdapterInfo;
    // Loop through all the adapters
    spdlog::info("GetAdapterInfoHooked called");
    while (pAdapterInfo)
    {
        //get a random mac address
        for (int i = 0; i < pAdapterInfo->AddressLength; i++)
        {
            pAdapterInfo->Address[i] = rand() % 256;
        }
        pAdapterInfo = pAdapterInfo->Next;
    }

    return ret;
}
void  GameManager::HookMachineCode()
{

    auto iphlpapi = LoadLibrary(L"iphlpapi.dll");
    if (iphlpapi == NULL)
    {
        spdlog::error("LoadLibrary failed: {0}", GetLastError());
        return;
    }

    getAdapterInfoOriginal = decltype(getAdapterInfoOriginal)(GetProcAddress(iphlpapi, "GetAdaptersInfo"));
    if (getAdapterInfoOriginal == NULL)
    {
        spdlog::error("GetProcAddress failed: {0}", GetLastError());
        return;
    }
    spdlog::info("GetAdaptersInfo hook address {0:x}", (uintptr_t)getAdapterInfoOriginal);
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID &)getAdapterInfoOriginal, GetAdapterInfoHooked);
    DetourTransactionCommit();
}

void GameManager::DeliverTask(int taskID, int npcID)
{
    zzj::Process process;
    zzj::Memory memory(process);
    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return;
    }
    uintptr_t gameClient = NULL;
    auto res             = memory.Read(baseAddr + gameClientOffset, &gameClient, sizeof(gameClient));
    if (!res)
    {
        spdlog::error("gameClient is null");
        return;
    }

    auto callAddr = baseAddr + rawSendPackageOffset;

    char buf[0x14]{0};
    buf[0]             = 0x14;
    *(DWORD *)&buf[4]  = 0x64592;
    *(DWORD *)&buf[8]  = taskID;
    *(DWORD *)&buf[12] = npcID;
    *(DWORD *)&buf[16] = 0x1;

    __asm
    {
        push ecx
        push eax
		mov ecx,gameClient
        push 0x14
        lea eax, buf
        push eax
		call callAddr
        pop eax
        pop ecx
    }
}
uint64_t(__fastcall *calculatorWorkFunc)(void *thisPtr, void *edx, void *buffer, uint64_t max, uint64_t a2, uint64_t a3,
                                    int a4) = nullptr;
uint64_t CalculatorHookFunction(void *thisPtr, void *edx, void *buffer, uint64_t max, uint64_t a2, uint64_t a3,
                           int a4)
{
    return max;
}
void GameManager::EnableMaxCalculator()
{
    zzj::Process process;
    zzj::Memory memory(process);
    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return;
    }

    calculatorWorkFunc = (decltype(calculatorWorkFunc))(baseAddr + calculatorOffset);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID &)calculatorWorkFunc, CalculatorHookFunction);
    DetourTransactionCommit();
}

void GameManager::DisableMaxCalculator()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&(PVOID &)calculatorWorkFunc, CalculatorHookFunction);
    DetourTransactionCommit();
}



GameManager::CQuest *GameManager::CQuestContainer::GetCQuest(int index)
{
    return cQuestPtr + index;
}
void GameManager::CloseSocket()
{
    zzj::Process process;
    zzj::Memory memory(process);
    auto baseAddr = GetModuleBaseAddress("SO3DPlus.exe");
    if (baseAddr == NULL)
    {
        return;
    }
    uintptr_t gameClient = NULL;
    auto res             = memory.Read(baseAddr + gameClientOffset, &gameClient, sizeof(gameClient));
    if (!res)
    {
        spdlog::error("gameClient is null");
        return;
    }
    uintptr_t vtableStartAddr;
    res = memory.Read(gameClient,&vtableStartAddr,sizeof(vtableStartAddr));
    spdlog::info("vatable start addr {:x}", vtableStartAddr);

    uintptr_t callAddr;
    res = memory.Read(vtableStartAddr, &callAddr, sizeof(callAddr));
    spdlog::info("call addr {:x}", callAddr);
    __asm
    {
        mov ecx,gameClient
        call callAddr
    }
    return;
}
