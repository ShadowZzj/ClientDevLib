#pragma once
#include <string>
#include <vector>
#include <exception>
#include <General/ThirdParty/openssl/include/openssl/evp.h>
#include <General/ThirdParty/openssl/include/openssl/aes.h>
#include <General/ThirdParty/openssl/include/openssl/err.h>
namespace zzj
{
class AES
{
   public:
    enum class Mode
    {
        ECB = 0,
        CBC = 1,
        OFB = 3,
        CTR = 4
    };
    AES(const std::string &key, const std::string &iv, Mode mode = Mode::CBC);

    std::string Encrypt(const std::string &plainText);
    std::string Decrypt(const std::string &cipherText);

   private:
    const EVP_CIPHER *GetCipher(Mode mode);
    std::vector<unsigned char> key_;
    std::vector<unsigned char> iv_;
    Mode mode_;
};

}  // namespace zzj
