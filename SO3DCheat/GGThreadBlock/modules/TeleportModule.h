#pragma once
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include "../util/PatternResolver.h"
#include <Detours/build/include/detours.h>
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <atomic>
#include <climits>
#include <cstdint>

namespace GGTB
{

// 点击瞬移 — Detour CLocalUser::SetAfterAction (0x7539E0),所有"设置移动目标格"
// 的真正汇聚点(点地走路、按住拖动走路、战斗追击都经它)。原函数把目标写
// +0x32F0/F4 并置 pending +0x2BC8=1,引擎 TraceMove (0x756F10) 每帧消费该队列
// 朝目标插值走过去。
//
// (早期版本 hook OnPlayerMoveClick(0x871D10) 失败:CE 实测走路时该函数根本不命中,
//  它只是拖动节流路径偶发调用,不是移动必经入口。改 hook 底层汇聚点 SetAfterAction。)
//
// 开关开 + action==1(移动):先调原函数让状态机正常入队,再立即 Teleport(x,y)
// 把可见坐标 +0x3C/+0x44 拍到目标 + 清 pending +0x2BC8=0 + 发 411000 同步包,
// TraceMove 下一帧见 pending=0 当作已到达,不再插值 = 点哪/拖到哪瞬移到哪。
// 战斗/技能等其他 action 原样放行,不干预。
// 开关关:原样放行,正常走路。坐标只有 X/Y 平面两维,无 Z(引擎按地形自算 +0x40)。
//
// 服务端仍校验跳跃距离 —— 大范围瞬移可能被拉回/拒绝,小范围闪现更稳,由用户实测。
//
// 纯开关:无 slider,无每帧 poke。NPG 可经 OnShutdown/OnResume 暂停(附近有真人时
// 别让瞬移这种最扎眼的行为暴露)。
class TeleportModule : public IModule
{
  public:
    TeleportModule() : IModule(u8"点击瞬移") {}

    ~TeleportModule() override
    {
        if (hookInstalled_)
            UninstallHook();
    }

    bool CanAutoPause() const override { return true; }
    Tab  GetTab() const override { return Tab::OP; }

    void OnRender() override
    {
        ImGui::Checkbox(u8"启用##Teleport", &enabled_);

        // hook 状态跟随 enabled_ 自动同步:无论开关是点击切换、配置加载恢复、
        // 还是 NPG OnResume 来的,只要 enabled_ 为 true 且未被 guard 暂停就装 hook。
        // (之前 hook 只在复选框回调里装,配置恢复的 enabled_=true 不会触发 InstallHook,
        //  导致开关显示为开但点击照常走路。)
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

        s_teleportActive_.store(enabled_ && hookInstalled_,
                                std::memory_order_release);

        ImGui::TextDisabled(u8"开启后点击地面 = 瞬移到点击点(不走寻路)");
        ImGui::TextDisabled(u8"小范围闪现更稳;大范围可能被服务端拉回");

        if (enabled_ && IsPausedByGuard())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               u8"已被 NPG 暂停 (附近有玩家)");
        }
        else if (enabled_ && hookInstalled_)
        {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f),
                               u8"已 hook OnPlayerMoveClick — 点击地面瞬移中");
        }
    }

    void OnShutdown() override
    {
        if (hookInstalled_)
            UninstallHook();
        s_teleportActive_.store(false, std::memory_order_release);
    }

    void OnResume() override
    {
        if (!enabled_)
            return;
        if (InstallHook())
            s_teleportActive_.store(true, std::memory_order_release);
        else
            enabled_ = false;
    }

    std::string ConfigKey() const override { return "Teleport"; }

  private:
    // CLocalUser::SetAfterAction(this=*g_pLocalUser, x, y, action, target) —
    // __thiscall。所有"设置移动/动作目标格"的真正汇聚点:点地走路、拖动走路、
    // 战斗追击都经它写 +0x32F0/F4 目标 + 置 +0x2BC8=1 pending,TraceMove 每帧消费。
    //
    // 之前 hook 的 OnPlayerMoveClick(0x871D10) 实测走路时根本不命中(只是拖动节流
    // 路径偶发调用),所以改 hook 这个底层汇聚点。__thiscall 用 __fastcall + dummy
    // edx 模拟(本项目标准做法)。走路 action==1;战斗/技能是别的 action,放行不干预。
    using fnSetAfterAction =
        int(__fastcall *)(void *ecx, void *edx, int x, int y, int action, int target);

    static fnSetAfterAction  s_oSetAfterAction_;
    static std::atomic<bool> s_teleportActive_; // hook 读它决定瞬移还是放行

    // 去重:UpdateAttackAction 每帧 tick 都会用同一目标格调 SetAfterAction(action=1),
    // 若每帧都瞬移+发同步包会洪泛发包(极易被服务端判异常)。记录上次已瞬移的目标格,
    // 相同则跳过 —— 于是"点一次地/拖一次"只在目标格变化的那一帧瞬移并发一次包。
    static std::atomic<int> s_lastTpX_;
    static std::atomic<int> s_lastTpY_;

    bool hookInstalled_ = false;

    bool InstallHook()
    {
        if (hookInstalled_) return true;
        auto addr = PatternResolver::Get("SetAfterAction");
        if (!addr)
        {
            spdlog::error("GGTB::Teleport: SetAfterAction 未解析");
            return false;
        }
        s_oSetAfterAction_ = reinterpret_cast<fnSetAfterAction>(addr);

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        LONG err = DetourAttach(reinterpret_cast<PVOID *>(&s_oSetAfterAction_),
                                HookSetAfterAction);
        LONG cm = DetourTransactionCommit();
        if (err != NO_ERROR || cm != NO_ERROR)
        {
            spdlog::error("GGTB::Teleport: Detour 失败 attach={} commit={}", err, cm);
            return false;
        }
        hookInstalled_ = true;
        spdlog::info("GGTB::Teleport: hook attached @ {:x} (SetAfterAction)", addr);
        return true;
    }

    void UninstallHook()
    {
        if (!hookInstalled_) return;
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(reinterpret_cast<PVOID *>(&s_oSetAfterAction_),
                     HookSetAfterAction);
        DetourTransactionCommit();
        hookInstalled_ = false;
        spdlog::info("GGTB::Teleport: hook detached");
    }

    static int __fastcall HookSetAfterAction(void *ecx, void *edx, int x, int y,
                                             int action, int target)
    {
        // 先让原函数跑完:保证状态机/pending/同步包逻辑一致(否则 TraceMove 的内部
        // 状态可能错乱)。原函数会写目标 +0x32F0/F4 并置 pending +0x2BC8=1。
        int result = s_oSetAfterAction_(ecx, edx, x, y, action, target);

        // 只对"移动"动作(action==1)做瞬移;战斗/技能等其他 action 原样放行。
        if (s_teleportActive_.load(std::memory_order_acquire) && action == 1)
        {
            // 去重:目标格没变(UpdateAttackAction 每帧用同一目标重复调)就跳过,
            // 避免每帧洪泛发同步包。只在新目标格出现的那一帧瞬移+发一次包。
            if (x != s_lastTpX_.load(std::memory_order_relaxed) ||
                y != s_lastTpY_.load(std::memory_order_relaxed))
            {
                s_lastTpX_.store(x, std::memory_order_relaxed);
                s_lastTpY_.store(y, std::memory_order_relaxed);

                // 原函数刚把目标写进 +0x32F0/F4。Teleport 把可见坐标拍到目标 + 清 pending
                // + 发同步包,于是 TraceMove 下一帧看到 pending=0 直接当作"已到达",
                // 不再逐帧插值 = 点哪/拖到哪瞬移到哪。Teleport 自身 SEH-wrapped。
                Teleport(static_cast<float>(x), static_cast<float>(y));
            }
        }
        return result;
    }
};

inline TeleportModule::fnSetAfterAction TeleportModule::s_oSetAfterAction_ = nullptr;
inline std::atomic<bool>                TeleportModule::s_teleportActive_  {false};
inline std::atomic<int>                 TeleportModule::s_lastTpX_         {INT_MIN};
inline std::atomic<int>                 TeleportModule::s_lastTpY_         {INT_MIN};

} // namespace GGTB
