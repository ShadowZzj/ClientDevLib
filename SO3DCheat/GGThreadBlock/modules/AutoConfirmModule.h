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

// UI_ShowMessageBox_Modal hook — __thiscall(this, text, dialogType, ctx, timeout).
// Emulated via __fastcall with dummy edx (standard x86 Detours pattern).
using fnUIMessageBoxModal = char(__fastcall *)(void *ecx, void *edx,
                                              const char *text, int dialogType,
                                              int ctx, float timeout);

// UI_CountDialog —— 数量选择弹窗 (购买/丢弃/卖物/邮件附件)。
// 原型: __int64 __thiscall(this, labelFmt, initial:i64, max:i64, price:i64, mode:i32)
//   返回 0 = Cancel；返回 N = 用户最终选定的数量。
//   勾选"自动确认弹窗以及物品最大"时直接 return max，等价于一键 Max+OK。
using fnUICountDialog = __int64(__fastcall *)(void *ecx, void *edx,
                                              const char *label,
                                              __int64 initial, __int64 maxCount,
                                              __int64 unitPrice, int mode);

namespace AutoConfirmDetail
{
// 用户层"自动确认弹窗"主开关。勾上后 ALL dialog/数量弹窗都会被吞掉。
inline std::atomic<bool> s_enabled{false};

// 临时 silence 窗口 (单调时钟 ms)。其他模块（AutoMail 发完信、AutoMakeBomb
// 自动炸弹完工等）想短路某次 ack 弹窗时，把 GetTickCount() + N 写到这里。
// 只对 dialogType==1 (单 OK 按钮 toast) 生效；YesNo (=2) 仍要求 s_enabled。
inline std::atomic<DWORD> s_okSilenceUntil{0};

inline fnUIMessageBoxModal s_origMsgBox = nullptr;
inline fnUICountDialog s_origCountDlg = nullptr;
inline bool s_hooked = false;

// 给其他模块用的 silence helper：从现在起静音 N 毫秒的 OK-toast。
inline void SilenceOkToastFor(DWORD ms)
{
    DWORD until = GetTickCount() + ms;
    DWORD prev  = s_okSilenceUntil.load(std::memory_order_relaxed);
    // 只往后延，避免被先前更长的窗口覆盖
    while (until > prev &&
           !s_okSilenceUntil.compare_exchange_weak(prev, until,
                                                    std::memory_order_relaxed))
    {
    }
}

inline bool IsOkSilenceActive()
{
    DWORD until = s_okSilenceUntil.load(std::memory_order_relaxed);
    if (!until) return false;
    // GetTickCount 32-bit wrap 时 (until - now) 仍按补码差比较仍是对的
    return static_cast<int32_t>(until - GetTickCount()) > 0;
}

inline char __fastcall HookUIMessageBoxModal(void *ecx, void *edx,
                                             const char *text, int dialogType,
                                             int ctx, float timeout)
{
    if (s_enabled.load(std::memory_order_relaxed))
        return 1;
    // dialogType==1 是单按钮 OK 通知 toast；其它模块的 silence 只覆盖这一类，
    // 避免误吞 YesNo 让玩家以为点了"是"。
    if (dialogType == 1 && IsOkSilenceActive())
        return 1;
    return s_origMsgBox(ecx, edx, text, dialogType, ctx, timeout);
}

inline __int64 __fastcall HookUICountDialog(void *ecx, void *edx,
                                            const char *label,
                                            __int64 initial, __int64 maxCount,
                                            __int64 unitPrice, int mode)
{
    if (s_enabled.load(std::memory_order_relaxed))
    {
        // 复刻原函数对 max 的两道钳位，避免越上限 (mode==2 的 sell 路径只允许 100)。
        __int64 capped = maxCount;
        if (capped > 0xE8D4A50FFFLL)
            capped = 0xE8D4A50FFFLL;
        if (mode == 2 && capped > 100)
            capped = 100;
        if (unitPrice > 0)
        {
            __int64 priceCap = 0xE8D4A50FFFLL / unitPrice;
            if (priceCap < capped)
                capped = priceCap;
        }
        return capped;
    }
    return s_origCountDlg(ecx, edx, label, initial, maxCount, unitPrice, mode);
}

// 把 hook 安装抽出来 —— AutoMail / 其他想 silence OK toast 的模块也可以
// 在 AutoConfirm 没启用的情况下手动触发安装一次。idempotent。
inline bool EnsureHooksInstalled()
{
    if (s_hooked) return true;

    uintptr_t msgBoxAddr = PatternResolver::Get("UIMessageBoxModal");
    uintptr_t countAddr  = PatternResolver::Get("UICountDialog");
    if (!msgBoxAddr || !countAddr)
    {
        spdlog::error("GGTB::AutoConfirm: addresses not resolved (msgBox={:x} count={:x})",
                      msgBoxAddr, countAddr);
        return false;
    }

    s_origMsgBox   = reinterpret_cast<fnUIMessageBoxModal>(msgBoxAddr);
    s_origCountDlg = reinterpret_cast<fnUICountDialog>(countAddr);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    LONG e1 = DetourAttach(reinterpret_cast<PVOID *>(&s_origMsgBox),
                           HookUIMessageBoxModal);
    LONG e2 = DetourAttach(reinterpret_cast<PVOID *>(&s_origCountDlg),
                           HookUICountDialog);
    LONG err = DetourTransactionCommit();

    if (err != NO_ERROR || e1 != NO_ERROR || e2 != NO_ERROR)
    {
        spdlog::error("GGTB::AutoConfirm: Detour failed: commit={} msgBox={} count={}",
                      err, e1, e2);
        return false;
    }

    s_hooked = true;
    spdlog::info("GGTB::AutoConfirm: hooks installed msgBox=@{:x} countDlg=@{:x}",
                 msgBoxAddr, countAddr);
    return true;
}

// ---------- 开箱战利品自动入袋 ----------
// 把 OnBoxLootRecv_AutoMoveToBag 的两道门 NOP 掉，让箱子开出东西无条件自动
// 入袋。两处都是 6 字节近跳转 (0F 84 / 0F 85 + rel32)。和"自动确认弹窗以及
// 物品最大"开关联动启停。
inline constexpr size_t kBoxGatePatchSize = 6;

inline BYTE s_boxGate1Orig[kBoxGatePatchSize] = {};
inline BYTE s_boxGate2Orig[kBoxGatePatchSize] = {};
inline bool s_boxGate1Captured = false;
inline bool s_boxGate2Captured = false;
inline bool s_boxAutoMovePatched = false;

inline bool NopBoxGate(const char *name, BYTE *backup, bool &captured)
{
    uintptr_t addr = PatternResolver::Get(name);
    if (!addr)
    {
        spdlog::error("GGTB::AutoConfirm: {} unresolved", name);
        return false;
    }
    DWORD oldProt = 0;
    if (!VirtualProtect(reinterpret_cast<void *>(addr), kBoxGatePatchSize,
                        PAGE_EXECUTE_READWRITE, &oldProt))
    {
        spdlog::error("GGTB::AutoConfirm: VirtualProtect failed @ {:x}", addr);
        return false;
    }
    if (!captured)
    {
        memcpy(backup, reinterpret_cast<void *>(addr), kBoxGatePatchSize);
        captured = true;
    }
    memset(reinterpret_cast<void *>(addr), 0x90, kBoxGatePatchSize);
    VirtualProtect(reinterpret_cast<void *>(addr), kBoxGatePatchSize, oldProt, &oldProt);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(addr),
                          kBoxGatePatchSize);
    spdlog::info("GGTB::AutoConfirm: NOP {} @ {:x}", name, addr);
    return true;
}

inline bool RestoreBoxGate(const char *name, const BYTE *backup, bool captured)
{
    if (!captured)
        return true;
    uintptr_t addr = PatternResolver::Get(name);
    if (!addr)
        return false;
    DWORD oldProt = 0;
    if (!VirtualProtect(reinterpret_cast<void *>(addr), kBoxGatePatchSize,
                        PAGE_EXECUTE_READWRITE, &oldProt))
        return false;
    memcpy(reinterpret_cast<void *>(addr), backup, kBoxGatePatchSize);
    VirtualProtect(reinterpret_cast<void *>(addr), kBoxGatePatchSize, oldProt, &oldProt);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(addr),
                          kBoxGatePatchSize);
    return true;
}

inline void SetBoxAutoMove(bool on)
{
    if (on)
    {
        if (s_boxAutoMovePatched)
            return;
        bool ok1 = NopBoxGate("BoxAutoMoveGate1", s_boxGate1Orig, s_boxGate1Captured);
        bool ok2 = NopBoxGate("BoxAutoMoveGate2", s_boxGate2Orig, s_boxGate2Captured);
        s_boxAutoMovePatched = ok1 && ok2;
    }
    else
    {
        if (!s_boxAutoMovePatched)
            return;
        RestoreBoxGate("BoxAutoMoveGate1", s_boxGate1Orig, s_boxGate1Captured);
        RestoreBoxGate("BoxAutoMoveGate2", s_boxGate2Orig, s_boxGate2Captured);
        s_boxAutoMovePatched = false;
        spdlog::info("GGTB::AutoConfirm: box auto-move-to-bag restored");
    }
}
} // namespace AutoConfirmDetail

class AutoConfirmModule : public IModule
{
  public:
    AutoConfirmModule() : IModule(u8"自动确认弹窗以及物品最大") {}

    bool CanAutoPause() const override { return false; }

    void OnRender() override
    {
        if (ImGui::Checkbox(u8"启用##AutoConfirm", &enabled_))
        {
            if (enabled_ && !AutoConfirmDetail::s_hooked)
                AutoConfirmDetail::EnsureHooksInstalled();
            AutoConfirmDetail::s_enabled.store(enabled_, std::memory_order_relaxed);
            AutoConfirmDetail::SetBoxAutoMove(enabled_);
        }
        if (enabled_)
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                               u8"所有确认对话框自动 YES，数量弹窗自动取 Max，开箱自动入袋");
        else
            ImGui::TextDisabled(u8"(不持久化,每次启动默认关闭)");
    }

    void OnShutdown() override
    {
        AutoConfirmDetail::s_enabled.store(false, std::memory_order_relaxed);
        AutoConfirmDetail::SetBoxAutoMove(false);
    }

    std::string ConfigKey() const override { return {}; }
};

} // namespace GGTB
