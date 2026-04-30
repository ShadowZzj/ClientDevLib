# SO3DCheat 迁移清单（from so3dFullCheat）

> 基址、偏移、数据结构均需重新逆向，以下列出旧版所有功能模块及其依赖的关键基址/结构体，供迁移时逐项核对。

---

## 1. 数据结构（需重新逆向偏移）

### 1.1 CLocalUser（本地玩家）
- 基址: `localPlayerOffset = 0x95a784`
- 关键字段偏移:
  - `x/y/z` (0x3C/0x40/0x44) — 坐标
  - `intX/intZ` (0x19C/0x1A0) — 整数坐标
  - `moveSpeed` (0x1A4) — 移动速度
  - `attackSpeed` (0x235C) — 攻击速度
  - `skillSpeed` (0x2360) — 技能速度
  - `name` (0x1410) — 角色名
  - `profession` (0x1448) — 职业
  - `currentHP/maxHP` (0x1450/0x1454)
  - `currentMP/maxMP` (0x1458/0x145C)
  - `money` (0x29B8) — 金币
  - `attack` (0x29C0) — 攻击力
  - `attackRange` (0x2ACC) — 攻击范围
  - `skillId/skillMode` (0x2C28/0x2C2C) — 当前技能
  - `dieTrigger` (0x2824) — 死亡触发
  - `loginUserName` (0x2828) — 登录账号名
  - `password` (0x28CB) — 密码
  - XOR加密值: `xorValOffset = 0x8b8cdc`

### 1.2 CCreature（怪物/NPC）
- 基址: `creatureBaseOffset = 0xa18618`, 多级偏移 `{0xC}`
- 结构体大小: `0x374`
- 关键字段:
  - `x/y/z` (0x3C/0x40/0x44) — 坐标
  - `monsterId` (0x0070)
  - `health` (0x0320) — 血量
  - `monsterStatTablePtr` (0x0368) — 怪物属性表指针
  - `preMonsterPointer/nextMonsterPointer` (0x036C/0x0370) — 链表

### 1.3 MonsterStatTable（怪物属性表）
- 结构体大小: `0x114`
- 关键字段: `index`, `name[144]`, `level`, `maxHp`, `attack`, `defense`, `exp`, `type`

### 1.4 CItemContainer / Item / DropItem
- 基址: `itemContainterOffset = 0x95c4b0`
- `firstItemOffset = 0xaa0`, `itemStructSize = 0xC8`, 最大192个
- **Item** 关键字段: `bagId`, `itemId`, `count`, `itemTable`指针, `cooldownLeft`, `gearLevel`, `gearInfo[3]`
- **DropItem**: 链表结构，含 `dropId`, `itemId`, `x/y/z`, `canPick`, `next` 指针
- **CItemTable** (0x3A8): `itemId`, `itemName[260]`, `hpHeal/mpHeal`, `description[256]`, `sellMoney`, `cooldown`

### 1.5 CSkill / SkillTable
- 基址: `skillIndexBase = 0xA1be38` → **新版: `SO3D.exe+0xDB2E78` (g_pSkillManager，独立于 g_pLocalPlayer)**
- `skillArrayPointerOffset = 0x440`, `skillArraySizeOffset = 0x444` ← **未变**
- **CSkill** (0x28): `vtable`, `skillId`(+0x04), `flag`(+0x08), `leftCoolDown`(+0x18,float), `skillLevel`(+0x20), `skillTable*`(+0x24)
- **SkillTable** (0x288): `skillId`(+0x04), `skillName[256]`(+0x0C,GBK), `skillType`(+0x110), `maxLevel`(+0x118), `apCost`(+0x13C), `skillCoverRange`(+0x148), `skillRange`(+0x14C), `preTime`(+0x150), `coolDown`(+0x158), `attack`(+0x160) ← **偏移全部未变**
- **SkillDataManager 单例**: `[0x1BA3948]`, +0x50 = 普通技能表 map, +0x70 = 强化技能表 map
- 详细分析见 [SKILL.md](SKILL.md)

### 1.6 GUI 相关
- `guiIndexerOffset = 0x13e5c98`
- `FindGuiWithIndexFuncOffset = 0x56a0c0`
- GUI 索引: RewardAccess=0x3f, RewardAttence=0x3e, Seller=0x17, CMagicSpringOption=0x4f
- `CMerchantVirtualTableOffset = 0x81c548`

### 1.7 AutoHuntManager（自动挂机）
- 基址: `autoHuntBaseAddr = 0x957438`
- 结构体大小: `0x404`
- 关键字段: `status` (0x28, Stop=1/Running=5), `x/y/z` (0x34/0x38/0x3C)

### 1.8 Quest 相关
- `cQuestContainerBaseOffset = 0xa1c350`

---

## 2. 功能模块

### 2.1 战斗增强
| 功能 | 说明 | 关键Pattern/偏移 |
|------|------|-----------------|
| **AttackRange** | 修改攻击范围 (1~10) | `attackRangePattern` |
| **AttackSpeed** | 修改攻击速度 (0.0~1.0) | `attackSpeedPattern` |
| **SkillRange** | 修改技能范围 | `skillRangePattern` |
| **SkillSpeed** | 修改技能施放速度 | `skillSpeedPattern` |
| **ItemNoCoolDown** | 物品无冷却 (可调 1~100) | `itemNoCoolDownPattern` |
| **FullFirePower** | 自动全力施放技能(X键切换) | 遍历技能表+范围内怪物，自动CastSkill |
| **MakeBomb** | 铁匠/爆破专用：制作+投掷炸弹 | skills[0x145] |

### 2.2 移动增强
| 功能 | 说明 | 关键Pattern/偏移 |
|------|------|-----------------|
| **MoveSpeed** | 修改移动速度 (1.0~25.0) | `moveSpeedPattern`, `moveSpeedUnlimitOffset`, `moveSpeedLimitValueOffset` |
| **SpeedHack** | 全局加速 (1.0~10.0) | Hook GetTickCount/GetTickCount64/QueryPerformanceCounter |

### 2.3 自动化
| 功能 | 说明 | 依赖 |
|------|------|------|
| **AutoHunt** | 自动挂机（通过Dll123接口控制） | `Dll123IsAutoHuntEnable`/`Dll123SetAutoHuntEnable` |
| **AutoHuntFirmPosition** | 挂机固定位置（保存/恢复坐标） | AutoHuntManager + 角色配置JSON |
| **AutoPickup** | 自动拾取掉落物（0.5秒间隔，距离<4） | DropItem链表 + PickItemFilter正则 |
| **AutoSell** | 自动贩卖（背包>150个物品时触发） | 使用商城摊贩呼叫道具 → SellItem |
| **AutoGear** | 自动附魔（按JSON目标循环ChangeGear） | GearInfo/GearGoal JSON配置 |
| **AutoLogin** | 自动登录 | `loginFunctionOffset`, `ChooseServer`, `ChooseRole` |

### 2.4 商店/物品
| 功能 | 说明 |
|------|------|
| **SellItem** | 指定背包格卖出（打开商城摊贩NPC） |
| **BuyItem** | 从远程摊贩购买物品 |
| **UseCashItem** | 使用商城道具（按ID或名称） |
| **CashItemHandler** | 定时自动使用商城道具（按JSON间隔配置） |
| **DropItem** | 丢弃指定背包格物品 |
| **ShowItems/ShowSkill/ShowMonsters/ShowDropItem** | 调试用：打印背包/技能/怪物/掉落物信息到日志 |
| **OpenBox** | 打开沙盒（自动消耗钥匙开箱） |

### 2.5 NPC/任务
| 功能 | 说明 |
|------|------|
| **DeliverLetter** | 送信（蓝眼/小引擎） |
| **DeliverThing** | 交付任务物品（牙齿/树/引擎/发卡） |
| **Teleport** | 传送（火/木/沙） |
| **GetReward** | 自动领取签到/登录奖励（6小时间隔） |

### 2.6 视觉/环境
| 功能 | 说明 | 偏移 |
|------|------|------|
| **CameraDistance** | 修改镜头最大距离 | `cameraDistanceHookFunctionOffset`, `camearDistanceMaxValueOffset` |
| **PopupWindowHook** | 屏蔽弹窗 | `popupWindowHandlerFuncOffset = 0x506af0` |
| **CalculatorMax** | 计算器最大值hook | `calculatorOffset = 0x4f76c0` |

### 2.7 网络/安全
| 功能 | 说明 |
|------|------|
| **HookSendAndRecv** | Hook发送和接收封包（HookSend开关） |
| **Messager** | 定时上报角色信息到远程服务器（金币/死亡/状态） |
| **CardHandler** | 卡密验证（10秒轮询，失败5次退出） |
| **AutoSwitch** | 检测周围陌生玩家 → 自动暂停所有hack功能 |

### 2.8 配置系统
| 功能 | 说明 |
|------|------|
| **RoleConfig** | 每角色独立JSON配置（自动加载/保存） |
| **GlobalConfig** | 全局config.json（nameAlert、PickItemFilter、roleConfig等） |
| **RunningEnvironment** | 运行时状态JSON（奖励领取时间、商城道具使用时间等） |

---

## 3. 关键封包函数偏移（需重新定位）

| 函数 | 旧偏移 |
|------|--------|
| skillSendPackage | `0x6a2d60` |
| generalSendPackage | `0x6a0cb0` |
| pickItemSendPackage | `0x6a0f50` |
| buyItemSendPackage | `0x6a2d60` |
| rawSendPackage | `0x6919e0` |
| useCashItemFunc | `0x6a0dc0` |
| sellItemFunc | `0x3be400` |
| loginFunction | `0x36d4c0` |
| useGearCall | `0x384640` |

---

## 4. 特征码（需重新扫描确认）

| 名称 | Pattern |
|------|---------|
| attackRange | `C7 80 CC 2A 00 00 01 00 00 00 8B 8D` |
| attackSpeed | `F3 0F 11 88 5c 23 00 00` |
| moveSpeed | `F3 0F 11 81 A4 01 00 00 8B 95 68 FB FF FF ...` |
| itemNoCoolDown | `F3 0F 2A 81 F8 02 00 00` |
| skillRange | `8B 81 4C 01 00 00` |
| skillNoPretime | `F3 0F 10 81 50 01 00 00` |
| skillSpeed | `F3 0F 11 88 60 23 00 00 F3 0F 2A 85 EC` |
| skillModeChange | `89 88 2C 2C 00 00` |
| animationModeChange | `89 88 94 01 00 00 8B 55 F8` |
| skillCoolDownCalculate | `F3 0F 10 8A 58 01 00 00` |
| sendPackageCall | `55 8B EC 83 EC 18 89 4D F8 8B 45 F8 0F B6 48` |

---

## 5. 建议迁移顺序

1. **基础设施**: GameManager 数据结构(CLocalUser/CCreature/CItem) + 基址定位
2. **核心战斗**: AttackRange → AttackSpeed → SkillRange → SkillSpeed
3. **移动**: MoveSpeed → SpeedHack
4. **自动化**: AutoPickup → AutoSell → FullFirePower → AutoHunt
5. **物品**: BuyItem → SellItem → UseCashItem → CashItemHandler
6. **视觉**: CameraDistance → PopupWindowHook
7. **网络**: HookSend → Messager → CardHandler
8. **高级**: AutoGear → AutoLogin → Reward → AutoSwitch

---

## 6. 技能释放流程分析（新版已验证）

> 以下基于 IDA Pro 逆向新版 SO3D.exe（基址 0x400000）的完整分析。

### 6.1 帧更新调用链

```
GameMainLoop (sub_9FEBF0)
  └─ EntitySystem__FrameUpdate (0xA285A0)
      ├─ CLocalUser__FrameUpdate (0xA23CB0)
      │   ├─ CLocalUser__ActionUpdate (0x871640)
      │   │   └─ AnimSystem__UpdateFrame (0xA784B0)  ← 动画速度乘数
      │   │       ├─ divss by [player+0x64A4] (attackSpeed, 普攻)
      │   │       └─ divss by [player+0x64A8] (skillSpeed, 技能)
      │   └─ CLocalUser__TickPreTimers (0x873BB0)  ← preTime 定时器
      │       └─ 检查 timing table，过期则触发动作
      └─ CLocalUser__TickSkillCooldowns (0x9DE790)  ← 技能冷却
          └─ CSkill.leftCoolDown -= deltaTime
```

### 6.2 技能施放响应处理

**服务器封包 → `CLocalUser__OnSkillCastResponse` (0x887610)**:

1. 读取服务器返回数据：技能ID、preTime原始值、目标数量
2. 计算 preTime：`preTime = (float)serverValue / constant` (0x887673)
3. 对每个目标调用 `SkillEffect__Execute` (0xAFCD50) — 读取 `player+0x64A8`(skillSpeed)
4. 调用 `CLocalUser__ScheduleAction` (0x871590) — 存储 preTime 到时间表 (0x88773D)
5. 设置动作状态（54-58，对应不同技能类型）

### 6.3 preTime 定时器机制

**时间表结构** (CLocalUser + 0x7054):
- 4层(layer) × 64槽(slot) × 20字节/条目 = 5120 字节
- 每条目 20 字节:

| 偏移 | 大小 | 说明 |
|------|------|------|
| +0 | 1B | active flag（1=活跃） |
| +4 | 4B | initTime（技能发出时的全局时间） |
| +8 | 4B | preTime（前摇持续时间，float） |
| +12 | 4B | 附加数据 |
| +16 | 4B | 保留 |

**写入**: `CLocalUser__ScheduleAction` (0x871590, 写指令 0x8715DC)
```asm
movups [edx+ecx*4+7054h], xmm0  ; 写入 {flag, initTime, preTime, extra}
```

**读取/判定**: `CLocalUser__TickPreTimers` (0x873BB0)
```asm
movss xmm1, [esi+eax*4+7058h]  ; initTime
addss xmm1, [esi+ecx*4+705Ch]  ; + preTime = deadline
comiss xmm0, xmm1              ; globalTime vs deadline
jb    still_waiting             ; 未到时间，继续等
```

全局时间变量: `g_fGlobalTime` (0x10A4344)

### 6.4 动画速度系统

**`AnimSystem__UpdateFrame` (0xA784B0)**, 关键逻辑:

```
frame_advance = animSpeed * deltaTime
if (anim_frame->flags & 1):           ← 标志位判断
    if (actionState >= 14):            ← 技能动作
        frame_advance /= player->skillSpeed (0x64A8)
    else:                              ← 普攻动作
        frame_advance /= player->attackSpeed (0x64A4)
```

- `flags & 1 == 1`: **前摇(前摆)帧** → 受速度乘数影响，可加速
- `flags & 1 == 0`: **后摇(收招)帧** → 固定速率播放，不受速度乘数影响
- 速度值越**小**，动画越**快**（因为是除法）

动画标志检查指令: `0xA78F87` (`test al, 1; jz skip_speed_divide`)

### 6.5 技能冷却系统

**CSkill 结构体** (大小 0x28 = 40字节):
- 数组指针: `[g_pSkillManager] + 0x440`  (g_pSkillManager = SO3D.exe + 0xDB2E78)
- 数组大小: `[g_pSkillManager] + 0x444`
- **注意**: CSkill 数组在 `g_pSkillManager` 上，不在 `g_pLocalPlayer` 上！

| 偏移 | 类型 | 说明 |
|------|------|------|
| +0x04 | uint32 | skillId |
| +0x08 | uint32 | flag (0=普通, !0=强化) |
| +0x18 | float | leftCoolDown（剩余冷却时间，每帧减 deltaTime） |
| +0x1C | uint32 | 有效标志（非零=有技能） |
| +0x20 | uint32 | skillLevel |
| +0x24 | ptr | skillTable (SkillTable*) |

**SkillTable 关键字段**:
| 偏移 | 说明 |
|------|------|
| +0x118 | maxLevel |
| +0x158 | coolDown（基础冷却时间，float） |
| +0x160 | 冷却减免系数 |

**冷却计算** (`CSkill__GetEffectiveCoolDown`, 0x9DC870):
```
effectiveCoolDown = baseCoolDown - (coolDownReductionStat * baseCoolDown / constant)
```
- `baseCoolDown` = SkillTable + 0x158
- `coolDownReductionStat` = player + 0x6DD4 (整数)

**冷却递减** (`CLocalUser__TickSkillCooldowns`, 0x9DE790):
```c
for each CSkill in skillArray:
    if (leftCoolDown > 0.0f)
        leftCoolDown -= deltaTime;
```

### 6.6 可加速点汇总

| # | 加速点 | 地址/偏移 | 方法 | 效果 |
|---|--------|----------|------|------|
| 1 | **攻击动画速度** | player+0x64A4, NOP写入0x8732AB | NOP写指令+设自定义值 | 普攻动画加速 |
| 2 | **技能动画速度** | player+0x64A8, NOP写入0x8732FF | NOP写指令+设自定义值 | 技能前摇动画加速 |
| 3 | **移动速度** | player+0x1A4, NOP写入0x8733B0/0x873426 | NOP写指令+设自定义值 | 移动加速 |
| 4 | **preTime跳过** | 时间表 player+0x705C+20*slot | ① NOP 0x8715DC 的写入; ② 在0x873BE4处patch比较 | 技能前摇延迟归零 |
| 5 | **后摇加速** | 0xA78F87 (flags&1检查) | patch掉flag检查，让所有帧都除以速度 | 前摇+后摇都加速 |
| 6 | **技能冷却** | CSkill+0x18 (leftCoolDown) | 每帧写0; 或NOP 0x9DE7C5的递减改为清零 | 无冷却 |
| 7 | **冷却减免属性** | player+0x6DD4 | 直接修改减免值 | 增强冷却减免 |

### 6.7 新版 vs 旧版差异

| 项目 | 旧版 | 新版 |
|------|------|------|
| preTime来源 | 本地SkillTable+0x150 | **服务器下发**，存timing table+0x705C |
| attackSpeed偏移 | 0x235C | **0x64A4** |
| skillSpeed偏移 | 0x2360 | **0x64A8** |
| CSkill.leftCoolDown | 在CSkill内（具体偏移未确认） | **CSkill+0x18** |
| SkillTable.coolDown | 0x158 | **0x158**（未变） |
| localPlayer基址 | 0x95a784 | **0xD6E3E0** (g_pLocalPlayer) |
| **skillIndexBase** | **0xA1BE38** | **0xDB2E78** (g_pSkillManager, 独立全局!) |
| skillArray指针 | [skillBase]+0x440 | **[g_pSkillManager]+0x440**（未变） |
| CSkill大小 | 0x28 | **0x28**（未变） |
| SkillTable偏移 | 全部 | **全部未变** (skillRange=0x14C, coolDown=0x158, attack=0x160) |
