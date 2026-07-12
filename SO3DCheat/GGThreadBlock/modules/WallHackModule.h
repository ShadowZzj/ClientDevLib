#pragma once
#include "../IModule.h"
#include "../util/PatternResolver.h"
#include <Detours/build/include/detours.h>
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <atomic>

namespace GGTB
{

// 穿墙 — Detour Map__IsBlocked (0xA97080),引擎唯一的"格子可走否"查询入口。
// 原型 __thiscall(this=g_pCurMap, int tileX, int tileY) -> int:返回碰撞网格
// grid[x+width*y],0=可走、非0=阻挡,越界/空网格返回 1(阻挡)。所有可达性判定
// 都汇聚到它:点击落点合法性(sub_871030 — 不可达时光标变红 X 并拒绝移动)、
// A* 寻路、目标规格检查。
//
// 开关开时:hook 恒返回 0(可走),于是任何格子都被当作可走 —— 光标不再变 X,
// 点哪都能寻路过去(包括本来不可达的墙后/水面/障碍),即穿墙。
// 开关关时:原样放行,正常碰撞判定。
//
// 纯开关:无 slider,无每帧 poke。NPG 可经 OnShutdown/OnResume 暂停(附近有真人时
// 别让穿墙这种扎眼行为暴露)。
//
// 注意:这只放开"客户端寻路/移动判定",服务端仍可能对穿越不可达区域做校验/拉回,
// 跨越大段阻挡尤其危险,由用户实测距离/区域容忍度。
class WallHackModule : public IModule
{
  public:
    WallHackModule() : IModule(u8"穿墙") {}

    ~WallHackModule() override
    {
        if (hookInstalled_)
            UninstallHook();
    }

    bool CanAutoPause() const override { return true; }
    Tab  GetTab() const override { return Tab::OP; }

    void OnRender() override
    {
        ImGui::Checkbox(u8"启用##WallHack", &enabled_);

        // hook 状态跟随 enabled_ 自动同步(点击切换 / 配置加载 / NPG 恢复 都覆盖)。
        const bool want = enabled_ && !IsPausedByGuard();
        if (want && !hookInstalled_)
        {
            if (!InstallHook())
                enabled_ = false;
        }
        else if (!want && hookInstalled_)
        {
            UninstallHook();
        }

        s_wallHackActive_.store(enabled_ && hookInstalled_,
                                std::memory_order_release);

        ImGui::TextDisabled(u8"开启后光标不再变 X — 点哪都能寻路过去(穿墙)");
        ImGui::TextDisabled(u8"跨越大段障碍可能被服务端拉回,小范围更稳");

        if (enabled_ && IsPausedByGuard())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               u8"已被 NPG 暂停 (附近有玩家)");
        }
        else if (enabled_ && hookInstalled_)
        {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f),
                               u8"已 hook Map__IsBlocked — 全图可走");
        }
    }

    void OnShutdown() override
    {
        if (hookInstalled_)
            UninstallHook();
        s_wallHackActive_.store(false, std::memory_order_release);
    }

    void OnResume() override
    {
        if (!enabled_)
            return;
        if (InstallHook())
            s_wallHackActive_.store(true, std::memory_order_release);
        else
            enabled_ = false;
    }

    std::string ConfigKey() const override { return "WallHack"; }

  private:
    // Map__IsBlocked(this=g_pCurMap, int x, int y) — __thiscall,用 __fastcall +
    // dummy edx 模拟(本项目标准做法)。
    using fnMapIsBlocked = int(__fastcall *)(void *ecx, void *edx, int x, int y);

    static fnMapIsBlocked    s_oMapIsBlocked_;
    static std::atomic<bool> s_wallHackActive_; // hook 读它决定恒可走还是放行

    bool hookInstalled_ = false;

    bool InstallHook()
    {
        if (hookInstalled_) return true;
        auto addr = PatternResolver::Get("MapIsBlocked");
        if (!addr)
        {
            spdlog::error("GGTB::WallHack: MapIsBlocked 未解析");
            return false;
        }
        s_oMapIsBlocked_ = reinterpret_cast<fnMapIsBlocked>(addr);

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        LONG err = DetourAttach(reinterpret_cast<PVOID *>(&s_oMapIsBlocked_),
                                HookMapIsBlocked);
        LONG cm = DetourTransactionCommit();
        if (err != NO_ERROR || cm != NO_ERROR)
        {
            spdlog::error("GGTB::WallHack: Detour 失败 attach={} commit={}", err, cm);
            return false;
        }
        hookInstalled_ = true;
        spdlog::info("GGTB::WallHack: hook attached @ {:x} (Map__IsBlocked)", addr);
        return true;
    }

    void UninstallHook()
    {
        if (!hookInstalled_) return;
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(reinterpret_cast<PVOID *>(&s_oMapIsBlocked_),
                     HookMapIsBlocked);
        DetourTransactionCommit();
        hookInstalled_ = false;
        spdlog::info("GGTB::WallHack: hook detached");
    }

    static int __fastcall HookMapIsBlocked(void *ecx, void *edx, int x, int y)
    {
        if (s_wallHackActive_.load(std::memory_order_acquire))
        {
            // 恒"可走"(0)。所有可达性判定经此都认为目标格可走 = 穿墙。
            return 0;
        }
        return s_oMapIsBlocked_(ecx, edx, x, y);
    }
};

inline WallHackModule::fnMapIsBlocked WallHackModule::s_oMapIsBlocked_  = nullptr;
inline std::atomic<bool>              WallHackModule::s_wallHackActive_ {false};

} // namespace GGTB
