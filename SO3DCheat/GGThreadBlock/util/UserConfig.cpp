#include "UserConfig.h"
#include "../Setting.h"
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <share.h>
#include <unordered_set>

namespace fs = std::filesystem;
using nlohmann::json;

namespace GGTB::UserConfig
{

namespace
{
// State guarded by s_mutex.
std::mutex                      s_mutex;
fs::path                        s_dllDir;            // <dll-dir>
fs::path                        s_userDir;           // <dll-dir>/GGConfig/_bootstrap then /<name>
std::string                     s_currentName;       // UTF-8, sanitised
std::atomic<bool>               s_ready{false};
std::unordered_set<std::string> s_whitelist;         // UTF-8 entries
std::atomic<int64_t>            s_pendingSaveAt{0};  // GetTickCount64() target; 0 = idle

constexpr const char *kLoggerName    = "ggtb";
constexpr const char *kBootstrapName = "_bootstrap";
constexpr const char *kConfigFile    = "config.json";
constexpr const char *kLogFile       = "ggtb.log";

// ClientDevLib's spdlog is built with the default filename_t = std::string,
// which means basic_file_sink calls fopen() and treats paths as ACP. CJK
// character names would be mojibake on disk. Defining SPDLOG_WCHAR_FILENAMES
// for just this target would ODR-conflict with the rest of the codebase, so
// instead we ship a tiny wide-path sink that opens the file via _wfsopen
// directly. spdlog's logger/registry don't depend on filename_t, so this is
// fully self-contained.
class WideFileSink final : public spdlog::sinks::base_sink<std::mutex>
{
  public:
    explicit WideFileSink(std::wstring path) : path_(std::move(path))
    {
        // _SH_DENYNO so other processes (tail-style viewers) can read it.
        fp_ = ::_wfsopen(path_.c_str(), L"ab", _SH_DENYNO);
    }

    ~WideFileSink() override
    {
        if (fp_) std::fclose(fp_);
    }

    bool ok() const { return fp_ != nullptr; }

  protected:
    void sink_it_(const spdlog::details::log_msg &msg) override
    {
        if (!fp_) return;
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);
        std::fwrite(formatted.data(), 1, formatted.size(), fp_);
    }

    void flush_() override
    {
        if (fp_) std::fflush(fp_);
    }

  private:
    std::wstring path_;
    FILE        *fp_ = nullptr;
};

// std::filesystem::path(const std::string&) on MSVC interprets narrow as ACP,
// so CJK UTF-8 input becomes mojibake on disk. Convert to wstring first.
fs::path Utf8ToPath(const std::string &u8)
{
    if (u8.empty()) return {};
    int wlen = MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return fs::path{u8}; // fallback; better than nothing
    std::wstring w(static_cast<size_t>(wlen - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), -1, w.data(), wlen);
    return fs::path{w};
}

std::string SanitizeName(const std::string &raw)
{
    std::string out;
    out.reserve(raw.size());
    for (unsigned char c : raw)
    {
        // ASCII control / Windows reserved
        if (c < 0x20) continue;
        switch (c)
        {
        case '<': case '>': case ':': case '"':
        case '/': case '\\': case '|': case '?': case '*':
            out.push_back('_');
            break;
        default:
            out.push_back(static_cast<char>(c));
        }
    }
    // trim leading/trailing spaces and dots (Windows hates trailing dots)
    while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
        out.pop_back();
    size_t start = 0;
    while (start < out.size() && out[start] == ' ') ++start;
    out.erase(0, start);
    if (out.size() > 64) out.resize(64);
    return out;
}

fs::path DllDirFromHandle(HMODULE h)
{
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(h, buf, MAX_PATH);
    return fs::path{buf}.parent_path();
}

void SwapLoggerTo(const fs::path &logPath)
{
    std::error_code ec;
    fs::create_directories(logPath.parent_path(), ec);
    try
    {
        spdlog::drop(kLoggerName);
        auto sink = std::make_shared<WideFileSink>(logPath.wstring());
        if (!sink->ok())
        {
            OutputDebugStringW(L"[GGTB::UserConfig] failed to open log file: ");
            OutputDebugStringW(logPath.c_str());
            return;
        }
        auto logger = std::make_shared<spdlog::logger>(kLoggerName, sink);
        logger->set_level(spdlog::level::info);
        logger->flush_on(spdlog::level::info);
        spdlog::set_default_logger(logger);
    }
    catch (const std::exception &e)
    {
        // can't really log this — logger swap failed. fall back to OutputDebugString.
        OutputDebugStringA("[GGTB::UserConfig] logger swap failed: ");
        OutputDebugStringA(e.what());
    }
}

void WriteConfigJsonLocked(Setting *setting)
{
    if (!s_ready.load() || !setting) return;

    json j;
    j["version"] = 1;

    json wl = json::array();
    for (auto &n : s_whitelist) wl.push_back(n);
    j["whitelist"] = std::move(wl);

    json mods = json::object();
    for (auto &mod : setting->GetModules())
    {
        if (!mod) continue;
        auto k = mod->ConfigKey();
        if (k.empty()) continue;
        json mj;
        mod->SaveState(mj);
        mods[k] = std::move(mj);
    }
    j["modules"] = std::move(mods);

    auto path = s_userDir / kConfigFile;
    auto tmp  = path;
    tmp += L".tmp";

    try
    {
        // C++17 path overload picks the wide-path ofstream on MSVC, no ACP round-trip.
        std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open())
        {
            spdlog::warn("GGTB::UserConfig: failed to open {} for write",
                         tmp.string());
            return;
        }
        ofs << j.dump(2);
        ofs.close();

        std::error_code ec;
        fs::rename(tmp, path, ec);
        if (ec)
        {
            // rename failed (e.g. dest exists on some filesystems) — try remove + rename
            fs::remove(path, ec);
            fs::rename(tmp, path, ec);
            if (ec)
                spdlog::warn("GGTB::UserConfig: rename {} -> {} failed: {}",
                             tmp.string(), path.string(), ec.message());
        }
    }
    catch (const std::exception &e)
    {
        spdlog::warn("GGTB::UserConfig: write {} failed: {}",
                     path.string(), e.what());
    }
}

// Loads config.json if present, calls LoadState on each module under lock.
// On parse failure: rename to config.json.broken-<ts> and continue with defaults.
void LoadConfigJsonLocked(Setting *setting)
{
    if (!setting) return;

    auto path = s_userDir / kConfigFile;
    std::error_code exists_ec;
    if (!fs::exists(path, exists_ec))
        return;

    json j;
    try
    {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open())
        {
            spdlog::warn("GGTB::UserConfig: cannot open {} for read", path.string());
            return;
        }
        ifs >> j;
    }
    catch (const std::exception &e)
    {
        auto broken = path;
        broken += L".broken-";
        broken += std::to_wstring(GetTickCount64());
        std::error_code ec;
        fs::rename(path, broken, ec);
        spdlog::error("GGTB::UserConfig: parse error on {}: {} — moved to {}",
                      path.string(), e.what(), broken.string());
        return;
    }

    if (j.contains("whitelist") && j["whitelist"].is_array())
    {
        s_whitelist.clear();
        for (auto &v : j["whitelist"])
            if (v.is_string()) s_whitelist.insert(v.get<std::string>());
    }

    if (j.contains("modules") && j["modules"].is_object())
    {
        auto &mods = j["modules"];
        for (auto &mod : setting->GetModules())
        {
            if (!mod) continue;
            auto k = mod->ConfigKey();
            if (k.empty() || !mods.contains(k)) continue;
            try
            {
                mod->LoadState(mods[k]);
            }
            catch (const std::exception &e)
            {
                spdlog::warn("GGTB::UserConfig: LoadState({}) threw: {}", k, e.what());
            }
        }
    }
}

} // anonymous

void Bootstrap(HMODULE hOwnerDll)
{
    std::lock_guard<std::mutex> lk(s_mutex);
    s_dllDir  = DllDirFromHandle(hOwnerDll);
    s_userDir = s_dllDir / "GGConfig" / kBootstrapName;

    std::error_code ec;
    fs::create_directories(s_userDir, ec);
    SwapLoggerTo(s_userDir / kLogFile);
    spdlog::info("GGTB::UserConfig: bootstrap logger ready at {}",
                 (s_userDir / kLogFile).string());
}

void Tick(Setting *setting)
{
    if (s_ready.load())
    {
        // Handle debounced save outside the heavy path.
        int64_t target = s_pendingSaveAt.load();
        if (target != 0 && static_cast<int64_t>(GetTickCount64()) >= target)
        {
            std::lock_guard<std::mutex> lk(s_mutex);
            // double-check under lock; another Tick may have raced
            if (s_pendingSaveAt.load() != 0 &&
                static_cast<int64_t>(GetTickCount64()) >= s_pendingSaveAt.load())
            {
                WriteConfigJsonLocked(setting);
                s_pendingSaveAt.store(0);
            }
        }
        return;
    }

    if (!setting || !setting->IsInitialized()) return;

    // GetLocalPlayerName() reads game memory. Setting::Init having run does NOT
    // imply a character is logged in; just means the menu has rendered once.
    std::string raw = GetLocalPlayerName();
    if (raw.empty()) return;

    std::string clean = SanitizeName(raw);
    if (clean.empty())
    {
        spdlog::warn("GGTB::UserConfig: name '{}' sanitised to empty, skipping", raw);
        return;
    }

    std::lock_guard<std::mutex> lk(s_mutex);
    if (s_ready.load()) return; // racy double-Tick; lost the race

    s_currentName = clean;
    s_userDir     = s_dllDir / "GGConfig" / Utf8ToPath(clean);

    std::error_code ec;
    fs::create_directories(s_userDir, ec);

    SwapLoggerTo(s_userDir / kLogFile);
    spdlog::info("GGTB::UserConfig: locked onto character '{}', logger swapped to {}",
                 clean, (s_userDir / kLogFile).string());

    LoadConfigJsonLocked(setting);

    s_ready.store(true);
    spdlog::info("GGTB::UserConfig: ready (whitelist size={})", s_whitelist.size());
}

bool IsReady() { return s_ready.load(); }

std::string CurrentName()
{
    std::lock_guard<std::mutex> lk(s_mutex);
    return s_currentName;
}

fs::path UserDir()
{
    std::lock_guard<std::mutex> lk(s_mutex);
    return s_userDir;
}

bool IsWhitelisted(const std::string &utf8name)
{
    if (utf8name.empty()) return false;
    std::lock_guard<std::mutex> lk(s_mutex);
    return s_whitelist.count(utf8name) > 0;
}

std::vector<std::string> GetWhitelist()
{
    std::lock_guard<std::mutex> lk(s_mutex);
    std::vector<std::string> out(s_whitelist.begin(), s_whitelist.end());
    std::sort(out.begin(), out.end());
    return out;
}

void AddWhitelist(const std::string &utf8name)
{
    if (utf8name.empty()) return;
    bool added = false;
    {
        std::lock_guard<std::mutex> lk(s_mutex);
        added = s_whitelist.insert(utf8name).second;
    }
    if (added)
    {
        spdlog::info("GGTB::UserConfig: whitelist + '{}'", utf8name);
        MarkDirty();
    }
}

void RemoveWhitelist(const std::string &utf8name)
{
    bool removed = false;
    {
        std::lock_guard<std::mutex> lk(s_mutex);
        removed = s_whitelist.erase(utf8name) > 0;
    }
    if (removed)
    {
        spdlog::info("GGTB::UserConfig: whitelist - '{}'", utf8name);
        MarkDirty();
    }
}

void MarkDirty()
{
    if (!s_ready.load()) return; // pre-login changes are irrelevant; default state
    s_pendingSaveAt.store(static_cast<int64_t>(GetTickCount64()) + 1000);
}

void SaveModuleStates(Setting *setting)
{
    std::lock_guard<std::mutex> lk(s_mutex);
    WriteConfigJsonLocked(setting);
    s_pendingSaveAt.store(0);
}

} // namespace GGTB::UserConfig
