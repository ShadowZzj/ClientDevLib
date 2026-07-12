#define WIN32_LEAN_AND_MEAN
#include "Stage1Trigger.h"

#include <Detours/build/include/detours.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <mutex>
#include <vector>

namespace GGTB::Stage1Trigger
{
namespace
{
using fnDeleteFileA = BOOL(WINAPI *)(LPCSTR);

fnDeleteFileA                      g_oDeleteFileA = ::DeleteFileA;
std::atomic<bool>                  s_installed{false};
std::atomic<bool>                  s_fired{false};
std::mutex                         s_mtx;
std::vector<std::function<void()>> s_callbacks;

void FireAll()
{
    // Snapshot under lock so a Register() racing with Fire can't see a
    // half-mutated vector. Run callbacks OUTSIDE the lock — they may call
    // back into Register (e.g. a stage-2 attach that itself schedules more
    // work) and we don't want to deadlock on the same mutex.
    std::vector<std::function<void()>> local;
    {
        std::lock_guard<std::mutex> lk(s_mtx);
        local.swap(s_callbacks);
    }
    for (auto &cb : local)
    {
        try { cb(); }
        catch (const std::exception &e)
        {
            spdlog::error("GGTB::Stage1Trigger: callback threw: {}", e.what());
        }
        catch (...)
        {
            spdlog::error("GGTB::Stage1Trigger: callback threw non-std exception");
        }
    }
}

BOOL WINAPI HookDeleteFileA(LPCSTR lpFileName)
{
    if (!s_fired.load() && lpFileName &&
        _stricmp(lpFileName, "error.txt") == 0)
    {
        // Set the flag BEFORE invoking callbacks so a callback that itself
        // triggers DeleteFileA won't recursively re-fire the chain.
        if (!s_fired.exchange(true))
        {
            spdlog::info("GGTB::Stage1Trigger: WinMain signal (error.txt) — firing stage-2");
            FireAll();
        }
    }
    return g_oDeleteFileA(lpFileName);
}
} // anonymous

bool Install()
{
    if (s_installed.load()) return true;

    g_oDeleteFileA = ::DeleteFileA;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    LONG e   = DetourAttach(reinterpret_cast<PVOID *>(&g_oDeleteFileA), HookDeleteFileA);
    LONG err = DetourTransactionCommit();
    if (err != NO_ERROR || e != NO_ERROR)
    {
        spdlog::error("GGTB::Stage1Trigger: DeleteFileA detour failed: commit={} attach={}",
                      err, e);
        return false;
    }

    s_installed.store(true);
    spdlog::info("GGTB::Stage1Trigger: armed; waiting for WinMain error.txt");
    return true;
}

void Register(std::function<void()> callback)
{
    if (!callback) return;

    // Two-phase check to avoid the race where FireAll drains the vector
    // between our check and our push_back:
    //   1) Take the lock, recheck s_fired. If still false, it's safe to
    //      enqueue — FireAll will see us next time it drains.
    //   2) If it already fired (either before the lock or while we were
    //      waiting for it), release the lock and run inline.
    bool runInline = false;
    {
        std::lock_guard<std::mutex> lk(s_mtx);
        if (s_fired.load())
            runInline = true;
        else
            s_callbacks.push_back(std::move(callback));
    }

    if (runInline)
    {
        try { callback(); }
        catch (const std::exception &e)
        {
            spdlog::error("GGTB::Stage1Trigger: late callback threw: {}", e.what());
        }
        catch (...)
        {
            spdlog::error("GGTB::Stage1Trigger: late callback threw non-std exception");
        }
    }
}

bool CancelPending()
{
    // The fired check and vector clear must be one operation with respect to
    // FireAll's swap. If fired is already true, the caller must keep every
    // callback-owned resource alive until the callback batch completes.
    std::lock_guard<std::mutex> lk(s_mtx);
    if (s_fired.load())
        return false;
    s_callbacks.clear();
    return true;
}

void Uninstall()
{
    if (s_installed.load())
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(reinterpret_cast<PVOID *>(&g_oDeleteFileA), HookDeleteFileA);
        DetourTransactionCommit();

        s_installed.store(false);
    }

    std::lock_guard<std::mutex> lk(s_mtx);
    s_callbacks.clear();
}
} // namespace GGTB::Stage1Trigger
