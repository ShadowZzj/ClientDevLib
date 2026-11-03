#include "DomainName.h"

namespace zzj::network
{
std::optional<std::string> NormalizeDomainName(std::string_view name)
{
    if (!name.empty() && name.back() == '.')
        name.remove_suffix(1);
    if (name.empty() || name.size() > 253)
        return std::nullopt;

    std::string result;
    result.reserve(name.size());
    std::size_t labelLength = 0;
    for (const unsigned char c : name)
    {
        if (c == '.')
        {
            if (labelLength == 0 || result.back() == '-')
                return std::nullopt;
            labelLength = 0;
        }
        else
        {
            const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            if (!letter && !(c >= '0' && c <= '9') && c != '-')
                return std::nullopt;
            if ((labelLength == 0 && c == '-') || ++labelLength > 63)
                return std::nullopt;
        }
        result.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c));
    }
    if (labelLength == 0 || result.back() == '-')
        return std::nullopt;
    return result;
}

bool DomainPatternMatches(std::string_view pattern, std::string_view domain)
{
    const auto normalized = NormalizeDomainName(domain);
    const bool wildcard = pattern.size() >= 2 && pattern.substr(0, 2) == "*.";
    if (wildcard)
        pattern.remove_prefix(2);
    const auto suffix = NormalizeDomainName(pattern);
    if (!normalized || !suffix)
        return false;
    if (!wildcard)
        return *normalized == *suffix;
    return normalized->size() > suffix->size() + 1 &&
           (*normalized)[normalized->size() - suffix->size() - 1] == '.' &&
           normalized->compare(normalized->size() - suffix->size(), suffix->size(), *suffix) == 0;
}
} // namespace zzj::network
