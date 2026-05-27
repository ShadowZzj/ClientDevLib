#include <Windows.h>
#include <TlHelp32.h>
#include <UserEnv.h>
#include <General/util/File/File.h>
#include <General/util/StrUtil.h>
#include <General/util/Sync/ProcessSync.hpp>
#include <Windows/util/Process/ProcessHelper.h>
#include <boost/filesystem.hpp>
#include <chrono>
#include <fstream>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Userenv.lib") 

namespace
{
struct EnumHWndsArg
{
    std::vector<HWND> *vecHWnds;
    DWORD dwProcessId;
};

BOOL CALLBACK EnumWindowsOfPid(HWND hwnd, LPARAM lParam)
{
    auto *pArg = reinterpret_cast<EnumHWndsArg *>(lParam);
    DWORD pid  = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == pArg->dwProcessId)
        pArg->vecHWnds->push_back(hwnd);
    return TRUE;
}

BOOL CALLBACK EnumAllWindows(HWND hwnd, LPARAM lParam)
{
    auto *pArg = reinterpret_cast<std::vector<HWND> *>(lParam);
    pArg->push_back(hwnd);
    return TRUE;
}

std::vector<HWND> GetWindowsByPid(DWORD pid)
{
    std::vector<HWND> ret;
    EnumHWndsArg arg{&ret, pid};
    EnumWindows(EnumWindowsOfPid, reinterpret_cast<LPARAM>(&arg));
    return ret;
}

std::vector<HWND> GetAllWindows()
{
    std::vector<HWND> ret;
    EnumWindows(EnumAllWindows, reinterpret_cast<LPARAM>(&ret));
    return ret;
}

bool IsUserLoggedIn(const std::string &username)
{
    const std::string target = "SO3D Plus|" + username;
    for (HWND w : GetAllWindows())
    {
        CHAR buf[MAX_PATH]{0};
        if (GetWindowTextA(w, buf, MAX_PATH) == 0)
            continue;
        if (target == buf)
            return true;
    }
    return false;
}

// Launches so3dplus.exe in the current dir, waits up to 30s for its window to
// show the default "SO3D Plus" title, then renames it to "SO3D Plus|<username>".
// groupIdx buckets accounts onto shared fake hardware identities: child
// inherits GGTB_HWFP_GROUP env var, which GGTB::HwFpSpoof::SeedPath reads to
// pick hwfp_seed_group<N>.bin. Launches are serial so overwriting the parent
// env var between calls is race-free.
// Returns true on successful rename.
bool LaunchAndRename(const std::string &username, int groupIdx)
{
    boost::filesystem::path exePath = zzj::GetExecutablePath();
    exePath /= "so3dplus.exe";
    exePath = boost::filesystem::absolute(exePath);

    if (!boost::filesystem::exists(exePath))
    {
        spdlog::error("so3dplus.exe not found at {}", exePath.string());
        return false;
    }

    std::wstring cmdLine = zzj::str::ansi2w(exePath.string()) +
                           L" \"^xhlrmxgkrhtlvdjdyTTdirmsgkrltlfjdy^\"";

    wchar_t groupStr[16];
    swprintf_s(groupStr, L"%d", groupIdx);
    SetEnvironmentVariableW(L"GGTB_HWFP_GROUP", groupStr);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    if (!CreateProcessW(NULL, cmdLine.data(), NULL, NULL, FALSE,
                        CREATE_UNICODE_ENVIRONMENT, NULL, NULL, &si, &pi))
    {
        spdlog::error("CreateProcess failed for {}: {}", username, GetLastError());
        return false;
    }

    spdlog::info("Launched so3dplus.exe pid={} for {} (hwfp group={})",
                 pi.dwProcessId, username, groupIdx);

    const std::string target = "SO3D Plus|" + username;
    auto start               = std::chrono::steady_clock::now();
    bool renamed             = false;

    while (true)
    {
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(30))
        {
            spdlog::error("Timeout waiting for window of {} pid={}", username,
                          pi.dwProcessId);
            zzj::Process::KillProcess(pi.dwProcessId);
            break;
        }

        zzj::Process proc(pi.dwProcessId, PROCESS_QUERY_LIMITED_INFORMATION);
        if (!proc.IsAlive())
        {
            spdlog::error("Process pid={} exited before window appeared", pi.dwProcessId);
            break;
        }

        for (HWND h : GetWindowsByPid(pi.dwProcessId))
        {
            CHAR titleBuf[MAX_PATH]{0};
            GetWindowTextA(h, titleBuf, MAX_PATH);
            if (std::string(titleBuf) == "SO3D Plus")
            {
                SetWindowTextA(h, target.c_str());
                spdlog::info("Renamed window to {}", target);
                renamed = true;
                break;
            }
        }
        if (renamed)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return renamed;
}
} // namespace

int main(int /*argc*/, char * /*argv*/[])
{
    zzj::ProcessSync processSync("mutex_ggthreadblock_starter");
    if (!processSync.try_lock())
    {
        spdlog::info("Another instance already running, exit");
        return 0;
    }

    boost::filesystem::path configPath = zzj::GetExecutablePath();
    configPath /= "autologin.txt";

    spdlog::info("GGThreadBlockStarter started, config={}", configPath.string());

    while (true)
    {
        try
        {
            std::ifstream ifs(configPath.string());
            if (!ifs.is_open())
            {
                spdlog::error("Cannot open {}", configPath.string());
                std::this_thread::sleep_for(std::chrono::seconds(5));
                continue;
            }

            std::vector<std::string> userNames;
            std::string line;
            while (std::getline(ifs, line))
            {
                // trim whitespace and CR
                auto l = line.find_first_not_of(" \t\r\n");
                auto r = line.find_last_not_of(" \t\r\n");
                if (l == std::string::npos)
                    continue;
                std::string name = line.substr(l, r - l + 1);
                if (name.empty() || name[0] == '#')
                    continue;
                userNames.push_back(name);
            }
            ifs.close();

            for (size_t i = 0; i < userNames.size(); ++i)
            {
                const auto &username = userNames[i];
                if (IsUserLoggedIn(username))
                {
                    spdlog::debug("{} already logged in", username);
                    continue;
                }
                int groupIdx = static_cast<int>(i / 3);
                spdlog::info("{} not logged in, launching (group={})", username, groupIdx);
                LaunchAndRename(username, groupIdx);
            }
        }
        catch (const std::exception &e)
        {
            spdlog::error("Loop exception: {}", e.what());
        }

        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    return 0;
}
