#include <General/util/Network/DomainName.h>
#include <General/util/Network/DnsWire.h>
#include <General/util/Network/IpAddress.h>

#include <cstdlib>
#include <iostream>
#include <random>

using namespace zzj::network;
namespace
{
std::size_t checks = 0;
#define CHECK(expression) do { ++checks; if (!(expression)) { std::cerr << "Failed line " << __LINE__ << ": " \
    << #expression << '\n'; std::exit(1); } } while (false)

std::vector<std::uint8_t> Query(std::uint16_t type = 1)
{
    std::vector<std::uint8_t> result{0x12, 0x34, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                                  2, 'O', 'A', 4, 'C', 'o', 'R', 'p', 7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 0,
                                  static_cast<std::uint8_t>(type >> 8), static_cast<std::uint8_t>(type), 0, 1};
    return result;
}
std::uint16_t Read16(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return static_cast<std::uint16_t>((bytes.at(offset) << 8) | bytes.at(offset + 1));
}
void DomainTests()
{
    CHECK(NormalizeDomainName("OA.CORP.Example.") == "oa.corp.example");
    CHECK(DomainPatternMatches("*.corp.example", "a.b.corp.example."));
    CHECK(DomainPatternMatches("OA.CORP.example.", "oa.corp.EXAMPLE"));
    CHECK(!DomainPatternMatches("*.corp.example", "corp.example"));
    CHECK(!DomainPatternMatches("*.corp.example", "evilcorp.example"));
    CHECK(!DomainPatternMatches("*.corp.example", "oa.corp.example.attacker.com"));
    CHECK(!DomainPatternMatches("corp.*", "corp.example"));
    for (const auto name : {"", ".", "..", "a..b", "a.b..", "-a.example", "a-.example", "a/b.example",
                            "a b.example", "a@b.example", "*.example", "a\xc3\xa9.example"})
        CHECK(!NormalizeDomainName(name));
    CHECK(!NormalizeDomainName(std::string(64, 'a') + ".example"));
    CHECK(NormalizeDomainName(std::string(63, 'a') + ".example"));
    CHECK(NormalizeDomainName("xn--bcher-kva.example"));
}
void IpTests()
{
    for (const auto address : {"0.0.0.0", "255.255.255.255", "198.18.0.1", "::", "::1", "2001:db8::abcd",
                              "2001:db8:1:2:3:4:5:6", "::ffff:192.0.2.1", "1:2:3:4:5:6:192.0.2.1"})
    {
        const auto parsed = IpAddress::Parse(address);
        CHECK(parsed);
        CHECK(IpAddress::Parse(parsed->ToString()) == parsed);
    }
    for (const auto address : {"", "192.1", "1.2.3.4.5", "256.0.0.1", "001.2.3.4", "+1.2.3.4", "1.2.3.4 ",
                              ":::", "1::2::3", "1:2:3:4:5:6:7:8:9", "1:2:3:4:5:6:7", "12345::", "[::1]",
                              "fe80::1%2", "1:2:3:4:5:6:7::8", "192.0.2.1::"})
        CHECK(!IpAddress::Parse(address));
    const auto subnet = IpNetwork::Parse("198.18.23.45/15");
    CHECK(subnet);
    CHECK(subnet->ToString() == "198.18.0.0/15");
    CHECK(subnet->Contains(*IpAddress::Parse("198.19.255.255")));
    CHECK(!subnet->Contains(*IpAddress::Parse("198.20.0.0")));
    CHECK(!subnet->Contains(*IpAddress::Parse("::ffff:c612:1")));
    CHECK(IpNetwork::Parse("0.0.0.0/0")->Contains(*IpAddress::Parse("255.255.255.255")));
    CHECK(IpNetwork::Parse("2001:db8::1/127")->Contains(*IpAddress::Parse("2001:db8::")));
    CHECK(!IpNetwork::Parse("2001:db8::1/127")->Contains(*IpAddress::Parse("2001:db8::2")));
    CHECK(IpNetwork::Parse("::/0")->Contains(*IpAddress::Parse("ffff:ffff::1")));
    CHECK(!IpNetwork::Parse("192.0.2.1/33"));
    CHECK(!IpNetwork::Parse("::/129"));
    CHECK(!IpNetwork::Parse("192.0.2.1"));
    CHECK(!IpNetwork::Parse("192.0.2.1/24/1"));
}
void DnsTests()
{
    const auto query = Query();
    const auto parsed = DnsWire::ParseQuery(query);
    CHECK(parsed);
    CHECK(parsed.question->id == 0x1234);
    CHECK(parsed.question->name == "oa.corp.example");
    CHECK(parsed.question->type == 1 && parsed.question->klass == 1);
    for (std::size_t size = 0; size < query.size(); ++size)
        CHECK(!DnsWire::ParseQuery(query.data(), size));
    CHECK(!DnsWire::ParseQuery(nullptr, query.size()));
    auto bad = query;
    bad[5] = 2;
    CHECK(DnsWire::ParseQuery(bad).error == DnsParseError::InvalidQuestionCount);
    bad = query;
    bad[12] = 0xc0;
    bad[13] = 12;
    CHECK(DnsWire::ParseQuery(bad).error == DnsParseError::InvalidName);
    bad[13] = 255;
    CHECK(!DnsWire::ParseQuery(bad));
    bad = query;
    bad[12] = 64;
    CHECK(!DnsWire::ParseQuery(bad));
    bad = query;
    bad.push_back(0);
    CHECK(DnsWire::ParseQuery(bad).error == DnsParseError::TrailingData);
    bad = query;
    bad[11] = 1;
    bad.insert(bad.end(), {0, 0, 41, 4, 0, 0, 0, 0, 0, 0, 0}); // EDNS OPT
    CHECK(DnsWire::ParseQuery(bad));
    bad.back() = 1; // advertised EDNS data exceeds the packet
    CHECK(!DnsWire::ParseQuery(bad));
    const auto answer = DnsWire::BuildAResponse(*parsed.question, {198, 18, 0, 1}, 0x10203040);
    CHECK(Read16(answer, 0) == 0x1234);
    CHECK(Read16(answer, 2) == 0x8180);
    CHECK(Read16(answer, 6) == 1);
    CHECK(Read16(answer, answer.size() - 6) == 4);
    CHECK(answer[answer.size() - 8] == 0x30 && answer[answer.size() - 7] == 0x40);
    CHECK(answer[answer.size() - 4] == 198 && answer.back() == 1);
    CHECK(DnsWire::ParseQuery(answer).error == DnsParseError::NotQuery);
    CHECK(DnsWire::ValidateResponse(answer.data(), answer.size(), *parsed.question));
    auto invalidResponse = answer;
    invalidResponse[1] ^= 1;
    CHECK(!DnsWire::ValidateResponse(invalidResponse.data(), invalidResponse.size(), *parsed.question));
    invalidResponse = answer;
    invalidResponse[14] = 'x';
    CHECK(!DnsWire::ValidateResponse(invalidResponse.data(), invalidResponse.size(), *parsed.question));
    invalidResponse = answer;
    invalidResponse.back() = 0; // a zero address remains a structurally valid DNS response
    CHECK(DnsWire::ValidateResponse(invalidResponse.data(), invalidResponse.size(), *parsed.question));
    invalidResponse[answer.size() - 5] = 5;
    CHECK(!DnsWire::ValidateResponse(invalidResponse.data(), invalidResponse.size(), *parsed.question));
    invalidResponse = answer;
    invalidResponse[2] |= 2; // TC: caller must fallback to TCP, never treat partial bytes as complete
    invalidResponse.resize(query.size());
    CHECK(!DnsWire::ValidateResponse(invalidResponse.data(), invalidResponse.size(), *parsed.question));
    CHECK(DnsWire::ValidateResponse(invalidResponse.data(), invalidResponse.size(), *parsed.question, true));
    auto question = *parsed.question;
    question.flags |= 0x20; // AD never survives a synthetic response
    CHECK((Read16(DnsWire::BuildNoDataResponse(question), 2) & 0x20) == 0);
    question.type = 28;
    CHECK(DnsWire::BuildAResponse(question, {198, 18, 0, 1}).empty());
    CHECK(Read16(DnsWire::BuildNoDataResponse(question), 6) == 0);
    question.name = std::string(64, 'a');
    CHECK(DnsWire::BuildNoDataResponse(question).empty());
    const auto error = DnsWire::BuildErrorResponse(query.data(), query.size(), DnsResponseCode::ServerFailure);
    CHECK(Read16(error, 0) == 0x1234 && (Read16(error, 2) & 0xf) == 2 && Read16(error, 4) == 1);
    const auto malformed = DnsWire::BuildErrorResponse(query.data(), 2, DnsResponseCode::FormatError);
    CHECK(malformed.size() == 12 && Read16(malformed, 0) == 0x1234 && Read16(malformed, 4) == 0);

    const auto generated = DnsWire::BuildQuery("OA.CORP.Example.", 0xabcd);
    const auto generatedQuestion = DnsWire::ParseQuery(generated);
    CHECK(generatedQuestion && generatedQuestion.question->id == 0xabcd &&
          generatedQuestion.question->name == "oa.corp.example");
    CHECK(DnsWire::BuildQuery("invalid..example", 1).empty());
    auto cnameResponse = DnsWire::BuildNoDataResponse(*parsed.question);
    cnameResponse[7] = 3;
    const auto wireName = [](const std::string& name) {
        const auto encoded = DnsWire::BuildQuery(name, 1);
        return std::vector<std::uint8_t>(encoded.begin() + 12, encoded.end() - 4);
    };
    const auto addRecord = [&](std::vector<std::uint8_t>& packet, const std::string& owner, std::uint16_t type,
                               const std::vector<std::uint8_t>& recordData) {
        const auto name = wireName(owner);
        packet.insert(packet.end(), name.begin(), name.end());
        packet.insert(packet.end(), {static_cast<std::uint8_t>(type >> 8), static_cast<std::uint8_t>(type),
                                    0, 1, 0, 0, 0, 60, static_cast<std::uint8_t>(recordData.size() >> 8),
                                    static_cast<std::uint8_t>(recordData.size())});
        packet.insert(packet.end(), recordData.begin(), recordData.end());
    };
    addRecord(cnameResponse, "oa.corp.example", 5, wireName("edge.example"));
    addRecord(cnameResponse, "unrelated.example", 1, {203, 0, 113, 99});
    addRecord(cnameResponse, "edge.example", 1, {192, 0, 2, 8});
    auto extracted = DnsWire::ExtractARecords(cnameResponse.data(), cnameResponse.size(), *parsed.question);
    CHECK(extracted && extracted->canonicalName == "edge.example" && extracted->addresses.size() == 1);
    CHECK(extracted->addresses.front() == (std::array<std::uint8_t, 4>{192, 0, 2, 8}));
    auto cnameLoop = DnsWire::BuildNoDataResponse(*parsed.question);
    cnameLoop[7] = 2;
    addRecord(cnameLoop, "oa.corp.example", 5, wireName("edge.example"));
    addRecord(cnameLoop, "edge.example", 5, wireName("oa.corp.example"));
    CHECK(!DnsWire::ExtractARecords(cnameLoop.data(), cnameLoop.size(), *parsed.question));
    auto additionalOnly = DnsWire::BuildNoDataResponse(*parsed.question);
    additionalOnly[11] = 1;
    addRecord(additionalOnly, "oa.corp.example", 1, {203, 0, 113, 99});
    extracted = DnsWire::ExtractARecords(additionalOnly.data(), additionalOnly.size(), *parsed.question);
    CHECK(extracted && extracted->addresses.empty());

    // Deterministic adversarial input sweep also checks malformed input always
    // produces bounded, header-only error replies with the original ID.
    std::mt19937 random(0x5a544e41);
    for (std::size_t i = 0; i < 10000; ++i)
    {
        std::vector<std::uint8_t> packet(random() % 512);
        for (auto& byte : packet)
            byte = static_cast<std::uint8_t>(random());
        const auto result = DnsWire::ParseQuery(packet);
        const auto reply = DnsWire::BuildErrorResponse(packet.data(), packet.size(), DnsResponseCode::FormatError);
        CHECK(!reply.empty() && reply.size() <= 512);
        if (packet.size() >= 2)
            CHECK(Read16(packet, 0) == Read16(reply, 0));
        if (!result)
            CHECK(reply.size() == 12 && Read16(reply, 4) == 0);
    }
}
} // namespace
int main()
{
    DomainTests();
    IpTests();
    DnsTests();
    std::cout << checks << " portable network checks passed\n";
}
