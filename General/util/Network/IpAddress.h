#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace zzj::network
{
enum class IpFamily : std::uint8_t { V4 = 4, V6 = 6 };

struct IpAddress
{
    IpFamily family = IpFamily::V4;
    // Network byte order; IPv4 occupies the first four bytes, the rest are zero.
    std::array<std::uint8_t, 16> bytes{};

    static std::optional<IpAddress> Parse(std::string_view text);
    std::string ToString() const;
    std::optional<std::array<std::uint8_t, 4>> V4Bytes() const;
    bool operator==(const IpAddress& other) const;
};

struct IpNetwork
{
    IpAddress network;
    std::uint8_t prefixLength = 0;

    // Explicit CIDR syntax required. Host bits are normalized to zero.
    static std::optional<IpNetwork> Parse(std::string_view text);
    bool Contains(const IpAddress& address) const;
    std::string ToString() const;
};
} // namespace zzj::network
