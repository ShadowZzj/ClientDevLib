#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace zzj::network
{
// Accepts ASCII DNS hostnames (including IDNA A-labels), strips one final dot,
// and folds case. Unicode callers must perform IDNA conversion first.
std::optional<std::string> NormalizeDomainName(std::string_view name);

// *.example.com matches one or more subdomain labels, never the apex or
// a suffix lacking a label boundary. Only a leading "*." wildcard is valid.
bool DomainPatternMatches(std::string_view pattern, std::string_view domain);
} // namespace zzj::network
