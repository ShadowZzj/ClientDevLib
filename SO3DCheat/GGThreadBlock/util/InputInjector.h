#pragma once
#include <Windows.h>
#include <vector>

namespace GGTB::InputInjector
{
// 把 VK 通过"hook GetAsyncKeyState 注入虚拟按下状态"的方式发给游戏。
//
// 为什么走这条:游戏的 Input_PollKeyboard (sub_B1E790 @ 0xB1E790) 每帧用
// GetAsyncKeyState 把 76+ 个键的状态轮询到 InputSystem+732..988,然后跟上一帧
// 状态做 diff 调 Input_QueueKeyEvent (sub_B1D820)。我们 hook 这个 API,游戏读
// 我们想发的 VK 时返回"已按下"(高位置 1),前后两帧的状态差自然产生 press 边沿,
// Alt+W 这类组合键只要两个 VK 同帧都返回按下就能形成 chord。
//
// 优点:不依赖窗口前台、不解析 InputSystem*、不 race 引擎写状态;OS API 边界稳。
// 缺点:进程内所有 GetAsyncKeyState 调用都受影响 — 包括 HackThread 自己的热键
// 轮询 (VK_OEM_4 切菜单、VK_END 退出),所以 SendVks 内置黑名单挡掉这俩,免得
// web 一发就把宿主关了。
//
// 时序:每个 VK 标"按下到 expire tick"为止(默认 80ms)。Tick 单位是
// GetTickCount(),32-bit 卷绕用差值比较抗 wrap。

void Install();   // detour GetAsyncKeyState。从 HackThread 调,不要从 DllMain。
void Uninstall(); // 卸 detour。幂等。

// 把传入的 VK 全部标"按下" holdMs 毫秒(默认 80ms,够 60fps 下覆盖 4-5 帧)。
// vks 里同时给多个键 = 同帧都按下 = 组合键(Alt+W 给 [VK_MENU, 'W'])。
// holdMs 范围 [10, 5000];超出范围被 clamp。返回实际放进队列的 VK 数(<= vks.size,
// 黑名单/越界 VK 会被剔)。
size_t SendVks(const std::vector<int> &vks, int holdMs = 80);
} // namespace GGTB::InputInjector
