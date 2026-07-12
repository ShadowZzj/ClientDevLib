#include "DailyTaskControl.h"

#include "NetLog.h"
#include "PatternResolver.h"
#include "RemoteControl.h"

#include <Windows.h>
#include <json.hpp>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

namespace GGTB::DailyTaskControl
{
namespace
{
constexpr int kContentId = 96;
constexpr size_t kTaskCount = 8;
constexpr size_t kHuntFirst = 3;
constexpr size_t kTaskStride = 20;

enum class Action
{
    Reroll,
    Accept,
    Finish,
};

struct TaskState
{
    uint32_t index;
    uint32_t taskId;
    uint32_t progress;
    uint32_t state;
    uint32_t pending;
    uint32_t replaceable;
    uint32_t grade;
    uint32_t target;
};

struct DailyState
{
    uintptr_t content;
    uint32_t taskCount;
    uint32_t completedCount;
    uint32_t changeChance;
    uint32_t rewardPending;
    TaskState tasks[kTaskCount];
};

struct WaitOutcome
{
    bool acknowledged;
    bool confirmed;
    bool serverError;
    uint64_t replySequence;
    uint64_t errorSequence;
    uint32_t replyValue;
    uint32_t errorValue0;
    uint32_t errorValue1;
    DailyState after;
};

using UIManagerGetSingletonFn = void *(__cdecl *)();
using UIManagerGetUIContentFn = void *(__thiscall *)(void *, int);
using UIManagerIsContentOpenFn = uint8_t(__thiscall *)(void *, uint8_t);
using DailyTaskActionFn = void(__thiscall *)(void *, int, int);

constexpr uint8_t kQuestActionPattern[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x30, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x33, 0xC5, 0x89,
    0x45, 0xFC, 0x89, 0x4D, 0xD4, 0x83, 0x7D, 0x08, 0x00, 0x7C, 0x10, 0x8B, 0x4D, 0xD4,
    0x83, 0xC1, 0x38, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x39, 0x45, 0x08, 0x76, 0x05,
};
constexpr uint8_t kQuestActionMask[] = {
    1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1,
};
constexpr uint8_t kRewardActionPattern[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x3C, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x33, 0xC5,
    0x89, 0x45, 0xFC, 0x89, 0x4D, 0xD0, 0x83, 0x7D, 0x0C, 0x03, 0x74, 0x21, 0x83,
    0x7D, 0x0C, 0x02, 0x74, 0x1B, 0x83, 0x7D, 0x08, 0x00, 0x7C, 0x10,
};
constexpr uint8_t kRewardActionMask[] = {
    1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

std::mutex s_commandMutex;

void *CallGetContentSEH(UIManagerGetSingletonFn singleton, UIManagerGetUIContentFn getContent)
{
    __try
    {
        void *manager = singleton();
        return manager ? getContent(manager, kContentId) : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

int CallIsContentOpenSEH(UIManagerGetSingletonFn singleton, UIManagerIsContentOpenFn isContentOpen)
{
    __try
    {
        void *manager = singleton();
        return manager ? (isContentOpen(manager, static_cast<uint8_t>(kContentId)) ? 1 : 0) : -1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

bool IsDailyTaskWindowOpen(bool *open)
{
    if (!open) return false;
    auto singletonAddr = PatternResolver::Get("UIManagerGetSingleton");
    auto isOpenAddr = PatternResolver::Get("UIManagerIsContentOpen");
    if (!singletonAddr || !isOpenAddr) return false;
    int result = CallIsContentOpenSEH(reinterpret_cast<UIManagerGetSingletonFn>(singletonAddr),
                                      reinterpret_cast<UIManagerIsContentOpenFn>(isOpenAddr));
    if (result < 0) return false;
    *open = result != 0;
    return true;
}

bool MatchActionPatternSEH(uintptr_t address, const uint8_t *pattern, const uint8_t *mask,
                           size_t size)
{
    if (!address || !pattern || !mask || size == 0) return false;
    __try
    {
        const uint8_t *code = reinterpret_cast<const uint8_t *>(address);
        for (size_t i = 0; i < size; ++i)
        {
            if (mask[i] && code[i] != pattern[i]) return false;
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool ValidateActionAddress(Action action, uintptr_t address)
{
    if (action == Action::Finish)
        return MatchActionPatternSEH(address, kRewardActionPattern, kRewardActionMask,
                                     _countof(kRewardActionPattern));
    return MatchActionPatternSEH(address, kQuestActionPattern, kQuestActionMask,
                                 _countof(kQuestActionPattern));
}

bool ReadStateOnce(void *content, DailyState *out)
{
    if (!content || !out) return false;

    DailyState value{};
    value.content = reinterpret_cast<uintptr_t>(content);
    __try
    {
        const uint8_t *base = static_cast<const uint8_t *>(content);
        uintptr_t begin = 0;
        uintptr_t end = 0;
        uintptr_t capacity = 0;
        std::memcpy(&begin, base + 0x38, sizeof(begin));
        std::memcpy(&end, base + 0x3C, sizeof(end));
        std::memcpy(&capacity, base + 0x40, sizeof(capacity));
        if (!begin || end < begin || capacity < end || (end - begin) % kTaskStride != 0)
            return false;

        const size_t count = (end - begin) / kTaskStride;
        if (count == 0 || count > kTaskCount) return false;
        value.taskCount = static_cast<uint32_t>(count);
        std::memcpy(&value.completedCount, base + 0x98, 4);
        std::memcpy(&value.changeChance, base + 0x9C, 4);
        uint8_t rewardPending = 0;
        std::memcpy(&rewardPending, base + 0xA0, 1);
        value.rewardPending = rewardPending;

        for (size_t i = 0; i < count; ++i)
        {
            const uint8_t *entry = reinterpret_cast<const uint8_t *>(begin + i * kTaskStride);
            uintptr_t table = 0;
            uint8_t pending = 0;
            std::memcpy(&table, entry, sizeof(table));
            std::memcpy(&value.tasks[i].progress, entry + 4, 4);
            std::memcpy(&value.tasks[i].state, entry + 8, 4);
            std::memcpy(&pending, entry + 0x0C, 1);
            if (!table) return false;

            const uint8_t *record = reinterpret_cast<const uint8_t *>(table);
            value.tasks[i].index = static_cast<uint32_t>(i);
            value.tasks[i].pending = pending;
            std::memcpy(&value.tasks[i].taskId, record + 0x08, 4);
            std::memcpy(&value.tasks[i].replaceable, record + 0x0C, 4);
            std::memcpy(&value.tasks[i].grade, record + 0x14, 4);
            std::memcpy(&value.tasks[i].target, record + 0x24, 4);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    if (value.completedCount > kTaskCount || value.changeChance > 100000 || value.rewardPending > 1)
        return false;
    for (size_t i = 0; i < value.taskCount; ++i)
    {
        const TaskState &task = value.tasks[i];
        if (!task.taskId || task.state > 3 || task.pending > 1 || task.replaceable > 1 ||
            task.grade > 5 || task.target > 1000000000)
            return false;
    }

    *out = value;
    return true;
}

bool SameState(const DailyState &a, const DailyState &b)
{
    return std::memcmp(&a, &b, sizeof(DailyState)) == 0;
}

bool ReadState(DailyState *out)
{
    auto singletonAddr = PatternResolver::Get("UIManagerGetSingleton");
    auto getContentAddr = PatternResolver::Get("UIManagerGetUIContent");
    if (!singletonAddr || !getContentAddr) return false;

    auto singleton = reinterpret_cast<UIManagerGetSingletonFn>(singletonAddr);
    auto getContent = reinterpret_cast<UIManagerGetUIContentFn>(getContentAddr);
    void *content = CallGetContentSEH(singleton, getContent);
    if (!content) return false;

    // Packet handlers update the inline vector in-place.  Two identical reads
    // prevent the broker from deciding from a half-applied task snapshot.
    DailyState first{};
    DailyState second{};
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        if (!ReadStateOnce(content, &first)) return false;
        Sleep(1);
        if (!ReadStateOnce(content, &second)) return false;
        if (SameState(first, second))
        {
            *out = second;
            return true;
        }
    }
    return false;
}

bool IsSafeHuntLayout(const DailyState &state)
{
    if (state.taskCount != kTaskCount) return false;
    for (size_t i = 0; i < kTaskCount; ++i)
    {
        const bool shouldBeHunt = i >= kHuntFirst;
        if ((state.tasks[i].replaceable != 0) != shouldBeHunt) return false;
    }
    return true;
}

const char *GradeName(uint32_t grade)
{
    static const char *names[] = {"SSS", "SS", "S", "A", "B", "C"};
    return grade < _countof(names) ? names[grade] : "unknown";
}

const char *StateName(uint32_t state)
{
    static const char *names[] = {"unaccepted", "accepted", "ready", "finished"};
    return state < _countof(names) ? names[state] : "unknown";
}

nlohmann::json TaskJson(const TaskState &task)
{
    return {
        {"index", task.index},
        {"taskId", task.taskId},
        {"state", task.state},
        {"stateName", StateName(task.state)},
        {"progress", task.progress},
        {"target", task.target},
        {"pending", task.pending != 0},
        {"gradeRaw", task.grade},
        {"gradeName", GradeName(task.grade)},
        {"replaceable", task.replaceable != 0},
        {"hunt", task.replaceable != 0},
    };
}

nlohmann::json StateJson(const DailyState &state)
{
    nlohmann::json tasks = nlohmann::json::array();
    for (size_t i = 0; i < state.taskCount; ++i) tasks.push_back(TaskJson(state.tasks[i]));
    return {
        {"taskCount", state.taskCount},          {"completedCount", state.completedCount},
        {"changeChance", state.changeChance},    {"rewardPending", state.rewardPending != 0},
        {"safeLayout", IsSafeHuntLayout(state)}, {"tasks", std::move(tasks)},
    };
}

RemoteControl::CmdResult Error(const char *code, const std::string &message,
                               const nlohmann::json &extra = nlohmann::json::object())
{
    nlohmann::json detail = extra;
    detail["code"] = code;
    detail["message"] = message;
    return {false, detail.dump()};
}

int ClampWaitMs(const nlohmann::json &args)
{
    int waitMs = args.value("waitMs", 2500);
    if (waitMs < 0) waitMs = 0;
    if (waitMs > 5000) waitMs = 5000;
    return waitMs;
}

int CallTaskActionSEH(DailyTaskActionFn fn, void *content, int index, int operation)
{
    __try
    {
        fn(content, index, operation);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

bool CompletionContains(const NetLog::DailyTaskCompletionReply &reply, uint32_t index)
{
    for (uint32_t i = 0; i < reply.count && i < _countof(reply.indices); ++i)
    {
        if (reply.indices[i] == index) return true;
    }
    return false;
}

bool StateConfirms(Action action, const DailyState &before, const DailyState &after, uint32_t index,
                   uint32_t replyValue)
{
    if (after.taskCount <= index) return false;
    const TaskState &task = after.tasks[index];
    switch (action)
    {
        case Action::Reroll:
            // Random selection can legitimately return the same task/grade.
            // The reply's newTaskId plus the consumed chance is authoritative.
            return before.changeChance > 0 && after.changeChance + 1 == before.changeChance &&
                   task.taskId == replyValue && task.state == 0 && task.progress == 0 &&
                   task.pending == 0;
        case Action::Accept:
            // A following delta may advance accepted directly to ready.
            // QuestAction op1 intentionally leaves entry+0x0C set, so that
            // byte is not a transient pending flag after acceptance.
            return task.taskId == before.tasks[index].taskId && task.state >= 1;
        case Action::Finish:
            return task.state == 3;
    }
    return false;
}

WaitOutcome WaitForOutcome(Action action, uint32_t index, const DailyState &before,
                           uint64_t previousReplySequence, uint64_t previousErrorSequence,
                           int waitMs)
{
    WaitOutcome result{};
    result.after = before;
    DWORD start = GetTickCount();
    do
    {
        NetLog::DailyTaskErrorReply error{};
        uint64_t errorSequence = NetLog::GetLastDailyTaskErrorReply(&error);
        if (errorSequence != 0 && errorSequence != previousErrorSequence)
        {
            result.serverError = true;
            result.errorSequence = errorSequence;
            result.errorValue0 = error.value0;
            result.errorValue1 = error.value1;
            ReadState(&result.after);
            return result;
        }

        if (action == Action::Finish)
        {
            NetLog::DailyTaskCompletionReply reply{};
            uint64_t sequence = NetLog::GetLastDailyTaskCompletionReply(&reply);
            if (sequence != 0 && sequence != previousReplySequence &&
                (reply.kind == 0 || reply.kind == 3) && CompletionContains(reply, index))
            {
                result.acknowledged = true;
                result.replySequence = sequence;
            }
        }
        else
        {
            NetLog::DailyTaskManageReply reply{};
            uint64_t sequence = NetLog::GetLastDailyTaskManageReply(&reply);
            const uint32_t expectedOperation = action == Action::Reroll ? 0 : 1;
            if (sequence != 0 && sequence != previousReplySequence &&
                reply.operation == expectedOperation && reply.index == index)
            {
                result.acknowledged = true;
                result.replySequence = sequence;
                result.replyValue = reply.value;
            }
        }

        DailyState live{};
        if (result.acknowledged && ReadState(&live))
        {
            result.after = live;
            if (StateConfirms(action, before, live, index, result.replyValue))
            {
                result.confirmed = true;
                return result;
            }
        }
        if (waitMs <= 0) break;
        Sleep(15);
    } while (static_cast<DWORD>(GetTickCount() - start) < static_cast<DWORD>(waitMs));

    ReadState(&result.after);
    return result;
}

RemoteControl::CmdResult Query()
{
    DailyState state{};
    if (!ReadState(&state))
        return Error("daily-task-state-unavailable",
                     "daily task content 96 is not ready or failed validation");
    return {true, StateJson(state).dump()};
}

RemoteControl::CmdResult RunAction(Action action, const nlohmann::json &args)
{
    int index = args.value("index", -1);
    if (index < static_cast<int>(kHuntFirst) || index >= static_cast<int>(kTaskCount))
        return Error("invalid-index", "daily hunt task index must be 3..7");

    // Avoid racing a human click. The native reroll action does not set the
    // entry pending flag, so two same-slot clicks could consume two chances.
    bool windowOpen = false;
    if (!IsDailyTaskWindowOpen(&windowOpen))
        return Error("daily-task-window-state-unavailable",
                     "could not determine whether the daily task window is open");
    if (windowOpen)
        return Error("daily-task-window-open",
                     "close the in-game daily task window before automatic actions");

    DailyState before{};
    if (!ReadState(&before))
        return Error("daily-task-state-unavailable",
                     "daily task content 96 is not ready or failed validation");
    if (!IsSafeHuntLayout(before))
        return Error("unsafe-task-layout",
                     "expected exactly three fixed tasks followed by five hunt tasks",
                     {{"snapshot", StateJson(before)}});

    const TaskState &task = before.tasks[index];
    if (!task.replaceable) return Error("not-hunt-task", "refusing to operate on a non-hunt task");
    // QuestAction op1 leaves entry+0x0C set after acceptance; ready tasks in
    // live state therefore legitimately have pending=1.  It only gates the
    // quest-action path (reroll/accept), not RewardAction finish.
    if (action != Action::Finish && task.pending)
        return Error("task-action-pending", "the selected task already has a pending action");

    const char *patternName = nullptr;
    int operation = 0;
    if (action == Action::Reroll)
    {
        if (task.state != 0)
            return Error("task-already-accepted", "only unaccepted hunt tasks can be rerolled");
        if (before.changeChance == 0)
            return Error("no-change-chance", "no daily task reroll chances remain");
        patternName = "DailyTaskQuestAction";
        operation = 0;
    }
    else if (action == Action::Accept)
    {
        if (task.state != 0)
            return Error("task-not-unaccepted", "only an unaccepted hunt task can be accepted");
        patternName = "DailyTaskQuestAction";
        operation = 1;
    }
    else
    {
        if (task.state != 2)
            return Error("task-not-ready", "only a ready hunt task can be finished");
        // RewardAction op0 does not gate on content+0xA0. The native function
        // sets that byte after sending, but ApplyCompletionReply does not clear
        // it while content 96 is inactive. Per-slot state 2 plus the matching
        // 511692/index confirmation is the authoritative duplicate guard.
        patternName = "DailyTaskRewardAction";
        operation = 0;
    }

    auto actionAddr = PatternResolver::Get(patternName);
    if (!actionAddr)
        return Error("daily-task-action-unresolved", "local daily task UI action is unresolved");
    if (!ValidateActionAddress(action, actionAddr))
        return Error("daily-task-action-signature-mismatch",
                     "resolved daily task action does not match the verified client signature");
    auto actionFn = reinterpret_cast<DailyTaskActionFn>(actionAddr);

    // Recheck immediately before the send to narrow the open/click race window.
    if (!IsDailyTaskWindowOpen(&windowOpen) || windowOpen)
        return Error("daily-task-window-open",
                     "daily task window became active during action preflight");

    const uint64_t previousReplySequence = action == Action::Finish
                                               ? NetLog::GetLastDailyTaskCompletionReply()
                                               : NetLog::GetLastDailyTaskManageReply();
    const uint64_t previousErrorSequence = NetLog::GetLastDailyTaskErrorReply();
    if (CallTaskActionSEH(actionFn, reinterpret_cast<void *>(before.content), index, operation) < 0)
        return Error("local-action-exception", "local daily task UI action raised an exception");

    WaitOutcome wait =
        WaitForOutcome(action, static_cast<uint32_t>(index), before, previousReplySequence,
                       previousErrorSequence, ClampWaitMs(args));
    nlohmann::json detail = {
        {"confirmed", wait.confirmed},
        {"acknowledged", wait.acknowledged},
        {"outcomeValid", wait.confirmed && !wait.serverError},
        {"serverError", wait.serverError},
        {"replySequence", wait.replySequence},
        {"replyValue", wait.replyValue},
        {"errorSequence", wait.errorSequence},
        {"errorValue0", wait.errorValue0},
        {"errorValue1", wait.errorValue1},
        {"before", StateJson(before)},
        {"after", StateJson(wait.after)},
    };
    spdlog::info("GGTB::DailyTask: action={} index={} ack={} confirmed={} serverError={}",
                 static_cast<int>(action), index, wait.acknowledged, wait.confirmed,
                 wait.serverError);
    return {true, detail.dump()};
}
}  // namespace

void RegisterHandlers()
{
    RemoteControl::RegisterCommandHandler("queryDailyTasks",
                                          [](const nlohmann::json &) -> RemoteControl::CmdResult
                                          {
                                              std::lock_guard<std::mutex> lock(s_commandMutex);
                                              return Query();
                                          });
    RemoteControl::RegisterCommandHandler("rerollDailyTask",
                                          [](const nlohmann::json &args) -> RemoteControl::CmdResult
                                          {
                                              std::lock_guard<std::mutex> lock(s_commandMutex);
                                              return RunAction(Action::Reroll, args);
                                          });
    RemoteControl::RegisterCommandHandler("acceptDailyTask",
                                          [](const nlohmann::json &args) -> RemoteControl::CmdResult
                                          {
                                              std::lock_guard<std::mutex> lock(s_commandMutex);
                                              return RunAction(Action::Accept, args);
                                          });
    RemoteControl::RegisterCommandHandler("finishDailyTask",
                                          [](const nlohmann::json &args) -> RemoteControl::CmdResult
                                          {
                                              std::lock_guard<std::mutex> lock(s_commandMutex);
                                              return RunAction(Action::Finish, args);
                                          });
}
}  // namespace GGTB::DailyTaskControl
