#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <deque>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>

// 包含 SDL3 官方头文件（必须在 detours 之前）
#include <SDL3/SDL.h>

#include <detours.h>

#pragma warning(push)
#pragma warning(disable: 4819)
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#pragma warning(pop)

// ============================================================================
// 按键映射表（方便配置）
// ============================================================================
struct KeyInfo {
    const char* name;   // 人类可读的名字（例如 "Q" / "SPACE" / "1"）
};

// 常用按键名（只存 name，其他值在初始化时 resolve）
static const KeyInfo KEY_Q     = {"Q"};
static const KeyInfo KEY_W     = {"W"};
static const KeyInfo KEY_E     = {"E"};
static const KeyInfo KEY_R     = {"R"};
static const KeyInfo KEY_A     = {"A"};
static const KeyInfo KEY_S     = {"S"};
static const KeyInfo KEY_D     = {"D"};
static const KeyInfo KEY_F     = {"F"};
static const KeyInfo KEY_B     = {"B"};
static const KeyInfo KEY_Z     = {"Z"};
static const KeyInfo KEY_X     = {"X"};
static const KeyInfo KEY_C     = {"C"};
static const KeyInfo KEY_V     = {"V"};
static const KeyInfo KEY_SPACE = {"SPACE"};
static const KeyInfo KEY_1     = {"1"};
static const KeyInfo KEY_2     = {"2"};
static const KeyInfo KEY_3     = {"3"};
static const KeyInfo KEY_4     = {"4"};
static const KeyInfo KEY_5     = {"5"};

struct ResolvedKey {
    SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
    SDL_Keycode key = 0;
    uint16_t raw = 0;          // Windows hardware scan code（可能为 0）
};

static uint16_t LookupWinRawScanCodeFromName(const char* name)
{
    if (!name) return 0;
    struct Entry { const char* n; uint16_t raw; };
    static const Entry kMap[] = {
        {"Q", 0x10}, {"W", 0x11}, {"E", 0x12}, {"R", 0x13},
        {"A", 0x1E}, {"S", 0x1F}, {"D", 0x20}, {"F", 0x21},
        {"B", 0x30}, {"Z", 0x2C}, {"X", 0x2D}, {"C", 0x2E}, {"V", 0x2F},
        {"SPACE", 0x39},
        {"1", 0x02}, {"2", 0x03}, {"3", 0x04}, {"4", 0x05}, {"5", 0x06},
    };
    for (const auto& e : kMap)
    {
        if (_stricmp(name, e.n) == 0)
            return e.raw;
    }
    return 0;
}

static const char* NormalizeKeyNameForSDL(const char* name)
{
    if (!name) return "";
    // SDL 的名字通常是 "Space" 这种；这里做几个常见兼容
    if (_stricmp(name, "SPACE") == 0) return "Space";
    return name;
}

static ResolvedKey ResolveKeyInfo(const KeyInfo& info)
{
    ResolvedKey out{};
    const char* name = info.name ? info.name : "";
    out.raw = LookupWinRawScanCodeFromName(name);

    const char* sdlName = NormalizeKeyNameForSDL(name);
    out.scancode = SDL_GetScancodeFromName(sdlName);
    if (out.scancode == SDL_SCANCODE_UNKNOWN)
    {
        // 兜底：尝试用 key 名字反查 scancode
        out.key = SDL_GetKeyFromName(sdlName);
        if (out.key != 0)
        {
            SDL_Keymod tmp = (SDL_Keymod)0;
            out.scancode = SDL_GetScancodeFromKey(out.key, &tmp);
        }
    }

    if (out.key == 0)
    {
        if (out.scancode != SDL_SCANCODE_UNKNOWN)
            out.key = SDL_GetKeyFromScancode(out.scancode, (SDL_Keymod)0, true /*key_event*/);
        else
            out.key = SDL_GetKeyFromName(sdlName);
    }

    return out;
}

// ============================================================================
// SDL 函数指针类型（使用 SDL 官方类型）
// ============================================================================
typedef int (SDLCALL *SDL_PeepEvents_t)(SDL_Event* events, int numevents, SDL_EventAction action, Uint32 minType, Uint32 maxType);

// ============================================================================
// 全局变量
// ============================================================================
static HMODULE g_hModule = nullptr;
static HMODULE g_hSDL3 = nullptr;
static std::shared_ptr<spdlog::logger> g_logger = nullptr;
static volatile bool g_shouldExit = false;

// SDL 函数指针
static SDL_PeepEvents_t Real_SDL_PeepEvents = nullptr;

// ============================================================================
// 注入事件上下文（windowID/which/mod 等）
// 默认值保留旧行为；但会在 hook 运行时尽量从“真实事件”学习并更新，提高兼容性。
static std::atomic<uint32_t> g_injectWindowID{ 3u };
static std::atomic<uint32_t> g_injectKeyboardWhich{ 0u };
static std::atomic<uint32_t> g_injectMouseWhich{ 0u };
static std::atomic<uint32_t> g_injectKeyMod{ 0x9000u };

static inline SDL_WindowID InjectWindowID() { return (SDL_WindowID)g_injectWindowID.load(); }
static inline SDL_KeyboardID InjectKeyboardWhich() { return (SDL_KeyboardID)g_injectKeyboardWhich.load(); }
static inline SDL_MouseID InjectMouseWhich() { return (SDL_MouseID)g_injectMouseWhich.load(); }
static inline SDL_Keymod InjectKeyMod() { return (SDL_Keymod)g_injectKeyMod.load(); }

static inline Uint64 NowSdlTimestampNs()
{
    // SDL3 事件 timestamp 的语义就是 SDL_GetTicksNS() 的返回值
    return SDL_GetTicksNS();
}

// 注入事件标记：用于区分“真实输入”与“我们自己注入的事件”
static constexpr Uint32 kInjectedMagic = 0x4A4E4A53u; // 'SJNJ' 任意 magic，用于标记注入事件
static inline void MarkInjected(SDL_Event& e)
{
    e.common.reserved = kInjectedMagic;
}
static inline bool IsInjected(const SDL_Event& e)
{
    return e.common.reserved == kInjectedMagic;
}

// ============================================================================
// 命名管道输入：从客户端接收简单“数据结构”命令 -> 转成 SDL_Event -> 由 Hook 注入
// ============================================================================
static HANDLE g_pipeThread = nullptr;
static HANDLE g_pipeHandle = INVALID_HANDLE_VALUE;
static constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\redpass_sdl3_hook_input";

static std::mutex g_injectQueueMutex;
static std::deque<SDL_Event> g_injectQueue;

// ============================================================================
// SDL 输入拦截开关（只拦截“真实输入”，不拦截我们注入的事件）
// NOTE: this must be declared before PipeServerThread uses it.
// ============================================================================
static volatile bool g_blockRealInput = true;  // 注入后默认开启：屏蔽真实键盘/鼠标输入

static void EnqueueInjectedEvent(const SDL_Event& e)
{
    std::lock_guard<std::mutex> _g(g_injectQueueMutex);
    g_injectQueue.push_back(e);
}

static int DrainInjectedEvents(SDL_Event* outEvents, int outCapacity, int startIndex)
{
    if (!outEvents || outCapacity <= 0 || startIndex >= outCapacity)
        return startIndex;

    std::lock_guard<std::mutex> _g(g_injectQueueMutex);
    while (!g_injectQueue.empty() && startIndex < outCapacity)
    {
        outEvents[startIndex] = g_injectQueue.front();
        g_injectQueue.pop_front();
        startIndex++;
    }
    return startIndex;
}

#pragma pack(push, 1)
struct PipeMsgHeader {
    uint32_t magic;   // 'RPS1'
    uint16_t type;
    uint16_t size;    // payload bytes
};
#pragma pack(pop)

static constexpr uint32_t kPipeMagic = 0x31535052u; // 'R''P''S''1' little-endian

enum class PipeCmd : uint16_t {
    MouseLeftDown  = 1,  // payload: float x, float y
    MouseLeftUp    = 2,  // payload: float x, float y
    MouseLeftClick = 3,  // payload: float x, float y, uint32 interval_ms

    MouseMove       = 4, // payload: float x, float y, float xrel, float yrel, uint32 state
    MouseRightDown  = 5, // payload: float x, float y
    MouseRightUp    = 6, // payload: float x, float y
    MouseRightClick = 7, // payload: float x, float y, uint32 interval_ms
    MouseWheel      = 8, // payload: float wheel_x, float wheel_y, float mouse_x, float mouse_y, int32 direction

    SetBlockRealInput    = 100, // payload: uint8 block (0/1)
    ToggleBlockRealInput = 101, // payload: empty

    KeyDown  = 10,       // payload: uint8 name_len, char[name_len]
    KeyUp    = 11,       // payload: uint8 name_len, char[name_len]
    KeyPress = 12,       // payload: uint8 name_len, char[name_len], uint32 interval_ms
};

struct MouseXY {
    float x;
    float y;
};

struct MouseClickXYI {
    float x;
    float y;
    uint32_t interval_ms;
};

struct MouseMotionXYRelState {
    float x;
    float y;
    float xrel;
    float yrel;
    uint32_t state;
};

struct MouseWheelXYMouseDir {
    float wheel_x;
    float wheel_y;
    float mouse_x;
    float mouse_y;
    int32_t direction;
};

struct BlockFlag {
    uint8_t block;
};

static bool ReadExact(HANDLE h, void* buf, DWORD len)
{
    uint8_t* p = (uint8_t*)buf;
    DWORD got = 0;
    while (len > 0)
    {
        if (!ReadFile(h, p, len, &got, nullptr) || got == 0)
            return false;
        p += got;
        len -= got;
    }
    return true;
}

static ResolvedKey ResolveKeyName(const char* name)
{
    KeyInfo ki{ name };
    return ResolveKeyInfo(ki);
}

static SDL_Event MakeKeyEvent(bool down, const ResolvedKey& rk)
{
    SDL_Event e{};
    memset(&e, 0, sizeof(e));
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    MarkInjected(e);
    e.key.timestamp = NowSdlTimestampNs();
    e.key.windowID = InjectWindowID();
    e.key.which = InjectKeyboardWhich();
    e.key.scancode = rk.scancode;
    e.key.key = rk.key;
    e.key.mod = InjectKeyMod();
    e.key.raw = rk.raw;
    e.key.down = down;
    e.key.repeat = false;
    return e;
}

static SDL_Event MakeMouseButtonEvent(bool down, uint8_t button, uint8_t clicks, float x, float y)
{
    SDL_Event e{};
    memset(&e, 0, sizeof(e));
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    MarkInjected(e);
    e.button.timestamp = NowSdlTimestampNs();
    e.button.windowID = InjectWindowID();
    e.button.which = InjectMouseWhich();
    e.button.button = button;
    e.button.down = down;
    e.button.clicks = clicks;
    e.button.x = x;
    e.button.y = y;
    return e;
}

static SDL_Event MakeMouseMotionEvent(float x, float y, float xrel, float yrel, uint32_t state)
{
    SDL_Event e{};
    memset(&e, 0, sizeof(e));
    e.type = SDL_EVENT_MOUSE_MOTION;
    MarkInjected(e);
    e.motion.timestamp = NowSdlTimestampNs();
    e.motion.windowID = InjectWindowID();
    e.motion.which = InjectMouseWhich();
    e.motion.state = (Uint32)state;
    e.motion.x = x;
    e.motion.y = y;
    e.motion.xrel = xrel;
    e.motion.yrel = yrel;
    return e;
}

static SDL_Event MakeMouseWheelEvent(float wx, float wy, float mx, float my, int32_t direction)
{
    SDL_Event e{};
    memset(&e, 0, sizeof(e));
    e.type = SDL_EVENT_MOUSE_WHEEL;
    MarkInjected(e);
    e.wheel.timestamp = NowSdlTimestampNs();
    e.wheel.windowID = InjectWindowID();
    e.wheel.which = InjectMouseWhich();
    e.wheel.x = wx;
    e.wheel.y = wy;
    e.wheel.mouse_x = mx;
    e.wheel.mouse_y = my;
    e.wheel.direction = (SDL_MouseWheelDirection)direction;
    e.wheel.integer_x = (Sint32)(wx >= 0 ? wx + 0.5f : wx - 0.5f);
    e.wheel.integer_y = (Sint32)(wy >= 0 ? wy + 0.5f : wy - 0.5f);
    return e;
}

static inline void UpdateInjectContextFromEvent(const SDL_Event& e)
{
    if (IsInjected(e))
        return;
    const Uint32 t = (Uint32)e.type;

    if (t == SDL_EVENT_KEY_DOWN || t == SDL_EVENT_KEY_UP)
    {
        if ((uint32_t)e.key.windowID != 0) g_injectWindowID.store((uint32_t)e.key.windowID);
        g_injectKeyboardWhich.store((uint32_t)e.key.which);
        g_injectKeyMod.store((uint32_t)e.key.mod);
    }
    else if (t == SDL_EVENT_MOUSE_MOTION)
    {
        if ((uint32_t)e.motion.windowID != 0) g_injectWindowID.store((uint32_t)e.motion.windowID);
        g_injectMouseWhich.store((uint32_t)e.motion.which);
    }
    else if (t == SDL_EVENT_MOUSE_BUTTON_DOWN || t == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        if ((uint32_t)e.button.windowID != 0) g_injectWindowID.store((uint32_t)e.button.windowID);
        g_injectMouseWhich.store((uint32_t)e.button.which);
    }
    else if (t == SDL_EVENT_MOUSE_WHEEL)
    {
        if ((uint32_t)e.wheel.windowID != 0) g_injectWindowID.store((uint32_t)e.wheel.windowID);
        g_injectMouseWhich.store((uint32_t)e.wheel.which);
    }
}

static DWORD WINAPI PipeServerThread(LPVOID)
{
    if (g_logger)
        g_logger->info("[PIPE] starting server: {}", "redpass_sdl3_hook_input");

    while (!g_shouldExit)
    {
        g_pipeHandle = CreateNamedPipeW(
            kPipeName,
            PIPE_ACCESS_INBOUND,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            0, 0,
            0,
            nullptr);

        if (g_pipeHandle == INVALID_HANDLE_VALUE)
        {
            const DWORD err = GetLastError();
            if (g_logger) g_logger->error("[PIPE] CreateNamedPipeW failed: {} (0x{:X})", err, err);
            Sleep(500);
            continue;
        }

        if (g_logger)
            g_logger->info("[PIPE] Pipe created, waiting for client connection...");

        BOOL ok = ConnectNamedPipe(g_pipeHandle, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!ok)
        {
            const DWORD err = GetLastError();
            if (g_logger) g_logger->warn("[PIPE] ConnectNamedPipe failed: {} (0x{:X})", err, err);
            CloseHandle(g_pipeHandle);
            g_pipeHandle = INVALID_HANDLE_VALUE;
            continue;
        }

        if (g_logger)
            g_logger->info("[PIPE] client connected");

        while (!g_shouldExit)
        {
            PipeMsgHeader hdr{};
            if (!ReadExact(g_pipeHandle, &hdr, (DWORD)sizeof(hdr)))
            {
                if (g_logger) g_logger->info("[PIPE] ReadExact header failed, client likely disconnected");
                break;
            }

            if (hdr.magic != kPipeMagic || hdr.size > 4096)
            {
                if (g_logger) g_logger->warn("[PIPE] Invalid header: magic=0x{:X} (expected 0x{:X}) size={}", 
                    hdr.magic, kPipeMagic, hdr.size);
                break;
            }

            std::vector<uint8_t> payload(hdr.size);
            if (hdr.size > 0 && !ReadExact(g_pipeHandle, payload.data(), hdr.size))
            {
                if (g_logger) g_logger->warn("[PIPE] ReadExact payload failed (size={})", hdr.size);
                break;
            }

            const PipeCmd cmd = (PipeCmd)hdr.type;
            switch (cmd)
            {
            case PipeCmd::MouseLeftDown:
            case PipeCmd::MouseLeftUp:
                if (hdr.size == sizeof(MouseXY))
                {
                    const MouseXY* m = (const MouseXY*)payload.data();
                    const bool down = (cmd == PipeCmd::MouseLeftDown);
                    EnqueueInjectedEvent(MakeMouseButtonEvent(down, SDL_BUTTON_LEFT, 1, m->x, m->y));
                }
                break;
            case PipeCmd::MouseLeftClick:
                if (hdr.size == sizeof(MouseClickXYI))
                {
                    const MouseClickXYI* m = (const MouseClickXYI*)payload.data();
                    const SDL_Event down = MakeMouseButtonEvent(true, SDL_BUTTON_LEFT, 1, m->x, m->y);
                    SDL_Event up = MakeMouseButtonEvent(false, SDL_BUTTON_LEFT, 1, m->x, m->y);
                    // 保证 UP 的 timestamp 比 DOWN 晚
                    up.button.timestamp = down.button.timestamp + (Uint64)m->interval_ms * 1000000ULL;
                    EnqueueInjectedEvent(down);
                    EnqueueInjectedEvent(up);
                }
                break;
            case PipeCmd::MouseMove:
                if (hdr.size == sizeof(MouseMotionXYRelState))
                {
                    const MouseMotionXYRelState* mm = (const MouseMotionXYRelState*)payload.data();
                    EnqueueInjectedEvent(MakeMouseMotionEvent(mm->x, mm->y, mm->xrel, mm->yrel, mm->state));
                }
                break;
            case PipeCmd::MouseRightDown:
            case PipeCmd::MouseRightUp:
                if (hdr.size == sizeof(MouseXY))
                {
                    const MouseXY* m = (const MouseXY*)payload.data();
                    const bool down = (cmd == PipeCmd::MouseRightDown);
                    EnqueueInjectedEvent(MakeMouseButtonEvent(down, SDL_BUTTON_RIGHT, 1, m->x, m->y));
                }
                break;
            case PipeCmd::MouseRightClick:
                if (hdr.size == sizeof(MouseClickXYI))
                {
                    const MouseClickXYI* m = (const MouseClickXYI*)payload.data();
                    const SDL_Event down = MakeMouseButtonEvent(true, SDL_BUTTON_RIGHT, 1, m->x, m->y);
                    SDL_Event up = MakeMouseButtonEvent(false, SDL_BUTTON_RIGHT, 1, m->x, m->y);
                    up.button.timestamp = down.button.timestamp + (Uint64)m->interval_ms * 1000000ULL;
                    EnqueueInjectedEvent(down);
                    EnqueueInjectedEvent(up);
                }
                break;
            case PipeCmd::MouseWheel:
                if (hdr.size == sizeof(MouseWheelXYMouseDir))
                {
                    const MouseWheelXYMouseDir* w = (const MouseWheelXYMouseDir*)payload.data();
                    EnqueueInjectedEvent(MakeMouseWheelEvent(w->wheel_x, w->wheel_y, w->mouse_x, w->mouse_y, w->direction));
                }
                break;
            case PipeCmd::SetBlockRealInput:
                if (hdr.size == sizeof(BlockFlag))
                {
                    const BlockFlag* b = (const BlockFlag*)payload.data();
                    g_blockRealInput = (b->block != 0);
                    if (g_logger)
                        g_logger->info("[INPUT-BLOCK] set via pipe: {}", g_blockRealInput ? "BLOCKED" : "ALLOWED");
                }
                break;
            case PipeCmd::ToggleBlockRealInput:
                if (hdr.size == 0)
                {
                    g_blockRealInput = !g_blockRealInput;
                    if (g_logger)
                        g_logger->info("[INPUT-BLOCK] toggled via pipe: {}", g_blockRealInput ? "BLOCKED" : "ALLOWED");
                }
                break;
            case PipeCmd::KeyDown:
            case PipeCmd::KeyUp:
            case PipeCmd::KeyPress:
                if (hdr.size >= 1)
                {
                    const uint8_t n = payload[0];
                    if (hdr.size >= (uint16_t)(1 + n) && n > 0)
                    {
                        std::string name((const char*)payload.data() + 1, (size_t)n);
                        ResolvedKey rk = ResolveKeyName(name.c_str());
                        if (rk.scancode != SDL_SCANCODE_UNKNOWN || rk.key != 0)
                        {
                            if (cmd == PipeCmd::KeyDown)
                            {
                                EnqueueInjectedEvent(MakeKeyEvent(true, rk));
                            }
                            else if (cmd == PipeCmd::KeyUp)
                            {
                                EnqueueInjectedEvent(MakeKeyEvent(false, rk));
                            }
                            else
                            {
                                uint32_t interval = 50;
                                if (hdr.size >= (uint16_t)(1 + n + sizeof(uint32_t)))
                                    memcpy(&interval, payload.data() + 1 + n, sizeof(uint32_t));

                                const SDL_Event kd = MakeKeyEvent(true, rk);
                                SDL_Event ku = MakeKeyEvent(false, rk);
                                ku.key.timestamp = kd.key.timestamp + (Uint64)interval * 1000000ULL;
                                EnqueueInjectedEvent(kd);
                                EnqueueInjectedEvent(ku);
                            }
                        }
                        else
                        {
                            if (g_logger) g_logger->warn("[PIPE] Resolve key failed: '{}'", name);
                        }
                    }
                }
                break;
            default:
                break;
            }
        }

        if (g_logger)
            g_logger->info("[PIPE] client disconnected");

        DisconnectNamedPipe(g_pipeHandle);
        CloseHandle(g_pipeHandle);
        g_pipeHandle = INVALID_HANDLE_VALUE;
    }

    return 0;
}

static inline bool IsRealInputEventType(Uint32 t)
{
    return (t == SDL_EVENT_KEY_DOWN ||
            t == SDL_EVENT_KEY_UP ||
            t == SDL_EVENT_TEXT_INPUT ||
            t == SDL_EVENT_TEXT_EDITING ||
            t == SDL_EVENT_TEXT_EDITING_CANDIDATES ||
            t == SDL_EVENT_MOUSE_MOTION ||
            t == SDL_EVENT_MOUSE_BUTTON_DOWN ||
            t == SDL_EVENT_MOUSE_BUTTON_UP ||
            t == SDL_EVENT_MOUSE_WHEEL);
}

// 统计信息（避免日志刷屏）
struct EventStats {
    int keyboardEvents = 0;
    int mouseEvents = 0;
    int otherEvents = 0;
    int totalCalls = 0;
    ULONGLONG lastReportTime = 0;
};
static EventStats g_stats = {0};

// ============================================================================
// 限频打印：每 2 秒只 dump 一次“收到的第一个输入事件”，避免刷屏
// ============================================================================
static ULONGLONG g_lastInputDumpTime = 0;
static bool g_inputDumpedInWindow = false;

static const char* EventTypeName(Uint32 t)
{
    switch (t)
    {
    case SDL_EVENT_KEY_DOWN: return "SDL_EVENT_KEY_DOWN";
    case SDL_EVENT_KEY_UP: return "SDL_EVENT_KEY_UP";
    case SDL_EVENT_MOUSE_MOTION: return "SDL_EVENT_MOUSE_MOTION";
    case SDL_EVENT_MOUSE_BUTTON_DOWN: return "SDL_EVENT_MOUSE_BUTTON_DOWN";
    case SDL_EVENT_MOUSE_BUTTON_UP: return "SDL_EVENT_MOUSE_BUTTON_UP";
    case SDL_EVENT_MOUSE_WHEEL: return "SDL_EVENT_MOUSE_WHEEL";
    default: return "SDL_EVENT_OTHER";
    }
}

static void DumpFirstInputEventRateLimited(const SDL_Event& e, ULONGLONG nowMs)
{
    if (!g_logger)
        return;

    if (g_lastInputDumpTime == 0 || (nowMs - g_lastInputDumpTime) >= 2000)
    {
        g_lastInputDumpTime = nowMs;
        g_inputDumpedInWindow = false;
    }

    if (g_inputDumpedInWindow)
        return;

    const Uint32 t = (Uint32)e.type;
    const bool isInput =
        (t == SDL_EVENT_KEY_DOWN || t == SDL_EVENT_KEY_UP ||
         t == SDL_EVENT_MOUSE_MOTION ||
         t == SDL_EVENT_MOUSE_BUTTON_DOWN || t == SDL_EVENT_MOUSE_BUTTON_UP ||
         t == SDL_EVENT_MOUSE_WHEEL);

    if (!isInput)
        return;

    g_inputDumpedInWindow = true;

    g_logger->info("==== INPUT_DUMP (first in 2s) type={} ({}) ====", t, EventTypeName(t));

    if (t == SDL_EVENT_KEY_DOWN || t == SDL_EVENT_KEY_UP)
    {
        g_logger->info("key: ts={} win={} which={} scancode={} key=0x{:X} mod=0x{:X} raw=0x{:X} down={} repeat={}",
            (unsigned long long)e.key.timestamp,
            (unsigned int)e.key.windowID,
            (unsigned int)e.key.which,
            (int)e.key.scancode,
            (unsigned int)e.key.key,
            (unsigned int)e.key.mod,
            (unsigned int)e.key.raw,
            e.key.down ? 1 : 0,
            e.key.repeat ? 1 : 0);
    }
    else if (t == SDL_EVENT_MOUSE_MOTION)
    {
        g_logger->info("motion: ts={} win={} which={} state=0x{:X} x={} y={} xrel={} yrel={}",
            (unsigned long long)e.motion.timestamp,
            (unsigned int)e.motion.windowID,
            (unsigned int)e.motion.which,
            (unsigned int)e.motion.state,
            e.motion.x, e.motion.y,
            e.motion.xrel, e.motion.yrel);
    }
    else if (t == SDL_EVENT_MOUSE_BUTTON_DOWN || t == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        g_logger->info("button: ts={} win={} which={} button={} down={} clicks={} x={} y={}",
            (unsigned long long)e.button.timestamp,
            (unsigned int)e.button.windowID,
            (unsigned int)e.button.which,
            (unsigned int)e.button.button,
            e.button.down ? 1 : 0,
            (unsigned int)e.button.clicks,
            e.button.x, e.button.y);
    }
    else if (t == SDL_EVENT_MOUSE_WHEEL)
    {
        g_logger->info("wheel: ts={} win={} which={} x={} y={} dir={} mouse_x={} mouse_y={} ix={} iy={}",
            (unsigned long long)e.wheel.timestamp,
            (unsigned int)e.wheel.windowID,
            (unsigned int)e.wheel.which,
            e.wheel.x, e.wheel.y,
            (int)e.wheel.direction,
            e.wheel.mouse_x, e.wheel.mouse_y,
            (int)e.wheel.integer_x, (int)e.wheel.integer_y);
    }
}

// ============================================================================
// 日志初始化
// ============================================================================
static void InitLogger()
{
    if (g_logger)
        return;

    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(path, L'\\');
    if (lastSlash)
        *(lastSlash + 1) = L'\0';
    wcscat_s(path, L"sdl3_hook.log");

    char pathA[MAX_PATH]{};
    WideCharToMultiByte(CP_UTF8, 0, path, -1, pathA, MAX_PATH, nullptr, nullptr);

    try
    {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(pathA, false);
        auto msvc_sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
        
        std::vector<spdlog::sink_ptr> sinks{ file_sink, msvc_sink };
        g_logger = std::make_shared<spdlog::logger>("sdl3_hook", sinks.begin(), sinks.end());
        g_logger->set_level(spdlog::level::info);
        g_logger->set_pattern("[%H:%M:%S.%e] [%n] %v");
        g_logger->flush_on(spdlog::level::info);
        
        g_logger->info("========== sdl3_hook_dll loaded ==========");
    }
    catch (const std::exception& e)
    {
        OutputDebugStringA("Failed to init spdlog: ");
        OutputDebugStringA(e.what());
    }
}

static void CloseLogger()
{
    if (g_logger)
    {
        g_logger->info("========== sdl3_hook_dll unloaded ==========");
        g_logger->flush();
        g_logger.reset();
    }
}

// ============================================================================
// Insert 键监控线程
// ============================================================================
static DWORD WINAPI KeyMonitorThread(LPVOID param)
{
    HMODULE hMod = (HMODULE)param;
    
    while (!g_shouldExit)
    {
        // F8 切换：是否屏蔽真实键鼠输入（只影响 SDL 事件队列层）
        if (GetAsyncKeyState(VK_F8) & 0x8000)
        {
            g_blockRealInput = !g_blockRealInput;
            if (g_logger)
                g_logger->info("[INPUT-BLOCK] Real SDL input {}", g_blockRealInput ? "BLOCKED" : "ALLOWED");

            while (GetAsyncKeyState(VK_F8) & 0x8000)
                Sleep(10);
        }

        if (GetAsyncKeyState(VK_INSERT) & 0x8000)
        {
            if (g_logger)
                g_logger->info("Insert key detected! Unloading DLL...");
            
            while (GetAsyncKeyState(VK_INSERT) & 0x8000)
                Sleep(10);
            
            CloseLogger();
            FreeLibraryAndExitThread(hMod, 0);
            return 0;
        }
        
        Sleep(50);
    }
    
    return 0;
}

// ============================================================================
// Hook SDL_PeepEvents（唯一的 hook - 在这里劫持和注入！）
// ============================================================================
static int SDLCALL Hook_SDL_PeepEvents(SDL_Event* events, int numevents, SDL_EventAction action, Uint32 minType, Uint32 maxType)
{
    // 调用原函数获取真实事件
    int ret = Real_SDL_PeepEvents(events, numevents, action, minType, maxType);
    
    g_stats.totalCalls++;
    
    // action == SDL_GETEVENT 表示获取并移除事件
    if (ret >= 0 && events && numevents > 0 && action == SDL_GETEVENT)
    {
        ULONGLONG currentTime = GetTickCount64();

        // 学习 windowID/which/mod，提高注入事件兼容性（即使之后会屏蔽真实输入也没关系）
        for (int i = 0; i < ret; i++)
            UpdateInjectContextFromEvent(events[i]);

        // 先拦截“真实输入”（只在 SDL 事件队列层面生效）
        // 逻辑：把真实键盘/鼠标/文本输入事件从 events[] 中移除；我们自己注入的事件会打 magic，不会被误删
        if (g_blockRealInput && ret > 0)
        {
            int w = 0;
            for (int r = 0; r < ret; r++)
            {
                const Uint32 t = (Uint32)events[r].type;
                if (!IsInjected(events[r]) && IsRealInputEventType(t))
                {
                    // 丢弃真实输入事件
                    continue;
                }
                if (w != r)
                    events[w] = events[r];
                w++;
            }
            ret = w;
        }

        // 注入：把管道线程塞进来的事件追加到 events[] 末尾
        ret = DrainInjectedEvents(events, numevents, ret);

        // 统计并只打印键盘事件（避免日志刷屏）
        bool hasKeyboardEvent = false;
        for (int i = 0; i < ret; i++)
        {
            if ((events[i].type == SDL_EVENT_KEY_DOWN || events[i].type == SDL_EVENT_KEY_UP) && !IsInjected(events[i]))
            {
                g_stats.keyboardEvents++;
                hasKeyboardEvent = true;
                
                // 只打印键盘事件（简化版，不再打印完整结构）
                if (g_logger)
                {
                    char keyChar = (events[i].key.key >= 32 && events[i].key.key < 127) ? (char)events[i].key.key : '?';
                    g_logger->info("[KEY {}] scancode={} key='{}' (0x{:X})", 
                                  (events[i].type == SDL_EVENT_KEY_DOWN) ? "DOWN" : "UP  ",
                                  events[i].key.scancode,
                                  keyChar,
                                  events[i].key.key);
                }
            }
            else if ((events[i].type == SDL_EVENT_MOUSE_BUTTON_DOWN || events[i].type == SDL_EVENT_MOUSE_BUTTON_UP) && !IsInjected(events[i]))
            {
                g_stats.mouseEvents++;
                
                // 只记录真实的鼠标点击事件（不记录注入的事件）
                if (g_logger)
                {
                    const char* buttonName = "UNKNOWN";
                    if (events[i].button.button == SDL_BUTTON_LEFT)
                        buttonName = "LEFT";
                    else if (events[i].button.button == SDL_BUTTON_RIGHT)
                        buttonName = "RIGHT";
                    else if (events[i].button.button == SDL_BUTTON_MIDDLE)
                        buttonName = "MIDDLE";
                    else if (events[i].button.button == SDL_BUTTON_X1)
                        buttonName = "X1";
                    else if (events[i].button.button == SDL_BUTTON_X2)
                        buttonName = "X2";
                    
                    g_logger->info("[MOUSE {}] button={} ({}) clicks={} x={:.2f} y={:.2f} win={} which={} ts={}", 
                                  (events[i].type == SDL_EVENT_MOUSE_BUTTON_DOWN) ? "DOWN" : "UP  ",
                                  (unsigned int)events[i].button.button,
                                  buttonName,
                                  (unsigned int)events[i].button.clicks,
                                  events[i].button.x,
                                  events[i].button.y,
                                  (unsigned int)events[i].button.windowID,
                                  (unsigned int)events[i].button.which,
                                  (unsigned long long)events[i].button.timestamp);
                }
            }
            else if (events[i].type >= 0x400 && events[i].type <= 0x404)
            {
                g_stats.mouseEvents++;
            }
            else
            {
                g_stats.otherEvents++;
            }
        }
        
    }
    
    return ret;
}

// ============================================================================
// DllMain
// ============================================================================
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (DetourIsHelperProcess())
        return TRUE;

    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        {
            DisableThreadLibraryCalls(hinstDLL);
            g_hModule = hinstDLL;
            
            InitLogger();
            
            // 初始化统计
            g_stats.lastReportTime = GetTickCount64();
            
            if (g_logger)
            {
                g_logger->info("DLL_PROCESS_ATTACH: hooking SDL3...");
                g_logger->info("Main thread ID: {}", GetCurrentThreadId());
            }

            // 查找 SDL3.dll
            g_hSDL3 = GetModuleHandleW(L"SDL3.dll");
            if (!g_hSDL3)
            {
                if (g_logger)
                    g_logger->error("SDL3.dll not found in process!");
                break;
            }
            
            if (g_logger)
                g_logger->info("SDL3.dll found at 0x{:X}", (uintptr_t)g_hSDL3);

            // 获取 SDL_PeepEvents 函数地址
            Real_SDL_PeepEvents = (SDL_PeepEvents_t)GetProcAddress(g_hSDL3, "SDL_PeepEvents");
            
            if (g_logger)
                g_logger->info("SDL_PeepEvents: 0x{:X}", (uintptr_t)Real_SDL_PeepEvents);

            if (!Real_SDL_PeepEvents)
            {
                if (g_logger)
                    g_logger->error("SDL_PeepEvents not found!");
                break;
            }

            // 安装 Hook
            if (g_logger) g_logger->info("Installing Detours hook...");
            DetourRestoreAfterWith();
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            
            if (g_logger) g_logger->debug("Calling DetourAttach for SDL_PeepEvents...");
            LONG err = DetourAttach(reinterpret_cast<PVOID*>(&Real_SDL_PeepEvents),
                         reinterpret_cast<PVOID>(Hook_SDL_PeepEvents));
            if (g_logger) g_logger->debug("DetourAttach returned: {}", err);
            
            if (g_logger) g_logger->debug("Committing detour transaction...");
            LONG commitResult = DetourTransactionCommit();
            if (g_logger) g_logger->debug("DetourTransactionCommit returned: {}", commitResult);
            
            if (commitResult == NO_ERROR && err == NO_ERROR)
            {
                if (g_logger)
                {
                    g_logger->info("SDL_PeepEvents hook installed successfully!");
                    g_logger->info("[PIPE] Will listen on \\\\.\\pipe\\redpass_sdl3_hook_input (binary protocol, magic 'RPS1')");
                    g_logger->info("[INPUT-BLOCK] Real SDL input is BLOCKED by default. Press F8 to toggle.");
                }

                // 启动命名管道线程：接收外部命令 -> 注入 SDL 事件队列
                if (g_logger) g_logger->info("Starting pipe server thread...");
                g_pipeThread = CreateThread(nullptr, 0, PipeServerThread, nullptr, 0, nullptr);
                if (g_pipeThread)
                {
                    if (g_logger) g_logger->info("Pipe server thread created with handle 0x{:X}", (uintptr_t)g_pipeThread);
                }
                else
                {
                    if (g_logger) g_logger->error("Failed to create pipe server thread: {}", GetLastError());
                }

                // 启动 Insert/F8 监控线程（Insert 卸载，F8 切换输入屏蔽）
                if (g_logger) g_logger->info("Starting key monitor thread...");
                HANDLE hKeyThread = CreateThread(nullptr, 0, KeyMonitorThread, g_hModule, 0, nullptr);
                if (hKeyThread)
                {
                    if (g_logger) g_logger->info("Key monitor thread created with handle 0x{:X}", (uintptr_t)hKeyThread);
                }
                else
                {
                    if (g_logger) g_logger->error("Failed to create key monitor thread: {}", GetLastError());
                }
                
                // no MessageBox: keep the injected process non-interactive; use logs instead.
                if (g_logger)
                {
                    DWORD pid = GetCurrentProcessId();
                    wchar_t procName[MAX_PATH]{};
                    GetModuleFileNameW(nullptr, procName, MAX_PATH);
                    wchar_t* baseName = wcsrchr(procName, L'\\');
                    if (baseName) baseName++; else baseName = procName;

                    // Convert wide process name to UTF-8 for logging
                    char baseNameUtf8[MAX_PATH * 4]{};
                    WideCharToMultiByte(CP_UTF8, 0, baseName, -1, baseNameUtf8, (int)sizeof(baseNameUtf8), nullptr, nullptr);
                    g_logger->info("SDL3 Hook Injected! Process: {} (PID: {})", baseNameUtf8, pid);
                }
            }
            else
            {
                if (g_logger)
                    g_logger->error("SDL_PeepEvents hook FAILED! err={}, commit={}", err, commitResult);
            }
        }
        break;

    case DLL_PROCESS_DETACH:
        g_shouldExit = true;

        // 停止管道（尽力释放阻塞中的 ConnectNamedPipe/ReadFile）
        if (g_pipeHandle != INVALID_HANDLE_VALUE)
        {
            DisconnectNamedPipe(g_pipeHandle);
            CloseHandle(g_pipeHandle);
            g_pipeHandle = INVALID_HANDLE_VALUE;
        }
        if (g_pipeThread)
        {
            CloseHandle(g_pipeThread);
            g_pipeThread = nullptr;
        }
        
        if (g_logger)
            g_logger->info("DLL_PROCESS_DETACH: removing hooks...");
        
        if (Real_SDL_PeepEvents)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            
            DetourDetach(reinterpret_cast<PVOID*>(&Real_SDL_PeepEvents),
                         reinterpret_cast<PVOID>(Hook_SDL_PeepEvents));
            
            DetourTransactionCommit();
        }

        if (g_logger)
            g_logger->info("All hooks removed.");
        CloseLogger();
        break;
    }

    return TRUE;
}
