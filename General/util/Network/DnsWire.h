#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace zzj::network
{
enum class DnsParseError { None, InvalidSize, NotQuery, UnsupportedOperation, InvalidQuestionCount,
                           Truncated, InvalidName, InvalidRecord, TrailingData };
enum class DnsResponseCode : std::uint8_t { NoError = 0, FormatError = 1, ServerFailure = 2,
                                          NameError = 3, NotImplemented = 4, Refused = 5 };

struct DnsQuestion
{
    std::uint16_t id = 0;
    std::uint16_t flags = 0;
    std::string name;
    std::uint16_t type = 0;
    std::uint16_t klass = 0;
    std::uint16_t udpPayloadSize = 512; // EDNS OPT CLASS, clamped to the DNS minimum
};
struct DnsParseResult
{
    std::optional<DnsQuestion> question;
    DnsParseError error = DnsParseError::None;
    explicit operator bool() const { return question.has_value(); }
};
struct DnsAddressRecords
{
    std::string canonicalName;
    std::vector<std::array<std::uint8_t, 4>> addresses;
};

// DNS wire packets, without the two-byte TCP length prefix. Synthetic answers
// deliberately clear DNSSEC AD and omit EDNS; they are never signed answers.
class DnsWire
{
  public:
    static DnsParseResult ParseQuery(const std::uint8_t* data, std::size_t size);
    static DnsParseResult ParseQuery(const std::vector<std::uint8_t>& packet)
    { return ParseQuery(packet.data(), packet.size()); }
    // Validates QR/opcode, original ID and decoded question, every RR's bounds,
    // and exact message consumption. Truncated UDP replies may be recognized
    // by allowing a complete question with TC; callers must then use TCP and
    // must not consume that partial message as a final response.
    static bool ValidateResponse(const std::uint8_t* data, std::size_t size,
                                  const DnsQuestion& expected, bool allowTruncated = false);
    static std::vector<std::uint8_t> BuildQuery(std::string name, std::uint16_t id, std::uint16_t type = 1);
    // Only IN/A answers reachable from the expected name through validated
    // answer-section CNAMEs qualify. Unrelated/additional A records are ignored.
    // Empty addresses plus a changed canonicalName permits a caller to perform
    // a further physical DNS query with its own bounded recursion policy.
    static std::optional<DnsAddressRecords> ExtractARecords(const std::uint8_t* data, std::size_t size,
                                                            const DnsQuestion& expected);

    static std::vector<std::uint8_t> BuildAResponse(const DnsQuestion& question,
                                                  const std::array<std::uint8_t, 4>& address,
                                                  std::uint32_t ttl = 60);
    static std::vector<std::uint8_t> BuildNoDataResponse(const DnsQuestion& question);
    // Safe for malformed input. Preserves a readable ID and RD bit, and
    // includes a question only when the complete query passes validation.
    static std::vector<std::uint8_t> BuildErrorResponse(const std::uint8_t* data, std::size_t size,
                                                      DnsResponseCode code);
};
} // namespace zzj::network
