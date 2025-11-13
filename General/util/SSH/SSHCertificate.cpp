#include "SSHCertificate.h"
#include <fstream>
#include <json.hpp>
#include <sstream>
#include <spdlog/spdlog.h>
#include <string>

using json = nlohmann::json;

namespace zzj
{
namespace SSH
{

bool SSHCertificate::loadFromFile(const std::string &certFilePath)
{
    std::ifstream file(certFilePath);
    if (!file.is_open())
    {
        spdlog::error("Certificate file not found: {}", certFilePath);
        return false;
    }

    std::string certLine;
    std::getline(file, certLine);
    file.close();

    if (certLine.empty())
    {
        spdlog::error("Certificate file is empty");
        return false;
    }

    // Parse: "ssh-rsa-cert-v01@openssh.com AAAAB3... comment"
    std::istringstream iss(certLine);
    std::string certType, certData;
    iss >> certType >> certData;

    // Base64 decode
    certificateBlob = zzj::Base64Help::Decode(certData);

    if (certificateBlob.empty())
    {
        spdlog::error("Failed to decode certificate");
        return false;
    }

    certificatePath = certFilePath;
    hasCertificate = true;

    spdlog::info("Certificate loaded: {} ({} bytes)", certType, certificateBlob.size());
    return true;
}

}  // namespace SSH
}  // namespace zzj
