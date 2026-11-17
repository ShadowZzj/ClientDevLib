#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Http/Http.h"
#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif
#include <algorithm>
#include <chrono>
#include <exception>
#include <memory>
#include <mutex>
#include <set>

namespace zzj
{
namespace
{
using Clock = std::chrono::steady_clock;
using Easy = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
using Url = std::unique_ptr<CURLU, decltype(&curl_url_cleanup)>;
void Invalid(const char *message) { throw NetworkError("INVALID_ARGUMENT", message); }
std::string Lower(std::string s) { for (auto &c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A'; return s; }
bool Control(const std::string &s)
{
    return std::any_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
void Initialize()
{
    static std::once_flag once;
    static CURLcode result = CURLE_FAILED_INIT;
    std::call_once(once, [] { result = curl_global_init(CURL_GLOBAL_DEFAULT); });
    if (result != CURLE_OK) throw NetworkError("UNAVAILABLE", "Network library initialization failed");
    if (!(curl_version_info(CURLVERSION_NOW)->features & CURL_VERSION_ASYNCHDNS))
        throw NetworkError("UNSUPPORTED", "Asynchronous hostname resolution is unavailable");
}
template<class T> void Set(CURL *curl, CURLoption key, T value)
{
    if (curl_easy_setopt(curl, key, value) != CURLE_OK)
        throw NetworkError("UNSUPPORTED", "Required network option is unavailable");
}
std::string Part(CURLU *url, CURLUPart part)
{
    char *text = nullptr;
    if (curl_url_get(url, part, &text, 0) != CURLUE_OK) return {};
    std::unique_ptr<char, decltype(&curl_free)> owner(text, curl_free);
    return text;
}
void ValidateUrl(const std::string &text)
{
    if (text.empty() || text.size() > 4096 || Control(text) || text.find(' ') != std::string::npos)
        Invalid("URL must be a bounded HTTP(S) URL without whitespace");
    Url url(curl_url(), curl_url_cleanup);
    if (!url) throw NetworkError("UNAVAILABLE", "URL parser allocation failed");
    if (curl_url_set(url.get(), CURLUPART_URL, text.c_str(), 0) != CURLUE_OK) Invalid("Invalid HTTP(S) URL");
    const auto scheme = Lower(Part(url.get(), CURLUPART_SCHEME));
    if ((scheme != "http" && scheme != "https") || Part(url.get(), CURLUPART_HOST).empty()) Invalid("Only HTTP(S) URLs are supported");
    const auto authorityStart = text.find("://");
    if (authorityStart == std::string::npos) Invalid("URL must include http:// or https://");
    const auto authorityEnd = text.find_first_of("/?#", authorityStart + 3);
    if (text.find('@', authorityStart + 3) < authorityEnd)
        Invalid("URL userinfo is not supported; pass explicit headers");
    if (text.find('#') != std::string::npos) Invalid("URL fragments are not sent over HTTP; remove the fragment");
}
std::string HostUrl(const std::string &host, uint16_t port)
{
    if (host.empty() || host.size() > 253 || !port) Invalid("Host and nonzero port are required");
    for (unsigned char c : host)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '-' || c == '_' || c == ':' || c == '[' || c == ']'))
            Invalid("Host must be a DNS name, IPv4 or IPv6 literal, without URL or port; use punycode for IDNs");
    const auto authority = host.find(':') != std::string::npos && host.front() != '[' ? '[' + host + ']' : host;
    const auto url = "http://" + authority + ':' + std::to_string(port) + '/';
    ValidateUrl(url);
    return url;
}
Easy Prepare(const std::string &url, const NetworkOptions &o)
{
    Initialize();
    ValidateUrl(url);
    if (o.timeoutMs < 1 || o.timeoutMs > 120000) Invalid("Invalid network timeout");
    if (o.family != "auto" && o.family != "ipv4" && o.family != "ipv6") Invalid("Invalid address family");
    Easy handle(curl_easy_init(), curl_easy_cleanup);
    if (!handle) throw NetworkError("UNAVAILABLE", "Network request allocation failed");
    auto *curl = handle.get();
    Set(curl, CURLOPT_URL, url.c_str());
    Set(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    Set(curl, CURLOPT_PROXY, "");
    Set(curl, CURLOPT_FOLLOWLOCATION, 0L);
    Set(curl, CURLOPT_NETRC, long(CURL_NETRC_IGNORED));
    Set(curl, CURLOPT_HTTPAUTH, long(CURLAUTH_NONE));
    Set(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    Set(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    Set(curl, CURLOPT_NOSIGNAL, 1L);
    Set(curl, CURLOPT_CONNECTTIMEOUT_MS, o.timeoutMs);
    Set(curl, CURLOPT_TIMEOUT_MS, o.timeoutMs);
    Set(curl, CURLOPT_IPRESOLVE, long(o.family == "ipv4" ? CURL_IPRESOLVE_V4 : o.family == "ipv6" ? CURL_IPRESOLVE_V6 : CURL_IPRESOLVE_WHATEVER));
    Set(curl, CURLOPT_HTTP_VERSION, long(CURL_HTTP_VERSION_1_1));
    return handle;
}
CURLcode Perform(CURL *curl, const NetworkOptions &o)
{
    // Multi polling checks cancellation even while waiting for DNS/headers.
    std::unique_ptr<CURLM, decltype(&curl_multi_cleanup)> multi(curl_multi_init(), curl_multi_cleanup);
    if (!multi) throw NetworkError("UNAVAILABLE", "Network poll allocation failed");
    if (curl_multi_add_handle(multi.get(), curl) != CURLM_OK) throw NetworkError("UNAVAILABLE", "Network poll setup failed");
    struct Remove { CURLM *multi; CURL *curl; ~Remove() { curl_multi_remove_handle(multi, curl); } } remove{multi.get(), curl};
    const auto deadline = Clock::now() + std::chrono::milliseconds(o.timeoutMs);
    while (true)
    {
        if (o.cancelled && o.cancelled()) return CURLE_ABORTED_BY_CALLBACK;
        if (Clock::now() >= deadline) return CURLE_OPERATION_TIMEDOUT;
        int running = 0;
        if (curl_multi_perform(multi.get(), &running) != CURLM_OK) throw NetworkError("UNAVAILABLE", "Network poll failed");
        int pending = 0;
        while (auto *message = curl_multi_info_read(multi.get(), &pending))
            if (message->msg == CURLMSG_DONE) return message->data.result;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (curl_multi_poll(multi.get(), nullptr, 0, static_cast<int>(std::max<int64_t>(1, std::min<int64_t>(50, remaining))), nullptr) != CURLM_OK)
            throw NetworkError("UNAVAILABLE", "Network wait failed");
    }
}
std::string Error(CURLcode code)
{
    switch (code)
    {
    case CURLE_OK: return "";
    case CURLE_COULDNT_RESOLVE_HOST: return "DNS_RESOLUTION_FAILED";
    case CURLE_COULDNT_CONNECT: return "TCP_CONNECT_FAILED";
    case CURLE_OPERATION_TIMEDOUT: return "TIMEOUT";
    case CURLE_ABORTED_BY_CALLBACK: return "CANCELLED";
    case CURLE_PEER_FAILED_VERIFICATION: return "TLS_CERTIFICATE_INVALID";
    case CURLE_SSL_CONNECT_ERROR: return "TLS_HANDSHAKE_FAILED";
    default: return "CURL_" + std::to_string(static_cast<int>(code));
    }
}
NetworkTimings Timings(CURL *curl)
{
    double dns = 0, connect = 0, tls = 0, first = 0, total = 0;
    curl_easy_getinfo(curl, CURLINFO_NAMELOOKUP_TIME, &dns);
    curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &connect);
    curl_easy_getinfo(curl, CURLINFO_APPCONNECT_TIME, &tls);
    curl_easy_getinfo(curl, CURLINFO_STARTTRANSFER_TIME, &first);
    curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total);
    auto ms = [](double seconds) { return static_cast<int64_t>(std::max(0.0, seconds) * 1000); };
    return {ms(dns), ms(connect - dns), ms(tls - connect), ms(first), ms(total)};
}
std::string Remote(CURL *curl) { char *ip = nullptr; curl_easy_getinfo(curl, CURLINFO_PRIMARY_IP, &ip); return ip ? ip : ""; }
struct ResolveState { DnsLookupResult result; std::exception_ptr error; };
curl_socket_t CollectAddress(void *data, curlsocktype, curl_sockaddr *address) noexcept
{
    auto &state = *static_cast<ResolveState *>(data);
    try
    {
        char text[INET6_ADDRSTRLEN]{};
        const void *ip = address->family == AF_INET ? static_cast<const void *>(&reinterpret_cast<sockaddr_in *>(&address->addr)->sin_addr) :
            address->family == AF_INET6 ? static_cast<const void *>(&reinterpret_cast<sockaddr_in6 *>(&address->addr)->sin6_addr) : nullptr;
        if (ip && inet_ntop(address->family, ip, text, sizeof(text)))
        {
            auto &list = state.result.addresses;
            if (std::find(list.begin(), list.end(), text) == list.end())
            {
                if (list.size() < 64) list.push_back(text);
                else state.result.truncated = true;
            }
        }
    }
    catch (...) { state.error = std::current_exception(); }
    // libcurl then visits the next resolved address. Never creates a TCP socket.
    return CURL_SOCKET_BAD;
}
struct ResponseState { HttpResponse result; size_t bodyLimit, headerBytes = 0; std::exception_ptr error; };
size_t Body(char *data, size_t size, size_t count, void *user) noexcept
{
    auto &s = *static_cast<ResponseState *>(user);
    try
    {
        const auto length = size * count;
        const auto keep = std::min(length, s.bodyLimit - s.result.body.size());
        s.result.body.append(data, keep);
        if (keep < length) { s.result.bodyTruncated = true; return 0; }
        return length;
    }
    catch (...) { s.error = std::current_exception(); return 0; }
}
size_t Header(char *data, size_t size, size_t count, void *user) noexcept
{
    auto &s = *static_cast<ResponseState *>(user);
    try
    {
        const auto length = size * count;
        s.headerBytes += length;
        if (s.headerBytes > 4096 || s.result.headers.size() >= 64) { s.result.headersTruncated = true; return 0; }
        std::string line(data, length);
        if (line.rfind("HTTP/", 0) == 0) s.result.headers.clear(); // Final response replaces interim headers.
        else if (const auto colon = line.find(':'); colon != std::string::npos)
        {
            auto value = line.substr(colon + 1);
            const auto start = value.find_first_not_of(" \t");
            value = start == std::string::npos ? "" : value.substr(start);
            const auto end = value.find_last_not_of(" \t\r\n");
            value = end == std::string::npos ? "" : value.substr(0, end + 1);
            s.result.headers.emplace_back(line.substr(0, colon), std::move(value));
        }
        return length;
    }
    catch (...) { s.error = std::current_exception(); return 0; }
}
}
DnsLookupResult NetworkDiagnostics::Resolve(const std::string &host, const NetworkOptions &o)
{
    Initialize();
    auto curl = Prepare(HostUrl(host, 80), o);
    ResolveState state;
    Set(curl.get(), CURLOPT_CONNECT_ONLY, 1L);
    Set(curl.get(), CURLOPT_OPENSOCKETFUNCTION, CollectAddress);
    Set(curl.get(), CURLOPT_OPENSOCKETDATA, &state);
    const auto start = Clock::now();
    const auto code = Perform(curl.get(), o);
    if (state.error) std::rethrow_exception(state.error);
    state.result.resolved = code == CURLE_COULDNT_CONNECT && !state.result.addresses.empty();
    state.result.errorCode = state.result.resolved ? "" : Error(code);
    state.result.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    return state.result;
}
TcpConnectResult NetworkDiagnostics::Connect(const std::string &host, uint16_t port, const NetworkOptions &o)
{
    Initialize();
    auto curl = Prepare(HostUrl(host, port), o);
    Set(curl.get(), CURLOPT_CONNECT_ONLY, 1L);
    const auto code = Perform(curl.get(), o);
    return {code == CURLE_OK, Remote(curl.get()), Error(code), Timings(curl.get())};
}
HttpResponse Http::RequestBounded(const HttpRequestOptions &o)
{
    const std::set<std::string> methods{"GET", "HEAD", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"};
    if (!methods.count(o.method)) Invalid("Unsupported HTTP method");
    if (o.body.size() > 8192 || !o.maxResponseBytes || o.maxResponseBytes > 4096 || o.headers.size() > 32)
        Invalid("HTTP body/header limits exceeded");
    if ((o.method == "GET" || o.method == "HEAD") && !o.body.empty()) Invalid("GET and HEAD cannot include a body");
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(nullptr, curl_slist_free_all);
    auto append = [&](const std::string &value) {
        auto *next = curl_slist_append(headers.get(), value.c_str());
        if (!next) throw NetworkError("RESOURCE_EXHAUSTED", "Header allocation failed");
        headers.release(); headers.reset(next);
    };
    size_t headerBytes = 0;
    const std::set<std::string> reserved{"host", "content-length", "transfer-encoding", "connection", "upgrade", "proxy-authorization", "proxy-connection", "trailer", "te", "expect"};
    for (const auto &[name, value] : o.headers)
    {
        const std::string tokenChars = "!#$%&'*+-.^_`|~";
        if (name.empty() || name.size() > 128 || value.size() > 2048 || Control(value) || reserved.count(Lower(name)))
            Invalid("Invalid or reserved HTTP header");
        for (unsigned char c : name)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || tokenChars.find(c) != std::string::npos))
                Invalid("HTTP header name must be a token");
        headerBytes += name.size() + value.size();
        if (headerBytes > 8192) Invalid("HTTP headers exceed size limit");
        append(name + (value.empty() ? ";" : ": " + value));
    }
    append("Expect:"); // Avoid a second phase/automatic 100-continue negotiation.
    auto curl = Prepare(o.url, o);
    ResponseState state{}; state.bodyLimit = o.maxResponseBytes;
    Set(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    Set(curl.get(), CURLOPT_CUSTOMREQUEST, o.method.c_str());
    if (o.method == "HEAD") Set(curl.get(), CURLOPT_NOBODY, 1L);
    else if (o.method != "GET")
    {
        Set(curl.get(), CURLOPT_POSTFIELDS, o.body.data());
        Set(curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(o.body.size()));
    }
    Set(curl.get(), CURLOPT_WRITEFUNCTION, Body); Set(curl.get(), CURLOPT_WRITEDATA, &state);
    Set(curl.get(), CURLOPT_HEADERFUNCTION, Header); Set(curl.get(), CURLOPT_HEADERDATA, &state);
    const auto code = Perform(curl.get(), o);
    if (state.error) std::rethrow_exception(state.error);
    auto &r = state.result;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &r.statusCode);
    r.remoteAddress = Remote(curl.get()); r.timings = Timings(curl.get());
    r.transportOk = code == CURLE_OK || (code == CURLE_WRITE_ERROR && r.bodyTruncated && !r.headersTruncated);
    r.errorCode = r.headersTruncated ? "RESPONSE_HEADERS_TOO_LARGE" : r.transportOk ? "" : Error(code);
    return r;
}
}
