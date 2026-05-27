#pragma once
#include <atomic>

namespace GGTB
{

// 火力全开 ↔ 多重施法 互斥状态。两个模块都 hook / 占用 Net__SendSkillPacket
// 这条路径(火力全开调用它,多重施法 detour 它),同时启用会:
//   - 多重施法把火力全开的每一轮自动施放放大 N 倍 → 包速翻倍直接掉线
//   - 状态语义不清晰(用户不知道当前实际 cast 倍数)
// 所以 UI 上点哪个开关就把对方拍下去。这两个 atomic flag 是单一事实源,
// 两个模块互相不需要 include 对方的 header。
//
// 写者:模块的 OnRender 检测 enabled_ 翻转时,把自己 flag set,把对方 flag clear,
// 同时调对方的 SetEnabled(false) 同步 checkbox。
// 读者:无 — 不需要在 hot path 读;UI 互斥就够了。
inline std::atomic<bool> g_fireFullPowerActive{false};
inline std::atomic<bool> g_multiCastActive    {false};

} // namespace GGTB
