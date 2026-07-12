#pragma once
#include "../IModule.h"
#include "../util/NetLog.h"
#include "../util/PatternResolver.h"
#include <Detours/build/include/detours.h>
#include <imgui/imgui.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

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
inline std::atomic<bool> s_hooked{false};
inline std::mutex s_hookMutex;

// A workflow-scoped confirmer for the native "Another extra entry ticket"
// prompt. It is deliberately separate from the broad user-facing AutoConfirm
// switch: one generation can accept this one exact Yes/No dialog at most once.
enum class DungeonEntryConfirmState : uint32_t
{
    Idle,
    Armed,
    Accepted,
    Declined,
    Expired,
    Cancelled,
};

enum class DungeonEntryTransactionState : uint32_t
{
    None,
    Prepared,
    CommitIssued,
    Committed,
    CommitFailed,
    Cancelled,
    Expired,
};

inline const char *DungeonEntryTransactionStateName(DungeonEntryTransactionState state)
{
    switch (state)
    {
        case DungeonEntryTransactionState::Prepared:
            return "Prepared";
        case DungeonEntryTransactionState::CommitIssued:
            return "CommitIssued";
        case DungeonEntryTransactionState::Committed:
            return "Committed";
        case DungeonEntryTransactionState::CommitFailed:
            return "CommitFailed";
        case DungeonEntryTransactionState::Cancelled:
            return "Cancelled";
        case DungeonEntryTransactionState::Expired:
            return "Expired";
        case DungeonEntryTransactionState::None:
            return "None";
    }
    return "None";
}

enum class DungeonEntryDialogMatchKind : uint32_t
{
    None,
    TicketUsePrompt,
    TicketItemName,
    TowerEntryPrompt,
    WrongOldKnightPrompt,
};

inline const char *DungeonEntryDialogMatchKindName(DungeonEntryDialogMatchKind kind)
{
    switch (kind)
    {
        case DungeonEntryDialogMatchKind::TicketUsePrompt:
            return "TicketUsePrompt";
        case DungeonEntryDialogMatchKind::TicketItemName:
            return "TicketItemName";
        case DungeonEntryDialogMatchKind::TowerEntryPrompt:
            return "TowerEntryPrompt";
        case DungeonEntryDialogMatchKind::WrongOldKnightPrompt:
            return "WrongOldKnightPrompt";
        case DungeonEntryDialogMatchKind::None:
            return "None";
    }
    return "None";
}

inline const char *DungeonEntryConfirmStateName(DungeonEntryConfirmState state)
{
    switch (state)
    {
        case DungeonEntryConfirmState::Armed:
            return "Armed";
        case DungeonEntryConfirmState::Accepted:
            return "Accepted";
        case DungeonEntryConfirmState::Declined:
            return "Declined";
        case DungeonEntryConfirmState::Expired:
            return "Expired";
        case DungeonEntryConfirmState::Cancelled:
            return "Cancelled";
        case DungeonEntryConfirmState::Idle:
            return "Idle";
    }
    return "Idle";
}

inline constexpr size_t kDungeonEntryTicketSlotCapacity = 80;
inline constexpr DWORD kDungeonEntryLateDialogGuardMs = 5000;

struct DungeonEntryTicketBaseline
{
    uint32_t bagId;
    uint32_t count;
};

struct DungeonEntryConfirmSnapshot
{
    std::string requestToken;
    DungeonEntryTransactionState transactionState;
    bool initialSelectClaimed;
    uint64_t generation;
    DungeonEntryConfirmState state;
    bool autoUseExtraTicket;
    uint64_t baselineSequence;
    uint64_t baselineDialogSelectSequence;
    uint32_t baselineTicketCount;
    DWORD remainingMs;
    uint32_t dialogSeenCount;
    int lastDialogType;
    DungeonEntryDialogMatchKind lastDialogMatch;
    bool lastDialogAuthorized;
    bool towerPromptAccepted;
    DungeonEntryTicketBaseline ticketSlots[kDungeonEntryTicketSlotCapacity];
    size_t ticketSlotCount;
};

struct DungeonEntryPreparedContext
{
    std::string requestToken;
    uint32_t npcId;
    uint32_t opt;
    uint32_t sub;
    uint32_t dialogOptionIndex;
    uint32_t expectedWarpTableId;
    bool autoUseExtraTicket;
    DWORD scopeTtlMs;
};

inline std::mutex s_dungeonEntryScopeMutex;
inline std::string s_dungeonEntryRequestToken;
inline std::atomic<DungeonEntryTransactionState> s_dungeonEntryTransactionState{
    DungeonEntryTransactionState::None};
inline std::atomic<bool> s_dungeonEntryInitialSelectClaimed{false};
inline DungeonEntryPreparedContext s_dungeonEntryPreparedContext{};
inline DWORD s_dungeonEntryPrepareDeadline = 0;
inline std::atomic<uint64_t> s_dungeonEntryNextGeneration{0};
inline std::atomic<uint64_t> s_dungeonEntryGeneration{0};
inline std::atomic<DungeonEntryConfirmState> s_dungeonEntryState{DungeonEntryConfirmState::Idle};
inline std::atomic<bool> s_dungeonEntryAutoUseExtraTicket{false};
inline std::atomic<uint64_t> s_dungeonEntryBaselineSequence{0};
inline std::atomic<uint64_t> s_dungeonEntryBaselineDialogSelectSequence{0};
inline std::atomic<uint32_t> s_dungeonEntryBaselineTicketCount{0};
inline std::atomic<DWORD> s_dungeonEntryScopeTtlMs{0};
inline std::atomic<DWORD> s_dungeonEntryDeadline{0};
inline std::atomic<DWORD> s_dungeonEntryTargetGuardUntil{0};
inline std::atomic<uint32_t> s_dungeonEntryDialogSeenCount{0};
inline std::atomic<int> s_dungeonEntryLastDialogType{-1};
inline std::atomic<DungeonEntryDialogMatchKind> s_dungeonEntryLastDialogMatch{
    DungeonEntryDialogMatchKind::None};
inline std::atomic<bool> s_dungeonEntryLastDialogAuthorized{false};
inline std::atomic<bool> s_dungeonEntryTowerPromptAccepted{false};
inline DungeonEntryTicketBaseline s_dungeonEntryTicketSlots[kDungeonEntryTicketSlotCapacity]{};
inline size_t s_dungeonEntryTicketSlotCount = 0;

inline void ExtendDungeonEntryTargetGuard(DWORD until)
{
    DWORD previous = s_dungeonEntryTargetGuardUntil.load(std::memory_order_relaxed);
    while (static_cast<int32_t>(until - previous) > 0 &&
           !s_dungeonEntryTargetGuardUntil.compare_exchange_weak(previous, until,
                                                                 std::memory_order_relaxed))
    {
    }
}

inline bool IsDungeonEntryTargetGuardActive()
{
    DWORD until = s_dungeonEntryTargetGuardUntil.load(std::memory_order_relaxed);
    return until != 0 && static_cast<int32_t>(until - GetTickCount()) > 0;
}

inline DungeonEntryConfirmState RefreshDungeonEntryConfirmStateLocked()
{
    DungeonEntryConfirmState state = s_dungeonEntryState.load(std::memory_order_acquire);
    if (state != DungeonEntryConfirmState::Armed) return state;

    DWORD now = GetTickCount();
    DWORD deadline = s_dungeonEntryDeadline.load(std::memory_order_relaxed);
    if (deadline != 0 && static_cast<int32_t>(deadline - now) <= 0)
    {
        DungeonEntryConfirmState expected = DungeonEntryConfirmState::Armed;
        if (s_dungeonEntryState.compare_exchange_strong(expected, DungeonEntryConfirmState::Expired,
                                                        std::memory_order_acq_rel))
        {
            ExtendDungeonEntryTargetGuard(now + kDungeonEntryLateDialogGuardMs);
            return DungeonEntryConfirmState::Expired;
        }
        state = expected;
    }
    return state;
}

inline DungeonEntryConfirmState RefreshDungeonEntryConfirmState()
{
    std::lock_guard<std::mutex> lock(s_dungeonEntryScopeMutex);
    return RefreshDungeonEntryConfirmStateLocked();
}

inline DungeonEntryTransactionState RefreshDungeonEntryTransactionStateLocked()
{
    auto state = s_dungeonEntryTransactionState.load(std::memory_order_acquire);
    if (state != DungeonEntryTransactionState::Prepared) return state;
    DWORD now = GetTickCount();
    if (s_dungeonEntryPrepareDeadline != 0 &&
        static_cast<int32_t>(s_dungeonEntryPrepareDeadline - now) <= 0)
    {
        s_dungeonEntryTransactionState.store(DungeonEntryTransactionState::Expired,
                                              std::memory_order_release);
        return DungeonEntryTransactionState::Expired;
    }
    return state;
}

inline bool SameDungeonEntryPreparedContext(const DungeonEntryPreparedContext &left,
                                            const DungeonEntryPreparedContext &right)
{
    return left.requestToken == right.requestToken && left.npcId == right.npcId &&
           left.opt == right.opt && left.sub == right.sub &&
           left.dialogOptionIndex == right.dialogOptionIndex &&
           left.expectedWarpTableId == right.expectedWarpTableId &&
           left.autoUseExtraTicket == right.autoUseExtraTicket &&
           left.scopeTtlMs == right.scopeTtlMs;
}

inline bool PrepareDungeonEntryTransaction(const DungeonEntryPreparedContext &context,
                                           DWORD prepareTtlMs, bool *idempotent)
{
    if (idempotent) *idempotent = false;
    if (context.requestToken.empty() || prepareTtlMs < 1000) return false;
    std::lock_guard<std::mutex> lock(s_dungeonEntryScopeMutex);
    auto transactionState = RefreshDungeonEntryTransactionStateLocked();
    auto confirmState = RefreshDungeonEntryConfirmStateLocked();

    if (context.requestToken == s_dungeonEntryRequestToken)
    {
        if (transactionState == DungeonEntryTransactionState::Prepared &&
            SameDungeonEntryPreparedContext(context, s_dungeonEntryPreparedContext))
        {
            if (idempotent) *idempotent = true;
            return true;
        }
        return false;
    }
    if (transactionState == DungeonEntryTransactionState::Prepared ||
        transactionState == DungeonEntryTransactionState::CommitIssued ||
        confirmState == DungeonEntryConfirmState::Armed)
        return false;

    s_dungeonEntryRequestToken = context.requestToken;
    s_dungeonEntryPreparedContext = context;
    s_dungeonEntryPrepareDeadline = GetTickCount() + prepareTtlMs;
    s_dungeonEntryInitialSelectClaimed.store(false, std::memory_order_relaxed);
    s_dungeonEntryTransactionState.store(DungeonEntryTransactionState::Prepared,
                                          std::memory_order_release);
    return true;
}

inline bool GetDungeonEntryPreparedContext(const std::string &requestToken,
                                           DungeonEntryPreparedContext *context)
{
    if (!context) return false;
    std::lock_guard<std::mutex> lock(s_dungeonEntryScopeMutex);
    if (RefreshDungeonEntryTransactionStateLocked() != DungeonEntryTransactionState::Prepared ||
        requestToken.empty() || requestToken != s_dungeonEntryRequestToken)
        return false;
    *context = s_dungeonEntryPreparedContext;
    return true;
}

inline bool BeginDungeonEntryCommit(
    const std::string &requestToken, uint64_t baselineSequence,
    uint32_t baselineTicketCount, const DungeonEntryTicketBaseline *ticketSlots,
    size_t ticketSlotCount, uint64_t *outGeneration,
    DungeonEntryPreparedContext *outContext)
{
    std::lock_guard<std::mutex> lock(s_dungeonEntryScopeMutex);
    if (RefreshDungeonEntryTransactionStateLocked() != DungeonEntryTransactionState::Prepared ||
        requestToken.empty() || requestToken != s_dungeonEntryRequestToken ||
        RefreshDungeonEntryConfirmStateLocked() == DungeonEntryConfirmState::Armed)
        return false;

    if (ticketSlotCount > kDungeonEntryTicketSlotCapacity)
        ticketSlotCount = kDungeonEntryTicketSlotCapacity;
    s_dungeonEntryTicketSlotCount = ticketSlotCount;
    for (size_t i = 0; i < ticketSlotCount; ++i) s_dungeonEntryTicketSlots[i] = ticketSlots[i];

    uint64_t generation = s_dungeonEntryNextGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
    if (generation == 0)
    {
        generation = s_dungeonEntryNextGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    DWORD now = GetTickCount();
    DWORD ttlMs = s_dungeonEntryPreparedContext.scopeTtlMs;
    DWORD deadline = now + ttlMs;
    s_dungeonEntryGeneration.store(generation, std::memory_order_relaxed);
    s_dungeonEntryAutoUseExtraTicket.store(
        s_dungeonEntryPreparedContext.autoUseExtraTicket, std::memory_order_relaxed);
    s_dungeonEntryBaselineSequence.store(baselineSequence, std::memory_order_relaxed);
    s_dungeonEntryBaselineDialogSelectSequence.store(
        NetLog::GetLastDungeonDialogSelect().sequence, std::memory_order_relaxed);
    s_dungeonEntryBaselineTicketCount.store(baselineTicketCount, std::memory_order_relaxed);
    s_dungeonEntryScopeTtlMs.store(ttlMs, std::memory_order_relaxed);
    s_dungeonEntryDeadline.store(deadline, std::memory_order_relaxed);
    s_dungeonEntryTargetGuardUntil.store(deadline + kDungeonEntryLateDialogGuardMs,
                                         std::memory_order_relaxed);
    s_dungeonEntryDialogSeenCount.store(0, std::memory_order_relaxed);
    s_dungeonEntryLastDialogType.store(-1, std::memory_order_relaxed);
    s_dungeonEntryLastDialogMatch.store(DungeonEntryDialogMatchKind::None,
                                        std::memory_order_relaxed);
    s_dungeonEntryLastDialogAuthorized.store(false, std::memory_order_relaxed);
    s_dungeonEntryTowerPromptAccepted.store(false, std::memory_order_relaxed);
    s_dungeonEntryPrepareDeadline = 0;
    s_dungeonEntryInitialSelectClaimed.store(false, std::memory_order_relaxed);
    s_dungeonEntryTransactionState.store(DungeonEntryTransactionState::CommitIssued,
                                          std::memory_order_relaxed);
    s_dungeonEntryState.store(DungeonEntryConfirmState::Armed, std::memory_order_release);
    if (outGeneration) *outGeneration = generation;
    if (outContext) *outContext = s_dungeonEntryPreparedContext;
    return true;
}

inline bool ClaimDungeonEntryInitialSelect(const std::string &requestToken,
                                           uint64_t generation)
{
    std::lock_guard<std::mutex> lock(s_dungeonEntryScopeMutex);
    if (requestToken.empty() || requestToken != s_dungeonEntryRequestToken ||
        generation == 0 || generation != s_dungeonEntryGeneration.load(std::memory_order_acquire) ||
        s_dungeonEntryTransactionState.load(std::memory_order_acquire) !=
            DungeonEntryTransactionState::CommitIssued ||
        RefreshDungeonEntryConfirmStateLocked() != DungeonEntryConfirmState::Armed ||
        s_dungeonEntryInitialSelectClaimed.load(std::memory_order_relaxed))
        return false;
    s_dungeonEntryInitialSelectClaimed.store(true, std::memory_order_release);
    return true;
}

inline bool CompleteDungeonEntryInitialSelect(const std::string &requestToken,
                                              uint64_t generation, bool success)
{
    std::lock_guard<std::mutex> lock(s_dungeonEntryScopeMutex);
    if (requestToken.empty() || requestToken != s_dungeonEntryRequestToken ||
        generation == 0 || generation != s_dungeonEntryGeneration.load(std::memory_order_acquire) ||
        !s_dungeonEntryInitialSelectClaimed.load(std::memory_order_acquire) ||
        s_dungeonEntryTransactionState.load(std::memory_order_acquire) !=
            DungeonEntryTransactionState::CommitIssued)
        return false;

    s_dungeonEntryTransactionState.store(
        success ? DungeonEntryTransactionState::Committed
                : DungeonEntryTransactionState::CommitFailed,
        std::memory_order_release);
    if (!success)
    {
        DungeonEntryConfirmState expected = DungeonEntryConfirmState::Armed;
        s_dungeonEntryState.compare_exchange_strong(expected,
                                                    DungeonEntryConfirmState::Cancelled,
                                                    std::memory_order_acq_rel);
        ExtendDungeonEntryTargetGuard(GetTickCount() + kDungeonEntryLateDialogGuardMs);
    }
    return true;
}

inline bool CancelDungeonEntryConfirm(uint64_t generation, bool *changed)
{
    std::lock_guard<std::mutex> lock(s_dungeonEntryScopeMutex);
    RefreshDungeonEntryTransactionStateLocked();
    RefreshDungeonEntryConfirmStateLocked();
    if (s_dungeonEntryGeneration.load(std::memory_order_acquire) != generation || generation == 0)
        return false;

    bool didCancel = false;
    DungeonEntryConfirmState expected = DungeonEntryConfirmState::Armed;
    if (s_dungeonEntryState.compare_exchange_strong(expected, DungeonEntryConfirmState::Cancelled,
                                                    std::memory_order_acq_rel))
    {
        didCancel = true;
    }
    if (s_dungeonEntryTransactionState.load(std::memory_order_acquire) ==
        DungeonEntryTransactionState::CommitIssued)
    {
        s_dungeonEntryTransactionState.store(DungeonEntryTransactionState::Cancelled,
                                              std::memory_order_release);
        didCancel = true;
    }
    // Repeating cancel on an already-terminal generation is state-idempotent,
    // but renews the short late-dialog guard from the caller's stop point.
    ExtendDungeonEntryTargetGuard(GetTickCount() + kDungeonEntryLateDialogGuardMs);
    if (changed) *changed = didCancel;
    return true;
}

inline bool CancelDungeonEntryConfirmByToken(const std::string &requestToken, bool *changed)
{
    std::lock_guard<std::mutex> lock(s_dungeonEntryScopeMutex);
    auto transactionState = RefreshDungeonEntryTransactionStateLocked();
    RefreshDungeonEntryConfirmStateLocked();
    if (requestToken.empty() || requestToken != s_dungeonEntryRequestToken) return false;

    bool didCancel = false;
    if (transactionState == DungeonEntryTransactionState::Prepared ||
        transactionState == DungeonEntryTransactionState::CommitIssued)
    {
        s_dungeonEntryTransactionState.store(DungeonEntryTransactionState::Cancelled,
                                              std::memory_order_release);
        s_dungeonEntryPrepareDeadline = 0;
        didCancel = true;
    }

    DungeonEntryConfirmState expected = DungeonEntryConfirmState::Armed;
    if (s_dungeonEntryState.compare_exchange_strong(expected,
                                                    DungeonEntryConfirmState::Cancelled,
                                                    std::memory_order_acq_rel))
        didCancel = true;

    ExtendDungeonEntryTargetGuard(GetTickCount() + kDungeonEntryLateDialogGuardMs);
    if (changed) *changed = didCancel;
    return true;
}

inline void CancelDungeonEntryConfirmForShutdown()
{
    std::string requestToken;
    {
        std::lock_guard<std::mutex> lock(s_dungeonEntryScopeMutex);
        requestToken = s_dungeonEntryRequestToken;
    }
    if (!requestToken.empty())
    {
        bool ignored = false;
        CancelDungeonEntryConfirmByToken(requestToken, &ignored);
    }
}

inline void FillDungeonEntryConfirmSnapshotLocked(DungeonEntryConfirmState state,
                                                  DungeonEntryConfirmSnapshot *out)
{
    if (!out) return;
    out->requestToken = s_dungeonEntryRequestToken;
    out->transactionState = s_dungeonEntryTransactionState.load(std::memory_order_relaxed);
    out->initialSelectClaimed =
        s_dungeonEntryInitialSelectClaimed.load(std::memory_order_relaxed);
    out->generation = s_dungeonEntryGeneration.load(std::memory_order_relaxed);
    out->state = state;
    out->autoUseExtraTicket = s_dungeonEntryAutoUseExtraTicket.load(std::memory_order_relaxed);
    out->baselineSequence = s_dungeonEntryBaselineSequence.load(std::memory_order_relaxed);
    out->baselineDialogSelectSequence =
        s_dungeonEntryBaselineDialogSelectSequence.load(std::memory_order_relaxed);
    out->baselineTicketCount = s_dungeonEntryBaselineTicketCount.load(std::memory_order_relaxed);
    DWORD deadline = s_dungeonEntryDeadline.load(std::memory_order_relaxed);
    DWORD now = GetTickCount();
    out->remainingMs =
        state == DungeonEntryConfirmState::Armed && static_cast<int32_t>(deadline - now) > 0
            ? deadline - now
            : 0;
    out->dialogSeenCount = s_dungeonEntryDialogSeenCount.load(std::memory_order_relaxed);
    out->lastDialogType = s_dungeonEntryLastDialogType.load(std::memory_order_relaxed);
    out->lastDialogMatch = s_dungeonEntryLastDialogMatch.load(std::memory_order_relaxed);
    out->lastDialogAuthorized =
        s_dungeonEntryLastDialogAuthorized.load(std::memory_order_relaxed);
    out->towerPromptAccepted =
        s_dungeonEntryTowerPromptAccepted.load(std::memory_order_relaxed);
    out->ticketSlotCount = s_dungeonEntryTicketSlotCount;
    for (size_t i = 0; i < out->ticketSlotCount; ++i)
        out->ticketSlots[i] = s_dungeonEntryTicketSlots[i];
}

inline bool GetDungeonEntryConfirmSnapshot(uint64_t generation, DungeonEntryConfirmSnapshot *out)
{
    std::lock_guard<std::mutex> lock(s_dungeonEntryScopeMutex);
    RefreshDungeonEntryTransactionStateLocked();
    DungeonEntryConfirmState state = RefreshDungeonEntryConfirmStateLocked();
    uint64_t currentGeneration = s_dungeonEntryGeneration.load(std::memory_order_acquire);
    if (generation == 0 || generation != currentGeneration) return false;
    FillDungeonEntryConfirmSnapshotLocked(state, out);
    return true;
}

inline bool GetDungeonEntryConfirmSnapshotByToken(const std::string &requestToken,
                                                  DungeonEntryConfirmSnapshot *out)
{
    std::lock_guard<std::mutex> lock(s_dungeonEntryScopeMutex);
    RefreshDungeonEntryTransactionStateLocked();
    DungeonEntryConfirmState state = RefreshDungeonEntryConfirmStateLocked();
    if (requestToken.empty() || requestToken != s_dungeonEntryRequestToken) return false;
    FillDungeonEntryConfirmSnapshotLocked(state, out);
    return true;
}

inline uint64_t GetDungeonEntryConfirmGeneration()
{
    return s_dungeonEntryGeneration.load(std::memory_order_acquire);
}

inline DungeonEntryConfirmState GetDungeonEntryConfirmState()
{
    return RefreshDungeonEntryConfirmState();
}

inline DungeonEntryTransactionState GetDungeonEntryTransactionState()
{
    std::lock_guard<std::mutex> lock(s_dungeonEntryScopeMutex);
    return RefreshDungeonEntryTransactionStateLocked();
}

inline std::string GetDungeonEntryRequestToken()
{
    std::lock_guard<std::mutex> lock(s_dungeonEntryScopeMutex);
    return s_dungeonEntryRequestToken;
}

// CP950 raw bytes for "Another extra entry ticket". The surrounding native
// prompt is intentionally ignored; matching the exact item name keeps this
// confirmer from consuming other dungeon tickets selected by the same UI path.
inline constexpr unsigned char kDungeonEntryTicketNameCp950[] = {
    0x41, 0x6E, 0x6F, 0x74, 0x68, 0x65, 0x72, 0x20, 0xC3,
    0x42, 0xA5, 0x7E, 0xA4, 0x4A, 0xB3, 0xF5, 0xA8, 0xE9};

// Live type=2 ticket confirmation:
// "確定要使用 Another 額外入場券 嗎？"
inline constexpr unsigned char kDungeonEntryTicketUsePromptCp950[] = {
    0xBD, 0x54, 0xA9, 0x77, 0xAD, 0x6E, 0xA8, 0xCF, 0xA5, 0xCE,
    0x20, 0x41, 0x6E, 0x6F, 0x74, 0x68, 0x65, 0x72, 0x20, 0xC3,
    0x42, 0xA5, 0x7E, 0xA4, 0x4A, 0xB3, 0xF5, 0xA8, 0xE9, 0x20,
    0xB6, 0xDC, 0xA1, 0x48};

// Legacy/wrong-shortcut type=2 prompt: "你要進入賢者之塔Another嗎?".
// The correct interaction-wheel + quest-dialog flow displays this sentence as
// an ordinary mode-1 NPC page before opt=9575, never as a modal. If it reaches
// this hook, fail closed instead of authorizing a ticket.
inline constexpr unsigned char kDungeonEntryTowerPromptCp950[] = {
    0xA7, 0x41, 0xAD, 0x6E, 0xB6, 0x69, 0xA4, 0x4A, 0xBD,
    0xE5, 0xAA, 0xCC, 0xA4, 0xA7, 0xB6, 0xF0, 0x41, 0x6E,
    0x6F, 0x74, 0x68, 0x65, 0x72, 0xB6, 0xDC, 0x3F};

// Wrong fallback observed when opt=9575 was sent without native warp context:
// "確定要使用 老騎士的推薦函 嗎？". It must always resolve as No
// while a targeted entry transaction (or its late-dialog guard) is active.
inline constexpr unsigned char kDungeonEntryWrongOldKnightPromptCp950[] = {
    0xBD, 0x54, 0xA9, 0x77, 0xAD, 0x6E, 0xA8, 0xCF, 0xA5, 0xCE,
    0x20, 0xA6, 0xD1, 0xC3, 0x4D, 0xA4, 0x68, 0xAA, 0xBA, 0xB1,
    0xC0, 0xC2, 0xCB, 0xA8, 0xE7, 0x20, 0xB6, 0xDC, 0xA1, 0x48};

inline bool ContainsDungeonEntryTicketName(const char *text)
{
    if (!text) return false;
    __try
    {
        constexpr size_t needleLength = sizeof(kDungeonEntryTicketNameCp950);
        for (size_t i = 0; i < 2048; ++i)
        {
            if (text[i] == '\0') break;
            size_t matched = 0;
            while (matched < needleLength && i + matched < 2048 && text[i + matched] != '\0' &&
                   static_cast<unsigned char>(text[i + matched]) ==
                       kDungeonEntryTicketNameCp950[matched])
            {
                ++matched;
            }
            if (matched == needleLength) return true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return false;
}

inline bool EqualsDungeonEntryTowerPrompt(const char *text)
{
    if (!text) return false;
    __try
    {
        constexpr size_t promptLength = sizeof(kDungeonEntryTowerPromptCp950);
        for (size_t i = 0; i < promptLength; ++i)
        {
            if (static_cast<unsigned char>(text[i]) != kDungeonEntryTowerPromptCp950[i])
                return false;
        }
        return text[promptLength] == '\0';
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

inline bool EqualsDungeonEntryTicketUsePrompt(const char *text)
{
    if (!text) return false;
    __try
    {
        constexpr size_t promptLength = sizeof(kDungeonEntryTicketUsePromptCp950);
        for (size_t i = 0; i < promptLength; ++i)
        {
            if (static_cast<unsigned char>(text[i]) != kDungeonEntryTicketUsePromptCp950[i])
                return false;
        }
        return text[promptLength] == '\0';
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

inline bool EqualsDungeonEntryWrongOldKnightPrompt(const char *text)
{
    if (!text) return false;
    __try
    {
        constexpr size_t promptLength = sizeof(kDungeonEntryWrongOldKnightPromptCp950);
        for (size_t i = 0; i < promptLength; ++i)
        {
            if (static_cast<unsigned char>(text[i]) !=
                kDungeonEntryWrongOldKnightPromptCp950[i])
                return false;
        }
        return text[promptLength] == '\0';
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

inline DungeonEntryDialogMatchKind MatchDungeonEntryDialogText(const char *text)
{
    if (EqualsDungeonEntryTicketUsePrompt(text))
        return DungeonEntryDialogMatchKind::TicketUsePrompt;
    if (EqualsDungeonEntryTowerPrompt(text))
        return DungeonEntryDialogMatchKind::TowerEntryPrompt;
    return DungeonEntryDialogMatchKind::None;
}

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

inline char __fastcall HookUIMessageBoxModal(void *ecx, void *edx, const char *text, int dialogType,
                                             int ctx, float timeout)
{
    DungeonEntryConfirmState dungeonState;
    uint64_t observedGeneration = 0;
    {
        std::lock_guard<std::mutex> scopeLock(s_dungeonEntryScopeMutex);
        dungeonState = RefreshDungeonEntryConfirmStateLocked();
        if (dungeonState == DungeonEntryConfirmState::Armed)
            observedGeneration = s_dungeonEntryGeneration.load(std::memory_order_acquire);
    }
    DungeonEntryDialogMatchKind matchKind = dialogType == 2
                                                ? MatchDungeonEntryDialogText(text)
                                                : DungeonEntryDialogMatchKind::None;
    bool exactTicketDialog = matchKind != DungeonEntryDialogMatchKind::None;
    bool wrongOldKnightDialog =
        dialogType == 2 && EqualsDungeonEntryWrongOldKnightPrompt(text);

    if (dungeonState == DungeonEntryConfirmState::Armed)
    {
        // The scoped generation owns dialog policy while armed. Unmatched
        // dialogs remain fully native; neither broad AutoConfirm nor OK-toast
        // silence may swallow them.
        if (!exactTicketDialog)
        {
            {
                std::lock_guard<std::mutex> scopeLock(s_dungeonEntryScopeMutex);
                if (RefreshDungeonEntryConfirmStateLocked() ==
                        DungeonEntryConfirmState::Armed &&
                    observedGeneration != 0 &&
                    s_dungeonEntryGeneration.load(std::memory_order_acquire) ==
                        observedGeneration)
                {
                    s_dungeonEntryDialogSeenCount.fetch_add(1, std::memory_order_relaxed);
                    s_dungeonEntryLastDialogType.store(dialogType, std::memory_order_relaxed);
                    s_dungeonEntryLastDialogMatch.store(
                        wrongOldKnightDialog
                            ? DungeonEntryDialogMatchKind::WrongOldKnightPrompt
                            : matchKind,
                        std::memory_order_relaxed);
                    s_dungeonEntryLastDialogAuthorized.store(false, std::memory_order_relaxed);
                    if (wrongOldKnightDialog)
                    {
                        DungeonEntryConfirmState expected = DungeonEntryConfirmState::Armed;
                        s_dungeonEntryState.compare_exchange_strong(
                            expected, DungeonEntryConfirmState::Declined,
                            std::memory_order_acq_rel);
                    }
                }
            }
            if (wrongOldKnightDialog) return 0;
            return s_origMsgBox(ecx, edx, text, dialogType, ctx, timeout);
        }

        // Serialize the one terminal decision with cancel/new-generation arm.
        // Without this lock, a delayed old prompt could observe Armed, get
        // preempted by cancel + re-arm, then CAS the new generation to Accepted.
        std::lock_guard<std::mutex> scopeLock(s_dungeonEntryScopeMutex);
        if (RefreshDungeonEntryConfirmStateLocked() != DungeonEntryConfirmState::Armed ||
            observedGeneration == 0 ||
            s_dungeonEntryGeneration.load(std::memory_order_acquire) != observedGeneration)
            return 0;

        s_dungeonEntryDialogSeenCount.fetch_add(1, std::memory_order_relaxed);
        s_dungeonEntryLastDialogType.store(dialogType, std::memory_order_relaxed);
        s_dungeonEntryLastDialogMatch.store(matchKind, std::memory_order_relaxed);
        s_dungeonEntryLastDialogAuthorized.store(false, std::memory_order_relaxed);

        uint64_t baseline = s_dungeonEntryBaselineSequence.load(std::memory_order_relaxed);
        uint64_t dialogSelectBaseline =
            s_dungeonEntryBaselineDialogSelectSequence.load(std::memory_order_relaxed);
        auto lastDialogSelect = NetLog::GetLastDungeonDialogSelect();
        auto transactionState =
            s_dungeonEntryTransactionState.load(std::memory_order_acquire);
        bool ownSelectObserved =
            (transactionState == DungeonEntryTransactionState::CommitIssued ||
             transactionState == DungeonEntryTransactionState::Committed) &&
            s_dungeonEntryInitialSelectClaimed.load(std::memory_order_acquire) &&
            lastDialogSelect.sequence > dialogSelectBaseline &&
            lastDialogSelect.npcId == s_dungeonEntryPreparedContext.npcId &&
            lastDialogSelect.opt == s_dungeonEntryPreparedContext.opt &&
            lastDialogSelect.sub == s_dungeonEntryPreparedContext.sub;
        bool authorized = ownSelectObserved &&
                          s_dungeonEntryAutoUseExtraTicket.load(std::memory_order_relaxed) &&
                          s_dungeonEntryBaselineTicketCount.load(std::memory_order_relaxed) > 0 &&
                          NetLog::GetLastDungeonTicketPromptSequence() > baseline;
        s_dungeonEntryLastDialogAuthorized.store(authorized, std::memory_order_relaxed);

        // In the real NPC UI flow, "你要進入賢者之塔Another嗎?" is a normal
        // mode-1 quest page, not a modal prerequisite. Seeing it here means the
        // client reached the legacy/wrong shortcut path. Fail closed and never
        // let that prompt authorize a cash-item use.
        if (matchKind == DungeonEntryDialogMatchKind::TowerEntryPrompt)
        {
            DungeonEntryConfirmState expected = DungeonEntryConfirmState::Armed;
            s_dungeonEntryState.compare_exchange_strong(expected,
                                                        DungeonEntryConfirmState::Declined,
                                                        std::memory_order_acq_rel);
            spdlog::warn(
                "GGTB::AutoConfirm: rejected unexpected tower-entry modal generation={}",
                s_dungeonEntryGeneration.load(std::memory_order_relaxed));
            return 0;
        }

        s_dungeonEntryLastDialogAuthorized.store(authorized, std::memory_order_relaxed);

        DungeonEntryConfirmState expected = DungeonEntryConfirmState::Armed;
        DungeonEntryConfirmState terminal =
            authorized ? DungeonEntryConfirmState::Accepted : DungeonEntryConfirmState::Declined;
        if (s_dungeonEntryState.compare_exchange_strong(expected, terminal,
                                                        std::memory_order_acq_rel))
        {
            spdlog::info(
                "GGTB::AutoConfirm: dungeon entry generation={} dialog={} type={} authorized={}",
                s_dungeonEntryGeneration.load(std::memory_order_relaxed),
                DungeonEntryDialogMatchKindName(matchKind), dialogType, authorized);
            // char return follows the native modal convention: 1=Yes, 0=No.
            return authorized ? 1 : 0;
        }

        // Another invocation already consumed this generation's one decision.
        return 0;
    }

    // A target prompt delivered just after cancellation/expiry (or a duplicate
    // after the one accepted prompt) must not fall through to global
    // AutoConfirm. The guard is bounded by the original TTL plus a short grace
    // so unrelated later manual use keeps the legacy behavior.
    if (exactTicketDialog || wrongOldKnightDialog)
    {
        if (IsDungeonEntryTargetGuardActive()) return 0;
        // Targeted dungeon-item prompts never inherit the broad AutoConfirm
        // policy. Outside a live generation they remain a native user choice,
        // so a duplicate can never consume a second ticket automatically.
        return s_origMsgBox(ecx, edx, text, dialogType, ctx, timeout);
    }

    if (s_enabled.load(std::memory_order_relaxed)) return 1;
    // dialogType==1 是单按钮 OK 通知 toast；其它模块的 silence 只覆盖这一类，
    // 避免误吞 YesNo 让玩家以为点了"是"。
    if (dialogType == 1 && IsOkSilenceActive()) return 1;
    return s_origMsgBox(ecx, edx, text, dialogType, ctx, timeout);
}

inline __int64 __fastcall HookUICountDialog(void *ecx, void *edx, const char *label,
                                            __int64 initial, __int64 maxCount, __int64 unitPrice,
                                            int mode)
{
    // Never let the broad AutoConfirm "Max" policy affect any count dialog
    // while the entry-ticket generation is armed.
    if (RefreshDungeonEntryConfirmState() == DungeonEntryConfirmState::Armed)
    {
        return s_origCountDlg(ecx, edx, label, initial, maxCount, unitPrice, mode);
    }
    if (s_enabled.load(std::memory_order_relaxed))
    {
        // 复刻原函数对 max 的两道钳位，避免越上限 (mode==2 的 sell 路径只允许 100)。
        __int64 capped = maxCount;
        if (capped > 0xE8D4A50FFFLL) capped = 0xE8D4A50FFFLL;
        if (mode == 2 && capped > 100) capped = 100;
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
    if (s_hooked.load(std::memory_order_acquire)) return true;
    std::lock_guard<std::mutex> lock(s_hookMutex);
    if (s_hooked.load(std::memory_order_relaxed)) return true;

    uintptr_t msgBoxAddr = PatternResolver::Get("UIMessageBoxModal");
    uintptr_t countAddr = PatternResolver::Get("UICountDialog");
    if (!msgBoxAddr || !countAddr)
    {
        spdlog::error("GGTB::AutoConfirm: addresses not resolved (msgBox={:x} count={:x})",
                      msgBoxAddr, countAddr);
        return false;
    }

    s_origMsgBox = reinterpret_cast<fnUIMessageBoxModal>(msgBoxAddr);
    s_origCountDlg = reinterpret_cast<fnUICountDialog>(countAddr);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    LONG e1 = DetourAttach(reinterpret_cast<PVOID *>(&s_origMsgBox), HookUIMessageBoxModal);
    LONG e2 = DetourAttach(reinterpret_cast<PVOID *>(&s_origCountDlg), HookUICountDialog);
    if (e1 != NO_ERROR || e2 != NO_ERROR)
    {
        DetourTransactionAbort();
        spdlog::error("GGTB::AutoConfirm: Detour attach failed: msgBox={} count={}", e1, e2);
        return false;
    }
    LONG err = DetourTransactionCommit();

    if (err != NO_ERROR)
    {
        spdlog::error("GGTB::AutoConfirm: Detour commit failed: {}", err);
        return false;
    }

    s_hooked.store(true, std::memory_order_release);
    spdlog::info("GGTB::AutoConfirm: hooks installed msgBox=@{:x} countDlg=@{:x}", msgBoxAddr,
                 countAddr);
    return true;
}

inline bool UninstallHooks()
{
    CancelDungeonEntryConfirmForShutdown();

    std::lock_guard<std::mutex> lock(s_hookMutex);
    if (!s_hooked.load(std::memory_order_acquire)) return true;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    LONG e1 = DetourDetach(reinterpret_cast<PVOID *>(&s_origCountDlg), HookUICountDialog);
    LONG e2 = DetourDetach(reinterpret_cast<PVOID *>(&s_origMsgBox), HookUIMessageBoxModal);
    if (e1 != NO_ERROR || e2 != NO_ERROR)
    {
        DetourTransactionAbort();
        spdlog::error("GGTB::AutoConfirm: Detour detach failed: count={} msgBox={}", e1, e2);
        return false;
    }

    LONG err = DetourTransactionCommit();
    if (err != NO_ERROR)
    {
        spdlog::error("GGTB::AutoConfirm: Detour detach commit failed: {}", err);
        return false;
    }

    s_hooked.store(false, std::memory_order_release);
    spdlog::info("GGTB::AutoConfirm: hooks uninstalled");
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
    bool IsCompact() const override { return true; }

    void OnRender() override
    {
        if (ImGui::Checkbox(u8"自动确认弹窗以及物品最大##AutoConfirm", &enabled_))
        {
            if (enabled_ && !AutoConfirmDetail::s_hooked.load(std::memory_order_acquire))
                AutoConfirmDetail::EnsureHooksInstalled();
            AutoConfirmDetail::s_enabled.store(enabled_, std::memory_order_relaxed);
            AutoConfirmDetail::SetBoxAutoMove(enabled_);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(enabled_
                                  ? u8"所有确认对话框自动 YES，数量弹窗自动取 Max，开箱自动入袋"
                                  : u8"不持久化,每次启动默认关闭");
    }

    void OnShutdown() override
    {
        // Cancel the scoped generation before any other shutdown work can
        // detach its observer/hook dependencies.
        AutoConfirmDetail::CancelDungeonEntryConfirmForShutdown();
        AutoConfirmDetail::s_enabled.store(false, std::memory_order_relaxed);
        AutoConfirmDetail::SetBoxAutoMove(false);
    }

    std::string ConfigKey() const override { return {}; }
};

} // namespace GGTB
