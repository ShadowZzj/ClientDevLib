#include "UserConfig.h"
#include "../Setting.h"
#include "../IModule.h"
#include "../entity/CLocalPlayer.h"
#include "AutoLoginSignal.h"

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
std::unordered_set<std::string> s_remoteWhitelist;   // UTF-8 entries, refreshed from broker sync file
uint64_t                        s_remoteLastLoadMs = 0;

constexpr const char *kLoggerName    = "ggtb";
constexpr const char *kBootstrapName = "_bootstrap";
constexpr const char *kConfigFile    = "config.json";
constexpr const char *kLogFile       = "ggtb.log";
constexpr const char *kManualWhitelistKey = "manualWhitelist";
constexpr const char *kRemoteWhitelistFile = "online_whitelist.json";
constexpr uint64_t    kRemoteWhitelistReloadMs = 5000;
constexpr int64_t     kDefaultRemoteWhitelistTtlMs = 30000;

// ClientDevLib's spdlog is built with the default filename_t = std::string,
// which means basic_file_sink calls fopen() and treats paths as ACP. CJK
// character names would be mojibake on disk. Defining SPDLOG_WCHAR_FILENAMES
// for just this target would ODR-conflict with the rest of the codebase, so
// instead we ship a tiny wide-path sink that opens the file via _wfsopen
// directly. spdlog's logger/registry don't depend on filename_t, so this is
// fully self-contained.
//
// 滚动 (rotation): 同一个 sink 自带,跟 spdlog::sinks::rotating_file_sink 一样
// 行为 —— 当前文件超过 kMaxBytes 时,按 ggtb.log → ggtb.1.log → ggtb.2.log
// (... → kMaxFiles-1) 整体右移,最旧的丢掉。这里没复用 rotating_file_sink
// 的原因还是 ACP/UTF-8 路径冲突,所以直接在 WideFileSink 上加 rotate 逻辑。
class WideFileSink final : public spdlog::sinks::base_sink<std::mutex>
{
  public:
    explicit WideFileSink(std::wstring path,
                          uint64_t     maxBytes  = 50ull * 1024 * 1024,
                          uint32_t     maxFiles  = 5)
        : path_(std::move(path))
        , maxBytes_(maxBytes)
        , maxFiles_(maxFiles ? maxFiles : 1)
    {
        // _SH_DENYNO so other processes (tail-style viewers) can read it.
        fp_ = ::_wfsopen(path_.c_str(), L"ab", _SH_DENYNO);
        if (fp_)
        {
            // ftell on append mode gives current EOF after open.
            std::fseek(fp_, 0, SEEK_END);
            long pos = std::ftell(fp_);
            currentBytes_ = (pos > 0) ? static_cast<uint64_t>(pos) : 0;
        }
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

        // 写之前先检查会不会超额。等号也走 rotate(下一条肯定超)。
        if (currentBytes_ + formatted.size() > maxBytes_ && currentBytes_ > 0)
            Rotate();

        if (!fp_) return; // rotate 失败兜底
        std::fwrite(formatted.data(), 1, formatted.size(), fp_);
        currentBytes_ += formatted.size();
    }

    void flush_() override
    {
        if (fp_) std::fflush(fp_);
    }

  private:
    // 构造 ggtb.N.log 这种 sibling 路径。N == 0 即主文件 (path_)。
    std::wstring NumberedPath(uint32_t n) const
    {
        if (n == 0) return path_;
        // 找最后一个 '.'(stem 和 ext 的分界),插入 ".<n>"
        // 例如 "ggtb.log" + n=2 → "ggtb.2.log"
        auto dot = path_.find_last_of(L'.');
        std::wstring base, ext;
        if (dot == std::wstring::npos)
        {
            base = path_;
            ext  = L"";
        }
        else
        {
            base = path_.substr(0, dot);
            ext  = path_.substr(dot); // includes '.'
        }
        return base + L"." + std::to_wstring(n) + ext;
    }

    // ggtb.log → ggtb.1.log → ggtb.2.log → ... → ggtb.{maxFiles_-1}.log
    // 末尾那个被丢掉。整体右移完后,重开 path_ (空文件)。
    //
    // 故意走 _wremove + _wrename 而不是 fs::rename,避免 std::filesystem
    // 异常和 spdlog 自身的错误处理纠缠。失败就吞掉 + OutputDebugString,
    // 整个 logging 进程必须继续工作。
    void Rotate()
    {
        std::fclose(fp_);
        fp_ = nullptr;

        // 从最旧的开始处理:删掉最末尾那一个,然后 N-1 → N, N-2 → N-1, ...
        std::wstring oldest = NumberedPath(maxFiles_ - 1);
        ::_wremove(oldest.c_str()); // OK if not exist

        for (uint32_t i = maxFiles_ - 1; i > 0; --i)
        {
            std::wstring src = NumberedPath(i - 1);
            std::wstring dst = NumberedPath(i);
            // _wrename 在 dst 已存在时会失败;前面 _wremove 已经把最末尾的
            // 拿掉了,而中间步骤每次循环都是把刚腾出的位置填掉,所以
            // dst 在循环里永远不存在。但仍 best-effort:失败也继续。
            ::_wrename(src.c_str(), dst.c_str());
        }

        // 重开主文件。注意这里用 "wb" 而不是 "ab" — 上一步 _wrename 已经把
        // 老的 path_ 挪走了,这是个全新文件;wb 显式截断到 0,即便 rename
        // 因为某种原因失败,也保证 currentBytes_ 计数从 0 重新开始一致。
        fp_ = ::_wfsopen(path_.c_str(), L"wb", _SH_DENYNO);
        currentBytes_ = 0;

        if (!fp_)
            OutputDebugStringA("[GGTB::UserConfig] rotate: reopen failed\n");
    }

    std::wstring path_;
    uint64_t     maxBytes_     = 0;
    uint32_t     maxFiles_     = 1;
    uint64_t     currentBytes_ = 0;
    FILE        *fp_           = nullptr;
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

fs::path GetEnvPath(const wchar_t *name)
{
    DWORD len = GetEnvironmentVariableW(name, nullptr, 0);
    if (!len)
        return {};
    std::wstring value(len, L'\0');
    DWORD written = GetEnvironmentVariableW(name, value.data(), len);
    if (!written)
        return {};
    if (!value.empty() && value.back() == L'\0')
        value.pop_back();
    return fs::path{value};
}

fs::path DefaultRemoteWhitelistPath()
{
    fs::path overridePath = GetEnvPath(L"GGTB_ONLINE_WHITELIST_FILE");
    if (!overridePath.empty())
        return overridePath;

    fs::path localAppData = GetEnvPath(L"LOCALAPPDATA");
    if (!localAppData.empty())
        return localAppData / "GGTB" / kRemoteWhitelistFile;

    if (!s_dllDir.empty())
        return s_dllDir / "GGConfig" / kRemoteWhitelistFile;

    return fs::path{kRemoteWhitelistFile};
}

void LoadRemoteWhitelistLocked(bool force = false)
{
    uint64_t now = GetTickCount64();
    if (!force && now - s_remoteLastLoadMs < kRemoteWhitelistReloadMs)
        return;
    s_remoteLastLoadMs = now;
    s_remoteWhitelist.clear();

    auto path = DefaultRemoteWhitelistPath();
    std::error_code ec;
    if (!fs::exists(path, ec))
        return;

    json j;
    try
    {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open())
            return;
        ifs >> j;
    }
    catch (const std::exception &e)
    {
        spdlog::warn("GGTB::UserConfig: remote whitelist read failed: {}", e.what());
        return;
    }

    int64_t nowEpochMs = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    int64_t fileTtlMs = kDefaultRemoteWhitelistTtlMs;
    if (j.contains("ttlMs") && j["ttlMs"].is_number_integer())
        fileTtlMs = std::max<int64_t>(1000, j["ttlMs"].get<int64_t>());
    else if (j.contains("expireMs") && j["expireMs"].is_number_integer())
        fileTtlMs = std::max<int64_t>(1000, j["expireMs"].get<int64_t>());

    auto addIfFresh = [&](const json &entry) {
        if (!entry.is_object())
            return;
        std::string name;
        if (entry.contains("name") && entry["name"].is_string())
            name = entry["name"].get<std::string>();
        else if (entry.contains("characterName") && entry["characterName"].is_string())
            name = entry["characterName"].get<std::string>();
        if (name.empty())
            return;

        int64_t expiresAt = 0;
        if (entry.contains("expiresAt") && entry["expiresAt"].is_number_integer())
            expiresAt = entry["expiresAt"].get<int64_t>();
        else if (entry.contains("lastSeen") && entry["lastSeen"].is_number_integer())
            expiresAt = entry["lastSeen"].get<int64_t>() + fileTtlMs;

        if (expiresAt > nowEpochMs)
            s_remoteWhitelist.insert(std::move(name));
    };

    if (j.contains("characters") && j["characters"].is_array())
    {
        for (const auto &entry : j["characters"])
            addIfFresh(entry);
    }
    else if (j.contains("whitelist") && j["whitelist"].is_array())
    {
        for (const auto &entry : j["whitelist"])
            addIfFresh(entry);
    }
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
    j[kManualWhitelistKey] = std::move(wl);

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

    if (j.contains(kManualWhitelistKey) && j[kManualWhitelistKey].is_array())
    {
        s_whitelist.clear();
        for (auto &v : j[kManualWhitelistKey])
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
    spdlog::info("GGTB::UserConfig: ready (manual whitelist size={})", s_whitelist.size());
    if (AutoLoginSignal::SignalCharacterReady())
    {
        spdlog::info("GGTB::UserConfig: signalled autologin character-ready event");
    }
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
    if (s_whitelist.count(utf8name) > 0)
        return true;
    LoadRemoteWhitelistLocked();
    return s_remoteWhitelist.count(utf8name) > 0;
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
