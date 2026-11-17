#pragma once
#include <Windows.h>
#include <json.hpp>
#include <mutex>
#include <string>
#include <vector>
#include <optional>
class GameManager
{
  public:
    // CCreature
    static const uintptr_t creatureBaseOffset                              = 0xa18618;
    inline static const std::vector<unsigned int> creatureMultiLevelOffset = {0xC};
    enum CreatureType : int
    {
        Monster0 = 0x0,
        Monster1 = 0x1,
        Monster2 = 69,
        Monster3 = 60,
        Monster4 = 71,
        Monster5 = 73,
        Monster6 = 38,
        Talkable = 0x3,
        Tree     = 0xA,
    };
    class MonsterStatTable
    {
      public:
        uintptr_t vtable;  // 0x0000
        uint32_t index;    // 0x0004
        char name[144];    // 0x0008
        char pad_0098[8];  // 0x0098
        uint32_t level;    // 0x00A0
        char pad_00A4[4];  // 0x00A4
        uint32_t maxHp;    // 0x00A8
        char pad_00AC[16]; // 0x00AC
        uint32_t attack;   // 0x00BC
        uint32_t defense;  // 0x00C0
        char pad_00C4[4];  // 0x00C4
        uint32_t hitRate;  // 0x00C8
        uint32_t missRate; // 0x00CC
        uint32_t exp;      // 0x00D0
        char pad_00D4[8];  // 0x00D4
        CreatureType type; // 0x00DC
        char pad_00E0[52]; // 0x00E0
      public:
        std::string GetName();
        bool IsMonster();
    }; // Size: 0x0114
    static_assert(sizeof(MonsterStatTable) == 0x114);
    class CCreature
    {
      public:
        uintptr_t vtable;                            // 0x0000
        char pad_0000[56];                           // 0x0004
        float x;                                     // 0x003C
        float y;                                     // 0x0040
        float z;                                     // 0x0044
        char pad_0048[40];                           // 0x0048
        uint32_t monsterId;                          // 0x0070
        char pad_0074[684];                          // 0x0074
        uint32_t health;                             // 0x0320
        char pad_0324[68];                           // 0x0324
        class MonsterStatTable *monsterStatTablePtr; // 0x0368
        class CCreature *preMonsterPointer;          // 0x036C
        class CCreature *nextMonsterPointer;         // 0x0370
      public:
        float GetDistance(const CCreature &other) const
        {
            return sqrt((x - other.x) * (x - other.x) + (y - other.y) * (y - other.y) + (z - other.z) * (z - other.z));
        }
    }; // Size: 0x0374
    static_assert(sizeof(CCreature) == 0x374);

    class CreatureWithAddress
    {
      public:
        uintptr_t address;
        CCreature creature;
    };
    // monsterID  -1代表未选中
    static const uintptr_t monsterIdOffset = 0x13FB734;

    // GameClient
    static const uintptr_t gameClientOffset = 0x13fbd58;
    // 搜索攻击力变换，然后找[???+0xoffset]，然后ce搜???，一级指针就是
    static const uintptr_t localPlayerOffset = 0x95a784;
    //// localPlayer offset
    enum class Profession : uint32_t
    {
        Swordsman  = 1,
        Knight     = 2,
        Clown      = 3,
        Mage       = 4,
        Priest     = 5,
        Blacksmith = 6,
        Hunter     = 9
    };
#pragma pack(push, 4) // 将对齐设置为4字节
    // Created with ReClass.NET 1.2 by KN4CK3R

    class CLocalUser
    {
      public:
        char pad_0000[60];      // 0x0000
        float x;                // 0x003C
        float y;                // 0x0040
        float z;                // 0x0044
        char pad_0048[40];      // 0x0048
        uint32_t unknown;       // 0x0070
        char pad_0074[276];     // 0x0074
        int32_t attackStatus;   // 0x0188
        char pad_018C[8];       // 0x018C
        uint32_t animation;     // 0x0194
        char pad_0198[4];       // 0x0198
        int32_t intX;           // 0x019C
        int32_t intZ;           // 0x01A0
        float moveSpeed;        // 0x01A4
        char pad_01A8[4712];    // 0x01A8
        char name[40];          // 0x1410
        char pad_1438[16];      // 0x1438
        uint32_t profession;    // 0x1448
        char pad_144C[4];       // 0x144C
        uint32_t currentHP;     // 0x1450
        uint32_t maxHP;         // 0x1454
        uint32_t currentMP;     // 0x1458
        uint32_t maxMP;         // 0x145C
        char pad_1460[3836];    // 0x1460
        float attackSpeed;      // 0x235C
        float skillSpeed;       // 0x2360
        char pad_2364[1216];    // 0x2364
        uint32_t dieTrigger;    // 0x2824
        char loginUserName[16]; // 0x2828
        char pad_2838[147];     // 0x2838
        char password[16];      // 0x28CB
        char pad_28DB[221];     // 0x28DB
        uint64_t money;         // 0x29B8
        uint32_t attack;        // 0x29C0
        char pad_29C4[264];     // 0x29C4
        uint32_t attackRange;   // 0x2ACC
        char pad_2AD0[344];     // 0x2AD0
        uint32_t skillId;       // 0x2C28
        uint32_t skillMode;     // 0x2C2C
      public:
        static const uintptr_t xorValOffset = 0x8b8cdc;
        uint32_t GetXorEncryptVal();
        std::string GetName();
        uint32_t GetCurrentHP();
        uint32_t GetMaxHP();
        uint32_t GetCurrentMP();
        uint32_t GetMaxMP();

    };       

    class CUser
    {
      public:
        char pad_0000[60];      // 0x0000
        float x;                // 0x003C
        float y;                // 0x0040
        float z;                // 0x0044
        char pad_0048[40];      // 0x0048
        uint32_t unknown;       // 0x0070
        char pad_0074[276];     // 0x0074
        int32_t attackStatus;   // 0x0188
        char pad_018C[8];       // 0x018C
        uint32_t animation;     // 0x0194
        char pad_0198[4];       // 0x0198
        int32_t intX;           // 0x019C
        int32_t intZ;           // 0x01A0
        float moveSpeed;        // 0x01A4
        char pad_01A8[4712];    // 0x01A8
        char name[40];          // 0x1410
      public:
        std::string GetName();
    };
#pragma pack(pop) // 恢复对齐设置

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
    static const uintptr_t aroundPlayerOffset                                  = 0x95a780;
    static const uintptr_t aroundPlayerNameOffset                              = 0x1410;
    inline static const std::vector<unsigned int> aroundPlayerMultiLevelOffset = {0x10};
    static const uintptr_t nextPlayerPointerOffset                             = 0x236c;

    // CItemContainter offset
    static const uintptr_t itemContainterOffset = 0x95c4b0;
    static const uintptr_t firstItemOffset      = 0xaa0;
    static const DWORD itemStructSize           = 0xc8;
    inline static unsigned int itemFullCount    = 192;
    static const unsigned int itemTableSize     = 0x3a8;

#pragma pack(push, 4) // 将对齐设置为4字节
    class CItemTable
    {
      public:
        uint32_t virtualTable; // 0x0000
        uint32_t itemId;       // 0x0004
        uint32_t itemId2;      // 0x0008
        char itemName[260];    // 0x000C
        uint32_t itemType;     // 0x0110
        char pad_0114[4];      // 0x0114
        uint32_t hpHeal;       // 0x0118
        uint32_t mpHeal;       // 0x011C
        char pad_0120[92];     // 0x0120
        char description[256]; // 0x017C
        char pad_027C[112];    // 0x027C
        uint64_t sellMoney;    // 0x02EC
        char pad_02F4[4];      // 0x02F4
        uint32_t cooldown;     // 0x02F8
        char pad_02FC[172];    // 0x02FC
      public:
        std::string GetItemName();
        std::string GetItemDescription();
    }; // Size: 0x03A8
    static_assert(sizeof(CItemTable) == itemTableSize);
#pragma pack(pop) // 恢复对齐设置
    inline static uintptr_t useGearCallOffset = 0x384640;
    enum class GearType : UINT32
    {
        None,
        attack,
        mana,
        hitrate,
        avoidance,
        defence,
        critical,
        attackSpeed,
        moveSpeed,
        hp,
        ap,
        hpPercent,
        apPercent,
        damagePercent,
        defensePercent,
        powerLevel,
        agileLevel,
        intelligenceLevel,
        luckyLevel,
        staminaLevel,
        spiritLevel,
        levelMinus,
        experience,
        dungeonDamage
    };
    class GearInfo
    {
      public:
        GearType type;  // 0x0000
        uint16_t value; // 0x0004
        uint16_t every; // 0x0006

    //operator ==
        bool operator!=(const GearInfo& other) const
        {
		    return type != other.type || value != other.value || every != other.every;
        }
    };                  // Size: 0x0008
    static_assert(sizeof(GearInfo) == 0x8);

    class Item
    {
      public:
        uint32_t bagId;              // 0x0000
        uint32_t itemId;             // 0x0004
        char pad_0008[8];            // 0x0008
        uint32_t count;              // 0x0010
        char pad_0014[12];           // 0x0014
        class CItemTable *itemTable; // 0x0020
        char pad_0024[4];            // 0x0024
        float cooldownLeft;          // 0x0028
        char pad_002C[120];          // 0x002C
        uint32_t gearLevel;          // 0x00A4
        class GearInfo gearInfo[3];  // 0x00A8
        char pad_00C0[8];            // 0x00C0
    };                               // Size: 0x00C8
    static_assert(sizeof(Item) == itemStructSize);

    class DropItem
    {
      public:
        uint32_t dropId;                // 0x0000
        uint32_t itemId;                // 0x0004
        char pad_0008[12];              // 0x0008
        float x;                        // 0x0014
        float y;                        // 0x0018
        float z;                        // 0x001C
        class CItemTable *itemTablePtr; // 0x0020
        char pad_0024[8];               // 0x0024
        int8_t N0000217A;               // 0x002C
        int8_t N00004785;               // 0x002D
        int8_t canPick;                 // 0x002E
        int8_t N00004786;               // 0x002F
        float pickLeftTime;             // 0x0030
        char pad_0034[84];              // 0x0034
        class DropItem *next;           // 0x0088
        char pad_008C[60];              // 0x008C
    };                                  // Size: 0x00C8
    static_assert(sizeof(DropItem) == 0xC8);

    class CItemContainer
    {
      public:
        uint32_t vtable;             // 0x0000
        char pad_0004[104];          // 0x0004
        class DropItem *dropItemPtr; // 0x006C
        char pad_0070[2608];         // 0x0070
        class Item items[192];       // 0x0AA0
        char pad_A0A0[3488];         // 0xA0A0
    };                               // Size: 0xAE40
    static_assert(sizeof(CItemContainer) == 0xAE40);

    inline static const std::vector<std::string> remoteSellerItemList = {
        u8"魔法戒指", u8"雞蛋", u8"香草", u8"紅藥水", u8"藍藥水", u8"紅藥水(中)", u8"藍藥水(中)", u8"紅藥水(大)", u8"藍藥水(大)"};
    // PacketStruct
    class BuyItemPacket
    {
      public:
        uint32_t constant = 2;
        uint32_t sellerItemIndex;
        uint32_t count;
        uint32_t bagPosStart0xD = 0xD;
        uint32_t sellerId = 0xfecd2408;//remote seller
    };
    // CCashInven offset
    static const uintptr_t cashItemTableOffset          = 0x13fbd58;
    static const uintptr_t cashInvenOffset              = 0x957700;
    static const uintptr_t cashInvenSize                = 80;
    static const uintptr_t firstCashItemOffset          = 0x40;
    inline static const uintptr_t useCashItemFuncOffset = 0x6a0dc0;
    inline static std::string cashSellerItemName        = "\xE6\x94\xA4\xE8\xB2\xA9\xE5\x91\xBC\xE5\x8F\xAB";
    // ItemTable offset
    static const uintptr_t itemCoolDownOffset = 0x2f8;
    // end
    //
    // SkillRelated offset
    static const uintptr_t skillIndexBase          = 0xA1be38;
    static const uintptr_t skillArrayPointerOffset = 0x440;
    static const uintptr_t skillArraySizeOffset    = 0x444;
    class SkillTable
    {
    public:
      enum class SkillType :uint32_t
      {
          Self = 1,
          SingleAttack = 2,
          RangeAttack = 3,
      };
    	uint32_t vtable; //0x0000
    	uint32_t skillId; //0x0004
    	uint32_t skillId2; //0x0008
    	char skillName[256]; //0x000C
    	char pad_010C[4]; //0x010C
    	SkillType skillType; //0x0110
    	char pad_0114[40]; //0x0114
    	uint32_t apCost; //0x013C
    	char pad_0140[8]; //0x0140
    	int32_t skillCoverRange; //0x0148
    	int32_t skillRange; //0x014C
    	float preTime; //0x0150
    	char pad_0154[4]; //0x0154
    	float coolDown; //0x0158
    	char pad_015C[4]; //0x015C
    	uint32_t attack; //0x0160
    	char pad_0164[12]; //0x0164
    	char description[60]; //0x0170
    	char pad_01AC[220]; //0x01AC

        std::string GetSkillName();
        std::string GetSkillDescription();
    }; //Size: 0x0288
    static_assert(sizeof(SkillTable) == 0x288);
    class CSkill
    {
      public:
        uintptr_t virtualTable;
        UINT32 skillId;
        char padding0[16];
        float leftCoolDown;
        char padding1[4];
        UINT32 skillLevel;
        SkillTable *skillTable;
    };
    static_assert(sizeof(CSkill) == 0x28);
    static const uintptr_t skillSendPackageOffset    = 0x6a2d60;
    static const uintptr_t generalSendPackageOffset  = 0x6a0cb0;
    static const uintptr_t pickItemSendPackageOffset = 0x6a0f50;
    static const uintptr_t buyItemSendPackageOffset = 0x6a2d60;
    static const uintptr_t rawSendPackageOffset      = 0x6919e0;
    // SkillTable offset
    static const uintptr_t skillRangeOffset   = 0x14c;
    static const uintptr_t skillPretimeOffset = 0x150;
    
    // end

    // GuiMenu
    static const uintptr_t CMerchantVirtualTableOffset                    = 0x81c548;
    static const uintptr_t CRewardAccessDialogVirtualTableOffset          = 0x826a50;
    static const uintptr_t guiIndexerOffset = 0x13e5c98;
    static const uintptr_t FindGuiWithIndexFuncOffset                          = 0x56a0c0;
    inline static const std::vector<unsigned int> guiMenuMultilevelOffset = {0x8,0x0,0x8};
    inline static const std::vector<unsigned int> firstGuiMenuMultilevelOffset = {0x0, 0x4};

    
    class CGUIMenuData
    {
      public:
        uint32_t bagABSPos; // 0x0000
        uint32_t hasItem;   // 0x0004
        uint32_t bagPos;    // 0x0008
        char pad_000C[56];  // 0x000C
    };                      // Size: 0x0044

    static_assert(sizeof(CGUIMenuData) == 0x44);
    class CGUIMenu
    {
      public:
        char pad_0000[876];       // 0x0000
        class CGUIMenuData *data; // 0x036C
        char pad_0370[20];        // 0x0370
    };                            // Size: 0x0384

    static_assert(sizeof(CGUIMenu) == 0x384);
    class CMenuContainerEx
    {
      public:
        uint32_t vtable;     // 0x0000
        char pad_0004[16];   // 0x0004
        uint32_t mouseX;     // 0x0014
        uint32_t mouseY;     // 0x0018
        char pad_001C[9];    // 0x001C
        bool isClosed;       // 0x0025
        char pad_0026[18]; // 0x0026
        CGUIMenu *guiMenu;
        char pad_0038[4];    // 0x0038
    };

    enum GUIIndex : uint32_t
    {
        RewardAccess  = 0x3f,
        RewardAttence = 0x3e,
        Seller = 0x17,
        CMagicSpringOption = 0x4f
	};
    class SingleRewardInfo
    {
      public:
        enum class Status : uint32_t
        {
            CanNotGet = 0,
            CanGet = 1,
            Got = 2
        };
        char pad0[4];
        char pad1[4];
        Status status;

    };
    template<size_t N>
    class RewardInfoTable
    {
      public:
        SingleRewardInfo *rewardInfo[N];
    };
    class CRewardAccessDialog :CMenuContainerEx
    {
      public:
        RewardInfoTable<6> *rewardInfoTable;
    };
    class CRewardAttenceDialog :CMenuContainerEx
    {
	  public:
		RewardInfoTable<28> *rewardInfoTable;
	};
    class GUIStruct
    {
      public:
          GUIStruct *left;
          char pad1[4];
          GUIStruct* right;
          char pad2[1];
          bool stopCondition;
          char pad3[2];
          UINT32 guiIndex;
          CMenuContainerEx* menu;
    };
    static_assert(sizeof(GUIStruct) == 0x18);
    //end

    //npc
    class NPCInteract
	{
      public:
        uint32_t len       = sizeof(NPCInteract);
        uint32_t packageId = 0x64578;
        uint32_t padding   = 0;
        uint32_t localPlayerX;
        uint32_t localPlayerZ;
        uint32_t npcId;
	};
    class DeliverLetterPacket
    {
      public:
        uint32_t len = sizeof(DeliverLetterPacket);
        uint32_t packageId = 0x64592;
        uint32_t pad1      = 0x6be;//blue eyes
        uint32_t pad2      = 0x4ffd;
        uint32_t pad3      = 1;
    };
    class NPCManager
    {
      public:
        static const uint32_t letterNpcId = 0x1f4;
        static void InteractWithNpc(uint32_t npcId);
        static void DeliverLetter(GameManager::DeliverLetterPacket packet);
    };

    //autohunt
    inline static const uintptr_t autoHuntBaseAddr = 0x957438;//not offset
    // Created with ReClass.NET 1.2 by KN4CK3R

    enum AutoHuntStatus : uint32_t
    {
        Stop = 1,
        Running = 5
    };
    class AutoHuntManager
    {
      public:
        char pad_0000[40];  // 0x0000
        AutoHuntStatus status; // 0x0028
        char pad_002C[8];   // 0x002C
        float x;            // 0x0034
        float y;            // 0x0038
        float z;            // 0x003C
        char pad_0040[964]; // 0x0040
    };  
    // Size: 0x0404
    static_assert(sizeof(AutoHuntManager) == 0x404);
    //end


    class CQuestTable
    {
    };
    class CQuest
    {
      public:
        uint32_t vtable;              // 0x0000
        char pad_0004[4];             // 0x0004
        uint32_t index;               // 0x0008
        class CQuestTable *N000051DD; // 0x000C
        char pad_0010[12];            // 0x0010
    };                                // Size: 0x001C
    static_assert(sizeof(CQuest) == 0x1C);


    class CQuestContainer
    {
      public:
        char pad_0000[1032];     // 0x0000
        class CQuest *cQuestPtr; // 0x0408

        CQuest *GetCQuest(int index);
    };                           // Size: 0x040C
    static_assert(sizeof(CQuestContainer) == 0x40C);

    inline static const uintptr_t cQuestContainerBaseOffset = 0xa1c350;
    inline static const std::string attackRangePattern = "C7 80 CC 2A 00 00 01 00 00 00 8B 8D";
    inline static const std::string attackSpeedPattern = "F3 0F 11 88 5c 23 00 00";
    inline static const std::string moveSpeedPattern =
        "F3 0F 11 81 A4 01 00 00 8B 95 68 FB FF FF F3 0F 10 82 A4 01 00 00 0F 2F 05 60";
    inline static const std::uintptr_t  moveSpeedUnlimitOffset       = 0x2e224b;
    inline static const std::uintptr_t moveSpeedLimitValueOffset = 0x80c360;

    inline static const std::string itemNoCoolDownPattern         = "F3 0F 2A 81 F8 02 00 00";
    inline static const std::string skillRangePattern             = "8B 81 4C 01 00 00";
    inline static const std::string skillNoPretimePattern         = "F3 0F 10 81 50 01 00 00";
    inline static const std::string skillSpeedPattern             = "F3 0F 11 88 60 23 00 00 F3 0F 2A 85 EC";
    inline static const std::string skillModeChangePattern          = "89 88 2C 2C 00 00";
    inline static const std::string animationModeChangePattern      = "89 88 94 01 00 00 8B 55 F8";
    inline static const std::string skillCoolDownCalculatePattern = "F3 0F 10 8A 58 01 00 00";
    inline static const std::string sendPackageCallPattern        = "55 8B EC 83 EC 18 89 4D F8 8B 45 F8 0F B6 48";
    inline static const uintptr_t cameraDistanceHookFunctionOffset  = 0x51d8a1;
    inline static const uintptr_t camearDistanceMaxValueOffset      = 0x85dc18;
    inline static const uintptr_t sellItemFuncAddressOffset       = 0x3be400;
    inline static const uintptr_t popupWindowHandlerFuncOffset    = 0x506af0;

    inline static const uintptr_t loginFunctionOffset = 0x36d4c0;
    inline static const uintptr_t mapFindValueFunctionOffset = 0x6b9710;
    inline static const uintptr_t mapFindValueFirstArgOffset = 0xa1cbbc;
    inline static const uintptr_t loginClientOffset = 0xa1cb8c;
    inline static const uintptr_t gServerOffset = 0x95ce04;
    inline static const uintptr_t gChannelOffset             = 0x95ce08;
    inline static const uintptr_t gServerMinus1Offset        = 0x94c788;
    inline static const uintptr_t gRolesBeginOffset          = 0x95e438;
    inline static const uintptr_t generalObjectOffset        = 0xa1cff0;
    inline static const uintptr_t calculatorOffset           = 0x4f76c0;
  public:
    GameManager();
    ~GameManager();
    void RefreshConfig();
    void SaveConfig();
    void DropItemFunc(unsigned int bagId, unsigned int count);
    void SellItem(unsigned int beginBagId);
    void SellItem(unsigned int bagId, unsigned int count);
    bool UseCashItem(unsigned int bagId);
    bool UseCashItem(std::string itemName);
    void CastSkill(CSkill skill, std::vector<CreatureWithAddress> creatures);
    void ThrowBomb();
    void PickItem(unsigned int dropId);
    void BuyItem(BuyItemPacket packet);
    void AutoLoginHandler();
    void Login(const std::string& userName, const std::string& password);
    void ChooseServer(int channel);
    void ChooseRole(int index);
    void OpenRewardAccessGui();
    void CloseRewardAccessGui();
    void GetRewardAccessReward(int index);
    void OpenRewardAttenceGui();
    void CloseRewardAttenceGui();
    void GetRewardAttenceReward(int index);
    void HookMachineCode();
    void DeliverTask(int taskID, int npcID);
    void EnableMaxCalculator();
    void DisableMaxCalculator();
    CMenuContainerEx *GetMenuContainer(GUIIndex index);
    std::vector<SingleRewardInfo> GetRewardInfo(GUIIndex rewardGuiType);
    AutoHuntManager* GetAutoHuntManager();
    void CloseSellerGui();
    CMenuContainerEx* GetCurrentOpenGuiMenu();
    bool HasMonsterSelected();
    uintptr_t GetLocalPlayerBase();

    bool EnablePopupWindowHook();
    bool EnableAttackRange();
    bool DisableAttackRange();

    bool EnableAttackSpeed();
    bool DisableAttackSpeed();

    bool EnableSkillRange();
    bool DisableSkillRange();

    bool EnableItemNoCoolDown();
    bool DisableItemNoCoolDown();

    bool EnableSkillSpeed();
    bool DisableSkillSpeed();

    bool EnableMoveSpeed();
    bool DisableMoveSpeed();

    bool EnableCameraDistance();
    bool DisableCameraDistance();

    bool EnableSpeedHack();
    bool DisableSpeedHack();
    void HookSendAndRecv();
    void SetSpeed(float speed);
    CItemContainer *GetItemContainer();
    std::optional<GameManager::Item> GetCurrentGearItem();
    void ChangeGear();
    std::vector<std::string> GetAroundPlayersName();
    std::vector<CUser> GetAroundPlayers();
    std::vector<Item> GetBagItems();
    int GetItemCount(const std::string &name);
    std::vector<Item> GetCashItems();
    std::vector<CSkill> GetSkills();
    std::vector<CreatureWithAddress> GetCreatures();
    std::vector<CreatureWithAddress> GetMonsters(uint32_t range);
    std::vector<CreatureWithAddress> GetMonsters(CCreature creature, uint32_t range);

    std::optional<bool> Dll123IsAutoHuntEnable();
    void GameManager::Dll123SetAutoHuntEnable(bool enable);
    void Dll123SetAutoHuntPos(int x, int z);
    static uintptr_t GetModuleBaseAddress(const std::string &moduleName);
    void CloseSocket();

    void OpenSandBox();
    void OpenSandBoxImp(const std::vector<GameManager::Item> &items, int bagPos);
    inline static int attackRange        = 1;
    inline static bool attackRangeEnable = false;

    inline static float attackSpeed      = 1.0f;
    inline static bool attackSpeedEnable = false;

    inline static bool skillRangeEnable = false;

    inline static bool itemNoCoolDownEnable = false;
    inline static int itemCoolDown          = 4;

    inline static bool skillSpeedEnable = false;

    inline static bool moveSpeedEnable = false;
    inline static float moveSpeed      = 7.0f;

    inline static bool speedHackEnable      = false;
    inline static float speedHack           = 1.0f;
    inline static uintptr_t localPlayerBase = 0;

    inline static bool skillAutoCastEnable = false;
    inline static bool autoPickItemEnable  = false;
    inline static bool fireFullPowerEnabled       = false;
    inline static int fireFullPowerIntervalValue = 300;
    inline static int fireFullPowerMaxCreature           = 20;
    inline static bool hookSendEnable             = false;
    inline static bool isAutoSell                        = false;
    inline static bool autohuntEnable             = true;
    inline static bool autohuntFirmPositionEnable = false;
    inline static bool tempPauseEnable                   = false;
    nlohmann::json config;

  private:
    std::mutex readMutex;
};

extern GameManager gameManager;
