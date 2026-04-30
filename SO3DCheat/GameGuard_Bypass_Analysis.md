# Seal Online CN — GameGuard 去除分析报告

## 基本信息

| 属性 | 值 |
|---|---|
| 文件名 | `unpacked_SO3D.exe` |
| IDB路径 | `G:\希望\unpacked_SO3D.exe.i64` |
| 游戏 | Seal Online CN (封印战记CN) |
| 架构 | x86 (32位) |
| 基地址 | `0x400000` |
| 镜像大小 | `0x2451000` (~36.3 MB) |
| 保护 | Themida (已脱壳) + nProtect GameGuard |
| 函数总数 | 56,176 (其中 47,607 未命名) |
| 字符串总数 | 44,887 |

---

## 第一步：定位 GameGuard 痕迹

### 1.1 字符串搜索

使用正则 `GameGuard|GameMon|npgg|nProtect|ggsrv|INCA` 搜索 IDB 中的字符串，
共找到 **34 条**相关匹配，关键的有：

| 地址 | 字符串 | 意义 |
|---|---|---|
| `0xEB0EC0` | `/gameguardon` | 游戏内开启 GG 的聊天命令 |
| `0xEB0ED0` | `/gameguardoff` | 游戏内关闭 GG 的聊天命令 |
| `0xEB7120` | `GameGuard exit(%d) CheckServerTime(%d)` | GG 退出时的日志 |
| `0xEB7802` | `GameGuard.des` | GG 主模块描述文件 |
| `0xEB7D50` | `npgmup.des` | GG 更新模块 |
| `0xEB8184` | `InitializeGameGuardFailed` | GG 初始化失败标记 |
| `0xEB82D8` | `NPGameMonCallback` | GG 回调函数名 |
| `0xEB82AC` | `gameGuard serverNumber:%d, NPNumber:%d send` | GG 服务器认证日志 |
| `0xECA60C` | `WWW.NPROTECT.COM` | nProtect 官网 |
| `0xEADD78` | `CSO3D::Packet_GC_GAME_GUARD2` | 服务器下发 GG 认证包 |

由此确认：
- 游戏名为 **Seal Online CN**（源码标识 `SO3D`）
- 使用 nProtect GameGuard 保护
- 存在 `/gameguardon` 和 `/gameguardoff` 命令（发送网络包给服务器控制）

### 1.2 段特征佐证

| 段名 | 权限 | 说明 |
|---|---|---|
| `.themida` | rwx | Themida 壳残留段 |
| `.boot` | rx | Themida 引导段 |
| `.SCY` | rwx | Themida/Oreans 签名段 |

rwx 权限段是加壳工具的典型特征，文件名 `unpacked_` 前缀也佐证已脱壳。

---

## 第二步：追踪 GameGuard 初始化流程

### 2.1 从字符串交叉引用入手

对关键字符串做 xrefs_to 分析：

| 字符串 | 被引用于 | 函数 |
|---|---|---|
| `InitializeGameGuardFailed` | `0xA0765A` | `sub_A07570` — GG 错误弹窗处理器 |
| `NPGameMonCallback` | `0xA0F166` | `sub_A0EFF0` — GG 运行时回调 |
| `/gameguardon` | `0x9D35AC` | `sub_9D22A0` — 聊天命令处理器 |
| `/gameguardoff` | `0x9D35D7` | 同上 |
| `GameGuard exit(...)` | `0x9FEC3F` | `sub_9FEBF0` — 游戏主循环 |

### 2.2 错误弹窗处理器 — `sub_A07570`

```
地址: 0xA07570
原型: int __cdecl sub_A07570(unsigned int error_code)
调用者: _WinMain@16
```

这是一个巨大的 switch-case 函数，根据 error_code 显示不同的 GameGuard 错误信息：
- case 110 (0x6E): GameGuard 一般错误
- case 114 (0x72): GameGuard 模块问题
- case 330 (0x14A): npgmup.des 更新失败
- case 1877 (0x755): **特殊值，直接跳到函数末尾返回**（即不弹窗）

**关键发现**: 1877 (0x755) 是 GameGuard 的"成功"标识码。当 error_code == 1877 时，
函数什么都不做直接返回。

### 2.3 NPGameMonCallback — `sub_A0EFF0`

```
地址: 0xA0EFF0
原型: int __stdcall sub_A0EFF0(int msg_type, int *data)
调用者: sub_CD0800, sub_CD11B0, sub_CD7EC0
```

这是游戏注册给 GameGuard 的回调函数，GameGuard 通过它通知游戏各种事件：

| msg_type | 行为 |
|---|---|
| 1001 | 返回 0（忽略） |
| 1002 | 设置 `dword_10A430C = 1`，返回 0 |
| 1011 | 设置 `dword_10A430C = 1`，返回 1 |
| 1012/1013 | 设置 `dword_10A430C = 1`，返回 0 |
| 1014 | 格式化错误信息，设置 `dword_10A430C = 1`，返回 0 |
| 1015 | 设置 `dword_10A430C = 1`，返回 0 |
| 1018 | 调用某个更新函数，返回 0 |
| 1019 | **转发认证数据给服务器**（`sub_BFBCC0`） |

**关键全局变量**: `dword_10A430C` 是 GameGuard 退出标志。
回调中多个错误分支都会将其设为 1，触发游戏退出。

### 2.4 回调的调用者分析

回调函数指针存储在 `off_10624B8`，被以下函数读取并调用：

| 函数 | 地址 | 角色 |
|---|---|---|
| `sub_CD0800` | `0xCD0800` | GG 通信线程（连接 GG 服务） |
| `sub_CD11B0` | `0xCD11B0` | GG 事件调度器（处理 GG 内部事件） |
| `sub_CD7EC0` | `0xCD7EC0` | GG 初始化完成通知 |

---

## 第三步：追踪完整初始化链

### 3.1 入口点 — `sub_44BE30`

```
地址: 0x44BE30
引用方式: 静态初始化器表 (0xE6A238, .CRT section)
```

```c
int sub_44BE30() {
    sub_CD9120("SealOnlineCN");    // 传入游戏标识
    return atexit(cleanup_func);    // 注册退出清理
}
```

这是一个 C++ 静态初始化器（放在 `.CRT$XCU` 表中），在 `main()` 之前执行。
游戏标识字符串 `"SealOnlineCN"` 用于 GameGuard 识别所保护的游戏。

### 3.2 核心初始化 — `sub_CD9120`

```
地址: 0xCD9120
原型: int sub_CD9120(unsigned __int8 *game_id)
调用者: sub_44BE30
```

```c
int sub_CD9120(unsigned __int8 *game_id) {
    if (lpParameter)                    // 已初始化则跳过
        return 0;

    old_filter = SetUnhandledExceptionFilter(sub_CD1AB0);  // 安装 GG 异常处理器

    lpParameter = new(0x3DB8);          // 分配 GG 对象 (~15.8KB)
    sub_CCE730(lpParameter);            // 构造函数

    result = sub_CD4570(lpParameter, game_id);  // 核心 GG 流程 (~9.8KB 巨型函数)
    lpParameter[4] = result;            // 存储结果码

    if (result == 1877) {               // 成功
        if (byte_1CE6CC0)
            sub_CD93B0(&byte_1CE6CC0);  // 后续初始化
    } else {                            // 失败
        err = sub_CD1BF0();
        if (err == 16001) lpParameter[4] = 116;
        if (err == 16002) lpParameter[4] = 117;
    }

    // 清理：终止临时进程，恢复异常处理器
    ...
    return lpParameter[4];
}
```

**关键点**:
- `lpParameter` 是全局 GameGuard 对象指针
- `sub_CD4570` 是 ~9.8KB 的巨型函数，负责：释放 GG 文件、启动 GG 进程、建立通信、
  注册回调、处理认证
- 返回值 **1877** 表示 GameGuard 初始化成功

### 3.3 WinMain 中的 GameGuard 检查

```
地址: 0xA2F750 (_WinMain@16)
```

```c
int WinMain(...) {
    ...
    v4 = sub_CD8F80();          // 读取 lpParameter[4] (GG 结果码)
    if (v4 == 1877) {           // 必须是 1877 才进入游戏
        // 加载资源、初始化窗口、进入游戏循环
        ...
        while (true) {
            if (dword_10A430C != 0)   // GG 退出标志
                break;                 // 退出游戏
            // PeekMessage / GameLoop
        }
    }
    ...
}
```

`sub_CD8F80` 的反汇编：

```asm
0xCD8F80: mov  ecx, ds:lpParameter
0xCD8F86: test ecx, ecx
0xCD8F88: jnz  short loc_CD8F8D      ; 有 GG 对象则读取 [ecx+16]
0xCD8F8A: xor  eax, eax              ; 无 GG 对象则返回 0
0xCD8F8C: retn
0xCD8F8D: jmp  loc_CD2B60            ; 读取 lpParameter[4] 并返回
```

### 3.4 游戏循环中的退出检查

```asm
0xA300B0: cmp  ds:dword_10A430C, 0   ; 检查 GG 退出标志
0xA300B7: jnz  short loc_A30126      ; 非零则跳转到退出流程
```

### 3.5 服务器交互

服务器通过 `Packet_GC_GAME_GUARD2` (处理函数 `sub_9822B0`) 下发 GameGuard
认证数据。客户端收到后调用 `sub_CD9360` → `sub_CD77E0` 传递给 GG 模块处理，
GG 模块通过回调 (case 1019) 将认证结果发回服务器。

聊天命令 `/gameguardon` 和 `/gameguardoff` 实际上是发送网络包 `0xADD5B` 给服务器，
由服务器决定是否开关 GG，不是本地开关。

---

## 第四步：补丁方案

### 思路

由上述分析可知，GameGuard 有 4 层防护：
1. **进程启动**: `GameGuard_StaticInit` 在 main() 之前的 C++ 静态初始化阶段就启动了 GG 进程
2. **初始化门槛**: `GameGuard_GetResult()` 必须返回 1877，游戏才会启动
3. **运行时退出**: `g_bGameGuardExit` 被 GG 回调设为 1 时，游戏退出
4. **错误弹窗**: `GameGuard_ErrorMessageBox` 在 GG 失败时弹出 MessageBox

因此需要 4 个补丁，分别瓦解每一层。

### Patch 0 (最关键): 阻止 GameGuard 进程启动

| 项目 | 值 |
|---|---|
| 地址 | `0x44BE30` (`GameGuard_StaticInit`) |
| 原始字节 | `68 xx xx xx xx` (push offset "SealOnlineCN") |
| 补丁字节 | `C3` (ret) |
| 含义 | 函数入口直接返回 |
| 效果 | **彻底阻止** GG 对象创建、GG 进程启动、GameGuard.des 释放 |

> **为什么这个补丁最关键？**
> `GameGuard_StaticInit` 位于 `.CRT$XCU` 静态初始化器表中（地址 0xE6A238），
> 它在 `WinMain` 执行之前就已经运行。内部调用 `GameGuard_Init` →
> `GameGuard_CoreManager`（9.8KB 巨型函数），后者负责释放 GameGuard.des 文件、
> 创建 GameGuard 子进程、建立 IPC 通信管道。如果不 patch 这里，GameGuard 进程
> 仍然会被启动，即使后续的结果检查被绕过。

### Patch 1 (补充): 强制 `GameGuard_RuntimeCheck` 返回 1877

| 项目 | 值 |
|---|---|
| 地址 | `0xCD8BB0` (`GameGuard_RuntimeCheck`) |
| 原始字节 | `8B 0D xx xx xx xx 85 C9 75 03 33 C0 C3` |
| 补丁字节 | `B8 55 07 00 00 C3` |
| 含义 | `mov eax, 0x755; ret` |
| 效果 | 游戏循环中 `IsGameGuardAlive()` 始终返回 true，防止 NpCheck 弹窗 |

> **为什么需要这个补丁？**
> 游戏循环 `sub_9FEBF0` 中每帧调用 `IsGameGuardAlive()` → `GameGuard_RuntimeCheck()`，
> 后者检查 `lpParameter` 并调用 GG 运行时验证。由于 Patch 0 阻止了 GG 初始化，
> `lpParameter` 为 NULL，函数返回 0 而非 1877，导致 `g_bNpCheckTriggered = 1`，
> 最终弹出标题为 "NpCheck" 的 MessageBox："检测到非法程序正在运行，游戏将退出"。

### Patch 2: 强制 `GameGuard_GetResult` 返回 1877

| 项目 | 值 |
|---|---|
| 地址 | `0xCD8F80` (`GameGuard_GetResult`) |
| 原始字节 | `8B 0D xx xx xx xx 85 C9 75 03 33 C0 C3` |
| 补丁字节 | `B8 55 07 00 00 C3` |
| 含义 | `mov eax, 0x755; ret` |
| 效果 | 不管 GG 是否初始化，都返回 1877，WinMain 判断通过 |

### Patch 3: 禁用游戏循环 GG 退出检查

| 项目 | 值 |
|---|---|
| 地址 | `0xA300B7` |
| 原始字节 | `75 6D` (jnz short loc_A30126) |
| 补丁字节 | `90 90` (nop nop) |
| 效果 | 即使 `g_bGameGuardExit` 被设为 1，游戏也不退出 |

### Patch 4: 屏蔽 GG 错误弹窗

| 项目 | 值 |
|---|---|
| 地址 | `0xA07570` (`GameGuard_ErrorMessageBox`) |
| 原始字节 | `55 8B EC 81 EC 04 01 00 00` |
| 补丁字节 | `33 C0 C2 04 00` |
| 含义 | `xor eax, eax; ret 4` |
| 效果 | 错误处理函数直接返回，不弹出 MessageBox |

### Patch 5: 忽略服务器 GG 认证包

| 项目 | 值 |
|---|---|
| 地址 | `0x9822B0` (`Packet_GC_GAME_GUARD2`) |
| 原始字节 | `55 8B EC 51 56 8D 45 FC` |
| 补丁字节 | `B8 01 00 00 00 C2 04 00` |
| 含义 | `mov eax, 1; ret 4` |
| 效果 | 直接忽略服务器下发的 GameGuard 认证包，不尝试处理也不崩溃 |

> **背景**：正常流程中，服务器定期发送 `GC_GAME_GUARD2` 包携带认证挑战数据，
> 客户端转交给 GameGuard 模块处理后，通过 `NPGameMonCallback(1019)` 回传
> 认证结果。由于 Patch 0 阻止了 GG 初始化，`lpParameter` 为 NULL，数据无法处理，
> 服务器收不到回复就断开连接。此补丁让包处理器直接返回 1（"已处理"），
> 避免任何无效操作。
>
> **注意**：此补丁阻止了客户端崩溃和日志噪音，但**无法伪造有效的 GG 认证回复**。
> 如果服务器强制要求 GG 认证回复，仍然会因超时断开连接。
> 此情况需要在**服务器端关闭 GameGuard 认证**才能彻底解决。

### 补丁关系图

```
程序加载 (.CRT 静态初始化)
  │
  ├─[Patch 0] GameGuard_StaticInit → ret
  │   └─ GameGuard 进程/DLL 完全不启动 ★ 根源性阻断
  │
  ▼
WinMain
  │
  ├─[Patch 2] GameGuard_GetResult → 强制返回 1877
  │   └─ WinMain 判断通过，进入游戏
  │
  ├─ 游戏循环
  │   ├─[Patch 1] GameGuard_RuntimeCheck → 强制返回 1877
  │   │   └─ IsGameGuardAlive() == true，防止 NpCheck 弹窗 ★ 关键
  │   │
  │   ├─[Patch 3] g_bGameGuardExit 检查 → NOP 掉跳转
  │   │   └─ GG 异常不再导致退出 (防御性)
  │   │
  │   └─ 正常游戏逻辑继续运行
  │
  └─[Patch 4] GameGuard_ErrorMessageBox → 直接返回
      └─ 不再弹出 GameGuard 错误窗口 (防御性)

服务器交互
  │
  └─[Patch 5] Packet_GC_GAME_GUARD2 → 直接返回 1
      └─ 忽略服务器 GG 认证包 (需服务器端配合关闭 GG)
```

---

## 第五步：版本更新后的特征码定位指南

游戏更新后基址会变化，但代码结构和字符串不会轻易变。
以下提供两种定位方式：**字符串锚点法**（推荐）和**字节特征码法**。

### 通用原则

- 地址中的 `??` 表示该字节会随版本变化（重定位地址 / 相对偏移）
- 先搜字符串锚点，再用字节特征码二次验证
- 所有 patch 都是 **函数入口处** 或 **单条指令处** 的修改，非常稳定

---

### Patch 0 — GameGuard_StaticInit （阻止 GG 进程启动）

**字符串锚点法（最可靠）：**
1. 在 IDA 中搜索字符串 `"SealOnlineCN"`
2. 找到该字符串的 **唯一代码交叉引用**（xref），即 `push offset "SealOnlineCN"` 指令
3. 这条 `push` 指令所在函数的 **第一个字节** 就是 patch 点
4. 将第一个字节改为 `C3`（ret）

**字节特征码：**
```
原始: 68 ?? ?? ?? ?? E8 ?? ?? ?? ?? 68 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 C4 08 C3
含义: push <"SealOnlineCN">; call <GG_Init>; push <lambda>; call <_atexit>; add esp,8; ret
补丁: 第一个字节 68 → C3
```

---

### Patch 1 — GameGuard_RuntimeCheck （防止 NpCheck 弹窗）

**字符串锚点法：**
1. 搜索字符串 `"NpCheck"`
2. 找到其 xref 所在函数 `NpCheck_ShowAndExit`
3. 向上追溯该函数的调用者，找到游戏主循环函数
4. 在主循环中找到 `cmp ds:byte_XXXX, 0` / `jz` 跳过 NpCheck 的分支
5. 该分支之前有 `call <IsGameGuardAlive>` → 进入该函数
6. 内部唯一一条 `call` 的目标就是 `GameGuard_RuntimeCheck`

**字节特征码（通过调用者 IsGameGuardAlive 精确定位）：**
```
调用者: E8 ?? ?? ?? ?? 33 C9 3D 55 07 00 00 0F 94 C1 8B C1 C3
含义:   call <RuntimeCheck>; xor ecx,ecx; cmp eax,755h; setz cl; mov eax,ecx; retn
```
1. 搜索固定字节 `33 C9 3D 55 07 00 00 0F 94 C1 8B C1 C3`（13字节，非常唯一）
2. 匹配位置 **前5字节** 是 `E8 XX XX XX XX`（call 指令）
3. 计算 call 目标地址：`目标 = 当前地址 + 5 + 签名中的4字节偏移`
4. 该目标地址就是 `GameGuard_RuntimeCheck`
5. 将目标函数开头 6 字节改为 `B8 55 07 00 00 C3`（mov eax, 0x755; ret）

---

### Patch 2 — GameGuard_GetResult （WinMain 初始化检查）

**字符串锚点法：**
1. 搜索字符串 `"InitializeGameGuardFailed"`
2. 找到其 xref 所在函数 `GameGuard_ErrorMessageBox`
3. 再找 `GameGuard_ErrorMessageBox` 的调用者 → 位于 `WinMain`
4. WinMain 中调用 `ErrorMessageBox` 之前的那条 `call` 就是 `call GameGuard_GetResult`
5. 该 call 的目标函数就是 patch 点

**字节特征码（WinMain 中的调用模式）：**
```
E8 ?? ?? ?? ?? 3D 55 07 00 00 74 ?? 50 E8 ?? ?? ?? ?? 83 C4 04
含义: call <GetResult>; cmp eax,755h; jz OK; push eax; call <ErrorBox>; add esp,4
```
1. 搜索固定字节 `3D 55 07 00 00 74`（cmp eax, 0x755; jz）
2. 该匹配位置 **前5字节** 是 `E8 XX XX XX XX`
3. 计算 call 目标地址 = patch 点
4. 将目标函数开头 6 字节改为 `B8 55 07 00 00 C3`

---

### Patch 3 — 游戏循环 GG 退出检查 （防御性）

**字符串锚点法：**
1. 搜索字符串 `"GameGuard exit(%d) CheckServerTime(%d)"`
2. 找到 xref 所在函数（游戏主循环 `CSO3D::GameLoop`）
3. 在该函数开头附近找到检查全局变量的 `cmp ds:[XXXX], 0` / `jnz` 模式
4. 这个 `jnz` 就是 patch 点

**字节特征码：**
```
上下文: 83 3D ?? ?? ?? ?? 00 75 ?? 83 3D ?? ?? ?? ?? 00
含义:   cmp [g_bGameGuardExit], 0; jnz <exit>; cmp [another_var], 0
```
1. 该模式位于 WinMain 的消息循环中，紧跟在 `mov ebx, ds:[PeekMessageA]` 之后
2. 搜索 `8B 1D ?? ?? ?? ?? 90 83 3D ?? ?? ?? ?? 00 75`
3. 最后的 `75 XX` (jnz) 即 patch 点，改为 `90 90` (nop nop)

---

### Patch 4 — GameGuard_ErrorMessageBox （防御性）

**字符串锚点法（最简单）：**
1. 搜索字符串 `"InitializeGameGuardFailed"`
2. 唯一 xref 所在函数就是 `GameGuard_ErrorMessageBox`
3. 函数开头改为 `33 C0 C2 04 00`（xor eax, eax; ret 4）

**验证特征：**
该函数内部一定同时引用以下字符串：
- `"InitializeGameGuardFailed"`
- `"GameGuard exit %s"`
- `"Game\SO3D.cpp"`

并调用 `MessageBoxA` 和 `wsprintfA`。

---

### Patch 5 — Packet_GC_GAME_GUARD2 （服务器认证包）

**字符串锚点法：**
1. 搜索字符串 `"CSO3D::Packet_GC_GAME_GUARD2"`
2. 找到其 xref 所在函数就是包处理器
3. 函数开头改为 `B8 01 00 00 00 C2 04 00`（mov eax, 1; ret 4）

**字节特征码（通过日志字符串定位）：**
1. 搜索字符串 `"gameGuard serverNumber:%d,nRecvSize:%d"`
2. 唯一 xref 所在函数就是 `Packet_GC_GAME_GUARD2`
3. 函数使用 `__stdcall` 约定（`retn 4`），1 个参数

---

### 快速定位 Cheat Sheet

| Patch | 最快定位方法 | 搜什么 |
|---|---|---|
| **0** | 字符串 xref | `"SealOnlineCN"` → 函数入口 → `C3` |
| **1** | 字节特征码 | `33 C9 3D 55 07 00 00 0F 94 C1 8B C1 C3` → call 目标 → `B8 55 07 00 00 C3` |
| **2** | 字节特征码 | `3D 55 07 00 00 74` → 前面的 call 目标 → `B8 55 07 00 00 C3` |
| **3** | 字符串 xref | `"GameGuard exit(%d) CheckServerTime(%d)"` → 同函数中找 `75 XX` → `90 90` |
| **4** | 字符串 xref | `"InitializeGameGuardFailed"` → 函数入口 → `33 C0 C2 04 00` |
| **5** | 字符串 xref | `"CSO3D::Packet_GC_GAME_GUARD2"` → 函数入口 → `B8 01 00 00 00 C2 04 00` |

> **优先级**: Patch 0、1、2 是必须的，缺一不可（阻止启动 + 运行时检查 + WinMain 检查）。
> Patch 5 在服务器启用了 GG 认证时必须打（否则断线）。
> Patch 3 和 4 是防御性的，大多数情况下不打也能运行。

---

### 局限性

- 如果**服务器主动发送** `GC_GAME_GUARD2` 认证包并等待回复，
  客户端无法产生有效的认证数据，服务器可能断开连接。
  这种情况只能在服务器端关闭 GG 认证。
- 私服环境通常不启用 GG 认证，上述 3 个补丁即可完全去除 GameGuard。

---

## 关键地址速查表（当前版本）

| 符号/地址 | 说明 |
|---|---|
| `lpParameter` (全局) | GameGuard 对象指针 |
| `g_bGameGuardExit` (`0x10A430C`) | GG 退出标志 (0=正常, 1=需退出) |
| `g_bNpCheckTriggered` (`0x11B359F`) | NpCheck 触发标志 |
| `off_10624B8` | NPGameMonCallback 函数指针 |
| `0x44BE30` | GameGuard_StaticInit — GG 静态初始化入口 ★Patch0 |
| `0xCD8BB0` | GameGuard_RuntimeCheck — 运行时 GG 存活检查 ★Patch1 |
| `0xCD8F80` | GameGuard_GetResult — 读取 GG 初始化结果 ★Patch2 |
| `0xA300B7` | 游戏循环中 GG 退出 jnz 指令 ★Patch3 |
| `0xA07570` | GameGuard_ErrorMessageBox — GG 错误弹窗 ★Patch4 |
| `0xCD9120` | GameGuard_Init — GG 核心初始化函数 |
| `0xCD4570` | GameGuard_CoreManager — GG 管理器主函数 (~9.8KB) |
| `0xA0EFF0` | NPGameMonCallback — GG 运行时回调 |
| `0x9F8D90` | IsGameGuardAlive — GG 存活判断包装函数 |
| `0x9FE9A0` | NpCheck_ShowAndExit — NpCheck 弹窗并退出 |
| `0xA2F750` | WinMain |
| `0x9FEBF0` | CSO3D::GameLoop — 游戏主循环 |
| `0x9822B0` | Packet_GC_GAME_GUARD2 — 服务器 GG 认证包处理 |

## 补丁总表

| # | 地址 | 原始字节 | 补丁字节 | 效果 |
|---|---|---|---|---|
| 0 | `0x44BE30` | `68` | `C3` | 阻止 GG 进程启动 |
| 1 | `0xCD8BB0` | `8B 0D ?? ?? ?? ?? 85 C9 75 03 33 C0 C3` | `B8 55 07 00 00 C3` | 运行时检查返回 1877 |
| 2 | `0xCD8F80` | `8B 0D ?? ?? ?? ?? 85 C9 75 03 33 C0 C3` | `B8 55 07 00 00 C3` | 初始化结果返回 1877 |
| 3 | `0xA300B7` | `75 6D` | `90 90` | 禁用 GG 退出跳转 |
| 4 | `0xA07570` | `55 8B EC 81 EC 04 01 00 00` | `33 C0 C2 04 00` | 屏蔽错误弹窗 |
| 5 | `0x9822B0` | `55 8B EC 51 56 8D 45 FC` | `B8 01 00 00 00 C2 04 00` | 忽略服务器 GG 认证包 |
