#include "SSHAgentProtocol.h"
#include <General/util/SSH/Bytebuffer.hpp>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

// SSH Agent protocol message types
#define SSH_AGENTC_REQUEST_IDENTITIES 11
#define SSH_AGENT_IDENTITIES_ANSWER 12
#define SSH_AGENTC_SIGN_REQUEST 13
#define SSH_AGENT_SIGN_RESPONSE 14
#define SSH_AGENT_FAILURE 5
#define SSH_AGENT_SUCCESS 6
#define SSH_AGENTC_EXTENSION 27

namespace zzj
{
namespace SSH
{

void SSHAgentProtocol::AddKey(std::shared_ptr<ISSHKey> key)
{
    if (key)
    {
        keys.push_back(key);
    }
}

std::vector<uint8_t> SSHAgentProtocol::HandleMessage(const std::vector<uint8_t> &message)
{
    if (message.empty())
    {
        return CreateFailureResponse();
    }

    uint8_t msgType = message[0];

    switch (msgType)
    {
        case SSH_AGENTC_REQUEST_IDENTITIES:
            spdlog::info("Received: REQUEST_IDENTITIES (11)");
            return HandleListIdentities();

        case SSH_AGENTC_SIGN_REQUEST:
            spdlog::info("Received: SIGN_REQUEST (13)");
            return HandleSignRequest(message);

        case SSH_AGENTC_EXTENSION:
            spdlog::info("Received: EXTENSION (27) - Not supported");
            return CreateFailureResponse();

        default:
            spdlog::warn("Received: Unknown message type {}", static_cast<int>(msgType));
            return CreateFailureResponse();
    }
}

std::vector<uint8_t> SSHAgentProtocol::HandleListIdentities()
{
    ByteBuffer response;
    response.writeByte(SSH_AGENT_IDENTITIES_ANSWER);

    // Count keys and certificates
    int count = 0;
    for (const auto &key : keys)
    {
        count++;  // Public key
        if (key->HasValidCertificate())
        {
            count++;  // Certificate
        }
    }

    spdlog::info("Listing {} identities", count);
    response.writeUint32(count);

    for (const auto &key : keys)
    {
        // Add public key
        response.writeString(key->GetPublicKeyBlob());
        response.writeString(key->GetComment());

        // Add certificate if available
        if (key->HasValidCertificate())
        {
            response.writeString(key->GetCertificateBlob());
            response.writeString(key->GetComment() + "-cert");
            spdlog::info("  + Certificate included");
        }
    }

    return response.data;
}

std::vector<uint8_t> SSHAgentProtocol::HandleSignRequest(const std::vector<uint8_t> &message)
{
    ByteBuffer buf;
    buf.data = message;
    buf.readPos = 1;

    auto keyBlob = buf.readString();
    auto dataToSign = buf.readString();
    uint32_t flags = buf.readUint32();

    spdlog::info("Sign request - blob size: {}, data size: {}, flags: {}", keyBlob.size(),
                 dataToSign.size(), flags);

    for (const auto &key : keys)
    {
        // Check if it matches public key or certificate
        bool matches = (key->GetPublicKeyBlob() == keyBlob);
        if (!matches && key->HasValidCertificate())
        {
            matches = (key->GetCertificateBlob() == keyBlob);
        }

        if (matches)
        {
            spdlog::info("Found matching key, signing...");

            auto signature = key->Sign(dataToSign, flags);
            if (!signature.empty())
            {
                ByteBuffer response;
                response.writeByte(SSH_AGENT_SIGN_RESPONSE);
                response.writeString(signature);

                spdlog::info("Sign successful!");
                return response.data;
            }
        }
    }

    spdlog::warn("Sign failed: key not found");
    return CreateFailureResponse();
}

std::vector<uint8_t> SSHAgentProtocol::CreateFailureResponse() { return {SSH_AGENT_FAILURE}; }

}  // namespace SSH
}  // namespace zzj
