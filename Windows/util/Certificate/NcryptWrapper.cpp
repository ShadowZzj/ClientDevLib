#include "NcryptWrapper.hpp"
#include <bcrypt.h>
#include <chrono>
#include <AclAPI.h>
#include <cstdint>
#include <combaseapi.h> // Add this include for CoTaskMemFree
#include <strsafe.h>
#include <shlobj.h> // Add this include for FOLDERID_RoamingAppData
#include <General/util/SPDLogHelper.h>
#include <sstream>
#include <iomanip>
#include <Shlwapi.h>
#pragma Comment(lib, "Advapi32.lib")


//------------------------------------------------------------------------------
// Utility functions (utils.go -> to_wstring, to_string)
//------------------------------------------------------------------------------
std::wstring zzj::ncrypt_utils::to_wstring(const std::string &utf8) {
    // utils.go wide()
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring wstr(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wstr[0], len);
    return wstr;
}

std::string zzj::ncrypt_utils::to_string(const std::wstring &wstr) {
    // utils.go conversion back
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string str(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], len, nullptr, nullptr);
    return str;
}

//------------------------------------------------------------------------------
// NCryptProvider (ncrypt.go -> NCryptOpenStorageProvider)
//------------------------------------------------------------------------------
zzj::NCryptProvider::NCryptProvider(const std::wstring &providerName, DWORD flags)
{
    // ncrypt.go: NCryptOpenStorageProvider
    SECURITY_STATUS st = NCryptOpenStorageProvider(&hProv_, providerName.c_str(), flags);
    if (FAILED(st)) throw NCryptException("NCryptOpenStorageProvider failed", st);

}

zzj::NCryptProvider::~NCryptProvider()
{
    // ncrypt.go: NCryptFreeObject
    if (hProv_) NCryptFreeObject(hProv_);
}

//------------------------------------------------------------------------------
// NCryptKey (ncrypt.go -> NCryptCreatePersistedKey, NCryptOpenKey, NCryptFinalizeKey)
//------------------------------------------------------------------------------
zzj::NCryptKey::NCryptKey(NCRYPT_PROV_HANDLE prov,
                     const std::wstring &keyName,
                     const std::wstring &algorithm,
                     DWORD legacyKeySpec,
                     DWORD flags) {
    // ncrypt.go: NCryptCreatePersistedKey
    SECURITY_STATUS st = NCryptCreatePersistedKey(
        prov, &hKey_, algorithm.c_str(), keyName.c_str(), legacyKeySpec, flags);
    if (FAILED(st)) throw NCryptException("NCryptCreatePersistedKey failed", st);
}

zzj::NCryptKey::~NCryptKey()
{
    // ncrypt.go: NCryptFreeObject
    if (hKey_) NCryptFreeObject(hKey_);
}

void zzj::NCryptKey::finalize(DWORD flags)
{
    // ncrypt.go: NCryptFinalizeKey
    SECURITY_STATUS st = NCryptFinalizeKey(hKey_, flags);
    if (FAILED(st)) throw NCryptException("NCryptFinalizeKey failed", st);
}

//------------------------------------------------------------------------------
// exportKey (utils.go -> NCryptExportKey)
//------------------------------------------------------------------------------
std::vector<BYTE> zzj::NCryptKey::ExportKey(const std::wstring &BlobType) const
{
    // utils.go NCryptExportKey size
    DWORD size = 0;
    SECURITY_STATUS st = NCryptExportKey(
        hKey_, /* hExportKey */ 0,
        BlobType.c_str(), /* pParameterList */ nullptr,
        /* pbOutput */ nullptr, 0, &size, 0);
    if (FAILED(st)) throw NCryptException("NCryptExportKey size failed", st);

    // utils.go NCryptExportKey data
    std::vector<BYTE> buf(size);
    st = NCryptExportKey(
        hKey_, 0,
        BlobType.c_str(), nullptr,
        buf.data(), size, &size, 0);
    if (FAILED(st)) throw NCryptException("NCryptExportKey data failed", st);
    return buf;
}

//------------------------------------------------------------------------------
// getProperty (ncrypt.go -> NCryptGetProperty)
//------------------------------------------------------------------------------
std::vector<BYTE> zzj::NCryptKey::GetProperty(const std::wstring &property, DWORD flags) const
{
    DWORD size = 0;
    SECURITY_STATUS st = NCryptGetProperty(hKey_, property.c_str(), nullptr, 0, &size, flags);
    if (FAILED(st)) throw NCryptException("NCryptGetProperty size failed", st);

    std::vector<BYTE> buf(size);
    st = NCryptGetProperty(hKey_, property.c_str(), buf.data(), size, &size, flags);
    if (FAILED(st)) throw NCryptException("NCryptGetProperty data failed", st);
    return buf;
}

//------------------------------------------------------------------------------
// signHash (utils.go -> NCryptSignHash)
//------------------------------------------------------------------------------
std::vector<BYTE> zzj::NCryptKey::SignHash(const std::vector<BYTE> &hash,
                                      const std::wstring &hashAlg,
                                      DWORD flags) const {
    // utils.go NCryptSignHash size
    BCRYPT_PKCS1_PADDING_INFO padInfo{const_cast<LPWSTR>(hashAlg.c_str())};
    DWORD sigSize = 0;
    SECURITY_STATUS st = NCryptSignHash(
        hKey_, hashAlg.empty() ? nullptr : &padInfo,
        const_cast<BYTE*>(hash.data()), (DWORD)hash.size(),
        nullptr, 0, &sigSize, flags);
    if (FAILED(st)) throw NCryptException("NCryptSignHash size failed", st);

    // utils.go NCryptSignHash data
    std::vector<BYTE> sig(sigSize);
    st = NCryptSignHash(
        hKey_, hashAlg.empty() ? nullptr : &padInfo, const_cast<BYTE *>(sig.data()),
                        static_cast<DWORD>(hash.size()), sig.data(), sigSize, &sigSize, flags);
    if (FAILED(st)) throw NCryptException("NCryptSignHash data failed", st);
    sig.resize(sigSize);
    return sig;
}

void zzj::NCryptKey::Destroy(DWORD flags)
{
    // ncrypt.go NCryptDeleteKey
    SECURITY_STATUS st = NCryptDeleteKey(hKey_, flags);
    if (FAILED(st)) throw NCryptException("NCryptDeleteKey failed", st);
    hKey_ = 0; // reset handle
}

//------------------------------------------------------------------------------
// enumKeys (ncrypt.go -> NCryptEnumKeys)
//------------------------------------------------------------------------------
std::vector<zzj::sshagent::KeyDescriptor> zzj::sshagent::enumKeys(NCRYPT_PROV_HANDLE prov,
                                                   const std::wstring &scope, DWORD flags)
{
    std::vector<zzj::sshagent::KeyDescriptor> list;
    PVOID enumState = nullptr;
    while (true) {
        NCryptKeyName *pName = nullptr;
        // ncrypt.go NCryptEnumKeys
        SECURITY_STATUS st = NCryptEnumKeys(
            prov,
            scope.empty() ? nullptr : scope.c_str(),
            &pName, &enumState, flags);
        if (st == NTE_NO_MORE_ITEMS) break;
        if (FAILED(st)) throw zzj::NCryptException("NCryptEnumKeys failed", st);

        zzj::sshagent::KeyDescriptor kd{pName->pszName, pName->pszAlgid, pName->dwLegacyKeySpec,
                                        pName->dwFlags};
        list.push_back(kd);
        // ncrypt.go NCryptFreeBuffer
        NCryptFreeBuffer(pName);
    }
    return list;
}

zzj::sshagent::KeyManagerAgent::KeyManagerAgent() { 
    OpenProvider();
}

zzj::sshagent::KeyManagerAgent::~KeyManagerAgent() {
    // TODO: stop listener if running
    std::lock_guard<std::mutex> lk(identities_mutex_);
    for (auto &id : identities_)
    {
        if (id.hKey) NCryptFreeObject(id.hKey);
    }
    identities_.clear();
    // free provider
    if (hProv_) NCryptFreeObject(hProv_);
    CleanupAllProcesses();
}

bool zzj::sshagent::KeyManagerAgent::InitDeployment()
{
    std::wstring container = L"redpass-ssh";
    //std::wstring password = L"123456";
    wchar_t *roaming = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) return false;
    std::wstring pubDir = std::wstring(roaming) + L"\\nCryptAgent\\PublicKeys";
    CoTaskMemFree(roaming);
    return DeploySSH(container, 2048, pubDir);
}

void zzj::sshagent::KeyManagerAgent::CreateNewRSAKey(const std::wstring &containerName, int keySize)
{
    // Open storage provider
    NCryptProvider provider(ncrypt::MS_PLATFORM_PROVIDER);
    NCRYPT_PROV_HANDLE hProv = provider.handle();

    // Create persisted key
    NCRYPT_KEY_HANDLE hKey = 0;
    SECURITY_STATUS st =
        NCryptCreatePersistedKey(hProv, &hKey, BCRYPT_RSA_ALGORITHM, containerName.c_str(), 0, 0);
    if (FAILED(st)) throw NCryptException("NCryptCreatePersistedKey failed", st);

    // Set key length property
    st = NCryptSetProperty(hKey, NCRYPT_LENGTH_PROPERTY, reinterpret_cast<BYTE *>(&keySize),
                           sizeof(keySize), 0);
    if (FAILED(st))
    {
        NCryptFreeObject(hKey);
        throw NCryptException("NCryptSetProperty length failed", st);
    }

    // Finalize key
    st = NCryptFinalizeKey(hKey, 0);
    if (FAILED(st))
    {
        NCryptFreeObject(hKey);
        throw NCryptException("NCryptFinalizeKey failed", st);
    }

    // Close handle (persistent key remains)
    NCryptFreeObject(hKey);

}

std::vector<BYTE> zzj::sshagent::KeyManagerAgent::ExportRSAPublicKey(const std::wstring &containerName)
{
    // Open provider and key
    NCryptProvider provider(ncrypt::MS_PLATFORM_PROVIDER);
    NCRYPT_PROV_HANDLE hProv = provider.handle();
    NCRYPT_KEY_HANDLE hKey = 0;
    SECURITY_STATUS st = NCryptOpenKey(hProv, &hKey, containerName.c_str(), 0, 0);
    if (FAILED(st)) throw NCryptException("NCryptOpenKey failed", st);

    // Use helper to marshal
    std::vector<BYTE> Blob = MarshalPublicKey(hKey);

    NCryptFreeObject(hKey);
    return Blob;
}

bool zzj::sshagent::KeyManagerAgent::DestroyRSAKey(const std::wstring &containerName)
{
    NCryptProvider provider(ncrypt::MS_PLATFORM_PROVIDER);
    NCRYPT_PROV_HANDLE hProv = provider.handle();
    NCRYPT_KEY_HANDLE hKey = 0;
    SECURITY_STATUS st = NCryptOpenKey(hProv, &hKey, containerName.c_str(), 0, 0);
    if (FAILED(st))
    {
        // The key may not exist, which is not necessarily an error
        //LOG_WARNING("Key container not found: %ws", containerName.c_str());
        return false;
    }

    st = NCryptDeleteKey(hKey, 0);
    NCryptFreeObject(hKey);
    if (FAILED(st))
    {
        LOG_ERROR("Failed to delete key container");
        return false;
    }

    LOG_INFO("Successfully deleted key container");
    return true;
}

bool zzj::sshagent::KeyManagerAgent::DeploySSH(const std::wstring &containerName, int keySize,
                                          const std::wstring &pubOutDir)
{
    OpenProvider();
    // Ensure output directory
    if (!EnsureDirectoryExists(pubOutDir)) return false;

    // Check existing and export
    for (auto &kd : enumKeys(hProv_, L""))
    {
        if (kd.name == containerName)
        {
            ReloadAllIdentities();
            return true;
        }
    }
    // Create new key with password
    NCRYPT_KEY_HANDLE hKey = 0;
    //SECURITY_STATUS st = NCryptCreatePersistedKey(hProv_, &hKey, BCRYPT_RSA_ALGORITHM, containerName.c_str(), 0, NCRYPT_MACHINE_KEY_FLAG);
    /* User-level key, no administrator privileges required */
    SECURITY_STATUS st = NCryptCreatePersistedKey(hProv_, &hKey, BCRYPT_RSA_ALGORITHM, containerName.c_str(), 0, 0);
    if (FAILED(st)) return false;
    // Set length
    st = NCryptSetProperty(hKey, NCRYPT_LENGTH_PROPERTY, reinterpret_cast<BYTE *>(&keySize),
                           sizeof(keySize), 0);
    if (FAILED(st)){
        LOG_ERROR("NCryptCreatePersistedKey failed");
        return false;
    }
    auto cleanup = [&](){
        NCryptDeleteKey(hKey, 0);
        NCryptFreeObject(hKey);
    };
    // Set PIN/password
    //st = NCryptSetProperty(hKey, NCRYPT_PIN_PROPERTY, reinterpret_cast<BYTE *>(const_cast<wchar_t *>(keyPassword.c_str())), static_cast<DWORD>((keyPassword.size() + 1) * sizeof(wchar_t)), 0);
    if (FAILED(st))
    {
        NCryptFreeObject(hKey);
        return false;
    }
    // Finalize key: silent generate .PCPKEY file
    st = NCryptFinalizeKey(hKey, 0);
    if (FAILED(st))
    {
        NCryptFreeObject(hKey);
        return false;
    }
    NCryptFreeObject(hKey);
    bool ok = CallNcryptAgent(containerName);
    // if (ok) { // Sync identities_ cache ReloadAllIdentities(); }
    return ok;
}

// Add auxiliary functions for safely calling exe
bool zzj::sshagent::KeyManagerAgent::CallNcryptAgent(const std::wstring &containerName)
{
    try
    {
        // Get the current module path
        wchar_t modulePath[MAX_PATH];
        if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0)
        {
            LOG_ERROR("Failed to get module path");
            return false;
        }

        // Build exe path
        std::wstring exePath = GetExecutablePath(modulePath);

        // Verify that the file exists
        if (!PathFileExistsW(exePath.c_str()))
        {
            LOG_ERROR("NcryptAgent.exe not found at: " +
                      std::string(exePath.begin(), exePath.end()));
            return false;
        }

        // Build command line parameters
        std::wstring commandLine = L"\"" + exePath + L"\" \"" + containerName + L"\"";

        // Start a process (non-blocking)
        return ExecuteProcess(commandLine, containerName);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Exception in CallNcryptAgent: " + std::string(e.what()));
        return false;
    }
}

// Get the full path of NcryptAgent.exe
std::wstring zzj::sshagent::KeyManagerAgent::GetExecutablePath(const std::wstring &modulePath)
{
    // Derived the project root directory from the current module path
    std::wstring path = modulePath;
    // Remove the file name and get the directory
    size_t lastSlash = path.find_last_of(L'\\');
    if (lastSlash != std::wstring::npos)
    {
        path = path.substr(0, lastSlash);
    }
    std::wstring exePath = path + L"\\NcryptAgent.exe";

    wchar_t canonicalPath[MAX_PATH];
    if (PathCanonicalizeW(canonicalPath, exePath.c_str()))
    {
        return std::wstring(canonicalPath);
    }

    return exePath;
}


bool zzj::sshagent::KeyManagerAgent::ExecuteProcess(const std::wstring &commandLine, const std::wstring &containerName)
{
    STARTUPINFOW si = {};
    PROCESS_INFORMATION pi = {};
    si.cb = sizeof(si);


    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    // Create a modifiable copy of the command line
    std::vector<wchar_t> cmdLine(commandLine.begin(), commandLine.end());
    cmdLine.push_back(L'\0');

    // Create a process
    BOOL success = CreateProcessW(nullptr,                              // Application name
                                  cmdLine.data(),                       // Command line
                                  nullptr,                              // Process safety attributes
                                  nullptr,                              // Thread safety attributes
                                  FALSE,                                // Do not inherit handles
                                  CREATE_NO_WINDOW | DETACHED_PROCESS,  // Create a detached process
                                  nullptr,                              // Environment variables
                                  nullptr,                              // Current directory
                                  &si,                                  // Startup information
                                  &pi                                   // Process information
    );

    if (!success)
    {
        DWORD error = GetLastError();
        LOG_ERROR("CreateProcess failed with error: " + std::to_string(error));
        return false;
    }

    //Check if the process started successfully (wait briefly)
    DWORD waitResult = WaitForSingleObject(pi.hProcess, 1000); 
    if (waitResult == WAIT_OBJECT_0)
    {
        // The process ended quickly, possibly startup failure.
        DWORD exitCode;
        if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode != 0)
        {
            LOG_ERROR("NcryptAgent.exe failed to start, exit code: " + std::to_string(exitCode));
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return false;
        }
    }
    // Save the process handle for subsequent management
    {
        std::lock_guard<std::mutex> lock(m_processMutex);
        // If a process with the same name already exists, close the old one first
        auto it = m_activeProcesses.find(containerName);
        if (it != m_activeProcesses.end())
        {
            TerminateProcess(it->second, 0);
            CloseHandle(it->second);
        }
        m_activeProcesses[containerName] = pi.hProcess;
    }

    // Close the thread handle (no need to save)
    CloseHandle(pi.hThread);

    LOG_INFO("NcryptAgent.exe started successfully for container redpass-ssh " );
    return true;

}

bool zzj::sshagent::KeyManagerAgent::StopNcryptAgent(const std::wstring &containerName)
{
    std::lock_guard<std::mutex> lock(m_processMutex);

    auto it = m_activeProcesses.find(containerName);
    if (it == m_activeProcesses.end())
    {
        LOG_INFO("No active process found for container: " +
                    std::string(containerName.begin(), containerName.end()));
        return true;  
    }

    HANDLE hProcess = it->second;
    bool success = true;

    // Check if the process is still running
    DWORD exitCode;
    if (GetExitCodeProcess(hProcess, &exitCode))
    {
        if (exitCode == STILL_ACTIVE)
        {
            if (!TerminateProcess(hProcess, 0))
            {
                LOG_ERROR("Failed to terminate NcryptAgent process");
                success = false;
            }
            else
            {
                WaitForSingleObject(hProcess, 1000);  
            }
        }
    }

    CloseHandle(hProcess);
    m_activeProcesses.erase(it);

    if (success)
    {
        LOG_INFO("NcryptAgent.exe stopped for container redpass-ssh " );
    }

    return success;
}

void zzj::sshagent::KeyManagerAgent::CleanupAllProcesses()
{
    std::lock_guard<std::mutex> lock(m_processMutex);

    for (auto &pair : m_activeProcesses)
    {
        const std::wstring &containerName = pair.first;
        HANDLE hProcess = pair.second;

        DWORD exitCode;
        if (GetExitCodeProcess(hProcess, &exitCode) && exitCode == STILL_ACTIVE)
        {
            LOG_INFO("Terminating NcryptAgent for container: " +
                     std::string(containerName.begin(), containerName.end()));
            TerminateProcess(hProcess, 0);
            WaitForSingleObject(hProcess, 3000);  // 等待3秒
        }
        CloseHandle(hProcess);
    }

    m_activeProcesses.clear();
    LOG_INFO("All NcryptAgent processes cleaned up");
}

// Check if the file exists
bool zzj::sshagent::KeyManagerAgent::fileExists(const std::wstring &path)
{
    DWORD attrs = GetFileAttributesW(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
}

// Remove the identity from the cache
void zzj::sshagent::KeyManagerAgent::RemoveIdentityFromCache(const std::wstring &containerName)
{
    std::lock_guard<std::mutex> lk(identities_mutex_);
    std::string containerNameStr = ncrypt_utils::to_string(containerName); 
    for (auto it = identities_.begin(); it != identities_.end();)
    {
        if (it->Comment == containerNameStr)
        {
            if (it->hKey)
            {
                NCryptFreeObject(it->hKey);
                it->hKey = 0;
            }
            it = identities_.erase(it);
            LOG_INFO("Removed identity from cache");
        }
        else
        {
            ++it;
        }
    }
}

bool zzj::sshagent::KeyManagerAgent::unDeploySSH(const std::wstring &containerName,
                                            const std::wstring &pubOutDir)
{
    bool success = true;
    if (!StopNcryptAgent(containerName))
    {
        LOG_ERROR("Failed to stop NcryptAgent process");
        success = false;
    }

    if (!DestroyRSAKey(containerName))
    {
        LOG_ERROR("Failed to delete key container");
        success = false;
    }
    RemoveIdentityFromCache(containerName);

    std::wstring pubKeyPath = pubOutDir + L"\\" + containerName + L".pub";
    if (fileExists(pubKeyPath))
    {
        if (!DeleteFileW(pubKeyPath.c_str()))
        {
            LOG_ERROR("Failed to delete public key file");
            success = false;
        }
    }

    std::wstring certPath = pubOutDir + L"\\" + containerName + L"-cert.pub";
    if (fileExists(certPath))
    {
        if (!DeleteFileW(certPath.c_str()))
        {
            LOG_ERROR("Failed to delete certificate file");
            success = false;
        }
    }
    //DeploymentChecker::ClearDeployment();

    return success;
}

bool zzj::sshagent::DeploymentChecker::IsDeployed()
{
    wchar_t *roaming = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) return false;
    std::wstring flag = std::wstring(roaming) + L"\\nCryptAgent\\deployed.flag";
    CoTaskMemFree(roaming);
    return GetFileAttributesW(flag.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void zzj::sshagent::DeploymentChecker::MarkDeployed()
{
    wchar_t *roaming = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)))
    {
        std::wstring dir = std::wstring(roaming) + L"\\nCryptAgent";
        CreateDirectoryW(dir.c_str(), nullptr);
        std::wstring flag = dir + L"\\deployed.flag";
        WriteBlobToFile(flag, std::vector<BYTE>{}); 
        CoTaskMemFree(roaming);
    }
}

void zzj::sshagent::DeploymentChecker::ClearDeployment(const std::wstring &containerName)
{
    wchar_t *roaming = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)))
    {
        std::wstring flag = std::wstring(roaming) + L"\\nCryptAgent\\deployed.flag";
        DeleteFileW(flag.c_str());
        std::wstring dir = std::wstring(roaming) + L"\\nCryptAgent";
        std::wstring pubKey = dir + L"\\" + containerName + L".pub";
        DeleteFileW(pubKey.c_str());

        std::wstring cert = dir + L"\\" + containerName + L"-cert.pub";
        DeleteFileW(cert.c_str());
        CoTaskMemFree(roaming);
        
    }
}


bool zzj::sshagent::DeploymentChecker::WriteBlobToFile(const std::wstring &fullPath,
                                                              const std::vector<BYTE> &data)
{
    // Set minimal security: only current user
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, FALSE};

    // Open file for writing
    HANDLE hFile = CreateFileW(fullPath.c_str(), GENERIC_WRITE, 0, &sa, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD totalWritten = 0;
    const BYTE *buffer = data.data();
    DWORD toWrite = static_cast<DWORD>(data.size());
    while (totalWritten < toWrite)
    {
        DWORD chunkWritten = 0;
        BOOL ok =
            WriteFile(hFile, buffer + totalWritten, toWrite - totalWritten, &chunkWritten, nullptr);
        if (!ok || chunkWritten == 0)
        {
            CloseHandle(hFile);
            return false;
        }
        totalWritten += chunkWritten;
    }
    CloseHandle(hFile);
    return true;
}

bool zzj::sshagent::KeyManagerAgent::UploadPublicKey(const std::vector<BYTE> &pubBlob,
                                                     const std::string &principal,
                               const std::string &caEndpoint)
{
    return TRUE;
}

bool zzj::sshagent::KeyManagerAgent::DownloadSSHCertificate(const std::string &certUrl,
                                                            const std::wstring &outPath)
{
    return TRUE;
}

std::vector<zzj::sshagent::KeyManagerAgent::Identity> zzj::sshagent::KeyManagerAgent::ExportAllIdentities()
{
    std::lock_guard<std::mutex> lk(identities_mutex_);
    return identities_;  
}


const DWORD zzj::sshagent::KeyManagerAgent::READ_BUF_SIZE = 64 * 1024;
const int zzj::sshagent::KeyManagerAgent::STABLE_CHECKS = 4;
const std::chrono::milliseconds zzj::sshagent::KeyManagerAgent::STABLE_DELAY(200);
const std::chrono::seconds zzj::sshagent::KeyManagerAgent::TOTAL_TIMEOUT(20);
bool zzj::sshagent::KeyManagerAgent::ReadFileToBuffer(const std::wstring &path, std::vector<char> &out)
{
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE){
        return false;
    }
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize)){
        CloseHandle(hFile);
        return false;
    }
    if (fileSize.QuadPart < 0 || fileSize.QuadPart > MAXDWORD){
        CloseHandle(hFile);
        return false;
    }
    DWORD fileSizeDW = static_cast<DWORD>(fileSize.QuadPart);
    try
    {
        out.resize(fileSizeDW);
    }
    catch (const std::bad_alloc &)
    {
        CloseHandle(hFile);
        return false;
    }
    DWORD bytesRead = 0;
    BOOL success = FALSE;
    if (fileSizeDW > 0){
        success = ReadFile(hFile, out.data(), fileSizeDW, &bytesRead, NULL);
    }
    else{
        success = TRUE;  // empty
        bytesRead = 0;
    }
    CloseHandle(hFile);

    if (!success || bytesRead != fileSizeDW){
        return false;
    }

    return true;
}


HANDLE zzj::sshagent::KeyManagerAgent::TryOpenFileReadExclusive(const std::wstring &path)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ,  // Allow other readers
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    return h;
}

bool zzj::sshagent::KeyManagerAgent::WaitFileStable(const std::wstring &fullpath,
                                     std::chrono::milliseconds perInterval, int stableNeeded,
                                     std::chrono::seconds timeout)
{
    using clock = std::chrono::steady_clock;
    auto deadline = clock::now() + timeout;
    long long prevSize = -1;
    int stableCount = 0;

    while (clock::now() < deadline)
    {
        // Try to get the file size
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (!GetFileAttributesExW(fullpath.c_str(), GetFileExInfoStandard, &fad))
        {
            // The file may not exist yet
            std::this_thread::sleep_for(perInterval);
            continue;
        }

        LARGE_INTEGER size;
        size.LowPart = fad.nFileSizeLow;
        size.HighPart = fad.nFileSizeHigh;
        long long curSize = static_cast<long long>(size.QuadPart);

        // Try to open the file to detect if the writer still holds the exclusive lock
        HANDLE h = TryOpenFileReadExclusive(fullpath);
        if (h == INVALID_HANDLE_VALUE){
            stableCount = 0;
            std::this_thread::sleep_for(perInterval);
            continue;
        }
        else{
            CloseHandle(h);
        }

        if (curSize == prevSize){
            stableCount++;
            if (stableCount >= stableNeeded){
                return true;
            }
        }
        else{
            prevSize = curSize;
            stableCount = 0;
        }

        std::this_thread::sleep_for(perInterval);
    }

    return false;
}

bool zzj::sshagent::KeyManagerAgent::SignPubKeyWithCA(const std::wstring &pubPath,
                                       const std::vector<char> &pubContent,
                                       const std::string &principal)
{
    try
    {
        // 1. Convert the public key content to the appropriate format
        std::string pubKeyStr(pubContent.begin(), pubContent.end());

        // 2. Call SSH CA to upload the public key and apply for a certificate
        // if (!sshagent::UploadPublicKey(pubKeyStr, principal, sshCaUploadUrl_)) {
        //     return false;
        // }

        // 3. Download the certificate and save it
        std::wstring certPath = pubPath.substr(0, pubPath.find_last_of(L'.')) + L"-cert.pub";
        // if (!sshagent::DownloadSSHCertificate(sshCaDownloadUrl_, certPath)) {
        //     return false;
        // }

        LOG_INFO("SignPubKeyWithCA: Successfully processed " +
                 std::string(pubPath.begin(), pubPath.end()));
        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("SignPubKeyWithCA failed: " + std::string(e.what()));
        return false;
    }
}

// Auxiliary: Attempt to create a marker file (atom: CREATE_NEW)
// Return handle (not INVALID_HANDLE_VALUE) to indicate successful creation. The caller is
// responsible for CloseHandle + DeleteFile(markerPath). If creation fails and GetLastError() ==
// ERROR_FILE_EXISTS, the marker already exists.
HANDLE zzj::sshagent::KeyManagerAgent::CreateSigningMarker(const std::wstring &markerPath)
{
    HANDLE h = CreateFileW(markerPath.c_str(), GENERIC_WRITE,
                           0,  // Disallow sharing, ensure exclusive use
                           NULL,
                           CREATE_NEW,  // Atomically create, fails if the file already exists
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    // If successful, returns the handle (which will be deleted in the subsequent CloseHandle call)
    return h;
}

// Delete the marker (if it still exists)
void zzj::sshagent::KeyManagerAgent::RemoveSigningMarkerHandle(HANDLE hMarker)
{
    if (hMarker != INVALID_HANDLE_VALUE)
    {
        // If FILE_FLAG_DELETE_ON_CLOSE was used during OPEN, CloseHandle will automatically delete
        // the file.
        CloseHandle(hMarker);
    }
}
// Check if the marker exists and is stale (timeout exceeded)
// If it exists, return true; if timeout exceeded, return true and set staleOut to true.
bool zzj::sshagent::KeyManagerAgent::IsSigningMarkerPresent(const std::wstring &markerPath, std::chrono::seconds timeout, bool &staleOut)
{
    staleOut = false;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(markerPath.c_str(), GetFileExInfoStandard, &fad))
    {
        return false;
    }
    // Get the last write time
    FILETIME ftWrite = fad.ftLastWriteTime;
    // Convert to system time
    ULONGLONG qw = (static_cast<ULONGLONG>(ftWrite.dwHighDateTime) << 32) | ftWrite.dwLowDateTime;
    // FILETIME units are 100-ns from 1601-01-01; convert to chrono
    using clock = std::chrono::system_clock;
    // convert FILETIME to time_point
    // first convert to 100-ns ticks since epoch 1601, then to chrono
    // For stale check approximate using GetTickCount64? Simpler: use CompareFileTime.
    // We'll convert FILETIME to ULONGLONG and convert to seconds approximate.
    // Compute current FILETIME
    FILETIME ftNow;
    GetSystemTimeAsFileTime(&ftNow);
    ULONGLONG nowQw = (static_cast<ULONGLONG>(ftNow.dwHighDateTime) << 32) | ftNow.dwLowDateTime;
    if (nowQw > qw)
    {
        ULONGLONG diff100ns = nowQw - qw;
        // convert to seconds
        ULONGLONG diffSec = diff100ns / 10000000ULL;
        if (diffSec > static_cast<unsigned long long>(timeout.count()))
        {
            staleOut = true;
        }
    }
    return true;
}

// Wait for marker removal OR certificate file creation (either one stops waiting)
// Returns true if certificate exists (and presumably ready) or marker removed and no cert =
// continue, false if timeout.
bool zzj::sshagent::KeyManagerAgent::WaitForMarkerRemovalOrCert( const std::wstring &markerPath, const std::wstring &certPath,
                                       std::chrono::seconds timeout)
{
    using clock = std::chrono::steady_clock;
    auto deadline = clock::now() + timeout;
    while (clock::now() < deadline)
    {
        // If cert exists -> success
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW(certPath.c_str(), GetFileExInfoStandard, &fad))
        {
            return true;
        }
        // If marker gone -> also return false to let caller try to acquire marker or skip
        if (!GetFileAttributesExW(markerPath.c_str(), GetFileExInfoStandard, &fad))
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return false;
}


bool zzj::sshagent::KeyManagerAgent::MonitorAndSignPubKey(const std::wstring &watchDir,
                                           const std::wstring &expectedFilename,
                                           const std::string &principal)
{

    HANDLE hDir = CreateFileW(watchDir.c_str(), FILE_LIST_DIRECTORY,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                              OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (hDir == INVALID_HANDLE_VALUE)
    {
        LOG_ERROR("MonitorAndSignPubKey: CreateFileW for directory failed, err=" +
                  std::to_string(GetLastError()));
        return false;
    }

    std::vector<BYTE> buffer(READ_BUF_SIZE);
    DWORD bytesReturned = 0;
    BOOL ok = ReadDirectoryChangesW(hDir, buffer.data(), static_cast<DWORD>(buffer.size()),
                                    FALSE,  // 非递归
                                    FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
                                    &bytesReturned, NULL, NULL);

    if (!ok)
    {
        LOG_ERROR("MonitorAndSignPubKey: ReadDirectoryChangesW failed, err=" +
                  std::to_string(GetLastError()));
        CloseHandle(hDir);
        return false;
    }

    FILE_NOTIFY_INFORMATION *pInfo = reinterpret_cast<FILE_NOTIFY_INFORMATION *>(buffer.data());
    bool found = false;
    std::wstring matchedFile;
    bool isAtomicMove = false;

    while (true)
    {
        std::wstring filename(pInfo->FileName, pInfo->FileName + pInfo->FileNameLength / sizeof(WCHAR));

        if (filename == expectedFilename)
        {
            matchedFile = filename;
            found = true;

            // If it is a rename, it is considered an atomic move and may be ready
            if (pInfo->Action == FILE_ACTION_RENAMED_NEW_NAME)
            {
                isAtomicMove = true;
                LOG_INFO("MonitorAndSignPubKey: RENAMED_NEW_NAME detected for " +
                         std::string(matchedFile.begin(), matchedFile.end()));
            }
            else
            {
                LOG_INFO("MonitorAndSignPubKey: file event detected for " +
                         std::string(matchedFile.begin(), matchedFile.end()) +
                         ", action=" + std::to_string(pInfo->Action));
            }
            break;
        }

        if (pInfo->NextEntryOffset == 0) break;
        pInfo = reinterpret_cast<FILE_NOTIFY_INFORMATION *>(reinterpret_cast<BYTE *>(pInfo) +
                                                            pInfo->NextEntryOffset);
    }

    CloseHandle(hDir);
    if (!found)
    {
        LOG_INFO("MonitorAndSignPubKey: Expected file not found in directory changes");
        return false;
    }

    std::wstring fullpath = watchDir + L"\\" + matchedFile;
    std::wstring certPath = fullpath.substr(0, fullpath.find_last_of(L'.')) + L"-cert.pub";
    std::wstring markerPath = fullpath + L".signing";
    // Wait for the file to stabilize
    bool ready =
        WaitFileStable(fullpath, STABLE_DELAY, isAtomicMove ? 2 : STABLE_CHECKS, TOTAL_TIMEOUT);
    if (!ready)
    {
        LOG_ERROR("MonitorAndSignPubKey: File not stable within timeout: " +
                  std::string(fullpath.begin(), fullpath.end()));
        return false;
    }

    // 2) if cert already exists, skip signing (avoid duplicate)
    WIN32_FILE_ATTRIBUTE_DATA tmpf;
    if (GetFileAttributesExW(certPath.c_str(), GetFileExInfoStandard, &tmpf))
    {
        LOG_INFO("MonitorAndSignPubKey: certificate already exists for " +
                 std::string(fullpath.begin(), fullpath.end()) + ", skipping sign");
        return true;
    }

    // 3) Try to create marker atomically. If marker exists, wait for its removal or cert creation.
    HANDLE hMarker = CreateSigningMarker(markerPath);
    if (hMarker == INVALID_HANDLE_VALUE)
    {
        // marker exists or failed to create
        DWORD err = GetLastError();
        if (err == ERROR_FILE_EXISTS)
        {
            LOG_INFO(
                "MonitorAndSignPubKey: signing marker exists, waiting for it to be removed or cert "
                "to appear...");
            bool certAppeared = WaitForMarkerRemovalOrCert(markerPath, certPath, TOTAL_TIMEOUT);
            if (certAppeared)
            {
                LOG_INFO(
                    "MonitorAndSignPubKey: cert appeared while waiting for existing marker; "
                    "skipping sign");
                return true;
            }
            else
            {
                // Marker still present after timeout -> consider it stale and attempt to remove it
                // (best-effort).
                bool stale = false;
                if (IsSigningMarkerPresent(markerPath, TOTAL_TIMEOUT, stale) && stale)
                {
                    LOG_INFO(
                        "MonitorAndSignPubKey: existing marker appears stale; attempting to remove "
                        "it and acquire marker");
                    // attempt to delete stale marker
                    if (!DeleteFileW(markerPath.c_str()))
                    {
                        LOG_ERROR("MonitorAndSignPubKey: failed to remove stale marker " +
                                  std::string(markerPath.begin(), markerPath.end()) +
                                  ", err=" + std::to_string(GetLastError()));
                        return false;
                    }
                    // try create again
                    hMarker = CreateSigningMarker(markerPath);
                    if (hMarker == INVALID_HANDLE_VALUE)
                    {
                        LOG_ERROR(
                            "MonitorAndSignPubKey: failed to acquire marker after removing stale "
                            "marker, err=" +
                            std::to_string(GetLastError()));
                        return false;
                    }
                }
                else
                {
                    LOG_ERROR(
                        "MonitorAndSignPubKey: marker exists and cert did not appear in time; "
                        "aborting");
                    return false;
                }
            }
        }
        else
        {
            LOG_ERROR("MonitorAndSignPubKey: CreateSigningMarker failed err=" +
                      std::to_string(err));
            return false;
        }
    }

    // Now we have the marker (hMarker != INVALID_HANDLE_VALUE). Ensure it will be closed/deleted on
    // all paths.
    bool signOK = false;
    try
    {
        // Double-check cert again before signing (race check)
        if (GetFileAttributesExW(certPath.c_str(), GetFileExInfoStandard, &tmpf))
        {
            LOG_INFO("MonitorAndSignPubKey: certificate appeared before signing; skipping sign");
            signOK = true;
        }
        else
        {
            // Read pub file content
            std::vector<char> content;
            if (!ReadFileToBuffer(fullpath, content))
            {
                LOG_ERROR("MonitorAndSignPubKey: Failed to read file after stable check: " +
                          std::string(fullpath.begin(), fullpath.end()));
                signOK = false;
            }
            else
            {
                // Call signing routine
                signOK = SignPubKeyWithCA(fullpath, content, principal);
            }
        }
    }
    catch (...)
    {
        LOG_ERROR("MonitorAndSignPubKey: exception during signing flow");
        signOK = false;
    }

    // If signOK, wait for cert to appear (signed by CA), with timeout
    if (signOK)
    {
        bool certReady = WaitFileStable(certPath, STABLE_DELAY, 3, TOTAL_TIMEOUT);
        if (!certReady)
        {
            LOG_ERROR("MonitorAndSignPubKey: signing reported success but cert did not " "appear/stabilize: " +
                std::string(certPath.begin(), certPath.end()));
            // we still remove marker to allow retries, but signal failure
            RemoveSigningMarkerHandle(hMarker);
            return false;
        }
        LOG_INFO("MonitorAndSignPubKey: Successfully processed " +
                 std::string(fullpath.begin(), fullpath.end()));
        // cleanup marker (will be deleted when handle closed due to FILE_FLAG_DELETE_ON_CLOSE)
        RemoveSigningMarkerHandle(hMarker);
        return true;
    }
    else
    {
        LOG_ERROR("MonitorAndSignPubKey: Signing routine failed for " +
                  std::string(fullpath.begin(), fullpath.end()));
        RemoveSigningMarkerHandle(hMarker);
        return false;
    }
}

//------------------------------------------------------------------------------
// marshalPublicKey (utils.go -> unmarshalRSA/ECC placeholder)
//------------------------------------------------------------------------------
std::vector<BYTE> zzj::sshagent::KeyManagerAgent::MarshalPublicKey(NCRYPT_KEY_HANDLE hKey)
{
    DWORD size = 0;
    SECURITY_STATUS st =
        NCryptExportKey(hKey, 0, BCRYPT_RSAPUBLIC_BLOB, nullptr, nullptr, 0, &size, 0);
    if (FAILED(st))
    {
        throw zzj::NCryptException("NCryptExportKey failed", st);
    }

    std::vector<BYTE> Blob(size);
    st = NCryptExportKey(hKey, 0, BCRYPT_RSAPUBLIC_BLOB, nullptr, Blob.data(), size, &size, 0);
    if (FAILED(st))
    {
        throw zzj::NCryptException("NCryptExportKey failed", st);
    }

    struct BCRYPT_RSAKEY_BLOB
    {
        ULONG Magic;
        ULONG BitLength;
        ULONG cbPublicExp;
        ULONG cbModulus;
        ULONG cbPrime1;
        ULONG cbPrime2;
    };

    BYTE *ptr = Blob.data();
    auto *header = reinterpret_cast<BCRYPT_RSAKEY_BLOB *>(ptr);
    BYTE *exp = ptr + sizeof(BCRYPT_RSAKEY_BLOB);
    BYTE *mod = exp + header->cbPublicExp;

    std::vector<BYTE> wire;

    auto append_mpint = [&](const BYTE *data, DWORD len)
    {
        /* while (len > 0 && data[0] == 0)
            ++data, --len;
        wire.push_back((len >> 24) & 0xFF);
        wire.push_back((len >> 16) & 0xFF);
        wire.push_back((len >> 8) & 0xFF);
        wire.push_back(len & 0xFF);
        wire.insert(wire.end(), data, data + len);
        */
        // 1. Little endian → tmp, then reverse to big endian
        std::vector<BYTE> tmp(data, data + len);
        std::reverse(tmp.begin(), tmp.end());
        // 2. Remove the leading 0 at the MSB end
        auto it = tmp.begin();
        while (it != tmp.end() && *it == 0) ++it;
        std::vector<BYTE> beData(it, tmp.end());
        // 3. If the highest bit is set to 1, insert a 0x00 in front
        if (!beData.empty() && (beData[0] & 0x80))
        {
            beData.insert(beData.begin(), 0x00);
        }
        // 4. Write length (4 bytes big endian) and write content
        DWORD outLen = static_cast<DWORD>(beData.size());
        wire.push_back((outLen >> 24) & 0xFF);
        wire.push_back((outLen >> 16) & 0xFF);
        wire.push_back((outLen >> 8) & 0xFF);
        wire.push_back(outLen & 0xFF);
        wire.insert(wire.end(), beData.begin(), beData.end());
    };

    auto append_str = [&](const char *s)
    {
        DWORD len = strlen(s);
        wire.push_back((len >> 24) & 0xFF);
        wire.push_back((len >> 16) & 0xFF);
        wire.push_back((len >> 8) & 0xFF);
        wire.push_back(len & 0xFF);
        wire.insert(wire.end(), s, s + len);
    };

    append_str("ssh-rsa");
    append_mpint(exp, header->cbPublicExp);
    append_mpint(mod, header->cbModulus);

    return wire;
}

bool zzj::sshagent::KeyManagerAgent::EnsureDirectoryExists(const std::wstring &dir)
{
    size_t pos = 0;
    std::wstring path;
    while ((pos = dir.find(L'\\', pos)) != std::wstring::npos)
    {
        path = dir.substr(0, pos++);
        if (path.empty()) continue;
        CreateDirectoryW(path.c_str(), nullptr);
    }
    return CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

void zzj::sshagent::KeyManagerAgent::OpenProvider() {
    if (hProv_) return;
    SECURITY_STATUS st = NCryptOpenStorageProvider(&hProv_, ncrypt::MS_PLATFORM_PROVIDER, 0);
    if (FAILED(st)) throw NCryptException("NCryptOpenStorageProvider failed", st);
}

void zzj::sshagent::KeyManagerAgent::ReloadAllIdentities() {
    std::lock_guard<std::mutex> lk(identities_mutex_);
    identities_.clear();
    // Enumerate all persistent keys
    NCryptKeyName *pName = nullptr;
    PVOID enumState = nullptr;
    while (true)
    {
        SECURITY_STATUS st = NCryptEnumKeys(hProv_,   // OpenProvider is already open hProv_
                                            nullptr,  // No special scope
                                            &pName, &enumState, 0);
        if (st == NTE_NO_MORE_ITEMS) break;
        if (FAILED(st)) throw NCryptException("NCryptEnumKeys failed", st);
        NCRYPT_KEY_HANDLE hKey = 0;
        st = NCryptOpenKey(hProv_, &hKey, pName->pszName, 0, 0);
        if (FAILED(st))
        {
            NCryptFreeBuffer(pName);
            throw NCryptException("NCryptOpenKey failed", st);
        }

        // Export public key blob
        std::vector<BYTE> blob = MarshalPublicKey(hKey);

        // Store identity with handle
        identities_.push_back({blob, ncrypt_utils::to_string(pName->pszName), hKey});

        NCryptFreeBuffer(pName);
    }

}


