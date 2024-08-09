#include "Certificate.h"
#include <General/util/Crypto/Base64.hpp>
std::vector<BYTE> zzj::Certificate::PEMToDer(const std::string &pem)
{
    const std::string beginCert = "-----BEGIN CERTIFICATE-----";
    const std::string endCert = "-----END CERTIFICATE-----";

    size_t start = pem.find(beginCert);
    size_t end = pem.find(endCert);
    if (start == std::string::npos || end == std::string::npos)
    {
        return std::vector<BYTE>();
    }

    start += beginCert.size();
    std::string base64 = pem.substr(start, end - start);
    //erace all the \n and \r 
    base64.erase(std::remove(base64.begin(), base64.end(), '\n'), base64.end());
    base64.erase(std::remove(base64.begin(), base64.end(), '\r'), base64.end());
    return zzj::Base64Help::Decode(base64);
}
