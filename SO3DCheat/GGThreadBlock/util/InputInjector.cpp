#include "InputInjector.h"

#include <Detours/build/include/detours.h>
#include <spdlog/spdlog.h>

#include <array>
#include <atomic>
#include <mutex>

namespace GGTB::InputInjector
{
namespace
{
// VK -> 期望"按下"持续到的 GetTickCount 值。0 = 未注入。
// 用 atomic<DWORD> 而不是 uint32_t — hook 在游戏线程读,SendVks 在 IO 线程写,
// 256 个独立原子量比一把锁更便宜(每帧 76+ 次 GetAsyncKeyState 都要查这表)。
std::array<std::atomic<DWORD>, 256> g_expireTickMs{};

using fnGetAsyncKeyState = SHORT(WINAPI *)(int vKey);
fnGetAsyncKeyState        g_origGetAsyncKeyState = nullptr;

std::atomic<bool> g_installed{false};
std::mutex        g_installMutex; // 只挡 Install/Uninstall,hot path 不进

// 不能注入的 VK — 进程内 HackThread 也用 GetAsyncKeyState 轮询热键,这里挡住
// 防止 web 端无意中触发宿主侧动作。
//   VK_OEM_4  (0xDB) — 切 ImGui 菜单
//   VK_END    (0x23) — 退出 HackThread,卸所有 hook
//   'N'       (0x4E) — 切火力全开
bool IsBlacklistedVk(int vk)
{
    return vk == VK_OEM_4 || vk == VK_END || vk == 'N';
}

// hook 入口 — 任何线程都可能进来。99% 调用是游戏的 Input_PollKeyboard,
// 但 HackThread 热键、ImGui 输入也可能命中。我们只对在表里的 VK 把高位 OR 上,
// 不动其它返回值,所以对真实物理键盘按键完全透明。
SHORT WINAPI HookGetAsyncKeyState(int vKey)
{
    SHORT real = g_origGetAsyncKeyState(vKey);

    if (vKey >= 0 && vKey < 256)
    {
        DWORD expire = g_expireTickMs[vKey].load(std::memory_order_acquire);
        if (expire != 0)
        {
            DWORD now = GetTickCount();
            // 32-bit wrap-safe: (int32_t)(expire - now) > 0 <=> expire 还在未来
            if (static_cast<int32_t>(expire - now) > 0)
                return real | static_cast<SHORT>(0x8000); // 高位 = "当前按下"
            // 过期了 — 清掉,免得后面每次调用都重新算时间。CAS 防多线程双清。
            DWORD expected = expire;
            g_expireTickMs[vKey].compare_exchange_strong(
                expected, 0, std::memory_order_release, std::memory_order_relaxed);
        }
    }
    return real;
}
} // namespace

void Install()
{
    std::lock_guard<std::mutex> lk(g_installMutex);
    if (g_installed.load())
        return;

    g_origGetAsyncKeyState = reinterpret_cast<fnGetAsyncKeyState>(
        DetourFindFunction("user32.dll", "GetAsyncKeyState"));
    if (!g_origGetAsyncKeyState)
    {
        spdlog::error("GGTB::InputInjector: GetAsyncKeyState resolve failed");
        return;
    }

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    LONG err = DetourAttach(reinterpret_cast<PVOID *>(&g_origGetAsyncKeyState),
                            HookGetAsyncKeyState);
    LONG cm  = DetourTransactionCommit();
    if (err != NO_ERROR || cm != NO_ERROR)
    {
        spdlog::error("GGTB::InputInjector: Detour failed attach={} commit={}", err, cm);
        return;
    }
    g_installed.store(true);
    spdlog::info("GGTB::InputInjector: GetAsyncKeyState hooked");
}

void Uninstall()
{
    std::lock_guard<std::mutex> lk(g_installMutex);
    if (!g_installed.load())
        return;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(reinterpret_cast<PVOID *>(&g_origGetAsyncKeyState),
                 HookGetAsyncKeyState);
    DetourTransactionCommit();
    g_installed.store(false);

    // 清残留虚拟按下,避免下次 Install 看到陈旧 expire
    for (auto &slot : g_expireTickMs)
        slot.store(0, std::memory_order_release);

    spdlog::info("GGTB::InputInjector: GetAsyncKeyState detached");
}

size_t SendVks(const std::vector<int> &vks, int holdMs)
{
    if (!g_installed.load())
    {
        spdlog::warn("GGTB::InputInjector: SendVks called before Install");
        return 0;
    }

    if (holdMs < 10)   holdMs = 10;
    if (holdMs > 5000) holdMs = 5000;

    DWORD now    = GetTickCount();
    DWORD expire = now + static_cast<DWORD>(holdMs);

    size_t accepted = 0;
    for (int vk : vks)
    {
        if (vk < 1 || vk > 254)
            continue;
        if (IsBlacklistedVk(vk))
        {
            spdlog::warn("GGTB::InputInjector: SendVks blacklisted vk={:#x}, skipped", vk);
            continue;
        }
        // 同 VK 重复发就延长 expire(取较大者),不是覆盖 — 多次短按会自然重叠成长按。
        DWORD prev = g_expireTickMs[vk].load(std::memory_order_acquire);
        DWORD want = expire;
        while (true)
        {
            if (prev != 0 && static_cast<int32_t>(prev - want) > 0)
                break; // 已有更晚的 expire,不动
            if (g_expireTickMs[vk].compare_exchange_weak(
                    prev, want, std::memory_order_release, std::memory_order_relaxed))
                break;
        }
        ++accepted;
    }
    spdlog::info("GGTB::InputInjector: SendVks count={} hold={}ms accepted={}",
                 vks.size(), holdMs, accepted);
    return accepted;
}
} // namespace GGTB::InputInjector
