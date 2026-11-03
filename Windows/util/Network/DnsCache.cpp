#include "DnsCache.h"
#include <Windows.h>
#include <windns.h>
namespace zzj::network::windows {
bool FlushResolverCache() noexcept {
    const auto module = LoadLibraryExW(L"dnsapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) return false;
    using Flush = BOOL (WINAPI*)();
    const auto flush = reinterpret_cast<Flush>(GetProcAddress(module, "DnsFlushResolverCache"));
    const bool success = flush && flush() != FALSE;
    FreeLibrary(module);
    return success;
}
}
