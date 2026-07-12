# GGThreadBlock

## 项目概述

GGThreadBlock 是注入到 `unpackd_so3d.exe` / `so3dplus.exe` (SO3D Plus 游戏客户端) 的功能性 DLL。它同时承担两种角色：

1. **GameGuard 抑制层**：Detour `CreateThread` 拦截来源于 `GameGuardDll.dll` 的 worker 线程，把它们重定向到一个永远 `Sleep(INFINITE)` 的 dummy 过程；同时 detour `SetWindowsHookExA/W` + `UnhookWindowsHookEx`，返回 sentinel HHOOK 让宿主 EXE 的 `WH_KEYBOARD_LL` 安装"成功"但实际没生效，从而夺回 Win 键 / 任务切换；并一次性把 `NPmsg.dll+0x103D3` 的 `jng` 改成 `jmp`。
2. **游戏功能模块宿主**：以 `SO3DCheat` 为蓝本的 ImGui/D3D9Hook 外挂骨架，载入一组 `IModule` (MoveSpeed, AttackSpeed, SkillSpeed, SpeedHack, ItemShortCD, AttackRange, AttackMove, SkillMove, AutoPickup, FireFullPower, AutoConfirm, AutoDelegation, StationaryFarm, PlayerESP, NearbyPlayerGuard, Status)。

此外还带一个附属 exe [`Starter.cpp`](Starter.cpp)：批量登录器，读账号列表启动多个 `so3dplus.exe`，按 `GGTB_HWFP_GROUP` 把账号分组到共享的假硬件指纹。

目标进程是 Winlicense 打包的 32-bit Windows 游戏；很多代码体在 DllMain 时尚未解包，需要 stage-2 触发器 (`Stage1Trigger` 监听宿主的首个 `DeleteFileA("error.txt")`) 之后才能 detour。

## 技术栈

- **语言/标准**：C++17，Windows 静态 CRT (`/MT`/`/MTd`)，UTF-8 源码 (`/utf-8`)，`UNICODE` / `_UNICODE`
- **构建**：CMake ≥ 3.15，目标平台 Win32 (x86) / x64，配置 `Debug` 或 `RelWithDebInfo`；`RelWithDebInfo` 下强制 `/Od` 便于调试
- **注入/钩子**：Microsoft Detours (vendored under `General/ThirdParty/Detours`)
- **UI**：Dear ImGui (Win32 + DirectX 9) —— 通过 `zzj::D3D::D3D9Hook` 挂 `EndScene`；菜单键位 `VK_INSERT`
- **日志**：spdlog，文件输出到 `<dll-dir>/GGConfig/_bootstrap/bootstrap.log`，`UserConfig::Tick` 认出角色后切换到 `<dll-dir>/GGConfig/<character>/ggtb.log`
- **JSON**：nlohmann/json (vendored `<json.hpp>`)
- **内部库**：`ClientDevLib` (本仓库静态库，封装了 `zzj::Process`/`zzj::Memory`/`zzj::MD5`/`zzj::D3D::D3D9Hook` 等)
- **第三方**：Boost 1.75 (`libboost_locale-vc142-mt-s-x32-1_75.lib`)，在 [`../CMakeLists.txt:47`](../CMakeLists.txt#L47) 链接
- **链接选项**：`/SAFESEH:NO`

## 目录结构

```
GGThreadBlock/
├── dllmain.cpp              DLL 入口 / HackThread / CreateThread + 键盘钩子 detour / NPmsg 补丁 / 模块注册
├── GGThreadBlock.h          暴露给模块的全局计数器 (g_blockedThreadCount)
├── IModule.h                模块基类 (OnInit/OnRender/OnShutdown/OnResume + 持久化 + NPG 暂停位)
├── Setting.{h,cpp}          zzj::D3D::Setting 派生；注册模块、ImGui 主题、CJK 字体、脏标记
├── Starter.cpp              附属 exe：批量账号登录器 (单独 target GGThreadBlockStarter)
├── entity/
│   └── CLocalPlayer.{h,cpp} 本地玩家结构偏移 + 所有字节补丁函数 (PatchMoveSpeed 等) + Patch/Restore 辅助
├── util/
│   ├── PatternResolver.{h,cpp}  特征码扫描 + MD5 缓存到 ggtb_pattern_cache.json
│   ├── Stage1Trigger.{h,cpp}    共享 DeleteFileA("error.txt") 回调，unpacker 完成后触发 stage-2
│   ├── SpeedHack.{h,cpp}        全局加速 (timeGetTime / QueryPerformanceCounter 篡改)
│   ├── NetLog.{h,cpp}           明文 send/recv 日志 (stage-2 hook)
│   ├── HwFpSpoof.{h,cpp}        硬件指纹伪造，种子持久化到 hwfp_seed.bin (stage-2 hook)
│   ├── DisconnectWatchdog.{h,cpp}  1842/1843 端口 5 分钟无 recv 则 WM_CLOSE 宿主
│   └── UserConfig.{h,cpp}       角色名识别 + 白名单 + config.json 持久化 (debounced)
└── modules/
    ├── StatusModule.h           展示 blocked-thread 计数，不可被 NPG 暂停
    ├── MoveSpeedModule.h        移速 (NOP 10 处写点 + 直写 [user+0x1A4])
    ├── AttackSpeedModule.h      普攻速度 ([user+0x2E1C])
    ├── SkillSpeedModule.h       技能速度 + preTime 常量篡改
    ├── SpeedHackModule.h        封装 SpeedHack 工具
    ├── ItemNoCDModule.h         物品 CD 缩到固定极短值
    ├── AttackRangeModule.h      攻击距离字段 + cap 放宽
    ├── AttackMoveModule.h       普攻/连击时允许移动 (370C==0 非技能路 @ 0x753A80)
    ├── SkillMoveModule.h        技能时允许移动 (370C∈{3,5,8} 技能路 @ 0x753A54)
    ├── AutoPickupModule.h       独立 worker 扫 CItemContainer 自动捡取
    ├── FireFullPowerModule.h    全程全威力 (热键 N 切换)
    ├── AutoConfirmModule.h      自动点确认弹窗
    ├── AutoDelegationModule.h   自动接受组队/委托
    ├── StationaryFarmModule.h   原地挂机
    ├── PlayerESPModule.h        附近玩家屏幕标记 (与 NPG 共享名单)
    └── NearbyPlayerGuardModule.h  NPG：附近有真人时把所有 CanAutoPause 模块拍暂停
```

产物与顶层构建统一在 [`../CMakeLists.txt`](../CMakeLists.txt)；本目录无自己的 CMakeLists。

## 架构/运行流程

1. **DllMain (DLL_PROCESS_ATTACH)**：`DisableThreadLibraryCalls` → `InstallDetour`（CreateThread + SetWindowsHookExA/W + UnhookWindowsHookEx）→ 起 `HackThread` 和 `NPmsgPatcherThread`。**禁止在 DllMain 里做文件 IO 或 spdlog 调用**（loader lock + registry mutex 死锁）。
2. **HackThread**：`UserConfig::Bootstrap` (建 `_bootstrap/` 日志) → `PatternResolver::Init` → `Stage1Trigger::Install` → `NetLog::Install`、`HwFpSpoof::Install`、`DisconnectWatchdog::Install`（后三者把真实 detour 延后到 stage-2 回调）→ 构造 `Setting`，按序注册模块（**NPG 最后注册**，这样 `Setting::GetModules()` 能看到所有被守护者）→ 循环 `Sleep(100)` 里 pump 热键 (`VK_END` 退出、`N` toggle FireFullPower) 和 `UserConfig::Tick`。
3. **D3D9Hook EndScene**：首次调用时 lazy 触发 `Setting::Init`（加载 CJK 字体、`mod->OnInit()`），之后每帧 `Setting::Render` 画 UI —— 左侧 profile 管理栏 + 右侧三标签页（常用/变态/PK，按 `IModule::GetTab()` 分流，双列紧凑布局），并靠 `ConfigGeneration()` 对齐基线、用 snapshot diff 算"未保存改动"脏标记。
4. **UserConfig::Tick**：Setting 初始化完 + 本地玩家加载出角色名后，把 logger 切到 `<character>/ggtb.log`，读 config.json（meta：manualWhitelist + activeProfile），加载活动 profile（`profiles/<name>.json`，仅含模块状态）并对每个模块 `LoadState`。白名单/activeProfile 改动 ~1 秒 debounce 回写 config.json；**模块状态只在点「保存」(`SaveProfile`) 时写 profile 文件**，从不自动落盘。
5. **退出路径**：`VK_END` 跳出主循环 → `UserConfig::FlushConfigMeta`（只刷 config.json meta，不写模块状态）→ 各 util `Uninstall` → `D3D9Hook::Destroy` → `FreeLibraryAndExitThread`。

### IModule 契约要点

- `CanAutoPause()` 默认为 `true`。Status / NPG 自己返回 `false`。
- 补丁类模块在 checkbox enable 分支和每帧 poke 时都要 gate `!IsPausedByGuard()`，否则 NPG 按下去后用户再点 checkbox 会重新打补丁、把效果 leak 给附近玩家。
- `OnShutdown` / `OnResume` 被 NPG 复用：真正 enable 由 `enabled_` 表示用户意图，`pausedByGuard_` 是运行时掩码。
- `LoadState` 先恢复 slider，再 `IModule::LoadState`；这样 `OnResume` 看得到正确的 slider。
- 想参与持久化就 override `ConfigKey()` 返回非空字符串，并在 `SaveState/LoadState` 里补上额外字段（参考 [MoveSpeedModule.h](modules/MoveSpeedModule.h)）。
- 有自己 worker 的模块（NPG / AutoPickup）：在 ctor 起线程、dtor set `stop_` + `join`；`Setting` 按值持有 `shared_ptr<IModule>`，所以析构会走到。

## 编码规范

- **命名**：类 `PascalCase`，成员变量尾下划线 `speed_`，全局/自由函数 `PascalCase`，常量 `kCamelCase`，namespace 常量用 `inline constexpr`
- **命名空间**：所有项目代码在 `namespace GGTB { ... }`；util 子系统用 `namespace GGTB::NetLog` 等
- **字符串**：源码 UTF-8；UI 文本一律 `u8"..."`；`IModule` 的 `name_` 是 UTF-8 中文；与 Big5 游戏内存交互统一转 UTF-8
- **日志**：`spdlog::info/warn/error`，tag 以 `"GGTB:"` 或 `"GGTB::<subsystem>:"` 开头。DllMain / 早期钩子里只能用 `OutputDebugStringA`（没有 logger，且 loader lock）
- **内存补丁**：经由 `PatternResolver::Get(name)`；补丁 size / 偏移 / cap 常量写在 [entity/CLocalPlayer.h](entity/CLocalPlayer.h) 顶部并附注释；Patch 返回 bool，Restore 必须幂等；backup 用 per-site `BYTE[]` + `bool captured`
- **VirtualProtect**：改完要恢复原保护位，然后 `FlushInstructionCache`（见 dllmain 的 NPmsg 补丁）
- **不写多余注释**：默认不加注释。当 *why* 非显然（隐藏约束、历史坑、offset 出处、为什么选 path-3 而不是 path-2）时写简短 `//`，不要写多段 docstring，不要写 `// 调用者：X`
- **并发**：UI 线程只读 worker 的 snapshot，全部 `std::mutex + std::lock_guard`；`stop_` 用 `std::atomic<bool>`；`pausedByGuard_` 也是 `std::atomic<bool>`
- **避免在 DllMain**：文件 IO、spdlog、重钩子安装、`CreateThread` 的 target 是本 DLL 以外的函数。只放 `DisableThreadLibraryCalls` + `InstallDetour` + 起 HackThread
- **IDA 逆向纪律**：弄清 global/function 含义时立刻在 IDA 里改名、加注释，不要堆积到"以后清理"

## 常用命令

项目使用 CMake + MSVC。顶层生成在 `../build/<Platform>/<Config>/`。

### 配置（Win32 / x86，Release 带 PDB）

```powershell
cmake -S . -B build\x86\Release -A Win32 -DBUILD_TYPE=Release
```

### 配置（Win32 / x86，Debug）

```powershell
cmake -S . -B build\x86\Debug -A Win32
```

### 构建 GGThreadBlock.dll

```powershell
cmake --build build\x86\Release --config RelWithDebInfo --target GGThreadBlock
```

### 构建 Starter

```powershell
cmake --build build\x86\Release --config RelWithDebInfo --target GGThreadBlockStarter
```

### 构建全部

```powershell
cmake --build build\x86\Release --config RelWithDebInfo
```

### 产物位置

`../SO3DCheat/build/x86/Release/bin/`（DLL、PDB）和 `.../archive/`（独立 PDB）。把 `GGThreadBlock.dll` 和 `GGThreadBlockStarter.exe` 放到 `so3dplus.exe` 同目录运行。

### 运行时调试

- 宿主日志：`<dll-dir>/GGConfig/_bootstrap/bootstrap.log`（角色登录前）
- 角色日志：`<dll-dir>/GGConfig/<character>/ggtb.log`
- 网络抓包：`<dll-dir>/GGConfig/<character>/net/{sendlog,recvlog}`
- Pattern 缓存：`<dll-dir>/ggtb_pattern_cache.json`，换游戏版本时 MD5 变了会自动失效重扫
- HW 指纹种子：`<dll-dir>/GGConfig/_bootstrap/hwfp_seed.bin`（删了 = 换一台假机器；`Starter` 的分组种子叫 `hwfp_seed_group<N>.bin`）
- 菜单显示：进游戏后按 `INSERT`；退出 HackThread 按 `END`

### 注入

本仓库有 `FEATURE_GHINJECTOR`（见顶层 `Features.cmake`），具体注入工具不在本目录内。以常见用法，DLL 由 Starter 在启动子进程时通过 DLL Hijack / LoadLibrary 注入 —— 参考 `Starter.cpp` 的 `LaunchAndRename`。
