#include "AES.h"
#include <General/ThirdParty/openssl/include/openssl/evp.h>
#include <General/ThirdParty/openssl/include/openssl/aes.h>
#include <General/ThirdParty/openssl/include/openssl/err.h>
#include <General/util/Exception/Exception.h>
namespace zzj
{


AES::AES(const std::string &key, const std::string &iv, Mode mode) : mode_(mode)
{
    std::string paddedKey = key;
    if (key.size() < 32)
    {
        paddedKey.append(32 - key.size(), '0');
    }
    else
    {
        paddedKey = key.substr(0, 32);
    }

    std::string paddedIv = iv;
    if (iv.size() < 16)
    {
        paddedIv.append(16 - iv.size(), '0');
    }
    else
    {
        paddedIv = iv.substr(0, 16);
    }

    key_.assign(paddedKey.begin(), paddedKey.end());
    iv_.assign(paddedIv.begin(), paddedIv.end());
}

std::string AES::Encrypt(const std::string &plainText)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
    {
        throw ZZJ_CRYPTO_EXCEPTION("Failed to create new cipher context");
    }

    const EVP_CIPHER *cipher = GetCipher(mode_);
    if (!cipher)
    {
        throw ZZJ_CRYPTO_EXCEPTION("Failed to get cipher");
    }

    if (!EVP_EncryptInit_ex(ctx, cipher, nullptr, key_.data(), iv_.data()))
    {
        throw ZZJ_CRYPTO_EXCEPTION("Failed to initialize encryption");
    }

    int len = 0;
    int outLen = 0;
    std::vector<unsigned char> out(plainText.size() + AES_BLOCK_SIZE);
    if (!EVP_EncryptUpdate(ctx, out.data(), &len,
                           reinterpret_cast<const unsigned char *>(plainText.data()),
                           plainText.size()))
    {
        throw ZZJ_CRYPTO_EXCEPTION("Failed to update encryption");
    }

    outLen += len;

    if (!EVP_EncryptFinal_ex(ctx, out.data() + outLen, &len))
    {
        throw ZZJ_CRYPTO_EXCEPTION("Failed to finalize encryption");
    }

    outLen += len;

    EVP_CIPHER_CTX_free(ctx);

    return std::string(out.begin(), out.begin() + outLen);
}

std::string AES::Decrypt(const std::string &cipherText)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
    {
        throw ZZJ_CRYPTO_EXCEPTION("Failed to create new cipher context");
    }

    const EVP_CIPHER *cipher = GetCipher(mode_);
    if (!cipher)
    {
        throw ZZJ_CRYPTO_EXCEPTION("Failed to get cipher");
    }

    if (!EVP_DecryptInit_ex(ctx, cipher, nullptr, key_.data(), iv_.data()))
    {
        throw ZZJ_CRYPTO_EXCEPTION("Failed to initialize decryption");
    }

    int len = 0;
    int outLen = 0;
    std::vector<unsigned char> out(cipherText.size() + AES_BLOCK_SIZE);
    if (!EVP_DecryptUpdate(ctx, out.data(), &len,
                           reinterpret_cast<const unsigned char *>(cipherText.data()),
                           cipherText.size()))
    {
        throw ZZJ_CRYPTO_EXCEPTION("Failed to update decryption");
    }

    outLen += len;

    if (!EVP_DecryptFinal_ex(ctx, out.data() + outLen, &len))
    {
        throw ZZJ_CRYPTO_EXCEPTION("Failed to finalize decryption");
    }

    outLen += len;

    EVP_CIPHER_CTX_free(ctx);

    return std::string(out.begin(), out.begin() + outLen);
}

const EVP_CIPHER *AES::GetCipher(Mode mode)
{
    switch (mode)
    {
        case Mode::ECB:
            return EVP_aes_256_ecb();
        case Mode::CBC:
            return EVP_aes_256_cbc();
        case Mode::OFB:
            return EVP_aes_256_ofb();
        case Mode::CTR:
            return EVP_aes_256_ctr();
        default:
            return nullptr;
    }
}

}  // namespace zzj
