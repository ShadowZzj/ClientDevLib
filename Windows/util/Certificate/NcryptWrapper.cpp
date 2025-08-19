#include "NcryptWrapper.hpp"
#include <bcrypt.h>
#include <chrono>
#include <AclAPI.h>
#include <cstdint>
#include <combaseapi.h>  // Add this include for CoTaskMemFree
#include <strsafe.h>
#include <shlobj.h>  // Add this include for FOLDERID_RoamingAppData
#include <General/util/SPDLogHelper.h>
#include <sstream>
#include <iomanip>
#include <Shlwapi.h>
#include <General/util/StrUtil.h>
#include <Windows/util/File/FileHelper.h>
#include "TPMKey.hpp"
#include <Windows/util/Process/ProcessHelper.h>
#include <General/util/User/User.h>
#pragma Comment(lib, "Advapi32.lib")

std::vector<zzj::sshagent::KeyManagerAgent::KeyDescriptor> zzj::sshagent::KeyManagerAgent::enumKeys(
    NCRYPT_PROV_HANDLE prov, const std::wstring &scope, DWORD flags)
{
    std::vector<zzj::sshagent::KeyManagerAgent::KeyDescriptor> list;
    PVOID enumState = nullptr;

    while (true)
    {
        NCryptKeyName *pName = nullptr;
        SECURITY_STATUS st = NCryptEnumKeys(prov, scope.empty() ? nullptr : scope.c_str(), &pName,
                                            &enumState, flags);
        if (st == NTE_NO_MORE_ITEMS) break;
        if (st != ERROR_SUCCESS)
        {
            return list;
        }
        if (pName)
        {
            zzj::sshagent::KeyManagerAgent::KeyDescriptor kd{
                pName->pszName ? std::wstring(pName->pszName) : std::wstring(),
                pName->pszAlgid ? std::wstring(pName->pszAlgid) : std::wstring(),
                pName->dwLegacyKeySpec, pName->dwFlags};
            list.push_back(std::move(kd));
            NCryptFreeBuffer(pName);
            pName = nullptr;
        }
    }

    if (enumState)
    {
        NCryptFreeBuffer(enumState);
    }
    return list;
}

zzj::sshagent::KeyManagerAgent::KeyManagerAgent() { OpenProvider(); }

zzj::sshagent::KeyManagerAgent::~KeyManagerAgent()
{
    std::lock_guard<std::mutex> lk(identities_mutex_);
    for (auto &id : identities_)
    {
        if (id.hKey)
        {
            NCryptFreeObject(id.hKey);
            id.hKey = NULL;
        }
    }
    identities_.clear();
    if (hProv_)
    {
        NCryptFreeObject(hProv_);
        hProv_ = 0;
    }
    CleanupAllProcesses();
}

bool zzj::sshagent::KeyManagerAgent::InitDeployment(const std::wstring &containerName)
{
    std::string appData = zzj::FileHelper::GetCurrentUserProgramDataFolder();
    if (appData.empty())
    {
        return false;
    }
    std::wstring roaming = zzj::str::ansi2w(appData);
    std::wstring pubDir = roaming + L"\\nCryptAgent\\PublicKeys";
    return DeploySSH(containerName, 2048, pubDir);
}

bool zzj::sshagent::KeyManagerAgent::DestroyRSAKey(const std::wstring &containerName)
{
    NCRYPT_PROV_HANDLE hProvLocal = NULL;
    SECURITY_STATUS st =
        NCryptOpenStorageProvider(&hProvLocal, MS_PLATFORM_KEY_STORAGE_PROVIDER, 0);
    if (st != ERROR_SUCCESS)
    {
        return false;
    }
    DEFER { NCryptFreeObject(hProvLocal); };

    NCRYPT_KEY_HANDLE hKey = NULL;
    st = NCryptOpenKey(hProvLocal, &hKey, containerName.c_str(), 0, 0);
    if (st != ERROR_SUCCESS)
    {
        return false;
    }
    DEFER
    {
        if (hKey) NCryptFreeObject(hKey);
    };

    st = NCryptDeleteKey(hKey, 0);
    if (st != ERROR_SUCCESS)
    {
        return false;
    }

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
            return true;
        }
    }
    NCRYPT_KEY_HANDLE hKey = 0;
    /* User-level key, no administrator privileges required */
    SECURITY_STATUS st = NCryptCreatePersistedKey(hProv_, &hKey, BCRYPT_RSA_ALGORITHM, containerName.c_str(), 0, 0);
    if (FAILED(st))
    {
        return false;
    }
    // Set length
    st = NCryptSetProperty(hKey, NCRYPT_LENGTH_PROPERTY, reinterpret_cast<BYTE *>(&keySize),
                           sizeof(keySize), 0);
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
    return ExecuteProcess();
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
DWORD zzj::sshagent::KeyManagerAgent::ExecuteProcess()
{
    auto userInfoVar = zzj::UserInfo::GetActiveUserInfo();
    if (!userInfoVar.has_value())
    {
        spdlog::debug("No active user for SSH Agent protection");
        return 0;
    }

    std::string processName = "nCryptAgent.exe";
    wchar_t currentModulePath[MAX_PATH];
    if (GetModuleFileNameW(NULL, currentModulePath, MAX_PATH) == 0)
    {
        spdlog::error("Failed to get current module path");
        return 0;
    }
    std::wstring wProcessPath = GetExecutablePath(currentModulePath);
    std::string processPath = zzj::str::w2utf8(wProcessPath);

    zzj::ProcessV2 processObj(processName);
    if (!boost::filesystem::is_regular_file(processPath))
    {
        spdlog::warn("SSH Agent executable not found: {}", processPath);
        return 0;
    }
    if (!processObj.IsProcessAlive())
    {
        spdlog::info("SSH Agent not running, attempting to start: {}", processPath);

        DWORD processId = zzj::Process::SystemCreateProcess(wProcessPath, false, false, 0, false);

        if (processId != 0)
        {
            spdlog::info("SSH Agent started successfully with PID: {}", processId);
            return processId;
        }
        else
        {
            spdlog::error("Failed to start SSH Agent");
            return 0;
        }
    }
    else
    {
        spdlog::debug("SSH Agent is already running");
        return TRUE;  
    }
}

DWORD zzj::sshagent::KeyManagerAgent::StopNcryptAgent(const std::wstring &containerName)
{
    std::lock_guard<std::mutex> lock(m_processMutex);
    if (zzj::Process::KillProcess("nCryptAgent.exe"))
    {
        spdlog::info("SSH Agent stopped successfully");
        return ERROR_SUCCESS;
    }
    else
    {
        spdlog::error("Failed to stop SSH Agent");
        return ERROR_PROCESS_ABORTED;
    }
}

void zzj::sshagent::KeyManagerAgent::CleanupAllProcesses()
{
    std::lock_guard<std::mutex> lock(m_processMutex);

    for (auto &pair : m_activeProcesses)
    {
        const std::wstring &containerName = pair.first;
        auto &info = pair.second;
        if (info.jobHandle)
        {
            // resetting jobHandle will close it and kill processes
            info.jobHandle = KeyManagerAgent::HandleRAII();
        }
        else if (info.processHandle)
        {
            TerminateProcess(info.processHandle.get(), 0);
            WaitForSingleObject(info.processHandle.get(), 3000);
            info.processHandle = KeyManagerAgent::HandleRAII();
        }
    }
    m_activeProcesses.clear();
}

DWORD zzj::sshagent::KeyManagerAgent::unDeploySSH(const std::wstring &containerName,
                                                 const std::wstring &pubOutDir)
{
    // 1. Stop process
    DWORD stopRes = StopNcryptAgent(containerName);
    if (stopRes != ERROR_SUCCESS)
    {
        return stopRes;
    }
    // 2. Destroy key (your DestroyRSAKey returns bool)
    if (!DestroyRSAKey(containerName))
    {
        return ERROR_GEN_FAILURE;
    }
    // 3. Delete public key file if exists
    std::filesystem::path pubKeyPath = std::filesystem::path(pubOutDir) / (containerName + L".pub");
    if (std::filesystem::exists(pubKeyPath))
    {
        if (!DeleteFileW(pubKeyPath.c_str()))
        {
            return GetLastError();
        }
    }
    // 4. Delete certificate file if exists (FIXED: previously checked pubKeyPath by mistake)
    std::filesystem::path certPath =
        std::filesystem::path(pubOutDir) / (containerName + L"-cert.pub");
    if (std::filesystem::exists(certPath))
    {
        if (!DeleteFileW(certPath.c_str()))
        {
            return GetLastError();
        }
    }
    return ERROR_SUCCESS;
}

bool zzj::sshagent::KeyManagerAgent::DeploymentChecker::IsDeployed()
{
    std::string appData = zzj::FileHelper::GetCurrentUserProgramDataFolder();
    if (appData.empty()) return false;
    std::wstring roaming = zzj::str::ansi2w(appData);
    std::wstring flag = roaming + L"\\nCryptAgent\\deployed.flag";
    return GetFileAttributesW(flag.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void zzj::sshagent::KeyManagerAgent::DeploymentChecker::MarkDeployed()
{
    std::string appData = zzj::FileHelper::GetCurrentUserProgramDataFolder();
    if (appData.empty()) return;
    std::wstring roaming = zzj::str::ansi2w(appData);
    std::wstring dir = roaming + L"\\nCryptAgent";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring flag = dir + L"\\deployed.flag";
    WriteBlobToFile(flag, std::vector<BYTE>{});
}

void zzj::sshagent::KeyManagerAgent::DeploymentChecker::ClearDeployment(
    const std::wstring &containerName)
{
    std::string appData = zzj::FileHelper::GetCurrentUserProgramDataFolder();
    if (appData.empty()) return;

    std::wstring roaming = zzj::str::ansi2w(appData);
    std::wstring flag = roaming + L"\\nCryptAgent\\deployed.flag";
    DeleteFileW(flag.c_str());
    std::wstring dir = roaming + L"\\nCryptAgent";
    std::wstring pubKey = dir + L"\\" + containerName + L".pub";
    DeleteFileW(pubKey.c_str());
    std::wstring cert = dir + L"\\" + containerName + L"-cert.pub";
    DeleteFileW(cert.c_str());
}

bool zzj::sshagent::KeyManagerAgent::DeploymentChecker::WriteBlobToFile(
    const std::wstring &fullPath, const std::vector<BYTE> &data)
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

bool zzj::sshagent::KeyManagerAgent::ReadFileToBuffer(const std::wstring &path,
                                                      std::vector<char> &out)
{
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize))
    {
        CloseHandle(hFile);
        return false;
    }
    if (fileSize.QuadPart < 0 || fileSize.QuadPart > MAXDWORD)
    {
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
    if (fileSizeDW > 0)
    {
        success = ReadFile(hFile, out.data(), fileSizeDW, &bytesRead, NULL);
    }
    else
    {
        success = TRUE;  // empty
        bytesRead = 0;
    }
    CloseHandle(hFile);

    if (!success || bytesRead != fileSizeDW)
    {
        return false;
    }

    return true;
}

bool zzj::sshagent::KeyManagerAgent::WaitFileStable(const std::wstring &fullpath,
                                                    std::chrono::milliseconds perInterval,
                                                    int stableNeeded, std::chrono::seconds timeout)
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

        LARGE_INTEGER size{};
        size.LowPart = fad.nFileSizeLow;
        size.HighPart = fad.nFileSizeHigh;
        long long curSize = static_cast<long long>(size.QuadPart);

        // Try to open the file to detect if the writer still holds the exclusive lock
        HANDLE h = CreateFileW(fullpath.c_str(), GENERIC_READ,
                                          FILE_SHARE_READ,  // Allow other readers
                                          NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            stableCount = 0;
            std::this_thread::sleep_for(perInterval);
            continue;
        }
        else {
            CloseHandle(h);
        }
        if (curSize == prevSize) {
            stableCount++;
            if (stableCount >= stableNeeded) {
                return true;
            }
        }
        else {
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
        return true;
    }
    catch (const std::exception &e)
    {
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
bool zzj::sshagent::KeyManagerAgent::IsSigningMarkerPresent(const std::wstring &markerPath,
                                                            std::chrono::seconds timeout,
                                                            bool &staleOut)
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
bool zzj::sshagent::KeyManagerAgent::WaitForMarkerRemovalOrCert(const std::wstring &markerPath,
                                                                const std::wstring &certPath,
                                                                std::chrono::seconds timeout)
{
    using clock = std::chrono::steady_clock;
    auto deadline = clock::now() + timeout;
    while (clock::now() < deadline)
    {
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

DWORD zzj::sshagent::KeyManagerAgent::MonitorAndSignPubKey(const std::wstring &watchDir,
                                                          const std::wstring &expectedFilename,
                                                          const std::string &principal)
{
    HANDLE hDir = CreateFileW(watchDir.c_str(), FILE_LIST_DIRECTORY,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                              OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (hDir == INVALID_HANDLE_VALUE)
    {
        return GetLastError();
    }

    std::vector<BYTE> buffer(READ_BUF_SIZE);
    DWORD bytesReturned = 0;
    BOOL ok = ReadDirectoryChangesW(hDir, buffer.data(), static_cast<DWORD>(buffer.size()),
                                    FALSE,  
                                    FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
                                    &bytesReturned, NULL, NULL);

    if (!ok)
    {
        DWORD error = GetLastError();
        CloseHandle(hDir);
        return error;
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
        return ERROR_FILE_NOT_FOUND;
    }

    std::wstring fullpath = watchDir + L"\\" + matchedFile;
    std::wstring certPath = fullpath.substr(0, fullpath.find_last_of(L'.')) + L"-cert.pub";
    std::wstring markerPath = fullpath + L".signing";
    // Wait for the file to stabilize
    bool ready =
        WaitFileStable(fullpath, STABLE_DELAY, isAtomicMove ? 2 : STABLE_CHECKS, TOTAL_TIMEOUT);
    if (!ready)
    {
        return ERROR_TIMEOUT;
    }

    // 2) if cert already exists, skip signing (avoid duplicate)
    WIN32_FILE_ATTRIBUTE_DATA tmpf{};
    if (GetFileAttributesExW(certPath.c_str(), GetFileExInfoStandard, &tmpf))
    {
        return ERROR_SUCCESS;
    }

    // 3) Try to create marker atomically. If marker exists, wait for its removal or cert creation.
    HANDLE hMarker = CreateSigningMarker(markerPath);
    if (hMarker == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_EXISTS)
        {
            bool certAppeared = WaitForMarkerRemovalOrCert(markerPath, certPath, TOTAL_TIMEOUT);
            if (certAppeared)
            {
                return ERROR_SUCCESS;
            }
            else
            {
                // Marker still present after timeout -> consider it stale and attempt to remove it
                // (best-effort).
                bool stale = false;
                if (IsSigningMarkerPresent(markerPath, TOTAL_TIMEOUT, stale) && stale)
                {
                    // attempt to delete stale marker
                    if (!DeleteFileW(markerPath.c_str()))
                    {
                        return GetLastError();
                    }
                    // try create again
                    hMarker = CreateSigningMarker(markerPath);
                    if (hMarker == INVALID_HANDLE_VALUE)
                    {
                        return GetLastError();
                    }
                }
                else
                {
                    return ERROR_TIMEOUT;
                }
            }
        }
        else
        {
            return err;
        }
    }

    // Now we have the marker (hMarker != INVALID_HANDLE_VALUE). Ensure it will be closed/deleted on
    // all paths.
    bool signOK = 0;
    try
    {
        // Double-check cert again before signing (race check)
        if (GetFileAttributesExW(certPath.c_str(), GetFileExInfoStandard, &tmpf))
        {
            signOK = ERROR_SUCCESS;
        }
        else
        {
            // Read pub file content
            std::vector<char> content;
            if (!ReadFileToBuffer(fullpath, content))
            {
                signOK = ERROR_READ_FAULT;
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
        signOK = ERROR_INVALID_DATA;
    }

    // If signOK, wait for cert to appear (signed by CA), with timeout
    if (signOK)
    {
        bool certReady = WaitFileStable(certPath, STABLE_DELAY, 3, TOTAL_TIMEOUT);
        if (!certReady)
        {
            // we still remove marker to allow retries, but signal failure
            RemoveSigningMarkerHandle(hMarker);
            return ERROR_TIMEOUT;
        }
        // cleanup marker (will be deleted when handle closed due to FILE_FLAG_DELETE_ON_CLOSE)
        RemoveSigningMarkerHandle(hMarker);
        return ERROR_SUCCESS;
    }
    else
    {
        RemoveSigningMarkerHandle(hMarker);
        return ERROR_TIMEOUT;
    }
}

//------------------------------------------------------------------------------
// marshalPublicKey (utils.go -> unmarshalRSA/ECC placeholder)
//------------------------------------------------------------------------------
// std::vector<BYTE> zzj::sshagent::KeyManagerAgent::MarshalPublicKey(NCRYPT_KEY_HANDLE hKey)
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

void zzj::sshagent::KeyManagerAgent::OpenProvider()
{
    if (hProv_) return;
    SECURITY_STATUS st = NCryptOpenStorageProvider(&hProv_, MS_PLATFORM_KEY_STORAGE_PROVIDER, 0);
    if (st != ERROR_SUCCESS)
    {
        hProv_ = 0;
    }
}
