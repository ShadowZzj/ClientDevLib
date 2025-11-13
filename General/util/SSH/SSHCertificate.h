#pragma once
#include <General/util/Crypto/Base64.hpp>
#include <string>
#include <vector>

namespace zzj
{
namespace SSH
{

// SSH Certificate Management
class SSHCertificate
{
   private:
    std::vector<uint8_t> certificateBlob;
    std::string certificatePath;
    bool hasCertificate = false;

   public:
    SSHCertificate() = default;
    ~SSHCertificate() = default;

    // Load certificate from file
    bool loadFromFile(const std::string &certFilePath);

    // Getters
    bool isValid() const { return hasCertificate; }
    const std::vector<uint8_t> &getBlob() const { return certificateBlob; }
    const std::string &getPath() const { return certificatePath; }
    void setPath(const std::string &path) { certificatePath = path; }
};

}  // namespace SSH
}  // namespace zzj

