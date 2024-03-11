#pragma once
#include <Windows.h>
#include <string>
#include <vector>
class GameManager
{
  public:
    GameManager();
    ~GameManager();

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


    bool EnableSpeedHack();
    bool DisableSpeedHack();
    void SetSpeed(float speed);
    std::vector<std::string> GetAroundPlayersName();
    inline static int attackRange = 1;
    inline static bool attackRangeEnable                   = false;

    inline static float attackSpeed      = 1.0f;
    inline static bool attackSpeedEnable = false;

    inline static bool skillRangeEnable = false;

    inline static bool itemNoCoolDownEnable = false;
    inline static int itemCoolDown          = 2;

    inline static bool skillSpeedEnable = false;

    inline static bool moveSpeedEnable = false;
    inline static float moveSpeed      = 7.0f;

    inline static bool speedHackEnable = false;
    inline static float speedHack      = 2.0f;

  private:
};