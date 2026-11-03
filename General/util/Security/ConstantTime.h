#pragma once
#include <string_view>

namespace zzj::security {
// Intended for equal-length, prehashed credential digests. Length is public;
// byte comparison has no input-dependent early return.
inline bool constantTimeEqual(std::string_view first, std::string_view second) noexcept {
    if (first.size() != second.size())
        return false;
    volatile unsigned char difference = 0;
    for (std::size_t i = 0; i < first.size(); ++i)
        difference = static_cast<unsigned char>(difference |
                     (static_cast<unsigned char>(first[i]) ^ static_cast<unsigned char>(second[i])));
    return difference == 0;
}
} // namespace zzj::security
