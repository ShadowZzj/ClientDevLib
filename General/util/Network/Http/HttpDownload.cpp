#include "Http.h"
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>

namespace zzj
{
bool Http::DownloadFile(const std::string &url, const std::string &destination,
                        size_t maxBytes, const std::function<bool()> &cancelled)
{
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    if (url.compare(0, 8, "https://") != 0) return false;
    struct Download
    {
        std::ofstream file;
        size_t remaining;
        const std::function<bool()> &cancelled;
    } download{std::ofstream(std::filesystem::u8path(destination), std::ios::binary), maxBytes, cancelled};
    if (!download.file) return false;
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) return false;
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &download);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION,
        +[](void *data, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
            auto &d = *static_cast<Download *>(data);
            return d.cancelled && d.cancelled() ? 1 : 0;
        });
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &download);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION,
        +[](char *data, size_t size, size_t count, void *state) -> size_t {
            auto &d = *static_cast<Download *>(state);
            if (size && count > d.remaining / size) return 0;
            const auto bytes = size * count;
            d.file.write(data, static_cast<std::streamsize>(bytes));
            d.remaining -= bytes;
            return d.file ? bytes : 0;
        });
    const auto result = curl_easy_perform(curl.get());
    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    download.file.flush();
    return result == CURLE_OK && status == 200 && download.file.good();
}
}
