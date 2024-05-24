// Created with ReClass.NET 1.2 by KN4CK3R

class CLocalUser
{
public:
	char pad_0000[60]; //0x0000
	float x; //0x003C
	float y; //0x0040
	float z; //0x0044
	char pad_0048[40]; //0x0048
	uint32_t unknown; //0x0070
	char pad_0074[276]; //0x0074
	int32_t attackStatus; //0x0188
	char pad_018C[8]; //0x018C
	uint32_t animation; //0x0194
	char pad_0198[4]; //0x0198
	int32_t intX; //0x019C
	int32_t intZ; //0x01A0
	float moveSpeed; //0x01A4
	char pad_01A8[4712]; //0x01A8
	char name[40]; //0x1410
	char pad_1438[16]; //0x1438
	uint32_t profession; //0x1448
	char pad_144C[4]; //0x144C
	uint32_t currentHP; //0x1450
	uint32_t maxHP; //0x1454
	uint32_t currentMP; //0x1458
	uint32_t maxMP; //0x145C
	char pad_1460[3836]; //0x1460
	float attackSpeed; //0x235C
	float skillSpeed; //0x2360
	char pad_2364[1216]; //0x2364
	uint32_t dieTrigger; //0x2824
	char loginUserName[16]; //0x2828
	char pad_2838[384]; //0x2838
	uint64_t money; //0x29B8
	uint32_t attack; //0x29C0
	char pad_29C4[120]; //0x29C4
	uint32_t comboValue; //0x2A3C
	uint32_t maxComboValue; //0x2A40
	char pad_2A44[136]; //0x2A44
	uint32_t attackRange; //0x2ACC
	char pad_2AD0[344]; //0x2AD0
	uint32_t skillId; //0x2C28
	uint32_t skillMode; //0x2C2C
	char pad_2C30[6204]; //0x2C30
}; //Size: 0x446C