# SO3D 技能系统逆向分析

> 基于 IDA Pro 对新版 SO3D.exe（基址 0x400000）的反编译分析，详细记录推导过程。

---

## 1. 入口点：两个访问 0x64A8 的指令

用户提供的两个断点：

| 地址 | 指令 | 所在函数 |
|------|------|----------|
| SO3D.exe+678F92 (0xA78F92) | `divss xmm0, [esi+64A8h]` | AnimSystem__UpdateFrame (0xA784B0) |
| SO3D.exe+6FCE69 (0xAFCE69) | `movss xmm0, [eax+64A8h]` | SkillEffect__Execute (0xAFCD50) |

这两处在释放技能时会被命中，分别处理**动画速度**和**技能效果**。

---

## 2. 动画速度系统 — AnimSystem__UpdateFrame (0xA784B0)

### 2.1 反编译核心逻辑

```c
// 简化后的关键代码路径：
frame_advance = animSpeed * deltaTime;

if (anim_frame->flags & 1) {           // 0xA78F87: test al, 1
    if (actionState >= 14) {            // 技能动作
        frame_advance /= player->skillSpeed;   // 0xA78F92: divss by [esi+64A8h]
    } else {                            // 普攻动作
        frame_advance /= player->attackSpeed;  // 0xA78F83: divss by [esi+64A4h]
    }
}
```

### 2.2 推导过程

1. 在 0xA78F92 反编译，发现它在一个巨大的函数 `sub_A784B0` 中（约 3000+ 行）
2. 向上追踪 `divss` 的分支条件，找到 `test al, 1`（flags & 1 检查）
3. 进一步向上找到 `cmp dword ptr [reg+194h], 0Eh`（actionState >= 14 判断）
4. 由此确定：
   - `flags & 1 == 1` 的帧 = **前摇帧**（受速度乘数影响）
   - `flags & 1 == 0` 的帧 = **后摇帧**（固定速率播放）
   - 速度值是**除数**，越小越快

### 2.3 关键偏移

| 偏移 | 字段 | 说明 |
|------|------|------|
| player+0x64A4 | attackSpeed | 普攻动画速度除数 |
| player+0x64A8 | skillSpeed | 技能动画速度除数 |
| player+0x194 | actionState | 动作状态（>=14 为技能） |

---

## 3. 技能效果处理 — SkillEffect__Execute (0xAFCD50)

### 3.1 反编译摘要

```c
char SkillEffect__Execute(this, skillName, skillObj, sourceId, targetId, ...) {
    if (!ParseSkillData(skillName, localData))
        goto fail;
    
    // 0xAFCE69: 读取技能速度
    localSkillSpeed = *(float*)(sourceEntity + 0x64A8);
    
    // 根据技能类型选择目标
    switch (*(skillObj->vtable + 324)) {
        case 0: target = FindEntity(sourceId); break;
        case 1: target = FindMonster(targetId); break;
        // ...
    }
    
    // 设置位置信息，调用 sub_86C310 处理效果
    sub_86C310(&targetList);
    sub_B08070(&skillData);
}
```

### 3.2 推导过程

1. 在 0xAFCE69 反编译，得到 `sub_AFCD50`
2. 追踪此函数的**调用者**（xrefs_to），发现 90+ 个调用点，其中关键的：
   - `sub_887610` (0x887610) — 技能施放服务器响应
   - `sub_973B80` (0x973B80) — 网络包处理（击杀确认）
   - `sub_9E3020` ~ `sub_9E8EA0` — 大量技能效果变体

---

## 4. 技能施放响应 — CLocalUser__OnSkillCastResponse (0x887610)

### 4.1 反编译核心

```c
int OnSkillCastResponse(this, packet, actionType, skillObj, flag) {
    // 从服务器包中读取数据
    read(packet, &skillId);       // v13
    read(packet, &preTimeRaw);    // v15
    read(packet, &targetCount);   // v16
    
    // 0x887673: 计算 preTime
    float preTime = (float)preTimeRaw / constant;
    
    for (int i = 0; i < targetCount; i++) {
        // 对每个目标执行技能效果
        SkillEffect__Execute(g_pEffectMgr, skillName, skillObj, targetId, -1, ...);
    }
    
    // 0x88773D: 将 preTime 存入定时器表
    CLocalUser__ScheduleAction(actionState, preTime, 0, extraData);
    
    if (flag) {
        this[3762] = ClampValue(skillId);  // 设置某个状态
    }
}
```

### 4.2 关键发现：preTime 来自服务器

旧版中 preTime 从本地 SkillTable+0x150 读取，可以直接 NOP 读取指令来跳过。
新版中 **preTime 由服务器在技能响应包中下发**，存储到本地定时器表。

---

## 5. PreTime 定时器 — 写入与消费

### 5.1 写入：CLocalUser__ScheduleAction (0x871590)

```asm
; 0x8715DC: movups [edx+ecx*4+7054h], xmm0
; 写入格式: {1(active), globalTime, preTime, extraData}
```

反编译：
```c
void ScheduleAction(this, actionState, preTime, subAction, extra) {
    if (preTime > 0.0f) {
        int idx = 5 * (actionState + subAction * 64);
        // 写 20 字节到 this + 0x7054 + idx*4:
        // [0] byte  = 1 (active)
        // [4] float = g_fGlobalTime (当前时间)
        // [8] float = preTime (前摇持续时间)
        // [12] uint = extra
        // [16] = 0
    }
}
```

### 5.2 消费：CLocalUser__TickPreTimers (0x873BB0)

**推导过程**：搜索偏移 0x7054 的所有引用，找到 9 个位置：

```
0x86F890 (Constructor) — 初始化
0x8715DC (ScheduleAction) — 写入
0x873BBC, 0x873C9E (TickPreTimers) — 读取判定
0x87E051, 0x882C57... (其他管理函数)
```

反编译 `TickPreTimers`：
```c
void TickPreTimers(this) {
    for (int slot = 0; slot < 64; slot++) {
        if (!IsSlotActive(slot, 0))  // 检查 active flag
            continue;
        
        // 0x873BD8: 读取 initTime
        // 0x873BE4: deadline = initTime + preTime
        float deadline = table[slot].initTime + table[slot].preTime;
        
        if (g_fGlobalTime < deadline) {
            // 未到时间，继续等待
            if (g_fGlobalTime >= deadline - threshold)
                table[slot].aboutToFire = 1;  // 即将触发标记
        } else {
            // preTime 过期！执行动作
            if (slot == 18)
                RecalcStats();
            else if (slot == 24)
                ResetSkillStates();
            ClearSlot(slot);
        }
    }
}
```

### 5.3 定时器表结构

| 基址 | CLocalUser + 0x7054 |
|------|---------------------|
| 规模 | 4 层 × 64 槽 × 20 字节 = 5120 字节 |

每条目 20 字节：

| 偏移 | 类型 | 说明 |
|------|------|------|
| +0 | byte | active flag (1=活跃) |
| +4 | float | initTime (发出时刻) |
| +8 | float | preTime (前摇持续时间) |
| +12 | uint32 | extraData |
| +16 | uint32 | reserved |

全局时间变量: `dword_10A4344` (SO3D.exe + 0xCA4344)

---

## 6. 帧更新调用链确认

通过 xrefs_to 逐级追踪：

```
sub_9FEBF0  (GameMainLoop)
  └─ EntitySystem__FrameUpdate  (0xA285A0)
      ├─ CLocalUser__FrameUpdate  (0xA23CB0)
      │   ├─ sub_871640  (ActionUpdate)
      │   │   └─ AnimSystem__UpdateFrame  (0xA784B0)  ← 0x64A4/0x64A8 除法
      │   └─ CLocalUser__TickPreTimers  (0x873BB0)    ← preTime 定时器
      └─ CLocalUser__TickSkillCooldowns  (0x9DE790)   ← 冷却递减
```

验证方法：
1. `xrefs_to(sub_873BB0)` → 仅被 `sub_A23CB0` 调用
2. `xrefs_to(sub_A23CB0)` → 仅被 `sub_A285A0` 调用
3. `xrefs_to(sub_A285A0)` → 被 `sub_9FEBF0` 多次调用
4. `xrefs_to(sub_871640)` → 被 `sub_A23CB0` 调用两次（说明每帧更新两次）

---

## 7. 技能冷却系统

### 7.1 CSkill 数组基址确认

**关键发现**：CSkill 数组不在 `g_pLocalPlayer` 上！

在 `EntitySystem__FrameUpdate` (0xA285A0) 中：
```asm
0xA28749: mov ecx, ds:dword_11B2E78     ; ECX = g_pSkillManager
0xA28754: call CLocalUser__TickSkillCooldowns
```

而同一函数后面：
```asm
0xA28795: mov ecx, ds:g_pLocalPlayer     ; ECX = g_pLocalPlayer (不同的全局!)
```

**结论**：

| 全局变量 | 地址 | RVA | 用途 |
|----------|------|-----|------|
| g_pLocalPlayer | 0x116E3E0 | SO3D.exe+0xD6E3E0 | 玩家实体（坐标/动画/属性） |
| g_pSkillManager | 0x11B2E78 | SO3D.exe+0xDB2E78 | 技能子系统（CSkill 数组） |

旧版对应: `localPlayerOffset=0x95A784`, `skillIndexBase=0xA1BE38` — 同样是两个不同的全局。

### 7.2 CSkill.leftCoolDown 递减

反编译 `CLocalUser__TickSkillCooldowns` (0x9DE790)：

```c
void TickSkillCooldowns(this, float deltaTime) {
    int count = this[273];  // this+0x444
    int offset = 0;
    for (int i = 0; i < count; i++) {
        float *pCD = (float*)(this[272] + offset + 24);  // CSkill+0x18
        if (*pCD > 0.0f)
            *pCD -= deltaTime;   // 0x9DE7C5: subss
        offset += 40;  // next CSkill (0x28)
    }
    // ... UI cooldown progress bar update
}
```

### 7.3 coolDown 基础值来源

反编译 `CSkill__GetEffectiveCoolDown` (0x9DC870)：

```asm
0x9DC8D1: movss xmm1, [esi+158h]     ; SkillTable.coolDown
0x9DC8DA: movd  xmm0, [ecx+6DD4h]    ; player.cooldownReductionStat
; effectiveCoolDown = baseCoolDown - (stat * baseCoolDown / constant)
```

---

## 8. SkillTable 结构与 CSkill 关系

### 8.1 从 0x9DCF32 出发

用户指出 `0x9DCF32: mov eax, [eax+14Ch]` 访问 SkillTable.skillRange。

反编译 `sub_9DCEF0`：
```c
int GetSkillRange(CSkill *this) {
    SkillTable *base = GetSkillTableByLevel(this, 1);
    int effectiveLevel = GetLevelBonus(this->skillId) + this->skillLevel;
    if (effectiveLevel > base->maxLevel)
        effectiveLevel = base->maxLevel;
    if (effectiveLevel)
        return GetSkillTableByLevel(this, effectiveLevel)->skillRange;  // +0x14C
    else
        return base->skillRange;
}
```

### 8.2 GetSkillTableByLevel 查找链 (sub_9DC810)

```asm
mov edi, ecx                    ; edi = CSkill* (this)
push esi                        ; push level arg
cmp dword ptr [edi+8], 0        ; CSkill.flag
jnz use_enhanced
  call sub_C1D0F0               ; get SkillDataManager singleton
  mov ecx, eax
  call sub_C1D570               ; mgr->GetRegularTable(level)  [mgr+0x50]
  jmp done
use_enhanced:
  call sub_C1D0F0
  mov ecx, eax
  call sub_C1D950               ; mgr->GetEnhancedTable(level) [mgr+0x70]
done:
  mov edx, [eax]                ; vector.begin()
  mov ecx, [edi+4]              ; CSkill.skillId
  mov eax, [edx+ecx*4]          ; return SkillTable* = vector[skillId]
```

### 8.3 SkillDataManager 单例 (sub_C1D0F0)

```c
// 全局单例指针: dword_1BA3948
SkillDataManager* GetInstance() {
    if (!g_pInstance) {
        g_pInstance = new SkillDataManager();  // 0x1C0 bytes
        g_pInstance->Init();
    }
    return g_pInstance;
}
```

### 8.4 CSkill.skillTable 直接指针

从 `sub_9DCE90` 确认：
```asm
0x9DCE90: cmp dword ptr [ecx+20h], 0   ; CSkill.skillLevel
0x9DCEA4: mov eax, [ecx+24h]           ; CSkill.skillTable -> SkillTable*
0x9DCEA7: mov eax, [eax+160h]          ; SkillTable.attack
```

所以 CSkill+0x24 直接存储了 SkillTable 指针，不需要每次通过 SkillDataManager 查找。

### 8.5 完整访问链

```
方式一（直接指针）:
  [SO3D.exe+0xDB2E78]  → g_pSkillManager
    +0x440              → CSkill* 数组指针
    CSkill[i]+0x24      → SkillTable*
    SkillTable+0x0C     → skillName (GBK, 256 bytes)

方式二（通过 SkillDataManager 按 ID 查找）:
  [0x1BA3948]           → SkillDataManager*
    +0x50               → map<level, vector<SkillTable*>>
    .find(level)        → vector
    vector[skillId]     → SkillTable*
```

---

## 9. 数据结构汇总

### CSkill (0x28 = 40 字节)

| 偏移 | 类型 | 字段 | 验证来源 |
|------|------|------|----------|
| +0x00 | ptr | vtable | 标准 C++ |
| +0x04 | uint32 | skillId | 0x9DC854: `mov ecx,[edi+4]` 用于查表 |
| +0x08 | uint32 | flag (0=普通, !0=强化) | 0x9DC81B: `cmp [edi+8],0` 分支判断 |
| +0x0C | pad[12] | - | |
| +0x18 | float | leftCoolDown | 0x9DE7B6: `movss xmm0,[v4+v5+18h]` |
| +0x1C | uint32 | validityFlag (非零=有技能) | 0x9DD555: `[base+28*a2+1Ch]!=0` |
| +0x20 | uint32 | skillLevel | 0x9DCE90: `cmp [ecx+20h],0` |
| +0x24 | ptr | skillTable (SkillTable*) | 0x9DCEA4: `mov eax,[ecx+24h]` |

### SkillTable (0x288 = 648 字节, 偏移全部未变)

| 偏移 | 类型 | 字段 | 验证来源 |
|------|------|------|----------|
| +0x00 | ptr | vtable | |
| +0x04 | uint32 | skillId | |
| +0x08 | uint32 | skillId2 | |
| +0x0C | char[256] | skillName (GBK) | 旧版结构 + 大小推算 |
| +0x110 | uint32 | skillType (1=Self,2=Single,3=Range) | |
| +0x118 | uint32 | maxLevel | 0x9DCF0E: `mov ecx,[edi+118h]` |
| +0x13C | uint32 | apCost | |
| +0x148 | int32 | skillCoverRange | |
| +0x14C | int32 | skillRange | 0x9DCF32: `mov eax,[eax+14Ch]` ✓ |
| +0x150 | float | preTime | 旧版结构（新版改服务器下发） |
| +0x158 | float | coolDown | 0x9DC8D1: `movss xmm1,[esi+158h]` ✓ |
| +0x160 | uint32 | attack | 0x9DCEA7: `mov eax,[eax+160h]` ✓ |

---

## 10. 可加速点总结

| # | 目标 | 地址 | 原理 | NOP/Patch 位置 |
|---|------|------|------|----------------|
| 1 | 攻击动画加速 | player+0x64A4 | 值越小动画越快(除数) | NOP 0x8732AB (写入指令) |
| 2 | 技能前摇动画加速 | player+0x64A8 | 同上 | NOP 0x8732FF |
| 3 | 移动速度 | player+0x1A4 | 直接赋值 | NOP 0x8733B0, 0x873426 |
| 4 | preTime 跳过 | timer[slot]+8 | 设为0或patch比较 | NOP 0x8715DC 或 patch 0x873BE4 |
| 5 | 后摇加速 | flags&1 检查 | 去掉判断让所有帧都除以速度 | patch 0xA78F87 |
| 6 | 无冷却 | CSkill+0x18 | 写0或NOP递减 | 写0 或 NOP 0x9DE7C5 |
| 7 | 冷却减免 | player+0x6DD4 | 增大减免值 | 直接修改 |
