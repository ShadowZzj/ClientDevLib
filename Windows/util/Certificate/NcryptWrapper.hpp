#pragma once

// avoid winsock conflicts if needed (uncomment & configure globally if required)
// #define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <ncrypt.h>
#include <bcrypt.h>

#include <string>
#include <vector>
#include <stdexcept>
#include <mutex>
#include <condition_variable>
#include <fstream>
#include <io.h>
#include <fcntl.h>
#include <queue>
#include <thread>
#include <memory>
#include <unordered_map>
#include <chrono>  // 

namespace zzj
{
namespace sshagent
{

class KeyManagerAgent
{
public:
    struct NCryptProviderStruct
    {
        NCRYPT_PROV_HANDLE hProv{0};
        std::wstring providerName;
        DWORD flags{0};
    };

    struct NCryptKeyStruct
    {
        NCRYPT_KEY_HANDLE hKey{0};
        std::wstring name;
        std::wstring algorithm;
        DWORD legacyKeySpec{0};
        DWORD flags{0};
    };

    struct Identity
    {
        std::vector<BYTE> Blob;
        std::string Comment;
        NCRYPT_KEY_HANDLE hKey;
    };
    struct KeyDescriptor
    {
        std::wstring name;
        std::wstring algorithm;
        DWORD legacySpec;
        DWORD flags;
    };

    KeyManagerAgent();
    ~KeyManagerAgent();

    bool InitDeployment(const std::wstring &containerName);
    void OpenProvider();

    bool DestroyRSAKey(const std::wstring &containerName);
    bool DeploySSH(const std::wstring &containerName, int keySize, const std::wstring &pubOutDir);
    DWORD unDeploySSH(const std::wstring &containerName, const std::wstring &pubOutDir);

    //bool CallNcryptAgent(const std::wstring &containerName);
    std::wstring GetExecutablePath(const std::wstring &modulePath);
    DWORD ExecuteProcess();
    //DWORD ExecuteProcess(const std::wstring &commandLine, const std::wstring &containerName);
    //DWORD AssignProcToJobAndStore(HANDLE hProc, DWORD pid, const std::wstring &commandLine, const std::wstring &containerName);
    DWORD StopNcryptAgent(const std::wstring &containerName);
    void CleanupAllProcesses();

    DWORD MonitorAndSignPubKey(const std::wstring &watchDir, const std::wstring &expectedFilename,
                             const std::string &principal);

    bool EnsureDirectoryExists(const std::wstring &dir);

    bool UploadPublicKey(const std::vector<BYTE> &pubBlob, const std::string &principal,
                         const std::string &caEndpoint);
    bool DownloadSSHCertificate(const std::string &certUrl, const std::wstring &outPath);

    static HANDLE CreateSigningMarker(const std::wstring &markerPath);
    static void RemoveSigningMarkerHandle(HANDLE hMarker);
    static bool IsSigningMarkerPresent(const std::wstring &markerPath, std::chrono::seconds timeout,
                                       bool &staleOut);
    static bool WaitForMarkerRemovalOrCert(const std::wstring &markerPath,
                                           const std::wstring &certPath,
                                           std::chrono::seconds timeout);
    // Use simple name (class is in its own scope)
    std::vector<KeyDescriptor> enumKeys(NCRYPT_PROV_HANDLE prov, const std::wstring &scope,
                                        DWORD flags = 0);
    class DeploymentChecker
    {
       public:
        bool IsDeployed();
        void MarkDeployed();
        void ClearDeployment(const std::wstring &containerName);
        static bool WriteBlobToFile(const std::wstring &fullPath, const std::vector<BYTE> &data);
    };

    inline static const DWORD READ_BUF_SIZE = 4096;
    inline static const int STABLE_CHECKS = 3;
    inline static const std::chrono::milliseconds STABLE_DELAY = std::chrono::milliseconds(200);
    inline static const std::chrono::seconds TOTAL_TIMEOUT = std::chrono::seconds(30);

    class HandleRAII
    {
       public:
        HandleRAII(HANDLE h = nullptr) : h_(h) {}
        ~HandleRAII()
        {
            if (h_) CloseHandle(h_);
        }
        HandleRAII(const HandleRAII &) = delete;
        HandleRAII &operator=(const HandleRAII &) = delete;
        HandleRAII(HandleRAII &&o) noexcept : h_(o.h_) { o.h_ = nullptr; }
        HandleRAII &operator=(HandleRAII &&o) noexcept
        {
            if (this != &o)
            {
                if (h_) CloseHandle(h_);
                h_ = o.h_;
                h_ = o.h_;
                o.h_ = nullptr;
            }
            return *this;
        }
        HANDLE get() const { return h_; }
        HANDLE release()
        {
            HANDLE t = h_;
            h_ = nullptr;
            return t;
        }
        explicit operator bool() const { return h_ != nullptr; }

       private:
        HANDLE h_;
    };

    struct ProcessInfo
    {
        HandleRAII processHandle;
        HandleRAII jobHandle;
        std::wstring cmdline;
        DWORD pid = 0;
    };

private:
    bool ReadFileToBuffer(const std::wstring &path, std::vector<char> &out);
    bool WaitFileStable(const std::wstring &fullpath,
                        std::chrono::milliseconds perInterval = STABLE_DELAY,
                        int stableNeeded = STABLE_CHECKS,
                        std::chrono::seconds timeout = TOTAL_TIMEOUT);
    bool SignPubKeyWithCA(const std::wstring &pubPath, const std::vector<char> &pubContent,
                          const std::string &principal);

    bool locked_{false};
    int pinTimeoutSec_{60};
    void Lock() { locked_ = true; }
    void Unlock() { locked_ = false; }

    NCRYPT_PROV_HANDLE hProv_{0};
    NCryptProviderStruct providerInfo_;
    mutable std::mutex identities_mutex_;
    std::vector<Identity> identities_;

    std::unordered_map<std::wstring, ProcessInfo> m_activeProcesses;
    std::mutex m_processMutex;
};

}  // namespace sshagent
}  // namespace zzj
