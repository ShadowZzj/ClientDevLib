#pragma once
#include <windows.h>
#include <ncrypt.h>
#include <bcrypt.h>
#include <cstdint>
#include <string>
#include <tchar.h>
#include <vector>
#include <wincrypt.h>
#include <memory>
#include <tuple>
#include <General/util/SSH/Bytebuffer.hpp>
namespace zzj
{
namespace SSH
{
// SSH Agent signature flags (from draft-miller-ssh-agent-11 section 5.3)
enum class SSHSignatureFlags : uint32_t
{
    None = 0,
    RSA_SHA2_256 = 2,  // SSH_AGENT_RSA_SHA2_256
    RSA_SHA2_512 = 4   // SSH_AGENT_RSA_SHA2_512
};

// Enable bitwise OR for flags
inline SSHSignatureFlags operator|(SSHSignatureFlags a, SSHSignatureFlags b)
{
    return static_cast<SSHSignatureFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline SSHSignatureFlags operator&(SSHSignatureFlags a, SSHSignatureFlags b)
{
    return static_cast<SSHSignatureFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

}  // namespace SSH

class TPMKey
{
   public:
    TPMKey() {}
    ~TPMKey()
    {
        if (_hKey != NULL) NCryptFreeObject(_hKey);
        if (_hProv != NULL) NCryptFreeObject(_hProv);
    }
    int Create(const std::string &keycontainer, LPCWSTR algorithm = BCRYPT_RSA_ALGORITHM,
               DWORD keyLength = 2048, DWORD flags = NCRYPT_OVERWRITE_KEY_FLAG);
    int Delete();
    int Finalize();
    int Open(const std::string &keycontainer);
    int ExportPublicKeyX509(PCERT_PUBLIC_KEY_INFO *publicKeyInfo);
    int ExportPublicKeySSH(std::vector<uint8_t> &publicKey);
    template <typename T>
    int SetProperty(const std::wstring &property, const T &value)
    {
        return NCryptSetProperty(_hKey, (PWSTR)property.c_str(), (PBYTE)&value, sizeof(T), 0);
    }

    int GenerateCsrDerFormat(const std::string &certSubjectNameFullQualified,
                             std::vector<BYTE> &csrDer);
    int GenerateCsrPemFormat(const std::string &certSubjectNameFullQualified, std::string &csrPem);
    int SignDataSha256(const std::vector<char> &data, std::vector<char> &signature);
    std::tuple<int, bool> VerifyDataSha256(const std::vector<char> &data,
                                           const std::vector<char> &signature);
    // Sign data in SSH format (returns SSH-formatted signature blob)
    int SignSSH(const std::vector<uint8_t> &data, uint32_t flags,
                std::vector<uint8_t> &sshSignature);
    static int AssociateCertificate(PCCERT_CONTEXT pCertContext, const std::string &keycontainer);
    static std::shared_ptr<TPMKey> OpenTpmKeyFromCertificate(PCCERT_CONTEXT pCertContext);

    // Check if TPM is available on this device
    static bool IsTPMAvailable();

    // Get key handle for direct operations (e.g., signing)
    NCRYPT_KEY_HANDLE GetKeyHandle() const { return _hKey; }

   private:
    NCRYPT_KEY_HANDLE _hKey = NULL;
    NCRYPT_PROV_HANDLE _hProv = NULL;
};

};  // namespace zzj