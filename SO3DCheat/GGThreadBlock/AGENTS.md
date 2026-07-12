# GGThreadBlock Codex instructions

## 阅读顺序与事实来源

- 处理本目录及子目录前，先完整阅读同目录 `CLAUDE.md`。它集中记录项目架构、构建命令和编码规范；本文件补充 Claude project memory 中的长期规则和当前已知校正。
- 当前工作树可能包含大量已暂存、未暂存和未跟踪的用户改动。以磁盘上的当前源码为实现真值，保留用户改动，不覆盖或格式化 `CLAUDE.md`。
- `CLAUDE.md` 的模块枚举、热键和注册顺序可能滞后。实际启用模块及顺序必须查看当前 `dllmain.cpp` 的 `RegisterModule` 调用和 `modules/`；不要从旧清单推断。
- `.claude/settings.local.json` 是 Claude 的历史权限白名单，不是项目知识，也不是 Codex 的授权来源，不得迁移其通配权限。

## 运行时架构与生命周期

- 这是注入 32-bit Winlicense 游戏进程的 DLL，同时承担 GameGuard 抑制层和 ImGui/D3D9 功能模块宿主。需要宿主解包后代码的 hook 必须走 `Stage1Trigger` 的 stage-2 回调。
- `DllMain` 保持最小化：禁止文件 IO、spdlog、依赖解包代码的重 hook，以及任何可能在 loader lock 下等待其他锁的操作。早期路径只能使用安全的调试输出。
- `CreateThread` 抑制只按 start routine 所属模块 basename 是否为 `GameGuardDll.dll` 判断，解析失败时 fail-open。命中后仍创建真实 OS 线程，但入口替换为永久 `Sleep(INFINITE)` 的 dummy，原参数丢弃。
- `g_blockedThreadCount` 是自 DLL 加载以来的累计重定向尝试数，不是当前存活线程数；底层创建失败前计数也已增加，且不会递减。不要把状态页或导出值解释成 live count。
- `CreateThread` 方案不会覆盖 `_beginthreadex`、`NtCreateThreadEx`、线程池或入口 trampoline 不在目标 DLL 的情况。分析绕过或兼容性问题时先核实实际创建路径。
- 主 EXE 安装键盘 hook 时返回 sentinel `HHOOK`，第三方调用透传；`UnhookWindowsHookEx` 只对 sentinel 假定成功。NPmsg 补丁由轮询线程匹配精确签名后写入，当前不会在退出时恢复。
- 卸载逻辑必须考虑永久 dummy 线程和 NPmsg 轮询线程与 DLL 代码生命周期的耦合。当前 NPmsg 线程没有 stop/join；修改卸载路径时不得忽略潜在的卸载后继续执行风险。
- 任何 worker（模块、singleton 或 util）都必须具备可中断的 stop 和确定性 join，并在它依赖的 hook、engine、D3D 或 DLL 代码卸载前停完。不要把这条规则只套在 NPG/AutoPickup 上；退出时还要覆盖全局 PathWalker、AutoTradeController 等非模块 worker。
- `Setting::End()` 只调用 `OnShutdown()`，不会统一 join 模块线程；当前很多模块要到 `setting.reset()` 析构时才 join。修改退出流程时应先显式 stop/join 所有 worker，再卸 LoginBridge、RemoteControl、InputInjector、watchdog、hooks、engine 和 D3D。
- 初始化阶段的 util 安装、handler 注册和模块构造有一部分位于主 try/catch 之外。新增可能抛异常的初始化时要提供对称清理/RAII，不能假设异常一定会走现有退出路径。

## 模块与配置契约

- `enabled_` 表示用户意图，`pausedByGuard_` 是运行时原子掩码；NPG 会复用 `OnShutdown`/`OnResume`。补丁模块在 checkbox 分支和每帧 poke 时都必须 gate `!IsPausedByGuard()`，避免暂停期间重新泄漏效果。
- `ConfigKey()` 非空的模块才参与 profile 持久化。模块状态只在用户显式保存 profile 时落盘；退出只 flush config meta，不得偷偷保存未确认的模块状态。
- `LoadState` 应先恢复 slider/额外字段，再调用基类逻辑，使 `OnResume` 看到正确参数。Restore 必须幂等，patch backup 按 site 独立保存。
- NPG 必须能看到所有需要守护的模块，但不要依赖“它是最后一个注册模块”的旧说法；检查当前注册图和显式依赖。
- 不要把“UI 只读加锁 snapshot”当成当前实现的普遍事实。`enabled_`、slider/interval 等仍有普通字段被 UI、远控和 worker 跨线程读写；触碰相关模块时逐项审计并用 mutex/atomic 或不可变 snapshot 消除 data race。
- 会遍历 `Setting::modules_` 或依赖其他模块的 worker，必须在注册图完成后再启动；避免构造函数起线程后继续向 `modules_` push 的并发窗口，优先采用显式 `Start()`/two-phase init。

## 逆向、游戏调用与 live 验证

- 逆向中一旦识别函数、全局、结构字段或补丁点，下一步就在 IDA 中 rename、写清用途/布局/patch key 与长度，并在一批改动后保存 IDB。源码常量和临时笔记不能替代 IDB。
- `unpackd_so3d.exe` 的 IDB 与 live `so3dplus.exe` 不是同一 build。函数逻辑和结构可参考，但枚举、kind/type、状态码、magic number、偏移和绝对地址在部署前必须用 CE/live 进程核验。
- `PatternResolver` fallback 参数是 RVA，`Get()` 会加 module base；不要把它当绝对地址再次“修正”。所有已记录的 live 类型值和绝对地址都视为 build-specific 观测，不跨版本照搬。
- 打开或操作仓库、商店、交易等游戏内 UI，必须复刻游戏本地 UI 调用链（如 `CUIManager::BeginContent`）；禁止只裸发末端 CG 包。成功、失败和取消路径都要用本地 close 调用清理 content，避免服务端状态与客户端 UI 状态失配导致角色卡住。
- 逆向游戏行为时先搜索 `Windows/test/example/so3dFullCheat/`。只借用其中已验证的 this/参数形态和调用序列；旧 binary 的偏移必须在当前 binary 重新定位。
- 通过 CE 远程调用游戏函数时，优先使用 `allocate_memory -> write_memory` 写位置无关 stub -> `execute_code`；不要使用本机已知会触发 pipe/附近分配问题的 `auto_assemble`。大结果用 `write_region_to_file` 后本地解析，游戏中文按 CP950/Big5 处理。

## 诊断与产品偏好

- 用户报告“没生效”时先取硬证据：重新检查部署 exe/js 的当前 mtime、重新读取 release 下当前持久化配置，再通过 broker 的 `GET /api/instances` 和 `POST /api/command/:pid` 执行只读 live 查询。证据不足时明确说未知，不凭陈旧读取抢答。
- 自动化、分流和扩缩容功能默认采用“全自动、单旋钮”：用户只配置必要阈值/开关，实例数、节点选择、扩缩容和故障切换由程序按运行时状态处理；手工脚本只作为可选调试入口。
- 出口分流工作从现有 mihomo TUN + process-name 路由方案继续，不重新引入与 `protect.dll` 冲突的 Proxifier/进程内 Winsock hook。正式服约 77 并发及“按真实出口 IP 限制”是 2026-06-30 的历史实测/推断，做新决策前必须重新验证；增加边缘节点不等于增加真实出口。

## 当前文档偏差提示（2026-07-10 工作树）

- 菜单键当前由 `Setting::GetToggleMenuKey()` 返回 `VK_OEM_6`（`]`），不是 `INSERT`。
- 主循环当前约每 10 ms pump，`UserConfig::Tick` 约每 100 ms；`N` 热键切换的是 MultiCast，不是 FireFullPower。
- 当前注册模块远多于 `CLAUDE.md` 的目录清单，Teleport/WallHack 仍为注释注册；NPG 后仍有 ESP、OpenNearbyStall、MonsterList 等注册项。
- `Starter.cpp` 当前能证实的是 suspended create/resume 与窗口标题处理，不能仅凭旧说明断言它自行通过 LoadLibrary/DLL hijack 注入。
- 上述偏差只是定位旧文档的警示。每次任务仍应重新查看当前实现，避免把这份校正也当成永远不变的真值。
