#define WIN32_LEAN_AND_MEAN
#include "DisconnectWatchdog.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace GGTB::DisconnectWatchdog
{
namespace
{
// Channel/login sits on port 3000 and keeps chatting even when the world
// connection has silently died, so we don't count it as a keep-alive. Every
// other peer port (world/combat/whatever) is treated as valid game traffic.
constexpr uint16_t kExcludedPort = 3000;

// 1 minute of silence on all non-channel ports is the disconnect threshold.
constexpr auto kIdleLimit = std::chrono::minutes(1);

// How often the watchdog thread wakes to check the timer. 10s is a good
// trade-off — we react within ~10s of the deadline without burning CPU.
constexpr auto kPollInterval = std::chrono::seconds(10);

std::atomic<bool>     s_installed{false};
std::atomic<bool>     s_stopRequested{false};
std::atomic<bool>     s_armed{false};         // flips true on first non-channel recv
std::atomic<int64_t>  s_lastRecvTickMs{0};    // GetTickCount64() of last non-channel recv
std::thread           s_thread;

// EnumWindows callback glue.
struct FindMainWindowCtx
{
    DWORD  pid;
    HWND   result;
};

BOOL CALLBACK FindMainWindowProc(HWND hwnd, LPARAM lParam)
{
    auto *ctx = reinterpret_cast<FindMainWindowCtx *>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->pid)
        return TRUE;

    // Visible, no owner — that's how the game's main window is configured.
    // Skip console hosts, IME popups, tool windows.
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;

    ctx->result = hwnd;
    return FALSE; // stop enumerating
}

HWND FindHostMainWindow()
{
    FindMainWindowCtx ctx{GetCurrentProcessId(), nullptr};
    EnumWindows(FindMainWindowProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.result;
}

void TriggerClose(const char *reason)
{
    spdlog::warn("GGTB::DisconnectWatchdog: triggering close — {}", reason);

    HWND hwnd = FindHostMainWindow();
    if (hwnd)
    {
        // WM_CLOSE goes through WindowProc exactly like the red-X click: the
        // game's handler gets to flush state, then DefWindowProc turns it
        // into a DestroyWindow → WM_QUIT → normal exit. PostMessage (not
        // Send) so we don't deadlock if the main thread is wedged.
        spdlog::info("GGTB::DisconnectWatchdog: posting WM_CLOSE to hwnd={}",
                     static_cast<void *>(hwnd));
        if (PostMessageW(hwnd, WM_CLOSE, 0, 0))
        {
            // Give the process up to 10s to exit cleanly. If the main thread
            // is stuck (hung in a blocking recv, for example) WM_CLOSE won't
            // land — fall through to ExitProcess.
            for (int i = 0; i < 100; ++i)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (s_stopRequested.load())
                    return; // Uninstall ran while we were waiting
            }
            spdlog::warn("GGTB::DisconnectWatchdog: WM_CLOSE ignored after 10s; forcing ExitProcess");
        }
        else
        {
            spdlog::warn("GGTB::DisconnectWatchdog: PostMessageW failed: err={}",
                         GetLastError());
        }
    }
    else
    {
        spdlog::warn("GGTB::DisconnectWatchdog: no main window found; forcing ExitProcess");
    }

    // Last resort — skips C++ destructors and DLL teardown, but at this
    // point the game is already dead on the wire, so "graceful" is moot.
    ::ExitProcess(0);
}

void WatchdogLoop()
{
    using clock = std::chrono::steady_clock;
    while (!s_stopRequested.load())
    {
        std::this_thread::sleep_for(kPollInterval);
        if (s_stopRequested.load()) return;

        if (!s_armed.load()) continue;

        int64_t last = s_lastRecvTickMs.load();
        int64_t now  = static_cast<int64_t>(::GetTickCount64());
        auto    idle = std::chrono::milliseconds(now - last);

        if (idle >= kIdleLimit)
        {
            TriggerClose("no recv on game ports for 1 minute");
            return; // TriggerClose usually doesn't return; if it did, we're done.
        }
    }
}
} // anonymous

void Install()
{
    if (s_installed.exchange(true)) return;
    s_stopRequested.store(false);
    s_armed.store(false);
    s_lastRecvTickMs.store(static_cast<int64_t>(::GetTickCount64()));
    s_thread = std::thread(WatchdogLoop);
    spdlog::info("GGTB::DisconnectWatchdog: armed, 1-min idle window on non-channel game traffic (ignoring port {})", kExcludedPort);
}

void Uninstall()
{
    if (!s_installed.exchange(false)) return;
    s_stopRequested.store(true);
    if (s_thread.joinable()) s_thread.join();
    spdlog::info("GGTB::DisconnectWatchdog: stopped");
}

void OnRecv(uint16_t port, uint32_t bytes)
{
    if (bytes == 0) return;
    if (port == kExcludedPort) return;

    s_lastRecvTickMs.store(static_cast<int64_t>(::GetTickCount64()));
    if (!s_armed.exchange(true))
        spdlog::info("GGTB::DisconnectWatchdog: armed on first non-channel recv "
                     "(port={}, bytes={})", port, bytes);
}
} // namespace GGTB::DisconnectWatchdog
