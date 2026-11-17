#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace zzj
{
namespace SSH
{

// Forward declaration - SSHKey interface
// The actual SSHManager class is defined in General/REDpassSSHAgent
// This protocol handler only works with keys through their interface
class ISSHKey
{
   public:
    virtual ~ISSHKey() = default;
    virtual bool HasValidCertificate() const = 0;
    virtual const std::vector<uint8_t> &GetPublicKeyBlob() const = 0;
    virtual const std::vector<uint8_t> &GetCertificateBlob() const = 0;
    virtual const std::string &GetComment() const = 0;
    virtual std::vector<uint8_t> Sign(const std::vector<uint8_t> &data, uint32_t flags = 0) = 0;
};

// SSH Agent Protocol Handler
// Only handles protocol messages, does not manage key lifecycle
class SSHAgentProtocol
{
   private:
    std::vector<std::shared_ptr<ISSHKey>> keys;

    std::vector<uint8_t> HandleListIdentities();
    std::vector<uint8_t> HandleSignRequest(const std::vector<uint8_t> &message);
    std::vector<uint8_t> CreateFailureResponse();

   public:
    SSHAgentProtocol() = default;
    ~SSHAgentProtocol() = default;

    // Add a key to the agent
    void AddKey(std::shared_ptr<ISSHKey> key);

    // Handle incoming SSH agent protocol message
    std::vector<uint8_t> HandleMessage(const std::vector<uint8_t> &message);
};

}  // namespace SSH
}  // namespace zzj
