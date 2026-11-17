#pragma once
namespace zzj::network::windows {
// Invalidate resolver answers after changing the subject's domain routing policy.
// This does not alter adapter DNS server configuration.
bool FlushResolverCache() noexcept;
}
