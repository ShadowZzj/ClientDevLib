#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace zzj
{
struct NetworkError : std::runtime_error
{
    std::string code;
    NetworkError(std::string code, const std::string &message) : std::runtime_error(message), code(std::move(code)) {}
};
struct NetworkOptions
{
    long timeoutMs = 5000;
    std::string family = "auto"; // auto / ipv4 / ipv6
    std::function<bool()> cancelled;
};
struct NetworkTimings
{
    int64_t dnsMs = 0, tcpMs = 0, tlsMs = 0, firstByteMs = 0, totalMs = 0;
};
struct DnsLookupResult
{
    bool resolved = false, truncated = false;
    std::vector<std::string> addresses;
    std::string errorCode;
    int64_t durationMs = 0;
};
struct TcpConnectResult
{
    bool connected = false;
    std::string remoteAddress, errorCode;
    NetworkTimings timings;
};
struct HttpRequestOptions : NetworkOptions
{
    std::string url, method = "GET", body;
    std::map<std::string, std::string> headers;
    size_t maxResponseBytes = 4096;
};
struct HttpResponse
{
    bool transportOk = false, bodyTruncated = false, headersTruncated = false;
    long statusCode = 0;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body, remoteAddress, errorCode;
    NetworkTimings timings;
};
class NetworkDiagnostics
{
  public:
    // Resolve through libcurl's configured resolver without opening a connection.
    // Includes hosts/cache results; this is not a raw DNS RR/TTL query.
    static DnsLookupResult Resolve(const std::string &host, const NetworkOptions &options = {});
    // Establish and immediately close TCP; sends no application payload.
    static TcpConnectResult Connect(const std::string &host, uint16_t port, const NetworkOptions &options = {});
};
}
