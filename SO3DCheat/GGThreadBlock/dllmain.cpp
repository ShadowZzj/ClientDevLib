#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Detours/build/include/detours.h>
#include <spdlog/spdlog.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
#include "util/EnchantStoneControl.h"
#include "util/DailyTaskControl.h"
#include "util/InputInjector.h"
#include "util/LoginBridge.h"
#include "entity/CLocalPlayer.h"
#include "modules/StatusModule.h"
#include "modules/MoveSpeedModule.h"
#include "modules/AttackSpeedModule.h"
#include "modules/SkillSpeedModule.h"
#include "modules/SpeedHackModule.h"
#include "modules/ItemNoCDModule.h"
#include "modules/AttackRangeModule.h"
#include "modules/AttackMoveModule.h"
#include "modules/SkillMoveModule.h"
#include "modules/NoShiftAttackModule.h"
#include "modules/IgnoreCCModule.h"
#include "modules/TeleportModule.h"
#include "modules/WallHackModule.h"
#include "modules/AutoPickupModule.h"
#include "modules/AutoDecomposeGemModule.h"
#include "modules/FireFullPowerModule.h"
#include "modules/MakeBombDropModule.h"
#include "modules/MultiCastModule.h"
#include "modules/AutoConfirmModule.h"  // 也用于 sendMoneyMail handler 静音 OK toast
#include "modules/AutoDelegationModule.h"
#include "modules/AutoMailModule.h"
#include "modules/AutoTradeController.h"
#include "modules/PickupFilter.h"
#include "modules/NearbyPlayerGuardModule.h"
#include "modules/PlayerESPModule.h"
#include "modules/StationaryFarmModule.h"
#include "modules/BlockLevelUpModule.h"
#include "modules/MultiHitModule.h"
#include "modules/AutoFleeModule.h"
#include "modules/TeleportTownModule.h"
#include "modules/AutoPathModule.h"
#include "modules/FixedPointMoveModule.h"
#include "modules/WarpCoordModule.h"
#include "modules/OpenNearbyStallModule.h"
#include "modules/MonsterListModule.h"
#include "modules/ScreenOverlay.h"

namespace GGTB
{
volatile LONG g_blockedThreadCount = 0;
} // namespace GGTB

// broker 远程控制 NPG:HackThread 注册模块后存下指针,setNpgConfig/getNpgConfig
// handler 通过它下发开关 + 保持时长。Setting 按 shared_ptr 持有模块,生命周期
// 比 handler 长;退出时 Uninstall handler 后再置空。
static std::atomic<GGTB::NearbyPlayerGuardModule *> g_npgModule{nullptr};

namespace
{
constexpr uint32_t kDungeonEntryTicketItemId = 26419;
constexpr uint32_t kDungeonEntryTargetOpt = 9575;
constexpr uint32_t kDungeonEntryExpectedWarpTableId = 564;
constexpr size_t kDungeonEntryRequestTokenMaxLength = 80;

bool ReadStrictJsonU32(const nlohmann::json &args, const char *name, uint32_t *value)
{
    if (!value) return false;
    auto it = args.find(name);
    if (it == args.end()) return false;
    try
    {
        if (it->is_number_unsigned())
        {
            uint64_t parsed = it->get<uint64_t>();
            if (parsed > UINT32_MAX) return false;
            *value = static_cast<uint32_t>(parsed);
            return true;
        }
        if (it->is_number_integer())
        {
            int64_t parsed = it->get<int64_t>();
            if (parsed < 0 || parsed > static_cast<int64_t>(UINT32_MAX)) return false;
            *value = static_cast<uint32_t>(parsed);
            return true;
        }
    }
    catch (...)
    {
    }
    return false;
}

bool ReadStrictJsonBool(const nlohmann::json &args, const char *name, bool fallback,
                        bool *value)
{
    if (!value) return false;
    auto it = args.find(name);
    if (it == args.end())
    {
        *value = fallback;
        return true;
    }
    if (!it->is_boolean()) return false;
    *value = it->get<bool>();
    return true;
}

bool ReadStrictDungeonEntryToken(const nlohmann::json &args, std::string *token)
{
    if (!token) return false;
    auto it = args.find("requestToken");
    if (it == args.end() || !it->is_string()) return false;
    *token = it->get<std::string>();
    if (token->empty() || token->size() > kDungeonEntryRequestTokenMaxLength) return false;
    for (unsigned char ch : *token)
    {
        if (ch <= 0x20 || ch >= 0x7F) return false;
    }
    return true;
}

uint32_t SnapshotDungeonEntryTickets(GGTB::AutoConfirmDetail::DungeonEntryTicketBaseline *outSlots,
                                     size_t *outSlotCount)
{
    size_t slotCount = 0;
    uint64_t total = 0;
    for (const auto &item : GGTB::GetCashBagItems())
    {
        if (item.itemId != kDungeonEntryTicketItemId) continue;
        total += item.count;
        if (outSlots && slotCount < GGTB::AutoConfirmDetail::kDungeonEntryTicketSlotCapacity)
        {
            outSlots[slotCount++] = {item.bagId, item.count};
        }
    }
    if (outSlotCount) *outSlotCount = slotCount;
    return total > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(total);
}

bool IsStrictDungeonTicketAck(const GGTB::NetLog::DungeonEntryEvent &event,
                              const GGTB::AutoConfirmDetail::DungeonEntryConfirmSnapshot &snapshot)
{
    if (event.kind != GGTB::NetLog::DungeonEntryEventKind::CashSlotUpdate) return false;

    for (size_t i = 0; i < snapshot.ticketSlotCount; ++i)
    {
        const auto &slot = snapshot.ticketSlots[i];
        if (event.wireSlot != slot.bagId || slot.count == 0) continue;

        if (event.itemId == kDungeonEntryTicketItemId && event.count == slot.count - 1) return true;

        // The server clears the template id when the final ticket empties the
        // slot. Preserve and recognize that authoritative 511325 form.
        if (event.itemId == 0 && slot.count == 1 && event.count == 0) return true;
    }
    return false;
}

nlohmann::json DungeonEntryEventToJson(const GGTB::NetLog::DungeonEntryEvent &event)
{
    nlohmann::json result = {
        {"seq", event.sequence},
        {"proto", event.proto},
        {"frameLen", event.frameLength},
    };
    switch (event.kind)
    {
        case GGTB::NetLog::DungeonEntryEventKind::InstanceRegister:
            result["result"] = event.result;
            result["nInsd"] = event.nInsd;
            break;
        case GGTB::NetLog::DungeonEntryEventKind::CashSlotUpdate:
            result["wireSlot"] = event.wireSlot;
            result["itemId"] = event.itemId;
            result["packedCount"] = event.packedCount;
            result["count"] = event.count;
            result["extra"] = event.extra;
            result["tailCode"] = event.tailCode;
            break;
        case GGTB::NetLog::DungeonEntryEventKind::NpcDialogConfirm:
            result["opt"] = event.opt;
            result["branch"] = event.branch;
            result["rawAux"] = event.rawAux;
            result["hasRawAux"] = event.hasRawAux;
            break;
    }
    return result;
}
}  // namespace

// ============================================================
//  CreateThread detour — swallow GameGuardDll.dll worker threads
// ============================================================

using fnCreateThread = HANDLE(WINAPI *)(LPSECURITY_ATTRIBUTES, SIZE_T,
                                        LPTHREAD_START_ROUTINE, LPVOID,
                                        DWORD, LPDWORD);

static fnCreateThread g_oCreateThread = CreateThread;
static volatile LONG g_remoteUnloadRequested = 0;

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

static uint32_t JsonU32Arg(const nlohmann::json &args, const char *name, uint32_t fallback)
{
    auto it = args.find(name);
    if (it == args.end() || it->is_null())
        return fallback;
    try
    {
        if (it->is_number_unsigned())
            return it->get<uint32_t>();
        if (it->is_number_integer())
            return static_cast<uint32_t>(it->get<int64_t>());
        if (it->is_number_float())
            return static_cast<uint32_t>(it->get<double>());
        if (it->is_string())
        {
            std::string s = it->get<std::string>();
            int base = 10;
            if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
                base = 16;
            return static_cast<uint32_t>(std::stoull(s, nullptr, base));
        }
    }
    catch (...)
    {
    }
    return fallback;
}

static std::unordered_set<uint32_t> JsonU32SetArg(const nlohmann::json &args, const char *name)
{
    std::unordered_set<uint32_t> out;
    auto it = args.find(name);
    if (it == args.end() || !it->is_array())
        return out;
    for (const auto &entry : *it)
    {
        nlohmann::json wrapper;
        wrapper["v"] = entry;
        uint32_t value = JsonU32Arg(wrapper, "v", 0);
        if (value != 0)
            out.insert(value);
    }
    return out;
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
// (browser, editor) is ignored.
static bool HostWindowHasFocus()
{
    HWND fg = GetForegroundWindow();
    if (!fg)
        return false;
    DWORD fgPid = 0;
    GetWindowThreadProcessId(fg, &fgPid);
    return fgPid == GetCurrentProcessId();
}

static bool IsKeyDown(DWORD vk)
{
    return (GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;
}

static DWORD WINAPI HackThread(LPVOID lpParam)
{
    auto hModule = static_cast<HMODULE>(lpParam);

    // Bootstrap logger to <dll-dir>/GGConfig/_bootstrap/bootstrap.log first —
    // PatternResolver::Init logs to the default sink, so it must exist by then.
    GGTB::UserConfig::Bootstrap(hModule);
    spdlog::info("GGThreadBlock DLL Attached");

    // Load cache/fallback addresses without scanning the still-changing image.
    // Queue the complete stage-2 sequence before arming its one-shot WinMain
    // signal: attach the order-sensitive hooks first, then scan stable .text
    // and refresh the cache, all before DeleteFileA returns to WinMain.
    GGTB::PatternResolver::Prepare(hModule);
    bool nativeUiStage2Stable = false;
    HANDLE stage2Ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stage2Ready)
    {
        std::atomic<bool> stage2CallbackDone{false};
        GGTB::NetLog::Install();
        GGTB::HwFpSpoof::Install();
        GGTB::Stage1Trigger::Register(
            []() { GGTB::PatternResolver::ResolvePatterns(); });
        GGTB::Stage1Trigger::Register([stage2Ready, &stage2CallbackDone]() {
            BOOL  signaled    = SetEvent(stage2Ready);
            DWORD signalError = signaled ? ERROR_SUCCESS : GetLastError();
            stage2CallbackDone.store(true, std::memory_order_release);
            if (!signaled)
                spdlog::error("GGTB: failed to signal stage-2 completion: {}", signalError);
        });

        if (GGTB::Stage1Trigger::Install())
        {
            for (;;)
            {
                DWORD waitResult = WaitForSingleObject(stage2Ready, 5000);
                if (waitResult == WAIT_OBJECT_0)
                {
                    while (!stage2CallbackDone.load(std::memory_order_acquire))
                        Sleep(0);
                    nativeUiStage2Stable = true;
                    break;
                }
                if (waitResult == WAIT_TIMEOUT)
                {
                    if (stage2CallbackDone.load(std::memory_order_acquire))
                    {
                        nativeUiStage2Stable = true;
                        break;
                    }
                    if (GGTB::Stage1Trigger::CancelPending())
                    {
                        spdlog::error("GGTB: WinMain stage-2 signal not observed within 5s; "
                                      "order-sensitive network/HW hooks disabled");
                        // Five seconds is well past the observed unpack point;
                        // resolve other modules, but never attach these hooks late.
                        GGTB::PatternResolver::ResolvePatterns();
                        nativeUiStage2Stable = true;
                        break;
                    }
                    spdlog::warn("GGTB: WinMain stage-2 fired; waiting for callbacks to finish");
                }
                else
                {
                    DWORD waitError = GetLastError();
                    if (GGTB::Stage1Trigger::CancelPending())
                    {
                        spdlog::error("GGTB: stage-2 wait failed before firing: {}; "
                                      "order-sensitive hooks disabled", waitError);
                        GGTB::PatternResolver::ResolvePatterns(false);
                        break;
                    }
                    spdlog::error("GGTB: stage-2 wait failed after firing: {}; "
                                  "waiting for callback completion", waitError);
                    while (!stage2CallbackDone.load(std::memory_order_acquire))
                        Sleep(1);
                    nativeUiStage2Stable = true;
                    break;
                }
            }
        }
        else
        {
            // Keep non-stage-2 modules usable when the sentinel detour itself
            // cannot be installed. Network/HW hooks stay queued and inactive.
            if (GGTB::Stage1Trigger::CancelPending())
                GGTB::PatternResolver::ResolvePatterns(false);
            else
            {
                // Defensive only: Install() cannot normally fail after firing,
                // but never release callback-owned stack resources if it does.
                while (!stage2CallbackDone.load(std::memory_order_acquire))
                    Sleep(1);
                nativeUiStage2Stable = true;
            }
        }
        CloseHandle(stage2Ready);
    }
    else
    {
        spdlog::error("GGTB: CreateEvent for stage-2 coordination failed: {}",
                      GetLastError());
        // Without a completion barrier we cannot safely own callback lifetime.
        // Keep fallback addresses, but do not arm or attach order-sensitive hooks.
        GGTB::PatternResolver::ResolvePatterns(false);
    }

    // The broker IO thread must never invoke NPC UI callbacks directly. These
    // two hooks rendezvous one-shot requests with the real CSO3D::GameLoop and
    // preserve the native dialog-update return/pending contract.
    if (!nativeUiStage2Stable)
        spdlog::error("GGTB: native NPC UI bridge not installed because stage-2 was not stable");
    else if (!GGTB::InstallNativeUiBridge())
        spdlog::error("GGTB: native NPC UI bridge unavailable; dungeon entry UI automation disabled");

    // Silent-disconnect watchdog — arms on first game-port recv (1842/1843),
    // then force-closes the process if both ports go quiet for 5 minutes.
    // Depends on NetLog's recv hook being live to feed OnRecv().
    GGTB::DisconnectWatchdog::Install();
    GGTB::LoginBridge::Install();

    // Long-running wash orchestration lives in the broker. These handlers only
    // expose one bounded query/send/wait step and are registered before the pipe
    // connects so the broker cannot race an unknown-action window.
    GGTB::EnchantStoneControl::RegisterHandlers();
    GGTB::DailyTaskControl::RegisterHandlers();

    // Web 远控通道:HackThread 起 IO 线程主动连 \\.\pipe\GGTB_BROKER。Install 之
    // 前先把 sendMoneyMail handler 注册好,避免 broker 已经把 connect 后立刻派
    // 命令的窗口期跑空。Handler 要在 IO 线程上同步执行 — 引擎的 packet 系列在
    // 任意 worker 线程都能调,这点 AutoMail 已经验过了。
    GGTB::RemoteControl::RegisterCommandHandler(
        "unloadGGTB",
        [](const nlohmann::json &) -> GGTB::RemoteControl::CmdResult {
            InterlockedExchange(&g_remoteUnloadRequested, 1);
            return {true, "unload requested"};
        });

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

    // 列出背包里所有"可合成"的技能宝石,按等级键(gradeKey)分组。每组给出 gradeKey、
    // 样本 itemId/名字、总件数,以及每"件"的 wire 槽(slotIndex+13)。broker 据此挑 3 个
    // 同组宝石凑一次合成。返回 JSON 数组。
    GGTB::RemoteControl::RegisterCommandHandler(
        "listComposeGems",
        [](const nlohmann::json &) -> GGTB::RemoteControl::CmdResult {
            auto groups = GGTB::ListComposeGemGroups();
            nlohmann::json arr = nlohmann::json::array();
            for (auto &g : groups)
            {
                arr.push_back({
                    {"gradeKey",     g.gradeKey},
                    {"sampleItemId", g.sampleItemId},
                    {"sampleName",   g.sampleName},
                    {"totalCount",   g.totalCount},
                    {"wireSlots",    g.wireSlots}, // 每件一个 slotIndex+13
                });
            }
            return {true, arr.dump()};
        });

    // 发一次三合一:args = { slots: [a, b, c] },三个背包 arrayIndex(0..191)。可全相同
    // (同堆叠)或同等级不同 itemId。只发包,不等回包/不校验数量 —— broker 在两次合成
    // 之间重读 listComposeGems 确认件数下降。
    GGTB::RemoteControl::RegisterCommandHandler(
        "composeGems",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            if (!args.contains("slots") || !args["slots"].is_array() ||
                args["slots"].size() != 3)
                return {false, "args.slots must be an array of 3 slot indices"};
            uint32_t s0 = args["slots"][0].get<uint32_t>();
            uint32_t s1 = args["slots"][1].get<uint32_t>();
            uint32_t s2 = args["slots"][2].get<uint32_t>();
            std::string err;
            bool ok = GGTB::ComposeSendRaw(s0, s1, s2, &err);
            return {ok, ok ? std::string{} : err};
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

    // teleport: 复刻聊天框输入「/狮子城」的城市传送 —— 客户端本地解析的命令。
    //   { "cityName": "狮子城" }   -> 先用 UIManager(99) 传送表把名字解析成 destId
    //                                (如狮子城 -> 202),再发 411076(destId)。城名是
    //                                UTF-8,DLL 内部转 Big5(此 build 是 TW 包)。
    //   { "destId": 202 }          -> 跳过名字解析直接发 411076(destId),绕开等级/
    //                                金钱闸(传送表对不满足条件的城会解析失败)。
    // 两者都给则 destId 优先。死亡/地图闸由服务端校验,这层不拦(项目惯例)。
    GGTB::RemoteControl::RegisterCommandHandler(
        "teleport",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            int destId = args.value("destId", 0);
            if (destId > 0)
            {
                bool ok = GGTB::SendTeleportToDest(destId);
                return {ok, ok ? std::string{} : std::string{"send failed (pattern unresolved or SEH)"}};
            }
            std::string cityName = args.value("cityName", std::string{});
            if (cityName.empty())
                return {false, "missing cityName or destId"};
            if (cityName.size() > 64)
                return {false, "cityName too long"};
            bool ok = GGTB::TeleportByCityName(cityName.c_str());
            return {ok, ok ? std::string{} : std::string{"could not resolve city (unknown / level / money / unavailable)"}};
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

    GGTB::RemoteControl::RegisterCommandHandler(
        "openNpcDialogFromWheel",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult
        {
            uint32_t npcId = 0;
            if (!ReadStrictJsonU32(args, "npcId", &npcId) || npcId == 0)
                return {false, "npcId must be a uint32 value greater than zero"};

            auto result = GGTB::OpenNpcDialogFromInteractionWheel(npcId);
            nlohmann::json detail = {
                {"ok", result.ok},
                {"retryable", result.retryable},
                {"callbackInvoked", result.callbackInvoked},
                {"alreadyOpen", result.alreadyOpen},
                {"foregroundUiId", result.foregroundUiId},
                {"npcId", result.npcId},
                {"error", result.error},
            };
            return {result.ok, detail.dump()};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "advanceDialogTowardOpt",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult
        {
            uint32_t npcId = 0;
            uint32_t targetOpt = 0;
            bool allowAdvance = true;
            if (!ReadStrictJsonU32(args, "npcId", &npcId) || npcId == 0 ||
                !ReadStrictJsonU32(args, "targetOpt", &targetOpt) || targetOpt == 0 ||
                !ReadStrictJsonBool(args, "allowAdvance", true, &allowAdvance))
                return {false, "npcId/targetOpt must be uint32 values greater than zero"};

            auto result = GGTB::AdvanceDialogTowardOpt(npcId, targetOpt, allowAdvance);
            nlohmann::json detail = {
                {"ok", result.ok},
                {"retryable", result.retryable},
                {"found", result.found},
                {"advanced", result.advanced},
                {"optionIndex", result.optionIndex},
                {"warpKey", result.warpKey},
                {"warpTableId", result.warpTableId},
                {"error", result.error},
            };
            return {result.ok, detail.dump()};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "prepareDungeonEntry",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult
        {
            std::string requestToken;
            uint32_t npcId = 0;
            uint32_t opt = 0;
            uint32_t sub = 0;
            uint32_t dialogOptionIndex = 0;
            uint32_t expectedWarpTableId = 0;
            uint32_t ttlMs = 0;
            bool autoUseExtraTicket = false;
            if (!ReadStrictDungeonEntryToken(args, &requestToken) ||
                !ReadStrictJsonU32(args, "npcId", &npcId) || npcId == 0 ||
                !ReadStrictJsonU32(args, "opt", &opt) || opt != kDungeonEntryTargetOpt ||
                !ReadStrictJsonU32(args, "sub", &sub) || sub != 1 ||
                !ReadStrictJsonU32(args, "dialogOptionIndex", &dialogOptionIndex) ||
                !ReadStrictJsonU32(args, "expectedWarpTableId", &expectedWarpTableId) ||
                expectedWarpTableId != kDungeonEntryExpectedWarpTableId ||
                !ReadStrictJsonU32(args, "ttlMs", &ttlMs) || ttlMs < 5000 || ttlMs > 120000 ||
                !ReadStrictJsonBool(args, "autoUseExtraTicket", false, &autoUseExtraTicket))
                return {false, "invalid targeted dungeon entry prepare arguments"};

            if (!GGTB::AutoConfirmDetail::EnsureHooksInstalled())
                return {false, "targeted confirm hooks unavailable"};

            auto dialog = GGTB::GetDialogSnapshot();
            auto selectedOption = std::find_if(
                dialog.options.begin(), dialog.options.end(),
                [dialogOptionIndex](const GGTB::DialogOption &entry) {
                    return entry.index == dialogOptionIndex;
                });
            if (!dialog.open || dialog.npcInteractId != npcId ||
                selectedOption == dialog.options.end() || selectedOption->opt != opt ||
                selectedOption->warpKey == 0 ||
                selectedOption->warpTableId != expectedWarpTableId)
                return {false, "native NPC dialog target/warp context no longer matches"};

            GGTB::AutoConfirmDetail::DungeonEntryPreparedContext context{
                requestToken, npcId, opt, sub, dialogOptionIndex, expectedWarpTableId,
                autoUseExtraTicket, static_cast<DWORD>(ttlMs)};
            bool idempotent = false;
            DWORD prepareTtlMs = static_cast<DWORD>(std::min<uint32_t>(ttlMs, 15000));
            if (!GGTB::AutoConfirmDetail::PrepareDungeonEntryTransaction(
                    context, prepareTtlMs, &idempotent))
            {
                nlohmann::json detail = {
                    {"error", "another dungeon entry transaction is active or token conflicts"},
                    {"requestToken", GGTB::AutoConfirmDetail::GetDungeonEntryRequestToken()},
                    {"transactionState",
                     GGTB::AutoConfirmDetail::DungeonEntryTransactionStateName(
                         GGTB::AutoConfirmDetail::GetDungeonEntryTransactionState())},
                };
                return {false, detail.dump()};
            }

            nlohmann::json detail = {
                {"requestToken", requestToken},
                {"transactionState", "Prepared"},
                {"idempotent", idempotent},
                {"dialogOptionIndex", dialogOptionIndex},
                {"warpKey", selectedOption->warpKey},
                {"warpTableId", selectedOption->warpTableId},
            };
            return {true, detail.dump()};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "commitDungeonEntry",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult
        {
            std::string requestToken;
            if (!ReadStrictDungeonEntryToken(args, &requestToken))
                return {false, "invalid requestToken"};

            GGTB::AutoConfirmDetail::DungeonEntryPreparedContext context{};
            if (!GGTB::AutoConfirmDetail::GetDungeonEntryPreparedContext(requestToken, &context))
                return {false, "dungeon entry transaction is not Prepared"};

            auto dialog = GGTB::GetDialogSnapshot();
            auto selectedOption = std::find_if(
                dialog.options.begin(), dialog.options.end(),
                [&context](const GGTB::DialogOption &entry) {
                    return entry.index == context.dialogOptionIndex;
                });
            if (!dialog.open || dialog.npcInteractId != context.npcId ||
                selectedOption == dialog.options.end() || selectedOption->opt != context.opt ||
                selectedOption->warpKey == 0 ||
                selectedOption->warpTableId != context.expectedWarpTableId)
                return {false, "native NPC dialog changed before commit"};

            GGTB::AutoConfirmDetail::DungeonEntryTicketBaseline
                ticketSlots[GGTB::AutoConfirmDetail::kDungeonEntryTicketSlotCapacity]{};
            size_t ticketSlotCount = 0;
            uint32_t ticketCount = SnapshotDungeonEntryTickets(ticketSlots, &ticketSlotCount);
            uint64_t baselineSequence = GGTB::NetLog::GetDungeonEntryLatestSequence();
            uint64_t generation = 0;
            GGTB::AutoConfirmDetail::DungeonEntryPreparedContext committedContext{};
            if (!GGTB::AutoConfirmDetail::BeginDungeonEntryCommit(
                    requestToken, baselineSequence, ticketCount, ticketSlots, ticketSlotCount,
                    &generation, &committedContext))
                return {false, "dungeon entry commit was already issued or cancelled"};

            if (!GGTB::AutoConfirmDetail::ClaimDungeonEntryInitialSelect(requestToken, generation))
            {
                bool ignored = false;
                GGTB::AutoConfirmDetail::CancelDungeonEntryConfirmByToken(requestToken, &ignored);
                return {false, "dungeon entry initial select claim failed"};
            }

            bool selected = GGTB::SelectDialogOptionChecked(
                committedContext.npcId, committedContext.opt,
                committedContext.dialogOptionIndex, committedContext.expectedWarpTableId);
            bool transactionCompleted =
                GGTB::AutoConfirmDetail::CompleteDungeonEntryInitialSelect(
                requestToken, generation, selected);
            if (!selected || !transactionCompleted)
            {
                nlohmann::json detail = {
                    {"requestToken", requestToken},
                    {"generation", generation},
                    {"transactionState",
                     GGTB::AutoConfirmDetail::DungeonEntryTransactionStateName(
                         GGTB::AutoConfirmDetail::GetDungeonEntryTransactionState())},
                    {"error", selected
                                  ? "native select completed after transaction cancellation; it will not be retried"
                                  : "checked native dialog select failed; it will not be retried"},
                };
                return {false, detail.dump()};
            }

            nlohmann::json detail = {
                {"requestToken", requestToken},
                {"generation", generation},
                {"baselineSeq", baselineSequence},
                {"ticketCount", ticketCount},
                {"autoUseExtraTicket", committedContext.autoUseExtraTicket},
                {"dialogOptionIndex", committedContext.dialogOptionIndex},
                {"expectedWarpTableId", committedContext.expectedWarpTableId},
                {"transactionState", "Committed"},
                {"state", GGTB::AutoConfirmDetail::DungeonEntryConfirmStateName(
                              GGTB::AutoConfirmDetail::GetDungeonEntryConfirmState())},
            };
            return {true, detail.dump()};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "queryDungeonEntry",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult
        {
            GGTB::AutoConfirmDetail::DungeonEntryConfirmSnapshot snapshot{};
            std::string requestToken;
            uint64_t generation = 0;
            bool found = false;
            if (args.contains("requestToken"))
            {
                if (!ReadStrictDungeonEntryToken(args, &requestToken))
                    return {false, "invalid requestToken"};
                found = GGTB::AutoConfirmDetail::GetDungeonEntryConfirmSnapshotByToken(
                    requestToken, &snapshot);
            }
            else
            {
                if (!args.contains("generation") || !args["generation"].is_number_unsigned())
                    return {false, "missing requestToken/generation"};
                generation = args["generation"].get<uint64_t>();
                found = GGTB::AutoConfirmDetail::GetDungeonEntryConfirmSnapshot(
                    generation, &snapshot);
            }
            if (!found)
            {
                nlohmann::json detail = {
                    {"error", "dungeon entry request token/generation mismatch"},
                    {"requestToken", requestToken},
                    {"generation", generation},
                    {"currentRequestToken",
                     GGTB::AutoConfirmDetail::GetDungeonEntryRequestToken()},
                    {"currentGeneration",
                     GGTB::AutoConfirmDetail::GetDungeonEntryConfirmGeneration()},
                    {"transactionState",
                     GGTB::AutoConfirmDetail::DungeonEntryTransactionStateName(
                         GGTB::AutoConfirmDetail::GetDungeonEntryTransactionState())},
                };
                return {false, detail.dump()};
            }

            uint64_t afterSequence = args.contains("afterSeq") ? args.value("afterSeq", uint64_t{0})
                                                               : snapshot.baselineSequence;
            std::vector<GGTB::NetLog::DungeonEntryEvent> events;
            bool truncated = false;
            uint64_t latestSequence =
                GGTB::NetLog::QueryDungeonEntryEvents(afterSequence, events, &truncated);

            // ACK evaluation always starts at this generation's immutable
            // baseline, independent of the caller's pagination cursor.
            std::vector<GGTB::NetLog::DungeonEntryEvent> ackEvents;
            GGTB::NetLog::QueryDungeonEntryEvents(snapshot.baselineSequence, ackEvents, nullptr);
            bool ticketAck = false;
            uint64_t ticketAckSequence = 0;
            for (const auto &event : ackEvents)
            {
                if (IsStrictDungeonTicketAck(event, snapshot))
                {
                    ticketAck = true;
                    ticketAckSequence = event.sequence;
                    break;
                }
            }

            nlohmann::json eventArray = nlohmann::json::array();
            for (const auto &event : events) eventArray.push_back(DungeonEntryEventToJson(event));

            uint32_t currentTicketCount = SnapshotDungeonEntryTickets(nullptr, nullptr);
            nlohmann::json detail = {
                {"requestToken", snapshot.requestToken},
                {"transactionState",
                 GGTB::AutoConfirmDetail::DungeonEntryTransactionStateName(
                     snapshot.transactionState)},
                {"initialSelectClaimed", snapshot.initialSelectClaimed},
                {"generation", snapshot.generation},
                {"state", GGTB::AutoConfirmDetail::DungeonEntryConfirmStateName(snapshot.state)},
                {"confirmState",
                 GGTB::AutoConfirmDetail::DungeonEntryConfirmStateName(snapshot.state)},
                {"autoUseExtraTicket", snapshot.autoUseExtraTicket},
                {"autoUse", snapshot.autoUseExtraTicket},
                {"baselineSeq", snapshot.baselineSequence},
                {"baselineDialogSelectSeq", snapshot.baselineDialogSelectSequence},
                {"latestSeq", latestSequence},
                {"truncated", truncated},
                {"ticketCount", snapshot.baselineTicketCount},
                {"currentTicketCount", currentTicketCount},
                {"remainingMs", snapshot.remainingMs},
                {"dialogSeenCount", snapshot.dialogSeenCount},
                {"lastDialogType", snapshot.lastDialogType},
                {"lastDialogMatch",
                 GGTB::AutoConfirmDetail::DungeonEntryDialogMatchKindName(
                     snapshot.lastDialogMatch)},
                {"lastDialogAuthorized", snapshot.lastDialogAuthorized},
                {"towerPromptAccepted", snapshot.towerPromptAccepted},
                {"ticketAck", ticketAck},
                {"ticketAckSeq", ticketAckSequence},
                {"events", std::move(eventArray)},
            };
            return {true, detail.dump()};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "cancelDungeonEntry",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult
        {
            std::string requestToken;
            uint64_t generation = 0;
            bool cancelled = false;
            bool matched = false;
            if (args.contains("requestToken"))
            {
                if (!ReadStrictDungeonEntryToken(args, &requestToken))
                    return {false, "invalid requestToken"};
                matched = GGTB::AutoConfirmDetail::CancelDungeonEntryConfirmByToken(
                    requestToken, &cancelled);
            }
            else
            {
                if (!args.contains("generation") || !args["generation"].is_number_unsigned())
                    return {false, "missing requestToken/generation"};
                generation = args["generation"].get<uint64_t>();
                matched = GGTB::AutoConfirmDetail::CancelDungeonEntryConfirm(
                    generation, &cancelled);
            }
            if (!matched)
            {
                nlohmann::json detail = {
                    {"error", "dungeon entry request token/generation mismatch"},
                    {"requestToken", requestToken},
                    {"generation", generation},
                    {"currentRequestToken",
                     GGTB::AutoConfirmDetail::GetDungeonEntryRequestToken()},
                    {"currentGeneration",
                     GGTB::AutoConfirmDetail::GetDungeonEntryConfirmGeneration()},
                    {"transactionState",
                     GGTB::AutoConfirmDetail::DungeonEntryTransactionStateName(
                         GGTB::AutoConfirmDetail::GetDungeonEntryTransactionState())},
                };
                return {false, detail.dump()};
            }

            GGTB::AutoConfirmDetail::DungeonEntryConfirmSnapshot snapshot{};
            if (!requestToken.empty())
                GGTB::AutoConfirmDetail::GetDungeonEntryConfirmSnapshotByToken(
                    requestToken, &snapshot);
            else
                GGTB::AutoConfirmDetail::GetDungeonEntryConfirmSnapshot(generation, &snapshot);
            nlohmann::json detail = {
                {"requestToken", snapshot.requestToken},
                {"generation", snapshot.generation},
                {"transactionState",
                 GGTB::AutoConfirmDetail::DungeonEntryTransactionStateName(
                     snapshot.transactionState)},
                {"state", GGTB::AutoConfirmDetail::DungeonEntryConfirmStateName(snapshot.state)},
                {"cancelled", cancelled},
            };
            return {true, detail.dump()};
        });

    // ---------- 发条 (Magic Spring) 自动洗 ----------
    // queryClockwork {slotIndex}: 读普通背包 arrayIndex 上装备的发条状态(grade+3属性)。
    // washClockwork {slotIndex, springType(0=實習生/1=高手/2=武爾坎努斯)}: 发 411590 洗一次。
    // getSpringAttrTable: 返回属性 id->中文名 表(给 web 选"需要属性")。
    GGTB::RemoteControl::RegisterCommandHandler(
        "queryClockwork",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            uint32_t slot = args.value("slotIndex", UINT32_MAX);
            if (slot == UINT32_MAX)
                return {false, "missing slotIndex"};
            GGTB::SpringState st = GGTB::ReadSpringState(slot);
            nlohmann::json o;
            o["valid"] = st.valid;
            o["grade"] = st.grade;
            nlohmann::json arr = nlohmann::json::array();
            for (int i = 0; i < 3; ++i)
            {
                uint32_t    id   = st.attrs[i].id;
                int32_t     val  = st.attrs[i].value;
                bool        pct  = false;
                std::string name = id ? GGTB::GetSpringAttrName(id, &pct) : std::string{};
                // 复合属性(15..20): value 打包 N=高16(每N等級)、M=低16(增加M)。
                bool composite = (id >= 15 && id <= 20);
                nlohmann::json e = {
                    {"id", id}, {"value", val}, {"name", name},
                    {"percent", pct}, {"composite", composite},
                };
                if (composite)
                {
                    e["n"] = (static_cast<uint32_t>(val) >> 16) & 0xFFFF;
                    e["m"] = static_cast<uint32_t>(val) & 0xFFFF;
                }
                arr.push_back(e);
            }
            o["attrs"] = arr;
            return {true, o.dump()};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "washClockwork",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            uint32_t slot   = args.value("slotIndex", UINT32_MAX);
            int      type   = args.value("springType", -1);
            int      waitMs = args.value("waitMs", 1500);
            if (slot == UINT32_MAX)
                return {false, "missing slotIndex"};
            if (type < 0 || type > 2)
                return {false, "springType must be 0..2"};
            if (waitMs < 0) waitMs = 0;
            if (waitMs > 4000) waitMs = 4000;

            // 记录发包前最近一次 511132 的 tick,发包后等它变化 = 本次洗的回包到了。
            DWORD       preTick = GGTB::NetLog::GetLastSpringAssign();
            std::string err;
            if (!GGTB::WashSpring(slot, type, &err))
                return {false, err};

            uint32_t rc = 0, grade = 0, ids[3] = {}, vals[3] = {};
            DWORD    tick     = preTick;
            DWORD    deadline = GetTickCount() + static_cast<DWORD>(waitMs);
            while (static_cast<int>(deadline - GetTickCount()) > 0)
            {
                Sleep(15);
                tick = GGTB::NetLog::GetLastSpringAssign(&rc, &grade, ids, vals);
                if (tick != preTick && tick != 0)
                    break;
            }

            nlohmann::json o;
            if (tick == preTick || tick == 0)
            {
                // 发出去了但 waitMs 内没等到 511132(服务端慢/掉包),本轮算「已发未确认」。
                o["confirmed"] = false;
                return {true, o.dump()};
            }
            o["confirmed"]  = true;
            o["resultCode"] = rc; // 0=成功;非0=服务端拒绝
            o["grade"]      = grade;
            nlohmann::json arr = nlohmann::json::array();
            for (int i = 0; i < 3; ++i)
            {
                uint32_t    id   = ids[i];
                int32_t     val  = static_cast<int32_t>(vals[i]);
                bool        pct  = false;
                std::string name = id ? GGTB::GetSpringAttrName(id, &pct) : std::string{};
                bool        composite = (id >= 15 && id <= 20);
                nlohmann::json e = {
                    {"id", id}, {"value", val}, {"name", name},
                    {"percent", pct}, {"composite", composite},
                };
                if (composite)
                {
                    e["n"] = (static_cast<uint32_t>(val) >> 16) & 0xFFFF;
                    e["m"] = static_cast<uint32_t>(val) & 0xFFFF;
                }
                arr.push_back(e);
            }
            o["attrs"] = arr;
            return {true, o.dump()};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "getSpringAttrTable",
        [](const nlohmann::json &) -> GGTB::RemoteControl::CmdResult {
            nlohmann::json arr = nlohmann::json::array();
            for (uint32_t id = 1; id <= 23; ++id)
            {
                bool        pct  = false;
                std::string name = GGTB::GetSpringAttrName(id, &pct);
                if (name.empty())
                    continue;
                arr.push_back({{"id", id}, {"name", name}, {"percent", pct},
                               {"composite", id >= 15 && id <= 20}});
            }
            return {true, arr.dump()};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "vendorOpen",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            uint32_t token = JsonU32Arg(args, "token", GGTB::kVendorSummonToken);
            bool ok = GGTB::SendVendorOpen(token);
            return {ok, ok ? "sent" : "failed"};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "getVendorShopItems",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            uint32_t vendorId = JsonU32Arg(args, "vendorId", GGTB::kVendorDefaultId);
            auto items = GGTB::GetVendorShopItems(vendorId);
            nlohmann::json arr = nlohmann::json::array();
            for (const auto &it : items)
            {
                arr.push_back({
                    {"shopIndex", it.shopIndex},
                    {"itemId", it.itemId},
                    {"unitPrice", it.unitPrice},
                    {"name", it.name},
                });
            }
            return {true, arr.dump()};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "vendorBuyItem",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            uint32_t itemId    = JsonU32Arg(args, "itemId", 0);
            uint32_t count     = JsonU32Arg(args, "count", 0);
            uint32_t vendorId  = JsonU32Arg(args, "vendorId", GGTB::kVendorDefaultId);
            uint32_t shopIndex = JsonU32Arg(args, "shopIndex", UINT32_MAX);
            uint32_t targetSlot = JsonU32Arg(args, "targetSlot", 0);
            uint32_t token     = JsonU32Arg(args, "token", GGTB::kVendorSummonToken);
            auto avoidTargetSlots = JsonU32SetArg(args, "avoidTargetSlots");
            if (count == 0)
                return {false, "missing count"};

            nlohmann::json detail;
            detail["vendorId"] = vendorId;
            detail["shopIndex"] = shopIndex;
            detail["itemId"] = itemId;
            detail["count"] = count;
            detail["token"] = token;
            detail["avoidTargetSlots"] = nlohmann::json::array();
            for (uint32_t slot : avoidTargetSlots)
                detail["avoidTargetSlots"].push_back(slot);
            bool ok = false;
            if (itemId != 0 && shopIndex != UINT32_MAX)
            {
                GGTB::VendorBuyResult r{};
                ok = GGTB::SendVendorBuyShopItem(
                    itemId, shopIndex, count, vendorId, token,
                    avoidTargetSlots.empty() ? nullptr : &avoidTargetSlots, &r);
                if (ok)
                {
                    detail["vendorId"] = r.vendorId;
                    detail["shopIndex"] = r.shopIndex;
                    detail["itemId"] = r.itemId;
                    detail["count"] = r.count;
                    detail["targetSlot"] = r.targetSlot;
                    detail["token"] = r.token;
                }
            }
            else if (itemId != 0)
            {
                GGTB::VendorBuyResult r{};
                ok = GGTB::SendVendorBuyItem(
                    itemId, count, vendorId, token,
                    avoidTargetSlots.empty() ? nullptr : &avoidTargetSlots, &r);
                if (ok)
                {
                    detail["vendorId"] = r.vendorId;
                    detail["shopIndex"] = r.shopIndex;
                    detail["itemId"] = r.itemId;
                    detail["count"] = r.count;
                    detail["targetSlot"] = r.targetSlot;
                    detail["token"] = r.token;
                }
            }
            else if (shopIndex != UINT32_MAX && targetSlot != 0)
            {
                ok = GGTB::SendVendorBuy(vendorId, shopIndex, count, targetSlot, token);
                if (ok)
                {
                    detail["vendorId"] = vendorId;
                    detail["shopIndex"] = shopIndex;
                    detail["count"] = count;
                    detail["targetSlot"] = targetSlot;
                    detail["token"] = token;
                }
            }
            else
            {
                return {false, "missing itemId+shopIndex or shopIndex+targetSlot"};
            }
            return {ok, detail.dump()};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "vendorClose",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            uint32_t token = JsonU32Arg(args, "token", GGTB::kVendorSummonToken);
            bool packetSentByUi = false;
            bool localOk = GGTB::CloseVendorWindowLocal(&packetSentByUi);
            bool packetOk = packetSentByUi || GGTB::SendVendorClose(token);
            nlohmann::json detail{
                {"localUiClosed", localOk},
                {"packetSent", packetOk},
                {"packetSentByUiHandler", packetSentByUi},
                {"token", token},
            };
            return {localOk || packetOk, detail.dump()};
        });

    // ---------- 账号共享仓库 (bank) ----------
    // 泡点网店买的物品进账号共享仓库,需开仓库 -> 等 511320 整桶回流 -> 把物品搬进 cash 背包。
    // broker 的 PurchaseMonitor 编排这三步。
    GGTB::RemoteControl::RegisterCommandHandler(
        "openBank",
        [](const nlohmann::json &) -> GGTB::RemoteControl::CmdResult {
            bool ok = GGTB::SendBankOpen();
            return {ok, ok ? "opened" : "failed"};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "closeBank",
        [](const nlohmann::json &) -> GGTB::RemoteControl::CmdResult {
            bool ok = GGTB::SendBankClose();
            return {ok, ok ? "closed" : "failed"};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "getBankItems",
        [](const nlohmann::json &) -> GGTB::RemoteControl::CmdResult {
            auto items = GGTB::GetBankItems();
            nlohmann::json arr = nlohmann::json::array();
            for (const auto &it : items)
            {
                arr.push_back({
                    {"uid", it.uid},
                    {"itemId", it.itemId},
                    {"count", it.count},
                });
            }
            return {true, arr.dump()};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "bankMoveToCash",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            uint32_t uid    = JsonU32Arg(args, "uid", 0);
            uint32_t itemId = JsonU32Arg(args, "itemId", 0);
            uint32_t count  = JsonU32Arg(args, "count", 0);
            if (uid == 0 || count == 0)
                return {false, "missing uid/count"};
            int dest = GGTB::SendBankMoveToCash(uid, itemId, count);
            if (dest < 0)
                return {false, "no free cash slot or send failed"};
            nlohmann::json detail{
                {"uid", uid},
                {"itemId", itemId},
                {"count", count},
                {"destSlot", dest},
            };
            return {true, detail.dump()};
        });

    // ---------- 每日奖励一键领取 (在线 + 签到) ----------
    // broker 的 RewardClaimer 在角色上线后编排:开窗 -> 等服务器回流 -> 领可领档 -> 关窗。
    // 同步在命令线程跑(内部有 ~4s 轮询等待),所以 broker 侧要给足 timeout。
    // detail 返回各档统计,broker 据此判定今天是否领完(day-done)。
    GGTB::RemoteControl::RegisterCommandHandler(
        "claimRewards",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            // online/signin 选择只领某一种还是都领,缺省(无 args)= 都领。
            bool doOnline = args.is_object() ? args.value("online", true) : true;
            bool doSignin = args.is_object() ? args.value("signin", true) : true;
            GGTB::RewardClaimResult res = GGTB::ClaimDailyRewards(doOnline, doSignin);
            nlohmann::json detail{
                {"online", {
                    {"claimed", res.onlineClaimed},
                    {"claimable", res.onlineClaimable},
                    {"locked", res.onlineLocked},
                }},
                {"signin", {
                    {"claimed", res.signinClaimed},
                    {"claimable", res.signinClaimable},
                    {"locked", res.signinLocked},
                }},
            };
            return {res.ok, detail.dump()};
        });

    // ---------- 只读探测「现在是否有可领奖励」(不开窗、不发包) ----------
    // 读 GetUIContent(62/61)+0x38 那个 HUD 图标闪烁位(闹钟/红心),服务器登录即推送填好。
    // broker 用它替代「定时盲开窗」:只有 online/signin 为 true 才真正发 claimRewards 去开窗领。
    // resolved=false = content 还没就绪(刚上线/UI 未建),claimable 不可信,broker 应稍后重试。
    GGTB::RemoteControl::RegisterCommandHandler(
        "peekRewards",
        [](const nlohmann::json &) -> GGTB::RemoteControl::CmdResult {
            GGTB::RewardPeekResult p = GGTB::PeekRewardClaimable();
            nlohmann::json detail{
                {"online", p.onlineClaimable},
                {"signin", p.signinClaimable},
                {"onlineResolved", p.onlineResolved},
                {"signinResolved", p.signinResolved},
            };
            return {p.ok, detail.dump()};
        });

    // 读 localUser 里的明文登录账号/密码(私服里 = 泡点网店账号),供 broker 购买时用,
    // 免去在 paodian 文件里手填密码。仅本机 pipe 通信。
    GGTB::RemoteControl::RegisterCommandHandler(
        "getLoginCredentials",
        [](const nlohmann::json &) -> GGTB::RemoteControl::CmdResult {
            GGTB::LoginCredentials cred;
            if (!GGTB::GetLoginCredentials(cred))
                return {false, "login credentials unavailable"};
            nlohmann::json detail{
                {"account", cred.account},
                {"password", cred.password},
            };
            return {true, detail.dump()};
        });

    // moveTo: 让角色走到世界坐标 (x, y)。单次 call 引擎的 CLocalUser::SetAfterAction
    // (跟点地走 UI 一模一样)。注意:引擎只把目标存一份,TraceMove 朝它直线逐 tile
    // 推进,撞到第一个阻挡 tile 就停,「不绕障」—— 所以这条只适合「近处、直线无遮挡」
    // 的一跳。要走远 / 可能有障碍,用下面的 pathTo(DLL 内 worker 分段寻路)。
    // action=1 是「纯走路」,action=3 是「走过去再打目标」, 配合 targetId 使用。
    // MoveTo 内部 SEH 包过,失败返回 {ok:false}.
    //
    // args:
    //   {"x": 123.4, "y": 567.8}                 — 走到 (x, y)
    //   {"x": ..., "y": ..., "action": 3, "targetId": 42}  — 走过去再打 id=42
    //
    // 注意:SetAfterAction 内部有 m_bCanMove / stunTime / 动画锁 等 reject 闸,
    // 在战斗/施法中可能拒绝。普攻态(370C==0)走 0x753A80 闸,技能态(370C∈{3,5,8})
    // 走 0x753A54 闸 —— 分别由「普攻移动」「技能移动」开关旁路。两个都开才能在任何
    // 动作态下走;是否打补丁由用户在开关里决定,这里不强制。
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
            return {ok, ok ? std::string{} : std::string{"engine rejected (try enable AttackMove/SkillMove)"}};
        });

    // pathTo: 远距离寻路走到 (x, y)。和 moveTo 的区别 —— moveTo 只 call 一次
    // SetAfterAction,引擎朝目标直线推进,撞到第一个阻挡 tile 就停(TraceMove 无绕
    // 障),所以远目标常常走两步就不动。pathTo 在 DLL 内起一条 worker 线程,自己在
    // 碰撞表上跑网格 A* 算出绕障路径,string-pull 平滑后逐拐点 MoveTo,卡住就 replan。
    // 凸障碍、U 型凹墙都能绕,只有真不可达/超出搜索框预算才放弃。
    //
    // args: {"x":.., "y":.., "action":1|3(可选), "targetId":..(可选,action=3 用)}
    // 立即返回 {ok:true} 表示「已接受并开始走」(不阻塞);用 pathStatus 轮询进度,
    // stopPath 取消。ok:false 仅当 g_pLocalUser 未就绪 / 起点不可走。
    GGTB::RemoteControl::RegisterCommandHandler(
        "pathTo",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            if (!args.contains("x") || !args.contains("y"))
                return {false, "missing x/y"};
            float    x        = args.value("x", 0.0f);
            float    y        = args.value("y", 0.0f);
            int      action   = args.value("action", 1);
            uint32_t targetId = args.value("targetId", uint32_t{0});
            if (action != 1 && action != 3)
                return {false, "action must be 1 (walk) or 3 (walk+attack)"};
            bool ok = GGTB::StartPathTo(x, y, action, targetId);
            return {ok, ok ? std::string{} : std::string{"not ready (no local player / start tile blocked)"}};
        });

    // stopPath: 取消当前 pathTo 寻路。幂等。
    GGTB::RemoteControl::RegisterCommandHandler(
        "stopPath",
        [](const nlohmann::json &) -> GGTB::RemoteControl::CmdResult {
            GGTB::StopPath();
            return {true, std::string{}};
        });

    // pathStatus: 读 pathTo 寻路进度。前端轮询用。
    // 返回 {"state":"idle|walking|arrived|failed", "x":当前X,"y":当前Y,
    //       "targetX":..,"targetY":..,"segments":已走段数,"error":失败原因}
    GGTB::RemoteControl::RegisterCommandHandler(
        "pathStatus",
        [](const nlohmann::json &) -> GGTB::RemoteControl::CmdResult {
            auto pr = GGTB::GetPathProgress();
            const char *st = "idle";
            switch (pr.state)
            {
            case GGTB::PathState::Walking: st = "walking"; break;
            case GGTB::PathState::Arrived: st = "arrived"; break;
            case GGTB::PathState::Failed:  st = "failed";  break;
            case GGTB::PathState::Idle:    st = "idle";    break;
            }
            nlohmann::json j;
            j["state"]    = st;
            j["x"]        = pr.curX;
            j["y"]        = pr.curY;
            j["targetX"]  = pr.targetX;
            j["targetY"]  = pr.targetY;
            j["segments"] = pr.segments;
            j["error"]    = pr.error;
            return {true, j.dump()};
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

    // warpTo: 直接传送到地图格坐标 (x, y) —— 发 CG 411597,复刻引擎街摊「传送到摊主」
    // 的发包(见 GGTB::WarpToCoordinate)。和 moveTo/pathTo 不同,这是瞬移不走路。
    // x/y 是整数地图格坐标;前端「填当前」走 getLocalPosition 取浮点再 round,所以这里
    // 收到的可能是带小数的数,统一 lround 成整数格再发。
    GGTB::RemoteControl::RegisterCommandHandler(
        "warpTo",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            if (!args.contains("x") || !args.contains("y"))
                return {false, "missing x/y"};
            int  x  = static_cast<int>(std::lround(args.value("x", 0.0)));
            int  y  = static_cast<int>(std::lround(args.value("y", 0.0)));
            bool ok = GGTB::WarpToCoordinate(x, y);
            nlohmann::json j{{"x", x}, {"y", y}};
            return {ok, ok ? j.dump() : std::string{"warp send failed (no local player / pattern unresolved)"}};
        });

    // setStationaryFarm: broker 自动复活页面「推送坐标到游戏」用。把定点挂机的锁定
    // 坐标设成 (x, y)。只写坐标,不动「启用」开关 —— 是否真锁位由用户在游戏内自己勾
    // (StationaryFarmModule::ApplyRemoteCoords 内有详细语义注释)。模块未注册/未实例
    // 化(理论上不会)返回 false。
    GGTB::RemoteControl::RegisterCommandHandler(
        "setStationaryFarm",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            if (!args.contains("x") || !args.contains("y"))
                return {false, "missing x/y"};
            int x = static_cast<int>(std::lround(args.value("x", 0.0)));
            int y = static_cast<int>(std::lround(args.value("y", 0.0)));
            bool ok = GGTB::StationaryFarmModule::ApplyRemoteCoords(x, y);
            if (!ok)
                return {false, "StationaryFarm module not ready"};
            nlohmann::json j{{"x", x}, {"y", y}};
            return {true, j.dump()};
        });

    // snapPath: 把一串「想走的」世界坐标途经点用引擎碰撞表校验一遍,返回校验后
    // 真正可走的途经点。给 broker 的同步「路径随机」用 —— broker 那边只会几何
    // 瞎画折线(它读不到地图),途经点可能落在墙/水/山里;这里逐段沿射线步进
    // (RaycastFurthestWalkable,内部走 Map__IsBlocked 碰撞表)把每个途经点吸附
    // 到「从上一个可达点出发、沿该方向最远仍可走的 tile」,落在障碍后的点自然被
    // 拉回到障碍前。终点不在这里裁剪 —— 终点是主角色站过的真实坐标,必然可达,
    // 由调用方自己附在末尾,交给引擎 A* 去精确寻路。
    //
    // args: { "startX":.., "startY":..,             # 起点(副角色当前坐标)
    //         "points": [ {"x":..,"y":..}, ... ] }  # 想走的途经点(不含起点/终点)
    // 返回: { "points": [ {"x":..,"y":..}, ... ] }  # 校验后的可走途经点(tile 中心)
    //        校验失败(地图没就绪)返回 {ok:false},调用方回退成直接走终点。
    GGTB::RemoteControl::RegisterCommandHandler(
        "snapPath",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            if (!args.contains("points") || !args["points"].is_array())
                return {false, "missing points[]"};

            int curX = static_cast<int>(std::floor(args.value("startX", 0.0f)));
            int curY = static_cast<int>(std::floor(args.value("startY", 0.0f)));
            // 起点都不可走(切图瞬时撕裂 / 没读到地图)就让调用方回退。
            if (!GGTB::IsTileWalkable(curX, curY))
                return {false, "start tile not walkable (map not ready?)"};

            nlohmann::json out = nlohmann::json::array();
            for (const auto &p : args["points"])
            {
                int wantX = static_cast<int>(std::floor(p.value("x", 0.0f)));
                int wantY = static_cast<int>(std::floor(p.value("y", 0.0f)));
                int reachX = curX, reachY = curY;
                // 从「当前可达点」沿射线步进到目标途经点,取最远仍可走的 tile。
                if (!GGTB::RaycastFurthestWalkable(curX, curY, wantX, wantY, reachX, reachY))
                    continue; // 这段连一步都走不了,丢弃该途经点
                // 原地没动(目标就是当前 tile,或第一步就被挡)不产生途经点,
                // 免得给引擎发一串重复点。
                if (reachX == curX && reachY == curY)
                    continue;
                out.push_back({{"x", static_cast<float>(reachX) + 0.5f},
                               {"y", static_cast<float>(reachY) + 0.5f}});
                curX = reachX;
                curY = reachY;
            }
            nlohmann::json j;
            j["points"] = out;
            return {true, j.dump()};
        });

    // talkOrAttack: mirror the real world-click dispatcher. Queue action=5 for
    // an NPC or action=3 for a monster; the game loop opens the dialog or starts
    // combat on its own thread after reaching interaction range.
    GGTB::RemoteControl::RegisterCommandHandler(
        "talkOrAttack",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            uint32_t creatureId = args.value("creatureId", uint32_t{0});
            if (creatureId == 0)
                return {false, "missing creatureId"};
            std::string error;
            bool ok = GGTB::TalkOrAttackOnGameThread(creatureId, &error);
            return {ok, ok ? std::string{} : error};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "selectMonsterForAttack",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            uint32_t creatureId = args.value("creatureId", uint32_t{0});
            if (creatureId == 0)
                return {false, "missing creatureId"};
            GGTB::MonsterTargetSelectionResult selection;
            std::string error;
            if (!GGTB::SelectMonsterForAttack(creatureId, selection, &error))
                return {false, error};
            nlohmann::json detail{
                {"creatureId", selection.creatureId},
                {"creatureType", selection.creatureType},
                {"hp", selection.hp},
                {"targetSelected", selection.targetSelected},
                {"attackQueued", selection.attackQueued},
            };
            return {true, detail.dump()};
        });

    // 兼容老 talkToNpc handler -> 内部转发到 talkOrAttack。
    GGTB::RemoteControl::RegisterCommandHandler(
        "talkToNpc",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            uint32_t npcId = args.value("npcId", uint32_t{0});
            if (npcId == 0)
                return {false, "missing npcId"};
            std::string error;
            bool ok = GGTB::TalkOrAttackOnGameThread(npcId, &error);
            return {ok, ok ? std::string{} : error};
        });

    // 列出附近所有 NPC + 怪物。attackable 保留现有攻击流程语义；
    // buffGateMonster 是独立的普通敌怪判定,只供 buff keeper 闸门使用。
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
                    {"isNpc",        n.isNpc},       // type==3 —— 对话 vs 攻击
                    {"attackable",   n.attackable},  // legacy attack-selection verdict
                    {"buffGateMonster", n.buffGateMonster}, // conservative ordinary enemy
                    {"level",        n.level},
                    {"distance",     n.distance},
                    {"x",            n.x},
                    {"y",            n.y},
                    {"z",            n.z},
                    {"hp",           n.hp},
                    {"maxHp",        n.maxHp},
                    {"name",         n.name},
                });
            }
            return {true, arr.dump()};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "getNearbyPlayers",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            float maxDist = args.value("maxDistance", 600.0f);
            auto players = GGTB::GetAroundPlayers(GGTB::GetLocalPlayerName(), maxDist);
            std::sort(players.begin(), players.end(),
                      [](const GGTB::NearbyPlayer &a, const GGTB::NearbyPlayer &b) {
                          return a.distance < b.distance;
                      });

            nlohmann::json arr = nlohmann::json::array();
            for (const auto &p : players)
            {
                arr.push_back({
                    {"name",           p.name},
                    {"profession",     p.profession},
                    {"professionName", p.professionName},
                    {"distance",       p.distance},
                    {"x",              p.x},
                    {"y",              p.y},
                    {"z",              p.z},
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
                    {"opt",   o.opt},
                    {"warpKey", o.warpKey},
                    {"warpTableId", o.warpTableId},
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
            auto dialog = GGTB::GetDialogSnapshot();
            auto selected = std::find_if(
                dialog.options.begin(), dialog.options.end(),
                [option](const GGTB::DialogOption &entry) { return entry.index == option; });
            if (selected != dialog.options.end() && selected->opt == kDungeonEntryTargetOpt)
                return {false, "opt=9575 is reserved for the targeted dungeon entry command"};
            bool ok = GGTB::SelectDialogOption(option);
            return {ok, ok ? std::string{} : std::string{"no dialog open or send failed"}};
        });

    // confirmDialog: mode==1(单按钮确认)的快捷键,等价于 selectDialogOption(0)。
    GGTB::RemoteControl::RegisterCommandHandler(
        "confirmDialog",
        [](const nlohmann::json &) -> GGTB::RemoteControl::CmdResult {
            auto dialog = GGTB::GetDialogSnapshot();
            if (!dialog.options.empty() && dialog.options.front().opt == kDungeonEntryTargetOpt)
                return {false, "opt=9575 is reserved for the targeted dungeon entry command"};
            bool ok = GGTB::ConfirmDialog();
            return {ok, ok ? std::string{} : std::string{"not in confirm mode"}};
        });

    // --- AutoRevive: 死亡 → 寻路 → 选项 ---
    //
    // 状态查询(web 用来高亮按钮 + 显示当前地图):
    //   getStatus -> { hp, isDead, mapId, userId, posX, posY, posZ }
    // 操作:
    //   reviveToTown -> 模拟「死亡弹框点 OK」,发 411170 让 server 把你 warp 回主城。
    // 其它步骤(MoveTo / TalkOrAttack / SelectDialogOption)已经存在 — web 端
    // 直接组合调用这些命令 + setTimeout 实现「复活脚本」。

    GGTB::RemoteControl::RegisterCommandHandler(
        "getStatus",
        [](const nlohmann::json &) -> GGTB::RemoteControl::CmdResult {
            nlohmann::json j;
            int64_t hp = 0;
            const bool hpKnown = GGTB::TryGetLocalHp(hp);
            const uint32_t userId = GGTB::GetLocalUserId();
            j["hp"]          = hpKnown ? hp : int64_t{-1};
            j["hpKnown"]     = hpKnown;
            j["playerReady"] = hpKnown && userId != 0;
            j["isDead"]      = hpKnown && hp == 0;
            j["clientClosing"] = GGTB::DisconnectWatchdog::IsDisconnectCloseTriggered();
            j["worldRecvIdleMs"] = GGTB::DisconnectWatchdog::GetWorldRecvIdleMs();
            j["mapId"]  = GGTB::GetCurrentMapId();
            j["userId"] = userId;
            // 坐标:broker 端「位置卡住自动复活」要靠它判定 X 分钟没移动。
            // GetLocalPosition 内部处理空指针,解析不出来就保持 0。
            float px = 0, py = 0, pz = 0;
            GGTB::GetLocalPosition(px, py, pz);
            j["posX"] = px;
            j["posY"] = py;
            j["posZ"] = pz;
            return {true, j.dump()};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "reviveToTown",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            bool safety = args.value("safetyCheck", true);
            int  mode   = args.value("mode", 1);
            bool ok = GGTB::ReviveToTown(safety, mode);
            return {ok, ok ? std::string{} : std::string{"send failed"}};
        });

    auto levelUpHandler =
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            int payload = args.value("payload", 412016);
            bool ok = GGTB::RequestLevelUpCheck(payload);
            return {ok, ok ? std::string{"sent"} : std::string{"send failed"}};
        };

    GGTB::RemoteControl::RegisterCommandHandler("requestLevelUp", levelUpHandler);
    GGTB::RemoteControl::RegisterCommandHandler("levelUpCheck", levelUpHandler);

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
            uint32_t sub   = args.value("sub",    uint32_t{1});
            if (npcId == 0)
                return {false, "npcId required (>0)"};
            if (opt == kDungeonEntryTargetOpt)
                return {false, "raw opt=9575 is reserved for the targeted dungeon entry command"};
            bool ok = GGTB::SendDialogSelect(npcId, opt, sub);
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

    // queryBuffs: 返回本地玩家当前 buff 快照。web 端轮询用来判断「缺某个 buffId / name」。
    // 参数: { kind: -1=all(默认) | 0=Normal | 1=Cash }
    // 返回 detail = JSON 数组,每项 { buffId, name, kind, category, durationMs,
    //              remainingMs(-1=无倒计时), skillId, isBoolBuff }。
    GGTB::RemoteControl::RegisterCommandHandler(
        "queryBuffs",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            int  kindFilter = args.value("kind", -1);
            auto buffs      = GGTB::GetActiveBuffs(kindFilter);
            auto arr        = nlohmann::json::array();
            for (const auto &b : buffs)
            {
                nlohmann::json j;
                j["buffId"]      = b.buffId;
                j["name"]        = b.name;
                j["kind"]        = (b.kind == GGTB::BuffKind::Cash) ? "cash" : "normal";
                j["category"]    = b.category;
                j["durationMs"]  = b.duration;
                j["remainingMs"] = b.remainingMs;
                j["skillId"]     = b.skillId;
                j["isBoolBuff"]  = b.isBoolBuff;
                arr.push_back(std::move(j));
            }
            return {true, arr.dump()};
        });

    // queryParty: 返回本地玩家所在组队的成员快照(含每人 buff)。web 端轮询用来实时
    // 显示队员列表 + 队友 buff 状态。
    // 参数: { buffs: true=带每人 buff(默认) | false=只要名单/血量 }
    // 返回 detail = JSON 对象:
    //   { inParty, role, selfIndex,
    //     members: [ { index, name, userId, isSelf, online, hp, maxHp,
    //                  nearby, distance,
    //                  buffs: [ {buffId,name,kind,category,durationMs,
    //                           remainingMs,skillId,isBoolBuff} ] } ] }
    // nearby/distance 来自 around-player AOI 反查:nearby=false 表示该队友不在视野
    // (太远/不同图),远程放 buff 到不了 — 组队 buff 守护据此跳过。自身 nearby=true。
    // 队友 buff 走 hostType=2(引擎自己的队伍 UI 同 key);自己走 hostType=0。
    GGTB::RemoteControl::RegisterCommandHandler(
        "queryParty",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            bool withBuffs = args.value("buffs", true);
            auto snap      = GGTB::GetPartyMembers(withBuffs);

            nlohmann::json root;
            root["inParty"]   = snap.inParty;
            root["role"]      = snap.role;
            root["selfIndex"] = snap.selfIndex;

            auto members = nlohmann::json::array();
            for (const auto &m : snap.members)
            {
                nlohmann::json jm;
                jm["index"]  = m.index;
                jm["name"]   = m.name;
                jm["userId"] = m.userId;
                jm["isSelf"] = m.isSelf;
                jm["online"] = m.online;
                jm["hp"]     = m.hp;
                jm["maxHp"]  = m.maxHp;
                jm["nearby"]   = m.nearby;
                jm["distance"] = m.distance;

                auto buffs = nlohmann::json::array();
                for (const auto &b : m.buffs)
                {
                    nlohmann::json jb;
                    jb["buffId"]      = b.buffId;
                    jb["name"]        = b.name;
                    jb["kind"]        = (b.kind == GGTB::BuffKind::Cash) ? "cash" : "normal";
                    jb["category"]    = b.category;
                    jb["durationMs"]  = b.duration;
                    jb["remainingMs"] = b.remainingMs;
                    jb["skillId"]     = b.skillId;
                    jb["isBoolBuff"]  = b.isBoolBuff;
                    buffs.push_back(std::move(jb));
                }
                jm["buffs"] = std::move(buffs);
                members.push_back(std::move(jm));
            }
            root["members"] = std::move(members);
            return {true, root.dump()};
        });

    // castSkill: 走引擎自己的 Combat__TryUseSkill 完整状态机释放技能(自 buff / 单体 / AOE 均可)。
    // 参数: { skillId(必填,>0), targetId=0(0=自身/无目标), checkCanCast=true }
    // checkCanCast=true 会先读 CSkill 的 learned/cooldown 字段做预检,避免冷却中刷包。
    GGTB::RemoteControl::RegisterCommandHandler(
        "castSkill",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            uint32_t skillId      = args.value("skillId",      uint32_t{0});
            uint32_t targetId     = args.value("targetId",     uint32_t{0});
            bool     checkCanCast = args.value("checkCanCast", true);
            if (skillId == 0)
                return {false, "skillId required (>0)"};
            bool ok = GGTB::CastSkillById(skillId, targetId, checkCanCast);
            if (!ok)
                return {false, "cast rejected (not learned / on cooldown / SEH)"};
            return {true, {}};
        });

    // 自动交易: 配置全由 broker 下发,所有角色共用一份。交易接受→锁定→确认的 lock-step
    // 状态机仍留在 DLL(recv 触发,管道往返跟不上),broker 只管开关 / 白名单 / 延时。
    GGTB::RemoteControl::RegisterCommandHandler(
        "setAutoTradeConfig",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            GGTB::AutoTradeController::Instance().SetConfig(args);
            return {true, GGTB::AutoTradeController::Instance().GetStatus().dump()};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "getAutoTradeStatus",
        [](const nlohmann::json &) -> GGTB::RemoteControl::CmdResult {
            return {true, GGTB::AutoTradeController::Instance().GetStatus().dump()};
        });

    // 自动拾取过滤: 配置全由 broker 下发,所有角色共用一份(mode + itemId 白名单)。
    // AutoPickupModule 的 worker 每轮发拾取包前查 PickupFilter::ShouldPick。
    GGTB::RemoteControl::RegisterCommandHandler(
        "setPickupFilter",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            GGTB::PickupFilter::Instance().SetConfig(args);
            return {true, GGTB::PickupFilter::Instance().GetStatus().dump()};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "getPickupFilterStatus",
        [](const nlohmann::json &) -> GGTB::RemoteControl::CmdResult {
            return {true, GGTB::PickupFilter::Instance().GetStatus().dump()};
        });

    // 工作流拾取只读快照: 不直接发拾取包，只暴露当前 AOI 掉落坐标以及与
    // AutoPickup 完全相同的 live 过滤结果。maxDistance<=0 表示当前可见 AOI 全部。
    GGTB::RemoteControl::RegisterCommandHandler(
        "getNearbyDropItems",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            float maxDist          = args.value("maxDistance", 0.0f);
            bool includeUnpickable = args.value("includeUnpickable", false);
            bool onlyMatches       = args.value("onlyFilterMatches", false);
            int  limit             = (std::max)(1, (std::min)(args.value("limit", 512), 2048));

            bool ready = false;
            auto drops = GGTB::GetNearbyDropItems(maxDist, includeUnpickable, &ready);
            if (!ready)
                return {false, "drop container or local position not ready"};

            nlohmann::json arr = nlohmann::json::array();
            for (const auto &drop : drops)
            {
                bool filterMatch = GGTB::PickupFilter::Instance().ShouldPick(drop.itemId);
                if (onlyMatches && !filterMatch)
                    continue;
                arr.push_back({
                    {"dropId",                  drop.dropId},
                    {"itemId",                  drop.itemId},
                    {"distance",                drop.distance},
                    {"x",                       drop.x},
                    {"y",                       drop.y},
                    {"z",                       drop.z},
                    {"canPick",                 drop.canPick},
                    {"matchesPickupFilter",     filterMatch},
                });
                if (static_cast<int>(arr.size()) >= limit)
                    break;
            }
            return {true, arr.dump()};
        });

    // 精确 dropId 状态。Missing 只来自引擎 LookupDropItemById 的空结果，不能被
    // 掉落链表中途读取失败伪造成“已拾取”。
    GGTB::RemoteControl::RegisterCommandHandler(
        "getDropItemState",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            uint32_t dropId = args.value("dropId", uint32_t{0});
            if (!dropId)
                return {false, "missing dropId"};

            GGTB::DropItemInfo drop{};
            auto state = GGTB::QueryDropItemById(dropId, drop);
            if (state == GGTB::DropItemLookupState::Unavailable)
                return {false, "exact drop lookup unavailable"};

            nlohmann::json detail{
                {"dropId", dropId},
                {"present", state == GGTB::DropItemLookupState::Present},
            };
            if (state == GGTB::DropItemLookupState::Present)
            {
                detail.update({
                    {"itemId", drop.itemId},
                    {"distance", drop.distance},
                    {"x", drop.x},
                    {"y", drop.y},
                    {"z", drop.z},
                    {"canPick", drop.canPick},
                    {"matchesPickupFilter",
                     GGTB::PickupFilter::Instance().ShouldPick(drop.itemId)},
                });
            }
            return {true, detail.dump()};
        });

    // 游戏原生掉落点击。只在 GameLoop 选择一次 action=4；是否拾取成功由调用方
    // 继续观察精确 dropId，绝不把 SetAfterAction 的返回值当作入包确认。
    GGTB::RemoteControl::RegisterCommandHandler(
        "selectDropForPickup",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            uint32_t dropId = args.value("dropId", uint32_t{0});
            uint32_t itemId = args.value("expectedItemId", uint32_t{0});
            uint32_t mapId = args.value("expectedMapId", uint32_t{0});
            float maxDistance = args.value("maxDistance", 6.0f);
            if (!dropId || !itemId || !mapId)
                return {false, "dropId, expectedItemId, and expectedMapId are required"};
            maxDistance = (std::max)(0.5f, (std::min)(maxDistance, 50.0f));

            auto result = GGTB::SelectDropForPickupOnGameThread(
                dropId, itemId, mapId, maxDistance);
            nlohmann::json detail{
                {"ok", result.ok},
                {"alreadyGone", result.alreadyGone},
                {"actionQueued", result.actionQueued},
                {"executionAmbiguous", result.executionAmbiguous},
                {"dropId", result.dropId},
                {"itemId", result.itemId},
                {"distance", result.distance},
                {"error", result.error},
            };
            return {true, detail.dump()};
        });

    // 附近玩家停手 (NPG): 开关 + 保持停手时长全由 broker 下发,所有角色共用一份。
    // 模块在下方 RegisterModule 时才创建,所以 handler 里判空(早期连上可能还没就绪,
    // 返回 ok=false 让 broker 重推)。
    GGTB::RemoteControl::RegisterCommandHandler(
        "setNpgConfig",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            auto *npg = g_npgModule.load();
            if (!npg)
                return {false, "NPG module not ready"};
            bool enabled     = args.value("enabled", true);
            int  holdSeconds = args.value("holdSeconds", 300);
            npg->ApplyRemoteConfig(enabled, holdSeconds);
            return {true, npg->RemoteStatus().dump()};
        });

    GGTB::RemoteControl::RegisterCommandHandler(
        "getNpgConfig",
        [](const nlohmann::json &) -> GGTB::RemoteControl::CmdResult {
            auto *npg = g_npgModule.load();
            if (!npg)
                return {false, "NPG module not ready"};
            return {true, npg->RemoteStatus().dump()};
        });

    // ---------- 组队列表 / 自动组队 (auto-team) ----------
    // getPartyList: 本角色打开组队列表(本地开窗+发 0x4400/0x4402),抓全所有页返回。
    //   web「刷新队伍列表」用一个在线角色拉一次,展示创建人/分配/人数。同步在命令线程跑
    //   (内部 ~秒级轮询等回流),broker 侧给足 timeout。
    GGTB::RemoteControl::RegisterCommandHandler(
        "getPartyList",
        [](const nlohmann::json &) -> GGTB::RemoteControl::CmdResult {
            GGTB::PartyBoardSnapshot snap = GGTB::FetchPartyBoard();
            nlohmann::json o;
            o["ok"]      = snap.ok;
            o["curPage"] = snap.curPage;
            o["maxPage"] = snap.maxPage;
            nlohmann::json arr = nlohmann::json::array();
            for (const auto &e : snap.entries)
            {
                nlohmann::json je;
                je["partyId"]      = e.partyId;
                je["distribution"] = e.distribution;
                je["curMembers"]   = e.curMembers;
                je["maxMembers"]   = e.maxMembers;
                je["mapId"]        = e.mapId;
                je["partyName"]    = e.partyName;
                je["leaderName"]   = e.leaderName;
                arr.push_back(std::move(je));
            }
            o["parties"] = std::move(arr);
            return {true, o.dump()};
        });

    // joinPartyByLeader { leaderName }: 抓列表 -> 找创建人(队长)名匹配的队伍 -> 发加入包。
    //   web「加入」对每个选中角色各下发一次。也支持直接 { partyId } 跳过名字解析。
    GGTB::RemoteControl::RegisterCommandHandler(
        "joinPartyByLeader",
        [](const nlohmann::json &args) -> GGTB::RemoteControl::CmdResult {
            uint32_t partyId = JsonU32Arg(args, "partyId", 0);
            if (partyId != 0)
            {
                bool ok = GGTB::SendJoinPartyById(partyId);
                nlohmann::json o;
                o["ok"]      = ok;
                o["partyId"] = partyId;
                return {ok, o.dump()};
            }

            std::string leaderName = args.value("leaderName", std::string{});
            uint32_t    joinedId   = 0;
            std::string err;
            bool ok = GGTB::JoinPartyByLeaderName(leaderName, &joinedId, &err);
            nlohmann::json o;
            o["ok"]         = ok;
            o["leaderName"] = leaderName;
            o["partyId"]    = joinedId;
            o["error"]      = err;
            return {ok, o.dump()};
        });

    // 会落空。InputInjector::Install 跟 RemoteControl::Install 顺序无强依赖 — 注入
    // 器只 hook user32 API,不依赖 broker / pipe / 引擎符号。
    GGTB::InputInjector::Install();
    GGTB::RemoteControl::Install();
    // 自动交易 worker 独立于 Setting / 模块系统(broker 远控,无 ImGui)。
    GGTB::AutoTradeController::Instance().Start();

    auto setting = std::make_shared<GGTB::Setting>();
    setting->RegisterModule(std::make_shared<GGTB::StatusModule>());
    setting->RegisterModule(std::make_shared<GGTB::MoveSpeedModule>());
    setting->RegisterModule(std::make_shared<GGTB::AttackSpeedModule>());
    setting->RegisterModule(std::make_shared<GGTB::SkillSpeedModule>());
    setting->RegisterModule(std::make_shared<GGTB::SpeedHackModule>());
    setting->RegisterModule(std::make_shared<GGTB::ItemNoCDModule>());
    setting->RegisterModule(std::make_shared<GGTB::AttackRangeModule>());
    setting->RegisterModule(std::make_shared<GGTB::AttackMoveModule>());
    setting->RegisterModule(std::make_shared<GGTB::SkillMoveModule>());
    setting->RegisterModule(std::make_shared<GGTB::NoShiftAttackModule>());
    setting->RegisterModule(std::make_shared<GGTB::IgnoreCCModule>());
    // 点击瞬移 / 穿墙:暂时隐藏并关闭(不注册 = UI 不显示、不加载配置、hook 永不安装)。
    // 需要时取消下面两行注释即可恢复。
    // setting->RegisterModule(std::make_shared<GGTB::TeleportModule>());
    // setting->RegisterModule(std::make_shared<GGTB::WallHackModule>());
    setting->RegisterModule(std::make_shared<GGTB::AutoPickupModule>());
    setting->RegisterModule(std::make_shared<GGTB::AutoDecomposeGemModule>());
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
    // 城镇传送(樂園鎮/獅子城):进「传送」标签页,原右上角常驻按钮已移除。
    setting->RegisterModule(std::make_shared<GGTB::TeleportTownModule>());
    setting->RegisterModule(std::make_shared<GGTB::AutoPathModule>());
    // 定点移动:Ctrl+K 设当前坐标为定点,偏离就自动寻路走回。CanAutoPause=false 不受 NPG 影响。
    auto fixedMoveMod = std::make_shared<GGTB::FixedPointMoveModule>();
    setting->RegisterModule(fixedMoveMod);
    setting->RegisterModule(std::make_shared<GGTB::WarpCoordModule>());
    // Guard registered LAST so it sees every other module via Setting::GetModules().
    // ESP 在 Guard 之后注册,这样 ESP 显示在 NPG 下方,且 CanAutoPause=false 不受 NPG 影响。
    auto espMod = std::make_shared<GGTB::PlayerESPModule>();
    auto npgMod = std::make_shared<GGTB::NearbyPlayerGuardModule>(setting.get(), espMod.get());
    setting->RegisterModule(npgMod);
    g_npgModule.store(npgMod.get());
    setting->RegisterModule(espMod);

    // 打开最近摆摊:勾选后 Ctrl+L 打开最近摊位(热键在下方 HackThread 泵里处理)。
    auto openStallMod = std::make_shared<GGTB::OpenNearbyStallModule>();
    setting->RegisterModule(openStallMod);

    // 周围怪物列表:只读内存,独占「怪物」标签页。
    setting->RegisterModule(std::make_shared<GGTB::MonsterListModule>());

    try
    {
        zzj::D3D::D3D9Hook::SetupOptions menuOptions{};
        menuOptions.windowClassName = "GGTB.ExternalMenu";
        menuOptions.windowName = "GGThreadBlock";
        menuOptions.width = 840;
        menuOptions.height = 520;
        zzj::D3D::D3D9Hook::Setup(setting, menuOptions);

        DWORD lastTickMs = GetTickCount();
        bool  endWasDown = false;
        bool  fireWasDown = false;
        bool  menuWasDown = false;
        bool  ctrlLWasDown = false;
        bool  ctrlKWasDown = false;
        const DWORD menuToggleKey = setting->GetToggleMenuKey();

        while (true)
        {
            // GGThreadBlock owns hotkey policy. The generic D3D9 menu only
            // exposes ToggleOpen/SetOpen and does not know which key toggles it.
            const bool focused  = HostWindowHasFocus();
            const bool endDown  = IsKeyDown(VK_END);
            const bool fireDown = IsKeyDown('N');
            const bool menuDown = IsKeyDown(menuToggleKey);
            const bool ctrlLDown = IsKeyDown(VK_CONTROL) && IsKeyDown('L');
            const bool ctrlKDown = IsKeyDown(VK_CONTROL) && IsKeyDown('K');
            const bool remoteUnload =
                InterlockedCompareExchange(&g_remoteUnloadRequested, 0, 0) != 0;

            if ((endDown && !endWasDown && focused) || remoteUnload)
                break;

            if (menuDown && !menuWasDown && focused)
                zzj::D3D::D3D9Hook::ToggleOpen();

            // N: toggle 多重施法 (MultiCast). Skip while NPG has us paused —
            // otherwise the user re-enabling mid-guard would re-install the
            // hook and leak the packet burst to the nearby player (same reason
            // the checkbox is disabled in the UI while guarded).
            if (fireDown && !fireWasDown && focused && multiCastMod &&
                !multiCastMod->IsPausedByGuard())
            {
                const bool wasEnabled = multiCastMod->IsEnabled();
                multiCastMod->SetEnabled(!wasEnabled);
                if (!wasEnabled)
                    multiCastMod->OnResume();
                else
                    multiCastMod->OnShutdown();
                // 不再 MarkDirty:模块状态只在点「保存」时落盘(仅点保存时写入)。
                // 这次热键切换会让 UI 显示"有未保存改动",由用户决定是否保存。
                const bool nowOn = multiCastMod->IsEnabled();
                GGTB::ScreenOverlay::ShowToast(
                    nowOn ? u8"多重施法：开" : u8"多重施法：关", 3000);
                spdlog::info("GGTB: hotkey N -> MultiCast {}", nowOn ? "ON" : "OFF");
            }

            // Ctrl+L: 打开最近的摆摊玩家店铺。OpenNearest 自身用勾选框 gate
            // (未勾选不动作) 并弹自己的 toast,所以这里只做边沿 + 焦点判断。
            if (ctrlLDown && !ctrlLWasDown && focused && openStallMod)
                openStallMod->OpenNearest();

            // Ctrl+K: toggle 定点移动。CanAutoPause=false,不受 NPG 影响,也不需要
            // gate IsPausedByGuard(NPG 永远不会拍停它)。Toggle 内部自己弹 toast。
            if (ctrlKDown && !ctrlKWasDown && focused && fixedMoveMod)
                fixedMoveMod->Toggle();

            endWasDown = endDown;
            fireWasDown = fireDown;
            menuWasDown = menuDown;
            ctrlLWasDown = ctrlLDown;
            ctrlKWasDown = ctrlKDown;

            const DWORD now = GetTickCount();
            if (now - lastTickMs >= 100)
            {
                lastTickMs = now;
                GGTB::UserConfig::Tick(setting.get());
            }

            Sleep(10);
        }
    }
    catch (const std::exception &e)
    {
        spdlog::error("GGTB HackThread exception: {}", e.what());
        MessageBoxA(nullptr, e.what(), "GGThreadBlock Error", MB_OK | MB_ICONERROR);
    }

    // Flush per-character meta (active profile + whitelist) so a pending
    // debounced write isn't lost on exit. Module states are NOT saved here —
    // they only reach disk on an explicit「保存」(SaveProfile); exit never
    // auto-persists them (仅点保存时写入).
    GGTB::UserConfig::FlushConfigMeta(setting.get());

    setting->End();
    // Detach in reverse install order. HwFpSpoof and NetLog no longer hold
    // DeleteFileA trampolines themselves — they just own their stage-2
    // detours. Stage1Trigger goes last so any late callback (shouldn't
    // happen after End() but kept for symmetry) has something to run on.
    // RemoteControl 早于 NetLog 卸,让 best-effort bye 帧的 send 走正常 socket。
    GGTB::LoginBridge::Uninstall();
    // Stop accepting before RemoteControl joins its IO thread. Any queued waiter
    // is cancelled now; an already executing GameLoop click remains one-shot.
    GGTB::StopNativeUiBridge();
    // 寻路 worker 先停(join) —— 它会调 MoveTo/读 g_pLocalUser,必须在卸载引擎相关
    // 钩子和 D3D9Hook::Destroy 之前停掉,否则卸载时 worker 还在跑引擎代码会崩。
    GGTB::PathWalkerShutdown();
    // 自动交易 worker 先停(join),之后 NetLog hook 卸了它也不会再轮询到。
    GGTB::AutoTradeController::Instance().Stop();
    GGTB::RemoteControl::Uninstall();
    bool nativeUiDetached = GGTB::UninstallNativeUiBridge();
    // No command can enter after RemoteControl stops. Detach the scoped/global
    // dialog hooks before NetLog removes the protocol observer they consult.
    GGTB::AutoConfirmDetail::UninstallHooks();
    // handler 已经摘掉,不会再有人通过裸指针访问 NPG 模块,置空。
    g_npgModule.store(nullptr);
    // InputInjector 卸在 RemoteControl 之后 — 不会再有命令进来要注入了。
    GGTB::InputInjector::Uninstall();
    GGTB::DisconnectWatchdog::Uninstall();
    GGTB::HwFpSpoof::Uninstall();
    GGTB::NetLog::Uninstall();
    GGTB::Stage1Trigger::Uninstall();
    GGTB::SpeedHack::Shutdown();
    zzj::D3D::D3D9Hook::Destroy();
    setting.reset();

    if (nativeUiDetached)
        spdlog::info("GGThreadBlock unloaded");
    else
        spdlog::critical(
            "GGThreadBlock remains resident because native UI hooks could not be safely drained");
    spdlog::shutdown();

    if (!nativeUiDetached) return 0;
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
        GGTB::RequestNativeUiBridgeStopNoWait();
        UninstallDetour();
        break;
    }
    return TRUE;
}
