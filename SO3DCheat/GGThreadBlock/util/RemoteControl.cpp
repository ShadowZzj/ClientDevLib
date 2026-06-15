#include "RemoteControl.h"

#include "../entity/CLocalPlayer.h"
#include "UserConfig.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace GGTB::RemoteControl
{
namespace
{
// 默认 pipe 名（无 \\.\pipe\ 前缀；CreateFile 时拼上）。Broker 用同名 net.createServer。
constexpr const char *kDefaultPipeName = "GGTB_BROKER";

// status tick 间隔。Broker 用它做 keepalive，<2s 时挑足够频繁地刷 UI 上的金钱即可。
constexpr DWORD kStatusTickMs = 1500;

// 连不上 broker / 断线后的重连间隔。Broker 还没起的常见情况，3s 不会刷屏。
constexpr DWORD kReconnectMs = 3000;

// ============================================================
// 全局状态
// ============================================================

std::atomic<bool>          g_stop{false};
std::thread                g_ioThread;
std::atomic<bool>          g_connected{false};
DWORD                      g_pid = 0;

// 连接级状态：每次重连重置。pipe handle 只在 IoThreadProc 里 Open，由 writer mutex
// 保护写入；read 在 IoThreadProc 自身循环里做。Close 时统一在 disconnect 路径清。
HANDLE                     g_pipe = INVALID_HANDLE_VALUE;
std::mutex                 g_writeMutex;

// command handler 表 — 用读多写少 mutex 简单挡一下。
std::mutex                                       g_handlersMutex;
std::unordered_map<std::string, CommandHandler>  g_handlers;

// 让 status tick 线程能被 stop_ 立即唤醒。
std::mutex              g_tickMutex;
std::condition_variable g_tickCv;

// ============================================================
// 工具：pipe 路径、JSON 写帧（带换行）
// ============================================================

std::string ResolvePipePath()
{
    char buf[256] = {};
    DWORD n = GetEnvironmentVariableA("GGTB_BROKER_PIPE", buf, sizeof(buf));
    std::string name = (n > 0 && n < sizeof(buf)) ? buf : kDefaultPipeName;
    return std::string("\\\\.\\pipe\\") + name;
}

// pipe 是 OVERLAPPED handle:read 在 IO 主线程上阻塞等命令,write 在 tick 子线程
// 上发 status。同步 handle 在这种 read/write 并发下 WriteFile 会被串行化卡死,
// 实测过 — 见 21:47 的 _bootstrap/ggtb.log。OVERLAPPED + GetOverlappedResult
// 让两端各自独立等自己那条 IO 完成。
bool WriteFrameLocked(const std::string &line)
{
    if (g_pipe == INVALID_HANDLE_VALUE)
        return false;
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent)
    {
        spdlog::warn("GGTB::RemoteControl: write CreateEvent failed, err={}", GetLastError());
        return false;
    }
    DWORD wrote = 0;
    BOOL  ok    = WriteFile(g_pipe, line.data(),
                            static_cast<DWORD>(line.size()), &wrote, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING)
    {
        ok = GetOverlappedResult(g_pipe, &ov, &wrote, TRUE);
    }
    DWORD err = ok ? 0 : GetLastError();
    CloseHandle(ov.hEvent);
    if (!ok || wrote != line.size())
    {
        spdlog::warn("GGTB::RemoteControl: write failed, err={} wrote={} expected={}",
                     err, wrote, line.size());
        return false;
    }
    return true;
}

bool WriteJson(const nlohmann::json &j)
{
    std::string line = j.dump();
    line.push_back('\n');
    std::lock_guard<std::mutex> lk(g_writeMutex);
    return WriteFrameLocked(line);
}

// ============================================================
// 命令分发
// ============================================================

CommandHandler FindHandler(const std::string &action)
{
    std::lock_guard<std::mutex> lk(g_handlersMutex);
    auto it = g_handlers.find(action);
    return (it == g_handlers.end()) ? CommandHandler{} : it->second;
}

// SEH 包装一层 — 防止某个 handler 调到的引擎 fn 在状态没收齐时炸了，
// 把 IO 线程一并送走。同 CallSendMoneyMailSEH 的思路。lambda + state-machine
// 混 __try 会被 MSVC 拒掉（C2712），所以拆 POD 局部。
struct DispatchOut
{
    bool ok = false;
};

// handler 结果存这里 — SEH wrapper 函数不能有带析构的局部变量,
// 但 thread-local 的生命周期不受 __try 管辖。
static thread_local std::string tls_detail;

// MSVC 严格禁止在含 unwind 的函数里用 __try (C2712)。所以 SEH 包装层必须
// **只用 POD 局部** — 任何 std::function/std::string 的拷贝/析构都不行。
// 先用普通 helper 跑 handler 把结果填到 POD out,再用一个 POD-only wrapper 套 SEH。
static void InvokeHandlerInto(CommandHandler &h, const nlohmann::json &args,
                              DispatchOut *out)
{
    CmdResult r = h(args);
    out->ok = r.ok;
    tls_detail = std::move(r.detail);
}

static int InvokeWithSEHRaw(CommandHandler *h, const nlohmann::json *args,
                            DispatchOut *out)
{
    __try
    {
        InvokeHandlerInto(*h, *args, out);
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 1;
    }
}

DispatchOut DispatchSEH(CommandHandler &h, const nlohmann::json &args)
{
    DispatchOut out;
    tls_detail.clear();
    if (InvokeWithSEHRaw(&h, &args, &out) != 0)
    {
        out.ok = false;
        tls_detail = "SEH caught in handler";
    }
    return out;
}

void HandleCommand(const nlohmann::json &cmd)
{
    std::string id     = cmd.value("id",     std::string{});
    std::string action = cmd.value("action", std::string{});
    nlohmann::json args = cmd.value("args", nlohmann::json::object());

    nlohmann::json ack = {
        {"type", "ack"},
        {"pid",  g_pid},
        {"id",   id},
    };

    auto h = FindHandler(action);
    if (!h)
    {
        ack["ok"]     = false;
        ack["detail"] = "unknown action: " + action;
        WriteJson(ack);
        return;
    }

    spdlog::info("GGTB::RemoteControl: dispatch action='{}' id='{}'", action, id);
    DispatchOut out = DispatchSEH(h, args);
    ack["ok"]     = out.ok;
    ack["detail"] = tls_detail;
    WriteJson(ack);
}

// ============================================================
// status tick 线程
// ============================================================

struct StatusSnapshot
{
    int64_t  money = 0;
    int64_t  hp = 0;
    float    x = 0, y = 0, z = 0;
    uint32_t mapId = 0;
};

// 只跑 SEH-wrap 的引擎读取 — 不涉及任何带析构的局部，绕开 MSVC C2712。
StatusSnapshot ReadStatusSEH()
{
    StatusSnapshot s;
    __try
    {
        s.money = GGTB::GetLocalMoney();
        s.hp    = GGTB::GetLocalHp();
        GGTB::GetLocalPosition(s.x, s.y, s.z);
        s.mapId = GGTB::GetCurrentMapId();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        s.money = 0;
        s.x = s.y = s.z = 0;
        s.mapId = 0;
    }
    return s;
}

void TickThreadProc()
{
    spdlog::info("GGTB::RemoteControl: tick thread started");
    std::string lastName;
    int         tickCount = 0;
    while (!g_stop.load() && g_connected.load())
    {
        StatusSnapshot s    = ReadStatusSEH();
        std::string    name = GGTB::UserConfig::CurrentName();

        nlohmann::json st = {
            {"type", "status"},
            {"pid",  g_pid},
            {"money", s.money},
            {"hp", s.hp},
            {"posX", s.x}, {"posY", s.y}, {"posZ", s.z},
            {"mapId", s.mapId},
        };
        if (!name.empty())
            st["characterName"] = name;
        bool wroteOk = WriteJson(st);
        // 头一次 + 每 ~30s(20 tick)打一行,方便定位 status 流是不是真的在跑
        if (tickCount == 0 || (tickCount % 20) == 0)
        {
            spdlog::info("GGTB::RemoteControl: status tick #{} ok={} money={} name='{}'",
                         tickCount, wroteOk, s.money, name);
        }
        ++tickCount;

        // identity diff — UserConfig 一旦认出角色名就补一帧(显式通知,broker 也会打日志)
        if (!name.empty() && name != lastName)
        {
            nlohmann::json id = {
                {"type", "identity"},
                {"pid",  g_pid},
                {"characterName", name},
            };
            WriteJson(id);
            lastName = name;
        }

        std::unique_lock<std::mutex> lk(g_tickMutex);
        g_tickCv.wait_for(lk, std::chrono::milliseconds(kStatusTickMs),
                          [] { return g_stop.load() || !g_connected.load(); });
    }
}

// ============================================================
// IO 主线程
// ============================================================

bool ConnectOnce(const std::string &path)
{
    // FILE_FLAG_OVERLAPPED 必给 — 读写在不同线程并发,同步 handle 会被内核串行化
    // (read 一阻塞,write 也得排队),实测 status 帧永远过不去。
    HANDLE h = CreateFileA(path.c_str(),
                           GENERIC_READ | GENERIC_WRITE,
                           0, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    // 切到 message mode — broker 用 net.Socket 默认是 byte stream，但 named pipe
    // 在 server 端创建时如果是 PIPE_TYPE_MESSAGE，client 端可以 SetState 切 message。
    // Node 这边创建 pipe 时不是 message type（它走的是 \\.\pipe 上的 byte 流），
    // 所以这里保留默认 byte mode；reader 自己按 \n 切帧。
    g_pipe = h;
    return true;
}

void DisconnectAndCleanup()
{
    HANDLE h;
    {
        std::lock_guard<std::mutex> lk(g_writeMutex);
        h = g_pipe;
        g_pipe = INVALID_HANDLE_VALUE;
    }
    if (h != INVALID_HANDLE_VALUE)
        CloseHandle(h);
    g_connected.store(false);
    g_tickCv.notify_all();
}

void WriteHello()
{
    char hostExe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, hostExe, MAX_PATH);
    nlohmann::json j = {
        {"type", "hello"},
        {"pid",  g_pid},
        {"protocol", 1},
        {"dllVersion", "0.1"},
        {"hostExe", hostExe},
    };
    WriteJson(j);
}

void WriteBye()
{
    nlohmann::json j = {{"type", "bye"}, {"pid", g_pid}};
    WriteJson(j);
}

// 在已连接 pipe 上做 ReadFile 切行循环。返回时连接已坏。
// pipe 是 OVERLAPPED 模式 — ReadFile 立刻返回 IO_PENDING,然后 GetOverlappedResult
// 阻塞等数据。Stop 时 IoThreadProc 会 CloseHandle 让我们立刻醒过来。
void ReadLoop()
{
    std::string buf;
    char        chunk[1024];
    HANDLE      readEvt = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (!readEvt)
    {
        spdlog::warn("GGTB::RemoteControl: ReadLoop CreateEvent failed, err={}", GetLastError());
        return;
    }
    for (;;)
    {
        DWORD      got = 0;
        OVERLAPPED ov  = {};
        ov.hEvent      = readEvt;
        ResetEvent(readEvt);
        BOOL ok = ReadFile(g_pipe, chunk, sizeof(chunk), &got, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING)
            ok = GetOverlappedResult(g_pipe, &ov, &got, TRUE);
        if (!ok || got == 0)
        {
            DWORD e = GetLastError();
            if (!g_stop.load())
                spdlog::info("GGTB::RemoteControl: read end, err={}", e);
            CloseHandle(readEvt);
            return;
        }
        buf.append(chunk, got);
        for (;;)
        {
            auto pos = buf.find('\n');
            if (pos == std::string::npos) break;
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            if (line.empty()) continue;
            try
            {
                auto j = nlohmann::json::parse(line);
                std::string type = j.value("type", std::string{});
                if (type == "command")        HandleCommand(j);
                else if (type == "ping")
                {
                    nlohmann::json pong = {{"type", "pong"}, {"pid", g_pid}};
                    WriteJson(pong);
                }
                else
                    spdlog::warn("GGTB::RemoteControl: unknown frame type='{}'", type);
            }
            catch (const std::exception &e)
            {
                spdlog::warn("GGTB::RemoteControl: parse failed: {}", e.what());
            }
        }
    }
}

void IoThreadProc()
{
    g_pid = GetCurrentProcessId();
    std::string path = ResolvePipePath();
    spdlog::info("GGTB::RemoteControl: io thread started, pipe={}", path);

    while (!g_stop.load())
    {
        if (!ConnectOnce(path))
        {
            // broker 没起 — 静默等
            std::unique_lock<std::mutex> lk(g_tickMutex);
            g_tickCv.wait_for(lk, std::chrono::milliseconds(kReconnectMs),
                              [] { return g_stop.load(); });
            continue;
        }

        spdlog::info("GGTB::RemoteControl: connected to {}", path);
        g_connected.store(true);
        WriteHello();

        std::thread tick(TickThreadProc);

        ReadLoop();

        // ReadLoop 出来 = 链路坏了或要退出。先标记 disconnected，
        // 让 tick 线程立即退；然后 close handle 释放 broker 那边。
        DisconnectAndCleanup();
        if (tick.joinable()) tick.join();

        if (!g_stop.load())
        {
            std::unique_lock<std::mutex> lk(g_tickMutex);
            g_tickCv.wait_for(lk, std::chrono::milliseconds(kReconnectMs),
                              [] { return g_stop.load(); });
        }
    }
    spdlog::info("GGTB::RemoteControl: io thread exiting");
}

} // namespace

// ============================================================
// public API
// ============================================================

void RegisterCommandHandler(const std::string &action, CommandHandler handler)
{
    std::lock_guard<std::mutex> lk(g_handlersMutex);
    g_handlers[action] = std::move(handler);
}

bool IsConnected() { return g_connected.load(); }

void Install()
{
    if (g_ioThread.joinable())
        return;

    g_stop.store(false);
    g_ioThread = std::thread(IoThreadProc);
}

void Uninstall()
{
    if (!g_ioThread.joinable())
        return;

    g_stop.store(true);

    // best-effort bye — 链路若还在，发了能让 broker 立即下线实例
    if (g_connected.load())
        WriteBye();

    // 让所有 wait_for 立即返回
    g_tickCv.notify_all();

    // 关 pipe 让 ReadFile 立刻醒
    DisconnectAndCleanup();

    g_ioThread.join();

    {
        std::lock_guard<std::mutex> lk(g_handlersMutex);
        g_handlers.clear();
    }
}

bool EmitFrame(const nlohmann::json &frame)
{
    if (!g_connected.load())
        return false;
    return WriteJson(frame);
}

} // namespace GGTB::RemoteControl
