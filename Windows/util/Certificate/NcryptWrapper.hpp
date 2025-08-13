#pragma once
// 必须先定义这个，阻止 Windows.h 包含 <winsock.h>
#define _WINSOCKAPI_
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// 需要包含 <windows.h> 之前定义这些宏，避免冲突
#include <winsock2.h>  // Winsock2 API
#include <ws2tcpip.h>  // 若需要 getaddrinfo 等函数 先这俩顺序不能乱
#include <windows.h>
#include <wincrypt.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <ncrypt.h>
#include <bcrypt.h>
#include <mutex>
#include <condition_variable>
#include <fstream> 
#include <io.h>        // _open_osfhandle, _close
#include <fcntl.h>     // _O_RDWR, _O_BINARY
#include <future>
#include <queue>
#include <thread>
#include <atomic>
#include <memory>

/*
extern "C" {

// 临时屏蔽 inline 宏污染
#pragma push_macro("inline")
#undef inline
#include <General/ThirdParty/libssh/build/config.h>
#include <General/ThirdParty/libssh/include/libssh/agent.h>
#include <General/ThirdParty/libssh/include/libssh/callbacks.h>
#include <General/ThirdParty/libssh/include/libssh/pki.h>
#include <General/ThirdParty/libssh/include/libssh/string.h>
#include <General/ThirdParty/libssh/include/libssh/buffer.h>
#include <General/ThirdParty/libssh/include/libssh/options.h>


#pragma pop_macro("inline")

}*/

//------------------------------------------------------------------------------
// Constants and Error Codes RSAonly
    // Define missing constants for SSH_AGENT_RSA_SHA2_512 and SSH_AGENT_RSA_SHA2_256  
    //constexpr int SSH_AGENT_RSA_SHA2_256 = 2;  
    //constexpr int SSH_AGENT_RSA_SHA2_512 = 4;
//------------------------------------------------------------------------------
namespace ncrypt {
    // Provider names
    static constexpr LPCWSTR MS_PLATFORM_PROVIDER = L"Microsoft Platform Crypto Provider";

} // namespace ncrypt

//------------------------------------------------------------------------------
// Exception for NCrypt operations
//------------------------------------------------------------------------------
class NCryptException : public std::runtime_error {
public:
    NCryptException(const std::string &msg, SECURITY_STATUS code)
        : std::runtime_error(msg + " (0x" + std::to_string(code) + ")"), code_(code) {}
    SECURITY_STATUS code() const noexcept { return code_; }
private:
    SECURITY_STATUS code_;
};

//------------------------------------------------------------------------------
// Utility functions for string conversion
//------------------------------------------------------------------------------
namespace ncrypt_utils {
    std::wstring to_wstring(const std::string &utf8);
    std::string  to_string(const std::wstring &wstr);
}  // namespace ncrypt_utils

//------------------------------------------------------------------------------
// NCryptProvider: wraps NCryptOpenStorageProvider
//------------------------------------------------------------------------------
class NCryptProvider {
public:
    NCryptProvider(const std::wstring &providerName, DWORD flags = 0);
    ~NCryptProvider();

    NCRYPT_PROV_HANDLE handle() const noexcept { return hProv_; }

private:
    NCRYPT_PROV_HANDLE hProv_{ 0 };
};

//------------------------------------------------------------------------------
// NCryptKey: wraps NCryptCreatePersistedKey, export, property, sign, destroy
//------------------------------------------------------------------------------
class NCryptKey {
public:
    NCryptKey(NCRYPT_PROV_HANDLE prov,
              const std::wstring &keyName,
              const std::wstring &algorithm,
              DWORD legacyKeySpec = 0,
              DWORD flags = NCRYPT_SILENT_FLAG);
    ~NCryptKey();
    // Sign digest (precomputed SHA256/SHA512)
    void finalize(DWORD flags = 0);
    std::vector<BYTE> ExportKey(const std::wstring &BlobType) const;
    std::vector<BYTE> GetProperty(const std::wstring &property, DWORD flags = 0) const;
    std::vector<BYTE> SignHash(const std::vector<BYTE> &hash,
                               const std::wstring &hashAlg = L"",
                               DWORD flags = 0) const;
    void Destroy(DWORD flags = 0);

    NCRYPT_KEY_HANDLE handle() const noexcept { return hKey_; }

private:
    NCRYPT_KEY_HANDLE hKey_{ 0 };
};

//------------------------------------------------------------------------------
// Key enumeration result
//------------------------------------------------------------------------------
struct KeyDescriptor {
    std::wstring name;
    std::wstring algorithm;
    DWORD        legacySpec;
    DWORD        flags;
};

std::vector<KeyDescriptor>
enumKeys(NCRYPT_PROV_HANDLE prov,
         const std::wstring &scope = L"",
         DWORD flags = 0);

//------------------------------------------------------------------------------
// Public key marshalling (to OpenSSH wire format)
//------------------------------------------------------------------------------
// utils.go: unmarshalRSA/unmarshalECC → marshalPublicKey
// Go 源：utils.go 中的 unmarshalRSA 与 unmarshalECC
std::vector<BYTE> MarshalPublicKey(NCRYPT_KEY_HANDLE keyHandle);

std::vector<BYTE> MarshalPublicKeyHandleFromName(NCryptKeyName *pName);

static bool WriteBlobToFile(const std::wstring &fullPath, const std::vector<BYTE> &data);
bool ReadBlobFromFile(const std::wstring &fullPath, std::vector<BYTE> &outData, size_t maxSize);
bool EnsureDirectoryExists(const std::wstring &dir);
std::string base64_encode(const BYTE *data, size_t length);
std::string narrow(const std::wstring &wstr);
// Helper: 将二进制公钥 Blob 写为 ssh-rsa 文本格式

    //------------------------------------------------------------------------------
// NCryptSigner: wraps Signer functionality
//------------------------------------------------------------------------------
class NCryptSigner
{
   public:
    NCryptSigner(NCRYPT_KEY_HANDLE keyHandle, int pinTimeoutSeconds = 0);
    ~NCryptSigner();

    std::vector<BYTE> Sign(const std::vector<BYTE> &digest,
                            const std::wstring &hashAlg);
    std::vector<BYTE> PublicKey() const;
    void setPinTimeout(int seconds);

private:
    void purgePinCache();
    mutable std::mutex mutex_;
    NCRYPT_KEY_HANDLE keyHandle_{ 0 };
    int pinTimeoutSec_{ 0 };
    bool timerActive_{ false };
    std::vector<BYTE> pubKeyBlob_;
};

// namespace pageant
namespace sshagent {

// 异常类型
class AgentException : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// KeyManagerAgent
class KeyManagerAgent
{
   
public:
    KeyManagerAgent();
 ~KeyManagerAgent();

 bool InitDeployment();

    // Identity storage
    struct Identity
    {
        std::vector<BYTE> Blob;
        std::string Comment;
        NCRYPT_KEY_HANDLE hKey;  // 对应的 NCrypt key handle
    };

    void OpenProvider();
    void ReloadAllIdentities();
    // TPMService: RSA key pair lifecycle
    void CreateNewRSAKey(const std::wstring &containerName, int keySize);
    std::vector<BYTE> ExportRSAPublicKey(const std::wstring &containerName);
    bool DestroyRSAKey(const std::wstring &containerName);
    bool DeploySSH(const std::wstring &containerName, int keySize, const std::wstring &pubOutDir);
    bool CallNcryptAgent(const std::wstring &containerName);
    std::wstring GetExecutablePath(const std::wstring &modulePath);
    bool ExecuteProcess(const std::wstring &commandLine);
    // NEW: Deployment setup: ensure key exists, generate .PCPKEY silently, export public key
    //bool DeploySSH(const std::wstring &containerName, int keySize, const std::wstring &keyPassword, const std::wstring &pubOutDir);
    // NEW: Save issued SSH certificate blob to file alongside public key
    bool SaveCertificateFile(const std::wstring &containerName, const std::vector<BYTE> &certBlob,
                             const std::wstring &pubOutDir);
    void RemoveIdentityFromCache(const std::wstring &containerName);
    bool unDeploySSH(const std::wstring &containerName, const std::wstring &pubOutDir);
    bool ExportPublicKeyToFile(const std::wstring &containerName, const std::wstring &pubOutDir);
    // Agent integration: load cert and start NamedPipe listener
    //void LoadCertificateToAgent(const std::wstring &containerName, const std::wstring &certPath);

    // SSH operations: list keys, sign data
    std::vector<Identity> GetStoredIdentities() const { return identities_; }
    std::vector<Identity> ExportAllIdentities();

    //std::vector<unsigned char> SignDataByAgentKey(const std::vector<unsigned char> &pubkeyBlob, const std::vector<unsigned char> &data, int flags);
    // 添加公钥文件监控方法
    bool MonitorAndSignPubKey(const std::wstring &watchDir, const std::wstring &expectedFilename,
                              const std::string &principal);

private:
    // Internal helper for NCrypt operations
    // 禁用/启用 agent
    // 文件监控相关常量
    // 声明静态成员（不初始化）
    static const DWORD READ_BUF_SIZE;
    static const int STABLE_CHECKS;
    static const std::chrono::milliseconds STABLE_DELAY;
    static const std::chrono::seconds TOTAL_TIMEOUT;
    
    // 文件监控辅助方法
    bool ReadFileToBuffer(const std::wstring& path, std::vector<char>& out);
    HANDLE TryOpenFileReadExclusive(const std::wstring& path);
    bool WaitFileStable(const std::wstring& fullpath,
                       std::chrono::milliseconds perInterval = STABLE_DELAY,
                       int stableNeeded = STABLE_CHECKS,
                       std::chrono::seconds timeout = TOTAL_TIMEOUT);
    bool SignPubKeyWithCA(const std::wstring& pubPath, const std::vector<char>& pubContent, 
                          const std::string& principal);
    bool locked_{false};
    int pinTimeoutSec_{60};
    void Lock() { locked_ = true; }
    void Unlock() { locked_ = false; }
    NCRYPT_PROV_HANDLE hProv_{0};
    mutable std::mutex identities_mutex_;
    std::vector<Identity> identities_;
};


// Deployment check and marker (persisted flag)
class DeploymentChecker
{
public:
 bool IsDeployed();
 void MarkDeployed();
 void ClearDeployment(const std::wstring &containerName);
};
bool fileExists(const std::wstring &path);

// SSHCA: upload public key, download SSH certificate
//bool UploadPublicKey(const std::vector<BYTE> &pubBlob, const std::string &principal, const std::string &caEndpoint);
bool UploadPublicKey(const std::vector<BYTE> &pubBlob, const std::string &principal,
                     const std::string &caEndpoint);
bool DownloadSSHCertificate(const std::string &certUrl, const std::wstring &outPath);

// Cleanup workflow: revoke and cleanup
bool RevokeSSHCertificate(const std::string &caRevokeEndpoint, const std::wstring &certSerial);
void CleanupAll(const std::string &caRevokeEndpoint, const std::wstring &containerName);


} // namespace sshagent

