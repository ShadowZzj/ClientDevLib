#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace SO3D
{

// ============================================================
//  Inheritance:  CCharacter → CUser → CLocalUser
//
//  CUser allocation: 0x52590 (heap via sub_A5CE50 / operator new)
//    But CUser ctor (sub_A770D0) only initializes up to ~0x6AF4.
//    Static CUser[8] array stride = 0x6B00.
//  CLocalUser allocation: 0x9D38 (single instance, g_pLocalPlayer)
//    CLocalUser ctor (0x86F610) adds fields from 0x6B00 onwards.
//
//  Entity manager: dword_116E3D4 (RVA 0xD6E3D4)
//    +0x00  std::map sentinel ptr (red-black tree, entity ID → entity*)
//    +0x04  std::map size
//    +0x0C  CUser linked-list HEAD
//    +0x10  CUser linked-list TAIL
//
//  CUser linked-list threading (inside each CUser):
//    +0x64B4  prev CUser*
//    +0x64B8  next CUser*
//
//  CUser vftable RVA = 0xABAEE8
// ============================================================

#pragma pack(push, 4)
class CUser
{
  public:
    char pad_0000[0x3C];        // 0x0000  (vftable at +0x00)
    float x;                    // 0x003C
    float y;                    // 0x0040
    float z;                    // 0x0044
    char pad_0048[0x28];        // 0x0048
    uint32_t entityId;          // 0x0070
    char pad_0074[0x114];       // 0x0074
    int32_t attackStatus;       // 0x0188
    char pad_018C[0x08];        // 0x018C
    uint32_t actionState;       // 0x0194  >=14 means skill action
    char pad_0198[0x04];        // 0x0198
    int32_t intX;               // 0x019C
    int32_t intZ;               // 0x01A0
    float moveSpeed;            // 0x01A4
    char pad_01A8[0x38D8];      // 0x01A8
    char name[0x50];            // 0x3A80  GBK null-terminated char[]
    char pad_3AD0[0x29D4];      // 0x3AD0
    float attackSpeed;          // 0x64A4  animation divisor, lower = faster
    float skillSpeed;           // 0x64A8  animation divisor, lower = faster
    char pad_64AC[0x08];        // 0x64AC
    uintptr_t prevUser;         // 0x64B4  linked-list prev
    uintptr_t nextUser;         // 0x64B8  linked-list next
    char pad_64BC[0x644];       // 0x64BC  rest of CUser (ctor touches up to 0x6AF4)

  public:
    std::string GetName();
};
#pragma pack(pop)

static_assert(offsetof(CUser, x) == 0x3C);
static_assert(offsetof(CUser, entityId) == 0x70);
static_assert(offsetof(CUser, moveSpeed) == 0x1A4);
static_assert(offsetof(CUser, name) == 0x3A80);
static_assert(offsetof(CUser, attackSpeed) == 0x64A4);
static_assert(offsetof(CUser, skillSpeed) == 0x64A8);
static_assert(offsetof(CUser, prevUser) == 0x64B4);
static_assert(offsetof(CUser, nextUser) == 0x64B8);
static_assert(sizeof(CUser) == 0x6B00);

// ============================================================
//  AroundPlayer query
// ============================================================

struct AroundPlayer
{
    CUser      *user;
    std::string name;
    float       dist;
};

std::vector<AroundPlayer> GetAroundPlayers(const std::string &localPlayerName);

} // namespace SO3D
