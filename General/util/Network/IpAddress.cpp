#include "IpAddress.h"

#include <algorithm>
#include <charconv>
#include <vector>

namespace zzj::network
{
namespace
{
std::optional<unsigned int> Number(std::string_view value, unsigned int maximum, int base = 10)
{
    if (value.empty())
        return std::nullopt;
    unsigned int result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result, base);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result > maximum)
        return std::nullopt;
    return result;
}

std::optional<std::array<std::uint8_t, 4>> ParseV4(std::string_view text)
{
    std::array<std::uint8_t, 4> result{};
    for (std::size_t i = 0; i < result.size(); ++i)
    {
        const auto dot = text.find('.');
        if ((i < 3 && dot == std::string_view::npos) || (i == 3 && dot != std::string_view::npos))
            return std::nullopt;
        const auto token = text.substr(0, dot);
        if (token.size() > 1 && token.front() == '0')
            return std::nullopt;
        const auto value = Number(token, 255);
        if (!value)
            return std::nullopt;
        result[i] = static_cast<std::uint8_t>(*value);
        if (i < 3)
            text.remove_prefix(dot + 1);
    }
    return result;
}

bool ParseGroups(std::string_view text, std::vector<std::uint16_t>& groups, bool allowV4)
{
    if (text.empty())
        return true;
    while (true)
    {
        const auto colon = text.find(':');
        const auto token = text.substr(0, colon);
        if (token.empty())
            return false;
        if (token.find('.') != std::string_view::npos)
        {
            if (!allowV4 || colon != std::string_view::npos)
                return false;
            const auto address = ParseV4(token);
            if (!address)
                return false;
            groups.push_back(static_cast<std::uint16_t>(((*address)[0] << 8) | (*address)[1]));
            groups.push_back(static_cast<std::uint16_t>(((*address)[2] << 8) | (*address)[3]));
        }
        else
        {
            if (token.size() > 4)
                return false;
            const auto value = Number(token, 65535, 16);
            if (!value)
                return false;
            groups.push_back(static_cast<std::uint16_t>(*value));
        }
        if (groups.size() > 8)
            return false;
        if (colon == std::string_view::npos)
            return true;
        text.remove_prefix(colon + 1);
        if (text.empty())
            return false;
    }
}
} // namespace

std::optional<IpAddress> IpAddress::Parse(std::string_view text)
{
    if (text.empty() || text.size() > 45)
        return std::nullopt;
    IpAddress result;
    if (text.find(':') == std::string_view::npos)
    {
        const auto v4 = ParseV4(text);
        if (!v4)
            return std::nullopt;
        std::copy(v4->begin(), v4->end(), result.bytes.begin());
        return result;
    }
    result.family = IpFamily::V6;
    const auto compressed = text.find("::");
    std::vector<std::uint16_t> left, right;
    if (compressed == std::string_view::npos)
    {
        if (!ParseGroups(text, left, true) || left.size() != 8)
            return std::nullopt;
    }
    else
    {
        if (text.find("::", compressed + 2) != std::string_view::npos ||
            !ParseGroups(text.substr(0, compressed), left, false) ||
            !ParseGroups(text.substr(compressed + 2), right, true) || left.size() + right.size() >= 8)
            return std::nullopt;
        left.resize(8 - right.size(), 0);
        left.insert(left.end(), right.begin(), right.end());
    }
    for (std::size_t i = 0; i < 8; ++i)
    {
        result.bytes[i * 2] = static_cast<std::uint8_t>(left[i] >> 8);
        result.bytes[i * 2 + 1] = static_cast<std::uint8_t>(left[i]);
    }
    return result;
}

std::string IpAddress::ToString() const
{
    if (family == IpFamily::V4)
        return std::to_string(bytes[0]) + '.' + std::to_string(bytes[1]) + '.' +
               std::to_string(bytes[2]) + '.' + std::to_string(bytes[3]);
    std::array<std::uint16_t, 8> groups{};
    for (std::size_t i = 0; i < 8; ++i)
        groups[i] = static_cast<std::uint16_t>((bytes[2 * i] << 8) | bytes[2 * i + 1]);
    std::size_t bestStart = 8, bestLength = 1;
    for (std::size_t i = 0; i < 8;)
    {
        if (groups[i] != 0)
        {
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < 8 && groups[end] == 0)
            ++end;
        if (end - i > bestLength)
        {
            bestStart = i;
            bestLength = end - i;
        }
        i = end;
    }
    std::string result;
    for (std::size_t i = 0; i < 8;)
    {
        if (i == bestStart)
        {
            result += "::";
            i += bestLength;
            continue;
        }
        if (!result.empty() && result.back() != ':')
            result += ':';
        char buffer[4];
        const auto conversion = std::to_chars(buffer, buffer + sizeof(buffer), groups[i], 16);
        result.append(buffer, conversion.ptr);
        ++i;
    }
    return result;
}

std::optional<std::array<std::uint8_t, 4>> IpAddress::V4Bytes() const
{
    if (family != IpFamily::V4)
        return std::nullopt;
    return std::array<std::uint8_t, 4>{bytes[0], bytes[1], bytes[2], bytes[3]};
}

bool IpAddress::operator==(const IpAddress& other) const
{
    return family == other.family && std::equal(bytes.begin(), bytes.begin() + (family == IpFamily::V4 ? 4 : 16),
                                              other.bytes.begin());
}

std::optional<IpNetwork> IpNetwork::Parse(std::string_view text)
{
    const auto slash = text.find('/');
    if (slash == std::string_view::npos)
        return std::nullopt;
    const auto address = IpAddress::Parse(text.substr(0, slash));
    if (!address)
        return std::nullopt;
    const auto prefix = Number(text.substr(slash + 1), address->family == IpFamily::V4 ? 32 : 128);
    if (!prefix)
        return std::nullopt;
    IpNetwork result{*address, static_cast<std::uint8_t>(*prefix)};
    const std::size_t width = address->family == IpFamily::V4 ? 4 : 16;
    for (std::size_t i = 0; i < width; ++i)
    {
        const int remaining = static_cast<int>(*prefix) - static_cast<int>(i * 8);
        if (remaining <= 0)
            result.network.bytes[i] = 0;
        else if (remaining < 8)
            result.network.bytes[i] &= static_cast<std::uint8_t>(0xffu << (8 - remaining));
    }
    return result;
}

bool IpNetwork::Contains(const IpAddress& address) const
{
    if (network.family != address.family || prefixLength > (network.family == IpFamily::V4 ? 32 : 128))
        return false;
    for (std::size_t i = 0; i < (prefixLength + 7u) / 8u; ++i)
    {
        const unsigned int remaining = prefixLength - static_cast<unsigned int>(i * 8);
        const auto mask = static_cast<std::uint8_t>(remaining >= 8 ? 0xff : (0xffu << (8 - remaining)));
        if ((network.bytes[i] & mask) != (address.bytes[i] & mask))
            return false;
    }
    return true;
}

std::string IpNetwork::ToString() const
{
    return network.ToString() + '/' + std::to_string(prefixLength);
}
} // namespace zzj::network
