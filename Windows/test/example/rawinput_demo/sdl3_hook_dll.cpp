#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <deque>
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
// 事件注入时使用固定字段（与目标程序日志保持一致）
static constexpr SDL_WindowID kFixedWindowID = (SDL_WindowID)3;
static constexpr SDL_KeyboardID kFixedKeyboardWhich = (SDL_KeyboardID)0;
static constexpr SDL_MouseID kFixedMouseWhich = (SDL_MouseID)0;
static constexpr SDL_Keymod kFixedKeyMod = (SDL_Keymod)0x9000;

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
    e.key.windowID = kFixedWindowID;
    e.key.which = kFixedKeyboardWhich;
    e.key.scancode = rk.scancode;
    e.key.key = rk.key;
    e.key.mod = kFixedKeyMod;
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
    e.button.windowID = kFixedWindowID;
    e.button.which = kFixedMouseWhich;
    e.button.button = button;
    e.button.down = down;
    e.button.clicks = clicks;
    e.button.x = x;
    e.button.y = y;
    return e;
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
            if (g_logger) g_logger->error("[PIPE] CreateNamedPipeW failed: {}", (uint32_t)GetLastError());
            Sleep(500);
            continue;
        }

        if (g_logger)
            g_logger->info("[PIPE] waiting client on \\\\.\\pipe\\redpass_sdl3_hook_input");

        BOOL ok = ConnectNamedPipe(g_pipeHandle, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!ok)
        {
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
                break;

            if (hdr.magic != kPipeMagic || hdr.size > 4096)
            {
                if (g_logger) g_logger->warn("[PIPE] bad header magic=0x{:X} size={}", hdr.magic, hdr.size);
                break;
            }

            std::vector<uint8_t> payload(hdr.size);
            if (hdr.size > 0 && !ReadExact(g_pipeHandle, payload.data(), hdr.size))
                break;

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
                            if (g_logger) g_logger->warn("[PIPE] resolve key failed: '{}'", name);
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

// ============================================================================
// SDL 输入拦截（只拦截“真实输入”，不拦截我们注入的事件）
// ============================================================================
static volatile bool g_blockRealInput = true;  // 注入后默认开启：屏蔽真实键盘/鼠标输入

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

        // 只在每个 2 秒窗口里 dump 一次“收到的第一个输入事件”
        for (int i = 0; i < ret; i++)
        {
            DumpFirstInputEventRateLimited(events[i], currentTime);
            if (g_inputDumpedInWindow)
                break;
        }
        
        // 统计并只打印键盘事件（避免日志刷屏）
        bool hasKeyboardEvent = false;
        for (int i = 0; i < ret; i++)
        {
            if (events[i].type == SDL_EVENT_KEY_DOWN || events[i].type == SDL_EVENT_KEY_UP)
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
            else if (events[i].type >= 0x400 && events[i].type <= 0x404)
            {
                g_stats.mouseEvents++;
            }
            else
            {
                g_stats.otherEvents++;
            }
        }
        
        // 每 5 秒打印一次统计摘要（如果没有键盘事件）
        if (!hasKeyboardEvent && g_logger && (currentTime - g_stats.lastReportTime >= 5000))
        {
            g_logger->info("[STATS] Calls={}, KB={}, Mouse={}, Other={}", 
                          g_stats.totalCalls, g_stats.keyboardEvents, g_stats.mouseEvents, g_stats.otherEvents);
            g_stats.lastReportTime = currentTime;
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
                MessageBoxW(nullptr, L"SDL3.dll not found!", L"sdl3_hook_dll", MB_OK | MB_ICONERROR);
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
                MessageBoxW(nullptr, L"SDL_PeepEvents not found!", L"sdl3_hook_dll", MB_OK | MB_ICONERROR);
                break;
            }

            // 安装 Hook
            DetourRestoreAfterWith();
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            
            LONG err = DetourAttach(reinterpret_cast<PVOID*>(&Real_SDL_PeepEvents),
                         reinterpret_cast<PVOID>(Hook_SDL_PeepEvents));
            
            LONG commitResult = DetourTransactionCommit();
            if (commitResult == NO_ERROR && err == NO_ERROR)
            {
                if (g_logger)
                {
                    g_logger->info("SDL_PeepEvents hook installed successfully!");
                    g_logger->info("[PIPE] listening on \\\\.\\pipe\\redpass_sdl3_hook_input (binary protocol, magic 'RPS1')");
                    g_logger->info("[INPUT-BLOCK] Real SDL input is BLOCKED by default. Press F8 to toggle.");
                }

                // 启动命名管道线程：接收外部命令 -> 注入 SDL 事件队列
                g_pipeThread = CreateThread(nullptr, 0, PipeServerThread, nullptr, 0, nullptr);

                // 启动 Insert/F8 监控线程（Insert 卸载，F8 切换输入屏蔽）
                CreateThread(nullptr, 0, KeyMonitorThread, g_hModule, 0, nullptr);
                
                wchar_t msg[512];
                DWORD pid = GetCurrentProcessId();
                wchar_t procName[MAX_PATH]{};
                GetModuleFileNameW(nullptr, procName, MAX_PATH);
                wchar_t* baseName = wcsrchr(procName, L'\\');
                if (baseName) baseName++; else baseName = procName;
                
                swprintf_s(msg, L"SDL3 Hook Injected!\n\nProcess: %s (PID: %lu)\n\nFeatures:\n- NamedPipe: \\\\.\\pipe\\redpass_sdl3_hook_input\n- Block real SDL input by default (press F8 to toggle)\n\nPress INSERT to unload", 
                          baseName, pid);
                MessageBoxW(nullptr, msg, L"sdl3_hook_dll", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
            }
            else
            {
                if (g_logger)
                    g_logger->error("SDL_PeepEvents hook FAILED! err={}, commit={}", err, commitResult);
                MessageBoxW(nullptr, L"SDL hook FAILED!", L"sdl3_hook_dll", MB_OK | MB_ICONERROR | MB_TOPMOST);
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
