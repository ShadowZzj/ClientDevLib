#include "DnsWire.h"

#include <algorithm>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace zzj::network
{
namespace
{
std::uint16_t Read16(const std::uint8_t* data)
{
    return static_cast<std::uint16_t>((data[0] << 8) | data[1]);
}
void Write16(std::vector<std::uint8_t>& packet, std::uint16_t value)
{
    packet.push_back(static_cast<std::uint8_t>(value >> 8));
    packet.push_back(static_cast<std::uint8_t>(value));
}
void Write32(std::vector<std::uint8_t>& packet, std::uint32_t value)
{
    Write16(packet, static_cast<std::uint16_t>(value >> 16));
    Write16(packet, static_cast<std::uint16_t>(value));
}

bool ReadName(const std::uint8_t* data, std::size_t size, std::size_t& offset, std::string& name)
{
    std::size_t cursor = offset;
    bool jumped = false;
    std::vector<std::size_t> visited;
    for (std::size_t step = 0; step < 128; ++step)
    {
        if (cursor >= size || std::find(visited.begin(), visited.end(), cursor) != visited.end())
            return false;
        visited.push_back(cursor);
        const auto length = data[cursor++];
        if ((length & 0xc0) == 0xc0)
        {
            if (cursor >= size)
                return false;
            const auto destination = static_cast<std::size_t>(((length & 0x3f) << 8) | data[cursor++]);
            if (!jumped)
                offset = cursor;
            jumped = true;
            cursor = destination;
            continue;
        }
        if ((length & 0xc0) != 0 || length > 63)
            return false;
        if (length == 0)
        {
            if (!jumped)
                offset = cursor;
            if (name.empty())
                name = ".";
            return true;
        }
        if (length > size - cursor || name.size() + (name.empty() ? 0 : 1) + length > 253)
            return false;
        if (!name.empty())
            name.push_back('.');
        for (std::size_t i = 0; i < length; ++i)
        {
            const auto c = data[cursor++];
            // Host/service labels supported; do not reinterpret binary names
            // or dots embedded in a label as a policy hostname.
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_'))
                return false;
            name.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : static_cast<char>(c));
        }
    }
    return false;
}

bool WriteName(std::vector<std::uint8_t>& packet, std::string_view name)
{
    if (name == ".")
    {
        packet.push_back(0);
        return true;
    }
    if (name.empty() || name.size() > 253)
        return false;
    while (!name.empty())
    {
        const auto dot = name.find('.');
        const auto label = name.substr(0, dot);
        if (label.empty() || label.size() > 63)
            return false;
        packet.push_back(static_cast<std::uint8_t>(label.size()));
        for (const auto c : label)
        {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_'))
                return false;
            packet.push_back(static_cast<std::uint8_t>(c));
        }
        if (dot == std::string_view::npos)
            break;
        name.remove_prefix(dot + 1);
        if (name.empty())
            return false;
    }
    packet.push_back(0);
    return true;
}

std::vector<std::uint8_t> Response(const DnsQuestion& question, DnsResponseCode code, bool answer)
{
    std::vector<std::uint8_t> packet;
    Write16(packet, question.id);
    Write16(packet, static_cast<std::uint16_t>(0x8080 | (question.flags & 0x0110) |
                                            static_cast<std::uint8_t>(code)));
    Write16(packet, 1);
    Write16(packet, answer ? 1 : 0);
    Write16(packet, 0);
    Write16(packet, 0);
    if (!WriteName(packet, question.name))
        return {};
    Write16(packet, question.type);
    Write16(packet, question.klass);
    return packet;
}
} // namespace

DnsParseResult DnsWire::ParseQuery(const std::uint8_t* data, std::size_t size)
{
    const auto failure = [](DnsParseError error) { return DnsParseResult{std::nullopt, error}; };
    if (!data || size < 12 || size > 65535)
        return failure(DnsParseError::InvalidSize);
    const auto flags = Read16(data + 2);
    if (flags & 0x8000)
        return failure(DnsParseError::NotQuery);
    if ((flags & 0x7a4f) != 0) // nonstandard opcode, TC, reserved Z, or query RCODE
        return failure(DnsParseError::UnsupportedOperation);
    if (Read16(data + 4) != 1)
        return failure(DnsParseError::InvalidQuestionCount);
    if (Read16(data + 6) != 0 || Read16(data + 8) != 0 || Read16(data + 10) > 64)
        return failure(DnsParseError::InvalidRecord);

    DnsQuestion question;
    question.id = Read16(data);
    question.flags = flags;
    std::size_t offset = 12;
    if (!ReadName(data, size, offset, question.name))
        return failure(DnsParseError::InvalidName);
    if (size - offset < 4)
        return failure(DnsParseError::Truncated);
    question.type = Read16(data + offset);
    question.klass = Read16(data + offset + 2);
    offset += 4;
    bool haveOpt = false;
    for (std::size_t i = 0; i < Read16(data + 10); ++i)
    {
        std::string recordName;
        if (!ReadName(data, size, offset, recordName) || size - offset < 10)
            return failure(DnsParseError::InvalidRecord);
        if (Read16(data + offset) == 41)
        {
            if (haveOpt || recordName != ".")
                return failure(DnsParseError::InvalidRecord);
            haveOpt = true;
            question.udpPayloadSize = std::max<std::uint16_t>(512, Read16(data + offset + 2));
        }
        const auto dataLength = Read16(data + offset + 8);
        offset += 10;
        if (dataLength > size - offset)
            return failure(DnsParseError::InvalidRecord);
        offset += dataLength;
    }
    if (offset != size)
        return failure(DnsParseError::TrailingData);
    return {std::move(question), DnsParseError::None};
}

bool DnsWire::ValidateResponse(const std::uint8_t* data, std::size_t size,
                               const DnsQuestion& expected, bool allowTruncated)
{
    if (!data || size < 12 || size > 65535 || Read16(data) != expected.id || Read16(data + 4) != 1)
        return false;
    const auto flags = Read16(data + 2);
    if (!(flags & 0x8000) || (flags & 0x7840) != 0)
        return false;
    std::size_t offset = 12;
    std::string name;
    if (!ReadName(data, size, offset, name) || size - offset < 4 || name != expected.name ||
        Read16(data + offset) != expected.type || Read16(data + offset + 2) != expected.klass)
        return false;
    offset += 4;
    if (flags & 0x0200)
        return allowTruncated;
    const std::size_t count = static_cast<std::size_t>(Read16(data + 6)) + Read16(data + 8) + Read16(data + 10);
    if (count > size / 11)
        return false;
    for (std::size_t i = 0; i < count; ++i)
    {
        std::string recordName;
        if (!ReadName(data, size, offset, recordName) || size - offset < 10)
            return false;
        const auto dataLength = Read16(data + offset + 8);
        offset += 10;
        if (dataLength > size - offset)
            return false;
        offset += dataLength;
    }
    return offset == size;
}

std::vector<std::uint8_t> DnsWire::BuildQuery(std::string name, std::uint16_t id, std::uint16_t type)
{
    if (name.size() > 1 && name.back() == '.')
        name.pop_back();
    for (auto& c : name)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + 32);
    std::vector<std::uint8_t> packet;
    Write16(packet, id);
    Write16(packet, 0x0100); // recursion desired
    Write16(packet, 1);
    Write16(packet, 0);
    Write16(packet, 0);
    Write16(packet, 0);
    if (!WriteName(packet, name))
        return {};
    Write16(packet, type);
    Write16(packet, 1);
    return packet;
}

std::optional<DnsAddressRecords> DnsWire::ExtractARecords(const std::uint8_t* data, std::size_t size,
                                                         const DnsQuestion& expected)
{
    if (expected.type != 1 || expected.klass != 1 || !ValidateResponse(data, size, expected))
        return std::nullopt;
    DnsAddressRecords result{expected.name, {}};
    if ((Read16(data + 2) & 0x000f) != 0)
        return result;
    std::unordered_map<std::string, std::string> aliases;
    std::unordered_map<std::string, std::vector<std::array<std::uint8_t, 4>>> addresses;
    std::size_t offset = 12;
    std::string ignored;
    if (!ReadName(data, size, offset, ignored))
        return std::nullopt;
    offset += 4;
    for (std::size_t i = 0; i < Read16(data + 6); ++i)
    {
        std::string owner;
        if (!ReadName(data, size, offset, owner))
            return std::nullopt;
        const auto type = Read16(data + offset), klass = Read16(data + offset + 2);
        const auto length = Read16(data + offset + 8);
        offset += 10;
        if (type == 1 && klass == 1)
        {
            if (length != 4)
                return std::nullopt;
            std::array<std::uint8_t, 4> value{data[offset], data[offset + 1], data[offset + 2], data[offset + 3]};
            auto& values = addresses[owner];
            if (std::find(values.begin(), values.end(), value) == values.end())
                values.push_back(value);
        }
        else if (type == 5 && klass == 1)
        {
            std::string target;
            auto nameOffset = offset;
            if (!ReadName(data, size, nameOffset, target) || nameOffset != offset + length)
                return std::nullopt;
            const auto found = aliases.find(owner);
            if (found != aliases.end() && found->second != target)
                return std::nullopt;
            aliases[owner] = std::move(target);
        }
        offset += length;
    }
    std::unordered_set<std::string> visited;
    for (std::size_t step = 0; step < 16; ++step)
    {
        if (!visited.insert(result.canonicalName).second)
            return std::nullopt;
        const auto alias = aliases.find(result.canonicalName);
        const auto records = addresses.find(result.canonicalName);
        if (alias != aliases.end())
        {
            if (records != addresses.end())
                return std::nullopt;
            result.canonicalName = alias->second;
            continue;
        }
        if (records != addresses.end())
            result.addresses = records->second;
        return result;
    }
    return std::nullopt;
}

std::vector<std::uint8_t> DnsWire::BuildAResponse(const DnsQuestion& question,
                                               const std::array<std::uint8_t, 4>& address,
                                               std::uint32_t ttl)
{
    if (question.type != 1 || question.klass != 1)
        return {};
    auto packet = Response(question, DnsResponseCode::NoError, true);
    if (packet.empty())
        return packet;
    Write16(packet, 0xc00c); // answer name refers to our own, uncompressed question
    Write16(packet, 1);
    Write16(packet, 1);
    Write32(packet, ttl);
    Write16(packet, 4);
    packet.insert(packet.end(), address.begin(), address.end());
    return packet;
}

std::vector<std::uint8_t> DnsWire::BuildNoDataResponse(const DnsQuestion& question)
{
    return Response(question, DnsResponseCode::NoError, false);
}

std::vector<std::uint8_t> DnsWire::BuildErrorResponse(const std::uint8_t* data, std::size_t size,
                                                   DnsResponseCode code)
{
    const auto parsed = ParseQuery(data, size);
    if (parsed)
        return Response(*parsed.question, code, false);
    std::vector<std::uint8_t> packet;
    Write16(packet, data && size >= 2 ? Read16(data) : 0);
    Write16(packet, static_cast<std::uint16_t>(0x8080 | (data && size >= 4 ? Read16(data + 2) & 0x0100 : 0) |
                                            static_cast<std::uint8_t>(code)));
    packet.resize(12, 0);
    return packet;
}
} // namespace zzj::network
