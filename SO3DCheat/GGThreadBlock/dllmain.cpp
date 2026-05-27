#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Detours/build/include/detours.h>
#include <spdlog/spdlog.h>
#include <intrin.h>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma intrinsic(_ReturnAddress)

#include "GGThreadBlock.h"
#include "Setting.h"
#include "util/PatternResolver.h"
#include "util/SpeedHack.h"
#include "util/Stage1Trigger.h"
#include "util/UserConfig.h"
#include "util/NetLog.h"
#include "util/HwFpSpoof.h"
#include "util/DisconnectWatchdog.h"
#include "util/RemoteControl.h"
#include "util/InputInjector.h"
#include "entity/CLocalPlayer.h"
#include "modules/StatusModule.h"
#include "modules/MoveSpeedModule.h"
#include "modules/AttackSpeedModule.h"
#include "modules/SkillSpeedModule.h"
#include "modules/SpeedHackModule.h"
#include "modules/ItemNoCDModule.h"
#include "modules/AttackRangeModule.h"
#include "modules/ActionMoveModule.h"
#include "modules/AutoPickupModule.h"
#include "modules/FireFullPowerModule.h"
#include "modules/MakeBombDropModule.h"
#include "modules/MultiCastModule.h"
#include "modules/AutoConfirmModule.h"  // 也用于 sendMoneyMail handler 静音 OK toast
#include "modules/AutoDelegationModule.h"
#include "modules/AutoMailModule.h"
#include "modules/NearbyPlayerGuardModule.h"
#include "modules/PlayerESPModule.h"
#include "modules/StationaryFarmModule.h"
#include "modules/BlockLevelUpModule.h"
#include "modules/MultiHitModule.h"
#include "modules/AutoFleeModule.h"

namespace GGTB
{
volatile LONG g_blockedThreadCount = 0;
} // namespace GGTB

// ============================================================
//  CreateThread detour — swallow GameGuardDll.dll worker threads
// ============================================================

using fnCreateThread = HANDLE(WINAPI *)(LPSECURITY_ATTRIBUTES, SIZE_T,
                                        LPTHREAD_START_ROUTINE, LPVOID,
                                        DWORD, LPDWORD);

static fnCreateThread g_oCreateThread = CreateThread;

static DWORD WINAPI DummyLoopThread(LPVOID)
{
    for (;;)
        Sleep(INFINITE);
    return 0;
}

static bool IsAddressInGameGuard(LPCVOID addr)
{
    if (!addr)
        return false;

    HMODULE hMod = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(addr), &hMod) ||
        !hMod)
        return false;

    char  path[MAX_PATH] = {};
    DWORD n = GetModuleFileNameA(hMod, path, MAX_PATH);
    if (n == 0)
        return false;

    const char *base = path;
    for (DWORD i = 0; i < n; ++i)
        if (path[i] == '\\' || path[i] == '/')
            base = path + i + 1;

    return _stricmp(base, "GameGuardDll.dll") == 0;
}

static HANDLE WINAPI HookCreateThread(LPSECURITY_ATTRIBUTES sec, SIZE_T stack,
                                      LPTHREAD_START_ROUTINE start, LPVOID param,
                                      DWORD flags, LPDWORD outTid)
{
    if (IsAddressInGameGuard(reinterpret_cast<LPCVOID>(start)))
    {
        InterlockedIncrement(&GGTB::g_blockedThreadCount);
        OutputDebugStringA("[GGThreadBlock] blocked GameGuard thread\n");
        return g_oCreateThread(sec, stack, DummyLoopThread, nullptr, flags, outTid);
    }
    return g_oCreateThread(sec, stack, start, param, flags, outTid);
}

// ============================================================
//  SetWindowsHookExA/W detour — fake-success the host exe's keyboard hook
//
//  Host exe installs a WH_KEYBOARD_LL (or legacy WH_KEYBOARD on old NT) hook
//  in WinMain that swallows VK_LWIN/VK_RWIN (and Ctrl+Esc on the legacy path)
//  for "fullscreen anti-mistake" — see RE notes on KeyboardHookProc_BlockWinKey
//  at unpackd_so3d.exe!0x978c50. We want Win key + task switching back, so we
//  return a sentinel HHOOK that pretends the install succeeded; UnhookWindowsHookEx
//  recognises the sentinel and reports success too.
//
//  Filter: only the host EXE's keyboard hooks are swallowed. Other DLLs (input
//  overlays, IME, accessibility tools, even GameGuard if it ever installed one)
//  pass through unmodified — _ReturnAddress() gives us the real caller.
// ============================================================

using fnSetWindowsHookExA   = HHOOK(WINAPI *)(int, HOOKPROC, HINSTANCE, DWORD);
using fnSetWindowsHookExW   = HHOOK(WINAPI *)(int, HOOKPROC, HINSTANCE, DWORD);
using fnUnhookWindowsHookEx = BOOL(WINAPI *)(HHOOK);

static fnSetWindowsHookExA   g_oSetWindowsHookExA   = SetWindowsHookExA;
static fnSetWindowsHookExW   g_oSetWindowsHookExW   = SetWindowsHookExW;
static fnUnhookWindowsHookEx g_oUnhookWindowsHookEx = UnhookWindowsHookEx;

// Non-NULL sentinel ('GGHB' little-endian). Distinct enough that
// UnhookWindowsHookEx can tell it apart from a real OS-allocated HHOOK.
static HHOOK const kFakeKeyboardHook = reinterpret_cast<HHOOK>(static_cast<uintptr_t>(0x42484747));

// ---- Captured 3rd-party HOOKPROCs --------------------------------------
//
// 自动复活脚本想直接「按 Alt+W」之类的快捷键 —— 不走 SendInput / keybd_event
// (那会被 host 自己 NPmsg / GameGuard 看到),而是直接调用 123.dll 已经注册
// 给 OS 的 keyboard HOOKPROC 。每次任何模块装 hook,我们把 (idHook, lpfn)
// 记下来,web 端通过 RemoteControl 命令拿 idHook + vkey + modifiers 触发。
//
// 对每个 idHook 只保留最后一次 install 的 lpfn —— 简单足够,host 重启 hook
// 的频率很低 (实际只 install 一次)。
struct CapturedHookInfo
{
    HOOKPROC      lpfn          = nullptr;
    HINSTANCE     hMod          = nullptr;
    DWORD         dwThreadId    = 0;
    uintptr_t     callerModBase = 0;        // for diagnostic 用,跟日志对得上
    char          callerPath[MAX_PATH] = {};
};
static std::mutex                                    g_capturedHooksMutex;
static std::unordered_map<int, CapturedHookInfo>     g_capturedHooks;

static void CaptureHookInstall(int idHook, HOOKPROC lpfn, HINSTANCE hMod,
                               DWORD dwThreadId, LPCVOID caller)
{
    CapturedHookInfo info;
    info.lpfn       = lpfn;
    info.hMod       = hMod;
    info.dwThreadId = dwThreadId;

    HMODULE callerMod = nullptr;
    if (caller &&
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(caller), &callerMod) &&
        callerMod)
    {
        info.callerModBase = reinterpret_cast<uintptr_t>(callerMod);
        GetModuleFileNameA(callerMod, info.callerPath, MAX_PATH);
    }

    std::lock_guard<std::mutex> lk(g_capturedHooksMutex);
    g_capturedHooks[idHook] = info;
}

// 由 RemoteControl handler 调用:从 captured map 里查出 lpfn,在当前线程
// 直接同步 invoke (HC_ACTION, vkey, lParam)。SEH 包一下,123.dll 那侧
// 如果挂了也不让 DLL 整个崩。
//
// SEH 不能和带 RAII 析构的对象(unordered_map iterator / lock_guard)放同一
// 个函数里,会触发 C2712。所以拆成纯 C++ 这一层 + 一个 SEH wrapper:
struct HookInvokeResult { bool ok; std::string detail; LRESULT rv; };

static LRESULT CallHookProcSEH(HOOKPROC fn, int code, WPARAM wParam,
                               LPARAM lParam, bool *outOk)
{
    LRESULT rv = 0;
    __try { rv = fn(code, wParam, lParam); *outOk = true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *outOk = false; }
    return rv;
}

static HookInvokeResult InvokeCapturedHook(int idHook, int code, WPARAM wParam,
                                           LPARAM lParam)
{
    HOOKPROC lpfn = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_capturedHooksMutex);
        auto it = g_capturedHooks.find(idHook);
        if (it == g_capturedHooks.end())
            return {false, "no captured hook for idHook", 0};
        lpfn = it->second.lpfn;
    }
    if (!lpfn)
        return {false, "captured lpfn is null", 0};

    bool   ok = false;
    LRESULT rv = CallHookProcSEH(lpfn, code, wParam, lParam, &ok);
    if (!ok)
        return {false, "SEH in HOOKPROC", 0};
    return {true, {}, rv};
}

// 暴露给 dllmain 末尾注册 handler 的入口(在另一个文件里也能拿得到 —— 但
// 当前用 in-file static 就够,因为 RegisterCommandHandler 也在这个文件里调)。
static bool ListCapturedHooks(std::vector<std::pair<int, CapturedHookInfo>> &out)
{
    std::lock_guard<std::mutex> lk(g_capturedHooksMutex);
    out.reserve(g_capturedHooks.size());
    for (auto &kv : g_capturedHooks)
        out.emplace_back(kv.first, kv.second);
    return true;
}

static bool IsAddressInHostExe(LPCVOID addr)
{
    if (!addr)
        return false;
    HMODULE hMod = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(addr), &hMod) ||
        !hMod)
        return false;
    return hMod == GetModuleHandleW(nullptr);
}

static bool ShouldFakeKeyboardHook(int idHook, LPCVOID caller)
{
    if (idHook != WH_KEYBOARD && idHook != WH_KEYBOARD_LL)
        return false;
    return IsAddressInHostExe(caller);
}

// 把 WH_xxx 数字转成可读名,日志里直接看出来是不是低级键盘 hook。
static const char *HookIdName(int idHook)
{
    switch (idHook)
    {
    case WH_MSGFILTER:       return "MSGFILTER";
    case WH_JOURNALRECORD:   return "JOURNALRECORD";
    case WH_JOURNALPLAYBACK: return "JOURNALPLAYBACK";
    case WH_KEYBOARD:        return "KEYBOARD";
    case WH_GETMESSAGE:      return "GETMESSAGE";
    case WH_CALLWNDPROC:     return "CALLWNDPROC";
    case WH_CBT:             return "CBT";
    case WH_SYSMSGFILTER:    return "SYSMSGFILTER";
    case WH_MOUSE:           return "MOUSE";
    case WH_DEBUG:           return "DEBUG";
    case WH_SHELL:           return "SHELL";
    case WH_FOREGROUNDIDLE:  return "FOREGROUNDIDLE";
    case WH_CALLWNDPROCRET:  return "CALLWNDPROCRET";
    case WH_KEYBOARD_LL:     return "KEYBOARD_LL";
    case WH_MOUSE_LL:        return "MOUSE_LL";
    default:                 return "?";
    }
}

// 记录 SetWindowsHookEx 的真实调用方所在模块 —— 这是用来定位
// 某个第三方 DLL 在装 WH_KEYBOARD_LL 监听 Alt+W 的关键日志。
// caller 由调用方在 trampoline 处 _ReturnAddress() 取出来传进来,
// 因为 SetWindowsHookEx 自己内部任何函数调用都会让 _ReturnAddress
// 失真。
static void LogSetWindowsHookExCall(const char *api, int idHook, HOOKPROC lpfn,
                                    HINSTANCE hMod, DWORD dwThreadId,
                                    LPCVOID caller, bool swallowed)
{
    char    callerPath[MAX_PATH] = "<unknown>";
    HMODULE callerMod            = nullptr;
    uintptr_t callerOff          = 0;
    if (caller &&
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(caller), &callerMod) &&
        callerMod)
    {
        GetModuleFileNameA(callerMod, callerPath, MAX_PATH);
        callerOff = reinterpret_cast<uintptr_t>(caller) -
                    reinterpret_cast<uintptr_t>(callerMod);
    }

    spdlog::info(
        "[GGTB] {} idHook={}({}) lpfn={} hMod={} tid={} caller={} +{:#x} {}",
        api, idHook, HookIdName(idHook),
        reinterpret_cast<void *>(lpfn),
        reinterpret_cast<void *>(hMod),
        dwThreadId,
        callerPath, callerOff,
        swallowed ? "[SWALLOWED]" : "[passthrough]");
}

static HHOOK WINAPI HookSetWindowsHookExA(int idHook, HOOKPROC lpfn,
                                          HINSTANCE hMod, DWORD dwThreadId)
{
    PVOID caller    = _ReturnAddress();
    bool  swallow   = ShouldFakeKeyboardHook(idHook, caller);
    LogSetWindowsHookExCall("SetWindowsHookExA", idHook, lpfn, hMod, dwThreadId,
                            caller, swallow);
    // 即使被 swallow,也别记录假 hook —— 那是我们自己塞回去的,不能 invoke。
    if (!swallow)
        CaptureHookInstall(idHook, lpfn, hMod, dwThreadId, caller);
    if (swallow)
    {
        OutputDebugStringA("[GGThreadBlock] swallowed host SetWindowsHookExA(KEYBOARD)\n");
        return kFakeKeyboardHook;
    }
    return g_oSetWindowsHookExA(idHook, lpfn, hMod, dwThreadId);
}

static HHOOK WINAPI HookSetWindowsHookExW(int idHook, HOOKPROC lpfn,
                                          HINSTANCE hMod, DWORD dwThreadId)
{
    PVOID caller    = _ReturnAddress();
    bool  swallow   = ShouldFakeKeyboardHook(idHook, caller);
    LogSetWindowsHookExCall("SetWindowsHookExW", idHook, lpfn, hMod, dwThreadId,
                            caller, swallow);
    if (!swallow)
        CaptureHookInstall(idHook, lpfn, hMod, dwThreadId, caller);
    if (swallow)
    {
        OutputDebugStringA("[GGThreadBlock] swallowed host SetWindowsHookExW(KEYBOARD)\n");
        return kFakeKeyboardHook;
    }
    return g_oSetWindowsHookExW(idHook, lpfn, hMod, dwThreadId);
}

static BOOL WINAPI HookUnhookWindowsHookEx(HHOOK hhk)
{
    if (hhk == kFakeKeyboardHook)
        return TRUE;
    return g_oUnhookWindowsHookEx(hhk);
}

// ============================================================
//  NPmsg.dll patcher — turn `jng` at NPmsg.dll+0x103D3 into `jmp`
//
//  Original (6 bytes): 0F 8E 0A 00 00 00   jng  NPmsg.dll+0x103E3
//  Patched  (6 bytes): E9 0B 00 00 00 90   jmp  NPmsg.dll+0x103E3 ; nop
//
//  NPmsg.dll loads after us, so a worker thread polls until the module is
//  present AND the expected bytes match, then patches once and exits.
//  Bytes are verified before patching so a version mismatch falls through
//  silently rather than corrupting unrelated code.
// ============================================================

namespace
{
constexpr DWORD kNPmsgPatchOffset      = 0x103D3;
constexpr BYTE  kNPmsgOriginalBytes[6] = { 0x0F, 0x8E, 0x0A, 0x00, 0x00, 0x00 };
constexpr BYTE  kNPmsgPatchedBytes[6]  = { 0xE9, 0x0B, 0x00, 0x00, 0x00, 0x90 };
} // namespace

static bool TryPatchNPmsgJng()
{
    HMODULE hMod = GetModuleHandleA("NPmsg.dll");
    if (!hMod)
        return false;

    BYTE *target = reinterpret_cast<BYTE *>(hMod) + kNPmsgPatchOffset;

    if (memcmp(target, kNPmsgPatchedBytes, sizeof(kNPmsgPatchedBytes)) == 0)
        return true; // already patched (e.g. previous run)

    if (memcmp(target, kNPmsgOriginalBytes, sizeof(kNPmsgOriginalBytes)) != 0)
        return false; // module loaded but section/bytes not yet what we expect

    DWORD oldProt = 0;
    if (!VirtualProtect(target, sizeof(kNPmsgPatchedBytes),
                        PAGE_EXECUTE_READWRITE, &oldProt))
    {
        OutputDebugStringA("[GGThreadBlock] NPmsg patch: VirtualProtect failed\n");
        return false;
    }
    memcpy(target, kNPmsgPatchedBytes, sizeof(kNPmsgPatchedBytes));
    DWORD tmp = 0;
    VirtualProtect(target, sizeof(kNPmsgPatchedBytes), oldProt, &tmp);
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(kNPmsgPatchedBytes));

    OutputDebugStringA("[GGThreadBlock] NPmsg.dll+0x103D3 jng -> jmp patched\n");
    return true;
}

static DWORD WINAPI NPmsgPatcherThread(LPVOID)
{
    for (;;)
    {
        if (TryPatchNPmsgJng())
            return 0;
        Sleep(100);
    }
}

static void InstallDetour()
{
    DetourRestoreAfterWith();
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(reinterpret_cast<PVOID *>(&g_oCreateThread),         HookCreateThread);
    DetourAttach(reinterpret_cast<PVOID *>(&g_oSetWindowsHookExA),    HookSetWindowsHookExA);
    DetourAttach(reinterpret_cast<PVOID *>(&g_oSetWindowsHookExW),    HookSetWindowsHookExW);
    DetourAttach(reinterpret_cast<PVOID *>(&g_oUnhookWindowsHookEx),  HookUnhookWindowsHookEx);
    DetourTransactionCommit();
}

static void UninstallDetour()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(reinterpret_cast<PVOID *>(&g_oCreateThread),         HookCreateThread);
    DetourDetach(reinterpret_cast<PVOID *>(&g_oSetWindowsHookExA),    HookSetWindowsHookExA);
    DetourDetach(reinterpret_cast<PVOID *>(&g_oSetWindowsHookExW),    HookSetWindowsHookExW);
    DetourDetach(reinterpret_cast<PVOID *>(&g_oUnhookWindowsHookEx),  HookUnhookWindowsHookEx);
    DetourTransactionCommit();
}

// ============================================================
//  Hack thread
// ============================================================

// True when the currently-focused window belongs to our own process. Hotkey
// handlers call this so a press while the user is typing in another app
// (browser, editor) is ignored. We still drain GetAsyncKeyState's `& 1` bit
// regardless of focus, so the same press doesn't fire later when focus returns.
static bool HostWindowHasFocus()
{
    HWND fg = GetForegroundWindow();
    if (!fg)
        return false;
    DWORD fgPid = 0;
    GetWindowThreadProcessId(fg, &fgPid);
    return fgPid == GetCurrentProcessId();
}

static DWORD WINAPI HackThread(LPVOID lpParam)
{
    auto hModule = static_cast<HMODULE>(lpParam);

    // Bootstrap logger to <dll-dir>/GGConfig/_bootstrap/bootstrap.log first —
    // PatternResolver::Init logs to the default sink, so it must exist by then.
    GGTB::UserConfig::Bootstrap(hModule);
    spdlog::info("GGThreadBlock DLL Attached");

    GGTB::PatternResolver::Init(hModule);

    // Arm the shared DeleteFileA("error.txt") shim BEFORE any stage-2 module
    // subscribes. Every module that needs to attach after the Winlicense
    // unpacker finishes (NetLog, HwFpSpoof, ...) registers a callback here;
    // the first error.txt delete in WinMain fires all of them in order.
    GGTB::Stage1Trigger::Install();

    // Net plaintext logger — depends on PatternResolver. Log files are opened
    // lazily the first time UserConfig::IsReady() flips true (after the user
    // selects a character), so pre-login auth traffic stays out of the per-
    // character log.
    GGTB::NetLog::Install();

    // Hardware-fingerprint spoof — also stage-2 (shares the Stage1Trigger
    // shim with NetLog) because sub_BCCBF0 lives inside Winlicense-packed
    // .text that isn't resolved until WinMain runs. Seeded from <dll-dir>/
    // GGConfig/_bootstrap/hwfp_seed.bin so the fake identity is stable across
    // launches.
    GGTB::HwFpSpoof::Install();

    // Silent-disconnect watchdog — arms on first game-port recv (1842/1843),
    // then force-closes the process if both ports go quiet for 5 minutes.
    // Depends on NetLog's recv hook being live to feed OnRecv().
    GGTB::DisconnectWatchdog::Install();

    // Web 远控通道:HackThread 起 IO 线程主动连 \\.\pipe\GGTB_BROKER。Install 之
    // 前先把 sendMoneyMail handler 注册好,避免 broker 已经把 connect 后立刻派
    // 命令的窗口期跑空。Handler 要在 IO 线程上同步执行 — 引擎的 packet 系列在
    // 任意 worker 线程都能调,这点 AutoMail 已经验过了。
    GGTB::RemoteControl::RegisterCommandHandler(
        "sendMoneyMail",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            std::string recipient = args.value("recipient", std::string{});
            int64_t     amount    = args.value("amount", int64_t{0});
            std::string body      = args.value("body", std::string{});
            if (recipient.empty() || recipient.size() > 15)
                return {false, "bad recipient (1..15 ASCII)"};
            // body 在 256-byte payload 的 [32..] 区间。留余量给 NUL+对齐,200 上限
            // 比 224 hard cap 保守得多。
            if (body.size() > 200)
                return {false, "body too long (>200 bytes)"};
            if (amount <= 0)
                return {false, "amount must be > 0"};
            int64_t have = GGTB::GetLocalMoney();
            if (amount > have)
                return {false, "insufficient funds"};

            // SendMoneyMail 成功后服务端会回 proto 0x7F55B 触发"讯息已寄送"
            // OK toast,不静音掉的话屏幕上会堆一片不会自己消失的弹窗。原因
            // 同 AutoMailModule.h 里的注释。8 秒覆盖正常 RTT + UI 渲染。
            GGTB::AutoConfirmDetail::EnsureHooksInstalled();
            GGTB::AutoConfirmDetail::SilenceOkToastFor(8000);

            bool ok = GGTB::SendMoneyMail(recipient.c_str(), amount, body.c_str());
            return {ok, ok ? std::string{} : std::string{"engine returned false"}};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "getBagItems",
        [](const nlohmann::json &) -> GGTB::RemoteControl::CmdResult {
            auto items = GGTB::GetBagItems();
            nlohmann::json arr = nlohmann::json::array();
            for (auto &it : items)
            {
                arr.push_back({
                    {"bagId",     it.bagId},
                    {"slotIndex", it.slotIndex},
                    {"itemId",    it.itemId},
                    {"count",     it.count},
                    {"name",      it.name},
                });
            }
            return {true, arr.dump()};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "sendItemMail",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            std::string recipient = args.value("recipient", std::string{});
            uint32_t    bagId     = args.value("bagId", uint32_t{0});
            uint32_t    count     = args.value("count", uint32_t{0});
            std::string body      = args.value("body", std::string{});
            if (recipient.empty() || recipient.size() > 15)
                return {false, "bad recipient (1..15 ASCII)"};
            if (body.size() > 200)
                return {false, "body too long (>200 bytes)"};
            if (bagId == 0)
                return {false, "bagId must be > 0"};
            if (count == 0)
                return {false, "count must be > 0"};

            GGTB::AutoConfirmDetail::EnsureHooksInstalled();
            GGTB::AutoConfirmDetail::SilenceOkToastFor(8000);

            bool ok = GGTB::SendItemMail(recipient.c_str(), bagId, count, body.c_str());
            return {ok, ok ? std::string{} : std::string{"engine returned false"}};
        });

    // 丢弃单个背包格子 —— web 远控批量丢弃用。broker 在前端按 dropIntervalMs 一格一格
    // 串行下发,所以这里只处理"一格"语义,不接 vector;让上层节奏可见、可中止。
    // bagId 走 BagItemInfo::bagId,已经是 wire 格式(slotIndex+13),不要再加 13。
    GGTB::RemoteControl::RegisterCommandHandler(
        "sendPublicChat",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            std::string message = args.value("message", std::string{});
            if (message.empty())
                return {false, "message must not be empty"};
            if (message.size() > 200)
                return {false, "message too long (>200 bytes)"};
            bool ok = GGTB::SendPublicChat(message.c_str());
            return {ok, ok ? std::string{} : std::string{"engine returned false"}};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "dropBagItem",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            uint32_t bagId = args.value("bagId", uint32_t{0});
            uint32_t count = args.value("count", uint32_t{0});
            if (bagId == 0)
                return {false, "bagId must be > 0"};
            if (count == 0)
                return {false, "count must be > 0"};
            bool ok = GGTB::DropBagItem(bagId, count);
            return {ok, ok ? std::string{} : std::string{"engine returned false"}};
        });

    // 输入注入:hook GetAsyncKeyState,把 web 发来的 VK 在 holdMs 内伪装成"按下",
    // 游戏 Input_PollKeyboard (sub_B1E790) 每帧轮询会自然 diff 出 press 边沿。
    // Install 之前先注册 handler,跟 sendMoneyMail 同样防 broker 命令窗口期跑空。
    // args 形态:
    //   {"vks": [18, 87], "holdMs": 80}    — Alt+W,按 80ms (默认值,可省)
    //   {"vks": ["alt", "w"]}              — 用名字写也行,InputInjector 不解析名字所
    //                                        以这层在这里展平成 VK 数字
    GGTB::RemoteControl::RegisterCommandHandler(
        "sendInput",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            // 名字 -> VK 映射只覆盖最常见的修饰键和方向键;一般功能键直接传数字即可。
            // 大小写无关比对,失败 fall through 到"无法识别"。
            auto nameToVk = [](std::string s) -> int {
                for (auto &c : s) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
                if (s == "alt" || s == "menu")  return VK_MENU;     // 0x12 (18)
                if (s == "ctrl" || s == "control") return VK_CONTROL; // 0x11 (17)
                if (s == "shift")               return VK_SHIFT;    // 0x10 (16)
                if (s == "tab")                 return VK_TAB;      // 0x09 (9)
                if (s == "enter" || s == "return") return VK_RETURN; // 0x0D (13)
                if (s == "esc" || s == "escape") return VK_ESCAPE;   // 0x1B (27)
                if (s == "space")               return VK_SPACE;    // 0x20 (32)
                if (s == "up")                  return VK_UP;       // 0x26 (38)
                if (s == "down")                return VK_DOWN;     // 0x28 (40)
                if (s == "left")                return VK_LEFT;     // 0x25 (37)
                if (s == "right")               return VK_RIGHT;    // 0x27 (39)
                if (s.size() == 1)
                {
                    char c = s[0];
                    if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
                    if (c >= '0' && c <= '9') return c;
                }
                if (s.size() >= 2 && s[0] == 'f') // f1..f24
                {
                    int n = std::atoi(s.c_str() + 1);
                    if (n >= 1 && n <= 24) return VK_F1 + n - 1;
                }
                return 0;
            };

            std::vector<int> vks;
            auto it = args.find("vks");
            if (it == args.end() || !it->is_array() || it->empty())
                return {false, "missing or empty vks[]"};
            for (auto &e : *it)
            {
                if (e.is_number_integer())
                {
                    vks.push_back(e.get<int>());
                }
                else if (e.is_string())
                {
                    int vk = nameToVk(e.get<std::string>());
                    if (vk == 0)
                        return {false, "unknown key name: " + e.get<std::string>()};
                    vks.push_back(vk);
                }
                else
                {
                    return {false, "vks[] must be int or string"};
                }
            }
            int    holdMs   = args.value("holdMs", 80);
            size_t accepted = GGTB::InputInjector::SendVks(vks, holdMs);
            if (accepted == 0)
                return {false, "no vk accepted (all blacklisted/out-of-range or injector not installed)"};
            return {true, std::string{"accepted "} + std::to_string(accepted) + "/" +
                              std::to_string(vks.size())};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "getCashBagItems",
        [](const nlohmann::json &) -> GGTB::RemoteControl::CmdResult {
            auto items = GGTB::GetCashBagItems();
            nlohmann::json arr = nlohmann::json::array();
            for (auto &it : items)
            {
                arr.push_back({
                    {"bagId",     it.bagId},
                    {"slotIndex", it.slotIndex},
                    {"itemId",    it.itemId},
                    {"count",     it.count},
                    {"name",      it.name},
                });
            }
            return {true, arr.dump()};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "useCashItem",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            uint32_t slot = args.value("slotIndex", UINT32_MAX);
            if (slot == UINT32_MAX)
                return {false, "missing slotIndex"};
            bool ok = GGTB::UseCashItem(slot);
            return {ok, ok ? "sent" : "failed"};
        });

    // moveTo: 让角色走到世界坐标 (x, y)。走的是引擎自己的 CLocalUser::SetAfterAction
    // 路径（跟点地走 UI 一模一样），所以一切寻路、避障、地形高度都是引擎自己算的,
    // 这层不掺和。action=1 是「纯走路」,action=3 是「走过去再打目标」, 配合 targetId
    // 使用。MoveTo 内部 SEH 包过,失败返回 {ok:false}.
    //
    // args:
    //   {"x": 123.4, "y": 567.8}                 — 走到 (x, y)
    //   {"x": ..., "y": ..., "action": 3, "targetId": 42}  — 走过去再打 id=42
    //
    // 注意:SetAfterAction 内部有 m_bCanMove / stunTime / 动画锁 等 reject 闸,
    // 在战斗/施法中可能拒绝。打开 ActionMoveModule 后 0x7539FE 处的 5 字节补丁
    // 会把所有 reject 闸跳过去 —— 那时候任何状态都能走。是否打补丁由用户在
    // 「攻击/技能时移动」开关里决定,这里不强制。
    GGTB::RemoteControl::RegisterCommandHandler(
        "moveTo",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            if (!args.contains("x") || !args.contains("y"))
                return {false, "missing x/y"};
            float    x        = args.value("x", 0.0f);
            float    y        = args.value("y", 0.0f);
            int      action   = args.value("action", 1);
            uint32_t targetId = args.value("targetId", uint32_t{0});
            if (action != 1 && action != 3)
                return {false, "action must be 1 (walk) or 3 (walk+attack)"};
            bool ok = GGTB::MoveTo(x, y, action, targetId);
            return {ok, ok ? std::string{} : std::string{"engine rejected (try enable ActionMove)"}};
        });

    // 配套的位置读取 —— 前端「读取当前位置」按钮用。也方便测试 MoveTo 是否成功
    // (调完之后位置应该开始朝目标变化)。
    GGTB::RemoteControl::RegisterCommandHandler(
        "getLocalPosition",
        [](const nlohmann::json &) -> GGTB::RemoteControl::CmdResult {
            float x = 0, y = 0, z = 0;
            if (!GGTB::GetLocalPosition(x, y, z))
                return {false, "g_pLocalUser not resolved"};
            nlohmann::json j;
            j["x"] = x;
            j["y"] = y;
            j["z"] = z;
            return {true, j.dump()};
        });

    // talkOrAttack: 「点 NPC / 怪物」统一入口 —— DLL 内部读 monsterTblId 调
    // 引擎自己的 Npc__LoadDialogScript 判断:
    //   有对话脚本(NPC) -> 走过去 (action=1) 然后开 dialog UI (本地调用,不发包)
    //   没对话脚本(怪) -> 复刻 OnTargetCreatureClick_TalkOrAttack:
    //                     g_TargetCreatureId=id, intent=3, SetAfterAction(action=3) 走过去开打
    // 跟手动鼠标点 creature 一样的行为,前端单按钮就行。
    GGTB::RemoteControl::RegisterCommandHandler(
        "talkOrAttack",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            uint32_t creatureId = args.value("creatureId", uint32_t{0});
            if (creatureId == 0)
                return {false, "missing creatureId"};
            bool ok = GGTB::TalkOrAttack(creatureId);
            return {ok, ok ? std::string{} : std::string{"engine rejected or creature not found"}};
        });

    // 兼容老 talkToNpc handler -> 内部转发到 talkOrAttack。
    GGTB::RemoteControl::RegisterCommandHandler(
        "talkToNpc",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            uint32_t npcId = args.value("npcId", uint32_t{0});
            if (npcId == 0)
                return {false, "missing npcId"};
            bool ok = GGTB::TalkOrAttack(npcId);
            return {ok, ok ? std::string{} : std::string{"engine rejected or creature not found"}};
        });

    // 列出附近所有 NPC + 怪物。每条带 hasDialog 字段(由引擎 Npc__LoadDialogScript
    // 判定),前端用这个分类显示而不是 monsterTblId 区段猜测。
    GGTB::RemoteControl::RegisterCommandHandler(
        "getNearbyNpcs",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            float maxDist = args.value("maxDistance", 100.0f);
            auto npcs = GGTB::GetNearbyNpcs(maxDist);
            nlohmann::json arr = nlohmann::json::array();
            for (auto &n : npcs)
            {
                arr.push_back({
                    {"id",           n.creatureId},
                    {"kind",         n.kind},
                    {"monsterTblId", n.monsterTblId},
                    {"hasDialog",    n.hasDialog},
                    {"level",        n.level},
                    {"distance",     n.distance},
                    {"x",            n.x},
                    {"y",            n.y},
                    {"z",            n.z},
                    {"hp",           n.hp},
                    {"name",         n.name},
                    {"isNpc",        n.hasDialog},  // 兼容老前端字段 — 现在就是 hasDialog 别名
                });
            }
            return {true, arr.dump()};
        });

    // getDialog: 读取当前 NPC 对话状态(题目文本 + 选项数组)。前端在跟 NPC 对
    // 话时轮询这个,根据 open/mode 字段刷新 UI。返回:
    //   { "open": false }                    — 没有对话
    //   { "open": true, "mode": 1, "npcInteractId": N,
    //     "body": "你确定要 xxx 吗?",
    //     "options": [ {"index":0, "text":"[确定]", "tag":...} ] }
    //   { "open": true, "mode": 2, "npcInteractId": N,
    //     "options": [
    //       {"index":0, "text":"购买物品", "tag":...},
    //       {"index":1, "text":"出售物品", "tag":...},
    //       ... ] }
    // 注意 mode==2 时 body 通常是空 —— 此 build 把提示语融在选项里(像 GUI
    // 一样直接展示一个选项菜单)。前端展示就用 options。
    GGTB::RemoteControl::RegisterCommandHandler(
        "getDialog",
        [](const nlohmann::json &) -> GGTB::RemoteControl::CmdResult {
            auto snap = GGTB::GetDialogSnapshot();
            nlohmann::json j;
            j["open"] = snap.open;
            if (!snap.open)
                return {true, j.dump()};
            j["mode"]          = static_cast<int>(snap.mode);
            j["npcInteractId"] = snap.npcInteractId;
            j["monsterTblId"]  = snap.monsterTblId;
            j["body"]          = snap.body;
            nlohmann::json opts = nlohmann::json::array();
            for (auto &o : snap.options)
            {
                opts.push_back({
                    {"index", o.index},
                    {"text",  o.text},
                    {"tag",   o.tag},
                    {"opt",   o.opt},  // *(*(option+396)+12)+332 = 真正发给服务器的 opt 值
                });
            }
            j["options"] = opts;
            return {true, j.dump()};
        });

    // selectDialogOption: 点选项发包 411026。option 是 GetDialog 返回的
    // options[i].index(就是 0-based)。NPC id 自动从 g_NpcDialogState 取,
    // 前端不用管。服务器收到后会推下一个菜单回来,前端再 getDialog 刷新即可。
    GGTB::RemoteControl::RegisterCommandHandler(
        "selectDialogOption",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            uint32_t option = args.value("option", uint32_t{0});
            bool ok = GGTB::SelectDialogOption(option);
            return {ok, ok ? std::string{} : std::string{"no dialog open or send failed"}};
        });

    // confirmDialog: mode==1(单按钮确认)的快捷键,等价于 selectDialogOption(0)。
    GGTB::RemoteControl::RegisterCommandHandler(
        "confirmDialog",
        [](const nlohmann::json &) -> GGTB::RemoteControl::CmdResult {
            bool ok = GGTB::ConfirmDialog();
            return {ok, ok ? std::string{} : std::string{"not in confirm mode"}};
        });

    // --- AutoRevive: 死亡 → 寻路 → 选项 ---
    //
    // 状态查询(web 用来高亮按钮 + 显示当前地图):
    //   getStatus -> { hp, isDead, mapId, userId }
    // 操作:
    //   reviveToTown -> 模拟「死亡弹框点 OK」,发 411170 让 server 把你 warp 回主城。
    // 其它步骤(MoveTo / TalkOrAttack / SelectDialogOption)已经存在 — web 端
    // 直接组合调用这些命令 + setTimeout 实现「复活脚本」。

    GGTB::RemoteControl::RegisterCommandHandler(
        "getStatus",
        [](const nlohmann::json &) -> GGTB::RemoteControl::CmdResult {
            nlohmann::json j;
            j["hp"]     = GGTB::GetLocalHp();
            j["isDead"] = GGTB::IsLocalDead();
            j["mapId"]  = GGTB::GetCurrentMapId();
            j["userId"] = GGTB::GetLocalUserId();
            return {true, j.dump()};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "reviveToTown",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            bool safety = args.value("safetyCheck", true);
            int  mode   = args.value("mode", 1);
            bool ok = GGTB::ReviveToTown(safety, mode);
            return {ok, ok ? std::string{} : std::string{"not dead or send failed"}};
        });

    // sendDialogSelectRaw: 直接发 CG_NPC_DIALOG_SELECT (411026) 的 14 字节 wire,
    // 不读 g_NpcDialogState、不做 UI 状态同步 —— 给「自动委托/复活脚本」用的:
    // 你已经知道某个 NPC + 某个 dialogOption 对应「跳挂机图」(实测捕获的包:
    // proto=411026 opt=10245 npc=19811 sub=1),那就直接发,服务器自己处理跳图。
    // (跟「寻路移动」里的 selectDialogOption 不同 — 那个走的是引擎本地点击 path,
    //  需要先打开 dialog 状态机才能用。)
    GGTB::RemoteControl::RegisterCommandHandler(
        "sendDialogSelectRaw",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            uint32_t npcId = args.value("npcId",  uint32_t{0});
            uint32_t opt   = args.value("option", uint32_t{0});
            if (npcId == 0)
                return {false, "npcId required (>0)"};
            bool ok = GGTB::SendDialogSelect(npcId, opt);
            return {ok, ok ? std::string{} : std::string{"pattern unresolved or SEH"}};
        });

    // listCapturedHooks: 返回所有被 SetWindowsHookExA/W hook 截获记录的第三方 hook,
    // 让 web 侧能看到哪些 idHook 可用、HOOKPROC 指针多少、是哪个 DLL 在装。
    GGTB::RemoteControl::RegisterCommandHandler(
        "listCapturedHooks",
        [](const nlohmann::json &) -> GGTB::RemoteControl::CmdResult {
            std::vector<std::pair<int, CapturedHookInfo>> rows;
            ListCapturedHooks(rows);
            nlohmann::json arr = nlohmann::json::array();
            for (auto &kv : rows)
            {
                nlohmann::json j;
                j["idHook"]     = kv.first;
                j["idHookName"] = HookIdName(kv.first);
                char ptrBuf[24];
                std::snprintf(ptrBuf, sizeof(ptrBuf), "0x%p", kv.second.lpfn);
                j["lpfn"]       = ptrBuf;
                std::snprintf(ptrBuf, sizeof(ptrBuf), "0x%p", kv.second.hMod);
                j["hMod"]       = ptrBuf;
                j["tid"]        = kv.second.dwThreadId;
                j["caller"]     = kv.second.callerPath;
                arr.push_back(j);
            }
            return {true, arr.dump()};
        });

    // invokeHookProc: 直接同步调用截获的某个 idHook 对应的 HOOKPROC。
    // 参数: { idHook, code=HC_ACTION(0), wParam, lParam } 全 int。
    // 返回: detail = JSON {rv: <LRESULT>}。
    //
    // WH_KEYBOARD (id=2) 单按键格式:
    //   code = HC_ACTION (0)
    //   wParam = vkey (VK_MENU=0x12 / 'W'=0x57 ...)
    //   lParam = (scancode << 16) | (extended<<24) | (context<<29) | (prevDown<<30) | (up<<31)
    //              repeatCount low16 一般 1。
    // 给一个 alt+w 示例: 先 push (VK_MENU,0x00380001), 再 (0x57, 0x20110001 alt-context),
    // 然后 (0x57, 0xC0110001 up), 最后 (VK_MENU, 0xC0380001 up)。
    GGTB::RemoteControl::RegisterCommandHandler(
        "invokeHookProc",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            if (!args.contains("idHook"))
                return {false, "idHook required"};
            int      idHook = args.value("idHook", 0);
            int      code   = args.value("code",   0);      // HC_ACTION
            // wParam/lParam 走 int64 收,避免 JS 端 32-bit 截断符号位。
            uint64_t wParam = args.value("wParam", uint64_t{0});
            uint64_t lParam = args.value("lParam", uint64_t{0});
            auto r = InvokeCapturedHook(idHook, code,
                                        static_cast<WPARAM>(wParam),
                                        static_cast<LPARAM>(lParam));
            if (!r.ok)
                return {false, r.detail};
            nlohmann::json j;
            j["rv"] = static_cast<int64_t>(r.rv);
            return {true, j.dump()};
        });

    // pressHookedKey: 高层封装,直接「按一下 vkey」(可加 alt/ctrl/shift)。
    // 给自动复活脚本调 Alt+W 用的便利接口 —— 不用 web 端自己拼 lParam。
    // 默认走 captured 的 WH_KEYBOARD (id=2),也可显式传 idHook。
    // 参数: { vkey, alt=false, ctrl=false, shift=false, idHook=2, gapMs=20 }
    GGTB::RemoteControl::RegisterCommandHandler(
        "pressHookedKey",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            int  idHook  = args.value("idHook", static_cast<int>(WH_KEYBOARD));
            int  vkey    = args.value("vkey",   0);
            bool withAlt = args.value("alt",    false);
            bool withCtl = args.value("ctrl",   false);
            bool withSft = args.value("shift",  false);
            int  gapMs   = args.value("gapMs",  20);
            if (vkey <= 0 || vkey > 0xFF)
                return {false, "vkey required (1..255)"};

            auto press = [&](int vk, bool down, bool altCtx) -> bool {
                UINT  scan   = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
                if (scan == 0) scan = 0x1E; // 兜底
                LPARAM lp = (LPARAM)((scan & 0xFFu) << 16) | 1u;
                if (vk == VK_RMENU || vk == VK_RCONTROL || vk == VK_INSERT ||
                    vk == VK_DELETE || vk == VK_HOME    || vk == VK_END ||
                    vk == VK_PRIOR  || vk == VK_NEXT    ||
                    vk == VK_LEFT || vk == VK_RIGHT ||
                    vk == VK_UP   || vk == VK_DOWN)
                    lp |= (1u << 24); // extended
                if (altCtx) lp |= (1u << 29);
                if (!down)  lp |= (3u << 30); // bit30=prev down, bit31=up
                auto r = InvokeCapturedHook(idHook, HC_ACTION, (WPARAM)vk, lp);
                return r.ok;
            };

            // 序列: Alt down → Ctrl down → Shift down → vkey down → vkey up
            //       → Shift up → Ctrl up → Alt up,每两步之间 gapMs。
            //       altCtx 标志只在 alt 持有期间为 true。
            auto sleep = [&]{ if (gapMs > 0) Sleep((DWORD)gapMs); };
            bool ok = true;
            if (withAlt) { ok &= press(VK_MENU,    true,  false); sleep(); }
            if (withCtl) { ok &= press(VK_CONTROL, true,  withAlt); sleep(); }
            if (withSft) { ok &= press(VK_SHIFT,   true,  withAlt); sleep(); }
            ok &= press(vkey, true,  withAlt); sleep();
            ok &= press(vkey, false, withAlt); sleep();
            if (withSft) { ok &= press(VK_SHIFT,   false, withAlt); sleep(); }
            if (withCtl) { ok &= press(VK_CONTROL, false, withAlt); sleep(); }
            if (withAlt) { ok &= press(VK_MENU,    false, false); }
            if (!ok)
                return {false, "no captured hook or HOOKPROC SEH"};
            return {true, {}};
        });

    // Install 必须 RegisterCommandHandler 之后,否则 broker 一接上立刻发 sendInput
    // 会落空。InputInjector::Install 跟 RemoteControl::Install 顺序无强依赖 — 注入
    // 器只 hook user32 API,不依赖 broker / pipe / 引擎符号。
    GGTB::InputInjector::Install();
    GGTB::RemoteControl::Install();

    auto setting = std::make_shared<GGTB::Setting>();
    setting->RegisterModule(std::make_shared<GGTB::StatusModule>());
    setting->RegisterModule(std::make_shared<GGTB::MoveSpeedModule>());
    setting->RegisterModule(std::make_shared<GGTB::AttackSpeedModule>());
    setting->RegisterModule(std::make_shared<GGTB::SkillSpeedModule>());
    setting->RegisterModule(std::make_shared<GGTB::SpeedHackModule>());
    setting->RegisterModule(std::make_shared<GGTB::ItemNoCDModule>());
    setting->RegisterModule(std::make_shared<GGTB::AttackRangeModule>());
    setting->RegisterModule(std::make_shared<GGTB::ActionMoveModule>());
    setting->RegisterModule(std::make_shared<GGTB::AutoPickupModule>());
    auto firePowerMod = std::make_shared<GGTB::FireFullPowerModule>();
    setting->RegisterModule(firePowerMod);
    auto multiCastMod = std::make_shared<GGTB::MultiCastModule>();
    setting->RegisterModule(multiCastMod);
    // 火力全开 ↔ 多重施法 互斥配对:谁开了对方就被关。两个 setter 都得在
    // RegisterModule 之后、Init/LoadState 之前调,这样后续 LoadState 触发的
    // OnResume 互斥检查能找到对方实例。
    GGTB::FireFullPowerModule::SetPeer(multiCastMod.get());
    GGTB::MultiCastModule    ::SetPeer(firePowerMod.get());
    setting->RegisterModule(std::make_shared<GGTB::MakeBombDropModule>());
    setting->RegisterModule(std::make_shared<GGTB::AutoConfirmModule>());
    setting->RegisterModule(std::make_shared<GGTB::AutoDelegationModule>());
    // 自动邮寄: CanAutoPause=false,跟 NPG 互不干扰 — 详见模块头注释。
    setting->RegisterModule(std::make_shared<GGTB::AutoMailModule>());
    setting->RegisterModule(std::make_shared<GGTB::StationaryFarmModule>());
    setting->RegisterModule(std::make_shared<GGTB::BlockLevelUpModule>());
    setting->RegisterModule(std::make_shared<GGTB::MultiHitModule>());
    setting->RegisterModule(std::make_shared<GGTB::AutoFleeModule>());
    // Guard registered LAST so it sees every other module via Setting::GetModules().
    // ESP 在 Guard 之后注册,这样 ESP 显示在 NPG 下方,且 CanAutoPause=false 不受 NPG 影响。
    auto espMod = std::make_shared<GGTB::PlayerESPModule>();
    setting->RegisterModule(std::make_shared<GGTB::NearbyPlayerGuardModule>(setting.get(), espMod.get()));
    setting->RegisterModule(espMod);

    try
    {
        zzj::D3D::D3D9Hook::Setup(setting);

        while (true)
        {
            // Hotkeys: drain GetAsyncKeyState's `& 1` bit every tick so a
            // press that happened while another app had focus does not fire
            // once the user alt-tabs back. Only honour presses when the host
            // window is currently in the foreground.
            const bool endPressed  = (GetAsyncKeyState(VK_END) & 1) != 0;
            const bool firePressed = (GetAsyncKeyState('N')     & 1) != 0;
            const bool focused     = HostWindowHasFocus();

            if (endPressed && focused)
                break;

            // N: toggle FireFullPower. Skip while NPG has us paused — otherwise
            // the user re-enabling mid-guard would re-apply the patch and leak
            // the effect to the nearby player (same reason the checkbox is
            // disabled in the UI while guarded).
            if (firePressed && focused && firePowerMod && !firePowerMod->IsPausedByGuard())
            {
                const bool wasEnabled = firePowerMod->IsEnabled();
                firePowerMod->SetEnabled(!wasEnabled);
                if (!wasEnabled)
                    firePowerMod->OnResume();
                else
                    firePowerMod->OnShutdown();
                GGTB::UserConfig::MarkDirty();
                spdlog::info("GGTB: hotkey N -> FireFullPower {}",
                             firePowerMod->IsEnabled() ? "ON" : "OFF");
            }

            GGTB::UserConfig::Tick(setting.get());
            Sleep(100);
        }
    }
    catch (const std::exception &e)
    {
        spdlog::error("GGTB HackThread exception: {}", e.what());
        MessageBoxA(nullptr, e.what(), "GGThreadBlock Error", MB_OK | MB_ICONERROR);
    }

    // Persist current state BEFORE setting->End() — OnShutdown may reset
    // enabled_/slider values and we want to save what the user actually had.
    GGTB::UserConfig::SaveModuleStates(setting.get());

    setting->End();
    // Detach in reverse install order. HwFpSpoof and NetLog no longer hold
    // DeleteFileA trampolines themselves — they just own their stage-2
    // detours. Stage1Trigger goes last so any late callback (shouldn't
    // happen after End() but kept for symmetry) has something to run on.
    // RemoteControl 早于 NetLog 卸,让 best-effort bye 帧的 send 走正常 socket。
    GGTB::RemoteControl::Uninstall();
    // InputInjector 卸在 RemoteControl 之后 — 不会再有命令进来要注入了。
    GGTB::InputInjector::Uninstall();
    GGTB::DisconnectWatchdog::Uninstall();
    GGTB::HwFpSpoof::Uninstall();
    GGTB::NetLog::Uninstall();
    GGTB::Stage1Trigger::Uninstall();
    GGTB::SpeedHack::Shutdown();
    zzj::D3D::D3D9Hook::Destroy();
    setting.reset();

    spdlog::info("GGThreadBlock unloaded");
    spdlog::shutdown();

    FreeLibraryAndExitThread(hModule, 0);
    return 0;
}

// ============================================================
//  Exports
// ============================================================

extern "C" __declspec(dllexport) LONG GGThreadBlock_GetBlockedCount()
{
    return GGTB::g_blockedThreadCount;
}

extern "C" __declspec(dllexport) int GGThreadBlock_Test()
{
    MessageBoxA(nullptr, "GGThreadBlock loaded", "GGThreadBlock",
                MB_OK | MB_ICONINFORMATION);
    return 0x1337;
}

// ============================================================
//  DllMain
// ============================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (DetourIsHelperProcess())
        return TRUE;

    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        InstallDetour();
        // Logger init moved into HackThread — file IO + spdlog registry mutex
        // under loader-lock can deadlock.
        CloseHandle(CreateThread(nullptr, 0, HackThread, hModule, 0, nullptr));
        // NPmsg.dll loads after us; the patcher polls until it shows up.
        CloseHandle(CreateThread(nullptr, 0, NPmsgPatcherThread, nullptr, 0, nullptr));
        break;
    case DLL_PROCESS_DETACH:
        UninstallDetour();
        break;
    }
    return TRUE;
}
