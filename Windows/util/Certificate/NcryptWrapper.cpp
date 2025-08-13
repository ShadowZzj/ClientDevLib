#include "NcryptWrapper.hpp"
#include <bcrypt.h>
#include <chrono>
#include <AclAPI.h>
#include <cstdint>
#include <combaseapi.h> // Add this include for CoTaskMemFree
#include <strsafe.h>
#include <shlobj.h> // Add this include for FOLDERID_RoamingAppData
#include <General/util/SPDLogHelper.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <Shlwapi.h>
#pragma Comment(lib, "Advapi32.lib")


//------------------------------------------------------------------------------
// Utility functions (utils.go -> to_wstring, to_string)
//------------------------------------------------------------------------------
std::wstring ncrypt_utils::to_wstring(const std::string &utf8) {
    // utils.go wide()
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring wstr(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wstr[0], len);
    return wstr;
}

std::string ncrypt_utils::to_string(const std::wstring &wstr) {
    // utils.go conversion back
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string str(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], len, nullptr, nullptr);
    return str;
}

//------------------------------------------------------------------------------
// NCryptProvider (ncrypt.go -> NCryptOpenStorageProvider)
//------------------------------------------------------------------------------
NCryptProvider::NCryptProvider(const std::wstring &providerName, DWORD flags) {
    // ncrypt.go: NCryptOpenStorageProvider
    SECURITY_STATUS st = NCryptOpenStorageProvider(&hProv_, providerName.c_str(), flags);
    if (FAILED(st)) throw NCryptException("NCryptOpenStorageProvider failed", st);

}

NCryptProvider::~NCryptProvider() {
    // ncrypt.go: NCryptFreeObject
    if (hProv_) NCryptFreeObject(hProv_);
}

//------------------------------------------------------------------------------
// NCryptKey (ncrypt.go -> NCryptCreatePersistedKey, NCryptOpenKey, NCryptFinalizeKey)
//------------------------------------------------------------------------------
NCryptKey::NCryptKey(NCRYPT_PROV_HANDLE prov,
                     const std::wstring &keyName,
                     const std::wstring &algorithm,
                     DWORD legacyKeySpec,
                     DWORD flags) {
    // ncrypt.go: NCryptCreatePersistedKey
    SECURITY_STATUS st = NCryptCreatePersistedKey(
        prov, &hKey_, algorithm.c_str(), keyName.c_str(), legacyKeySpec, flags);
    if (FAILED(st)) throw NCryptException("NCryptCreatePersistedKey failed", st);
}

NCryptKey::~NCryptKey() {
    // ncrypt.go: NCryptFreeObject
    if (hKey_) NCryptFreeObject(hKey_);
}

void NCryptKey::finalize(DWORD flags) {
    // ncrypt.go: NCryptFinalizeKey
    SECURITY_STATUS st = NCryptFinalizeKey(hKey_, flags);
    if (FAILED(st)) throw NCryptException("NCryptFinalizeKey failed", st);
}

//------------------------------------------------------------------------------
// exportKey (utils.go -> NCryptExportKey)
//------------------------------------------------------------------------------
std::vector<BYTE> NCryptKey::ExportKey(const std::wstring &BlobType) const {
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
std::vector<BYTE> NCryptKey::GetProperty(const std::wstring &property, DWORD flags) const {
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
std::vector<BYTE> NCryptKey::SignHash(const std::vector<BYTE> &hash,
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

void NCryptKey::Destroy(DWORD flags) {
    // ncrypt.go NCryptDeleteKey
    SECURITY_STATUS st = NCryptDeleteKey(hKey_, flags);
    if (FAILED(st)) throw NCryptException("NCryptDeleteKey failed", st);
    hKey_ = 0; // reset handle
}

//------------------------------------------------------------------------------
// enumKeys (ncrypt.go -> NCryptEnumKeys)
//------------------------------------------------------------------------------
std::vector<KeyDescriptor> enumKeys(NCRYPT_PROV_HANDLE prov,
                                    const std::wstring &scope,
                                    DWORD flags) {
    std::vector<KeyDescriptor> list;
    PVOID enumState = nullptr;
    while (true) {
        NCryptKeyName *pName = nullptr;
        // ncrypt.go NCryptEnumKeys
        SECURITY_STATUS st = NCryptEnumKeys(
            prov,
            scope.empty() ? nullptr : scope.c_str(),
            &pName, &enumState, flags);
        if (st == NTE_NO_MORE_ITEMS) break;
        if (FAILED(st)) throw NCryptException("NCryptEnumKeys failed", st);

        KeyDescriptor kd{pName->pszName, pName->pszAlgid, pName->dwLegacyKeySpec, pName->dwFlags};
        list.push_back(kd);
        // ncrypt.go NCryptFreeBuffer
        NCryptFreeBuffer(pName);
    }
    return list;
}

//------------------------------------------------------------------------------
// marshalPublicKey (utils.go -> unmarshalRSA/ECC placeholder)
//------------------------------------------------------------------------------
std::vector<BYTE> MarshalPublicKey(NCRYPT_KEY_HANDLE hKey) {
    DWORD size = 0;
    SECURITY_STATUS st = NCryptExportKey(
        hKey, 0, BCRYPT_RSAPUBLIC_BLOB, nullptr, nullptr, 0, &size, 0);
    if (FAILED(st)) {
        throw NCryptException("NCryptExportKey failed", st);
    }

    std::vector<BYTE> Blob(size);
    st = NCryptExportKey(hKey, 0, BCRYPT_RSAPUBLIC_BLOB, nullptr, Blob.data(), size, &size, 0);
    if (FAILED(st)) {
        throw NCryptException("NCryptExportKey failed", st);
    }

    struct BCRYPT_RSAKEY_BLOB {
        ULONG Magic;
        ULONG BitLength;
        ULONG cbPublicExp;
        ULONG cbModulus;
        ULONG cbPrime1;
        ULONG cbPrime2;
    };

    BYTE* ptr = Blob.data();
    auto* header = reinterpret_cast<BCRYPT_RSAKEY_BLOB*>(ptr);
    BYTE* exp = ptr + sizeof(BCRYPT_RSAKEY_BLOB);
    BYTE* mod = exp + header->cbPublicExp;

    std::vector<BYTE> wire;

    auto append_mpint = [&](const BYTE* data, DWORD len) {
        /* while (len > 0 && data[0] == 0)
            ++data, --len;
        wire.push_back((len >> 24) & 0xFF);
        wire.push_back((len >> 16) & 0xFF);
        wire.push_back((len >> 8) & 0xFF);
        wire.push_back(len & 0xFF);
        wire.insert(wire.end(), data, data + len);
        */
        // 1. 小端 → tmp，再反转得大端
        std::vector<BYTE> tmp(data, data + len);
        std::reverse(tmp.begin(), tmp.end());
        // 2. 去掉 MSB 端前导 0
        auto it = tmp.begin();
        while (it != tmp.end() && *it == 0) ++it;
        std::vector<BYTE> beData(it, tmp.end());
        // 3. 如果最高位被置 1，插一个 0x00 在前面
        if (!beData.empty() && (beData[0] & 0x80))
        {
            beData.insert(beData.begin(), 0x00);
        }
        // 4. 写长度（4 字节大端）和写内容
        DWORD outLen = static_cast<DWORD>(beData.size());
        wire.push_back((outLen >> 24) & 0xFF);
        wire.push_back((outLen >> 16) & 0xFF);
        wire.push_back((outLen >> 8) & 0xFF);
        wire.push_back(outLen & 0xFF);
        wire.insert(wire.end(), beData.begin(), beData.end());
    };

    auto append_str = [&](const char* s) {
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
std::vector<BYTE> MarshalPublicKeyHandleFromName(NCryptKeyName *pName)
{
    if (!pName || !pName->pszName)
    {
        throw std::invalid_argument("Invalid NCryptKeyName pointer or empty name.");
    }

    // Open provider
    NCryptProvider provider(ncrypt::MS_PLATFORM_PROVIDER);
    NCRYPT_PROV_HANDLE hProv = provider.handle();

    // Open key by name
    NCRYPT_KEY_HANDLE hKey = 0;
    SECURITY_STATUS st = NCryptOpenKey(hProv, &hKey, pName->pszName, 0, 0);
    if (FAILED(st))
    {
        throw NCryptException("NCryptOpenKey failed", st);
    }

    // Marshal public key
    std::vector<BYTE> blob = MarshalPublicKey(hKey);

    // Free key handle
    NCryptFreeObject(hKey);

    return blob;
}
//------------------------------------------------------------------------------
// NCryptSigner (signer.go -> newNCryptSigner, Sign, handlePinTimer)
//------------------------------------------------------------------------------
NCryptSigner::NCryptSigner(NCRYPT_KEY_HANDLE keyHandle, int pinTimeoutSeconds)
    : keyHandle_(keyHandle), pinTimeoutSec_(pinTimeoutSeconds), timerActive_(false) {
    // signer.go newNCryptSigner: load publicKey
    pubKeyBlob_ = MarshalPublicKey(keyHandle_);
}

NCryptSigner::~NCryptSigner() {
    // signer.go handlePinTimer cleanup
    purgePinCache();
}

std::vector<BYTE> NCryptSigner::Sign(const std::vector<BYTE> &digest,
                                     const std::wstring &hashAlg) {
    // signer.go Sign(): call NCryptSignHash
    std::vector<BYTE> sig = 
        NCryptKey(0, L"", L"", 0, 0).SignHash(digest, hashAlg, NCRYPT_SILENT_FLAG);

    // signer.go handlePinTimer logic
    if (pinTimeoutSec_ > 0 && !timerActive_) {
        timerActive_ = true;
        std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::seconds(pinTimeoutSec_));
            purgePinCache();
        }).detach();
    }
    return sig;
}

std::vector<BYTE> NCryptSigner::PublicKey() const {
    // signer.go Public()
    return pubKeyBlob_;
}

void NCryptSigner::setPinTimeout(int seconds) {
    // signer.go SetPINTimeout
    pinTimeoutSec_ = seconds;
}

void NCryptSigner::purgePinCache()
{
    // signer.go handlePinTimer: NCryptSetProperty to clear PIN
    std::lock_guard<std::mutex> lock(mutex_);
    NCryptSetProperty(
        keyHandle_,
        NCRYPT_PIN_PROPERTY,
        nullptr, 0, 0);
    timerActive_ = false;
}

sshagent::KeyManagerAgent::KeyManagerAgent() { 
    OpenProvider();
}

sshagent::KeyManagerAgent::~KeyManagerAgent() {
    // TODO: stop listener if running
    std::lock_guard<std::mutex> lk(identities_mutex_);
    for (auto &id : identities_)
    {
        if (id.hKey) NCryptFreeObject(id.hKey);
    }
    identities_.clear();
    // free provider
    if (hProv_) NCryptFreeObject(hProv_);
}

bool sshagent::KeyManagerAgent::InitDeployment()
{
    std::wstring container = L"redpass-ssh";
    //std::wstring password = L"123456";
    wchar_t *roaming = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) return false;
    std::wstring pubDir = std::wstring(roaming) + L"\\nCryptAgent\\PublicKeys";
    CoTaskMemFree(roaming);
    return DeploySSH(container, 2048, pubDir);
}

void sshagent::KeyManagerAgent::CreateNewRSAKey(const std::wstring &containerName, int keySize)
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

std::vector<BYTE> sshagent::KeyManagerAgent::ExportRSAPublicKey(const std::wstring &containerName)
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

bool sshagent::KeyManagerAgent::DestroyRSAKey(const std::wstring &containerName)
{
    NCryptProvider provider(ncrypt::MS_PLATFORM_PROVIDER);
    NCRYPT_PROV_HANDLE hProv = provider.handle();
    NCRYPT_KEY_HANDLE hKey = 0;
    SECURITY_STATUS st = NCryptOpenKey(hProv, &hKey, containerName.c_str(), 0, 0);
    if (FAILED(st))
    {
        // 密钥可能不存在，这不一定是个错误
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

bool sshagent::KeyManagerAgent::DeploySSH(const std::wstring &containerName, int keySize,
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
            // 同步 identities_ 缓存
            ReloadAllIdentities();
            return true;
      ; 
        }
    }
    // Create new key with password
    NCRYPT_KEY_HANDLE hKey = 0;
    //SECURITY_STATUS st = NCryptCreatePersistedKey(hProv_, &hKey, BCRYPT_RSA_ALGORITHM, containerName.c_str(), 0, NCRYPT_MACHINE_KEY_FLAG);
    SECURITY_STATUS st = NCryptCreatePersistedKey(hProv_, &hKey, BCRYPT_RSA_ALGORITHM, containerName.c_str(), 0,
                                 /* 用户级 key，无需管理员权限 */ 0);
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
    //if (ok) { // 同步 identities_ 缓存 ReloadAllIdentities();   }
    return ok;
}


// 添加安全调用 exe 的辅助函数
bool sshagent::KeyManagerAgent::CallNcryptAgent(const std::wstring &containerName)
{
    try
    {
        // 获取当前模块路径
        wchar_t modulePath[MAX_PATH];
        if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0)
        {
            LOG_ERROR("Failed to get module path");
            return false;
        }

        // 构建 exe 路径
        std::wstring exePath = GetExecutablePath(modulePath);

        // 验证文件存在
        if (!PathFileExistsW(exePath.c_str()))
        {
            LOG_ERROR("NcryptAgent.exe not found at: " +
                      std::string(exePath.begin(), exePath.end()));
            return false;
        }

        // 构建命令行参数
        std::wstring commandLine = L"\"" + exePath + L"\" \"" + containerName + L"\"";

        // 安全调用进程
        return ExecuteProcess(commandLine);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Exception in CallNcryptAgent: " + std::string(e.what()));
        return false;
    }
}

// 获取 NcryptAgent.exe 的完整路径
std::wstring sshagent::KeyManagerAgent::GetExecutablePath(const std::wstring &modulePath)
{
    // 从当前模块路径推导出项目根目录
    std::wstring path = modulePath;

    // 移除文件名，得到目录
    size_t lastSlash = path.find_last_of(L'\\');
    if (lastSlash != std::wstring::npos)
    {
        path = path.substr(0, lastSlash);
    }

    // 相对路径应该是：..\NcryptAgent\build\NcryptAgent.exe
    std::wstring exePath = path + L"\\NcryptAgent.exe";

    // 规范化路径
    wchar_t canonicalPath[MAX_PATH];
    if (PathCanonicalizeW(canonicalPath, exePath.c_str()))
    {
        return std::wstring(canonicalPath);
    }

    return exePath;
}


bool sshagent::KeyManagerAgent::ExecuteProcess(const std::wstring &commandLine)
{
    STARTUPINFOW si = {};
    PROCESS_INFORMATION pi = {};
    si.cb = sizeof(si);

    // 创建命令行的可修改副本
    std::vector<wchar_t> cmdLine(commandLine.begin(), commandLine.end());
    cmdLine.push_back(L'\0');

    // 创建进程
    BOOL success = CreateProcessW(nullptr,           // 应用程序名称
                                  cmdLine.data(),    // 命令行
                                  nullptr,           // 进程安全属性
                                  nullptr,           // 线程安全属性
                                  FALSE,             // 不继承句柄
                                  CREATE_NO_WINDOW,  // 创建标志（隐藏窗口）
                                  nullptr,           // 环境变量
                                  nullptr,           // 当前目录
                                  &si,               // 启动信息
                                  &pi                // 进程信息
    );

    if (!success)
    {
        DWORD error = GetLastError();
        LOG_ERROR("CreateProcess failed with error: " + std::to_string(error));
        return false;
    }

    // 等待进程完成（可选，根据需求决定）
    DWORD waitResult = WaitForSingleObject(pi.hProcess, 30000);  // 30秒超时

    DWORD exitCode = 0;
    bool result = false;

    if (waitResult == WAIT_OBJECT_0)
    {
        // 进程正常结束，获取退出码
        if (GetExitCodeProcess(pi.hProcess, &exitCode))
        {
            result = (exitCode == 0);  // 假设0表示成功
            if (!result)
            {
                LOG_ERROR("NcryptAgent.exe returned error code: " + std::to_string(exitCode));
            }
        }
    }
    else if (waitResult == WAIT_TIMEOUT)
    {
        LOG_ERROR("NcryptAgent.exe execution timeout");
        TerminateProcess(pi.hProcess, 1);
    }
    else
    {
        LOG_ERROR("Wait for NcryptAgent.exe failed");
    }

    // 清理资源
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return result;
}

bool sshagent::KeyManagerAgent::SaveCertificateFile(const std::wstring &containerName,
                                                    const std::vector<BYTE> &certBlob,
                                                    const std::wstring &pubOutDir){
    // Ensure output directory exists
    if (certBlob.size() == 0 || certBlob.size() > 1024 * 1024) return false;
    if (!EnsureDirectoryExists(pubOutDir)) return false;
    CreateDirectoryW(pubOutDir.c_str(), nullptr);
    std::wstring outFile = pubOutDir + L"\\" + containerName + L"-cert.pub";
    return WriteBlobToFile(outFile, certBlob);

}

// 检查文件是否存在
bool sshagent::fileExists(const std::wstring &path)
{
    DWORD attrs = GetFileAttributesW(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
}

// 从缓存中移除身份
void sshagent::KeyManagerAgent::RemoveIdentityFromCache(const std::wstring &containerName)
{
    std::lock_guard<std::mutex> lk(identities_mutex_);
    std::string containerNameStr = ncrypt_utils::to_string(containerName); 
    for (auto it = identities_.begin(); it != identities_.end();)
    {
        if (it->Comment == containerNameStr)
        {
            // 释放密钥句柄
            if (it->hKey)
            {
                NCryptFreeObject(it->hKey);
                it->hKey = 0;
            }
            // 从缓存移除
            it = identities_.erase(it);
            LOG_INFO("Removed identity from cache");
        }
        else
        {
            ++it;
        }
    }
}

bool sshagent::KeyManagerAgent::unDeploySSH(const std::wstring &containerName,
                                            const std::wstring &pubOutDir)
{
    bool success = true;

    // 1. 删除密钥容器
    if (!DestroyRSAKey(containerName))
    {
        LOG_ERROR("Failed to delete key container");
        success = false;
    }

    // 2. 清除缓存中的身份
    RemoveIdentityFromCache(containerName);

    // 3. 删除公钥文件
    std::wstring pubKeyPath = pubOutDir + L"\\" + containerName + L".pub";
    if (fileExists(pubKeyPath))
    {
        if (!DeleteFileW(pubKeyPath.c_str()))
        {
            LOG_ERROR("Failed to delete public key file");
            success = false;
        }
    }

    // 4. 删除证书文件（如果存在）
    std::wstring certPath = pubOutDir + L"\\" + containerName + L"-cert.pub";
    if (fileExists(certPath))
    {
        if (!DeleteFileW(certPath.c_str()))
        {
            LOG_ERROR("Failed to delete certificate file");
            success = false;
        }
    }

    // 5. 清理部署标记（可选，根据需求）
    // DeploymentChecker::ClearDeployment();

    return success;
}

static bool WriteBlobToFile(const std::wstring &fullPath, const std::vector<BYTE> &data)
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
bool ReadBlobFromFile(const std::wstring &fullPath, std::vector<BYTE> &outData,
                      size_t maxSize = 1024 * 1024)  // 默认 1MB 上限
{
    // 使用最小安全：仅本用户可读
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, FALSE};

    // 以只读方式打开
    HANDLE hFile = CreateFileW(fullPath.c_str(),
                               GENERIC_READ,     // 只读
                               FILE_SHARE_READ,  // 允许其他读
                               &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    // 获取文件大小
    LARGE_INTEGER filesize = {};
    if (!GetFileSizeEx(hFile, &filesize) || filesize.QuadPart <= 0 ||
        filesize.QuadPart > static_cast<LONGLONG>(maxSize))
    {
        CloseHandle(hFile);
        return false;
    }

    DWORD toRead = static_cast<DWORD>(filesize.QuadPart);
    outData.resize(toRead);

    DWORD totalRead = 0;
    BYTE *buffer = outData.data();
    while (totalRead < toRead)
    {
        DWORD chunk = 0;
        BOOL ok = ReadFile(hFile, buffer + totalRead, toRead - totalRead, &chunk, nullptr);
        if (!ok || chunk == 0)
        {
            CloseHandle(hFile);
            return false;
        }
        totalRead += chunk;
    }

    CloseHandle(hFile);
    return true;
}

bool EnsureDirectoryExists(const std::wstring &dir)
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


std::string base64_encode(const BYTE *data, size_t length)
{
    DWORD base64Len = 0;
    if (!CryptBinaryToStringA(data, static_cast<DWORD>(length),
                              CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &base64Len))
    {
        throw std::runtime_error("Base64 length error");
    }

    std::string result(base64Len, '\0');
    if (!CryptBinaryToStringA(data, static_cast<DWORD>(length),
                              CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &result[0], &base64Len))
    {
        throw std::runtime_error("Base64 encoding error");
    }
    //result.pop_back();  // 移除尾部的null字符
    if (!result.empty() && result.back() == '\0')
    {
        result.pop_back();
    }
    return result;
}

// 宽字符串转UTF-8字符串
std::string narrow(const std::wstring &wstr)
{
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, NULL, NULL);
    return result.c_str();  // 自动处理末尾空字符
}

bool WriteStringToFile(const std::wstring &filePath, const std::string &content)
{
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    DWORD written = 0;
    if (!WriteFile(hFile, content.data(), static_cast<DWORD>(content.size()), &written, NULL))
    {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);
    return written == content.size();
}


bool sshagent::KeyManagerAgent::ExportPublicKeyToFile(const std::wstring &containerName,
                                            const std::wstring &pubOutDir){
    try
    {
        auto Blob = ExportRSAPublicKey(containerName);
        // 转换为Base64
        const std::string base64Data = base64_encode(Blob.data(), Blob.size());
        // Ensure output directory exists// 构建公钥文件内容
        const std::string pubKeyContent = "ssh-rsa " + base64Data ;

        // 确保输出目录存在
        if (!EnsureDirectoryExists(pubOutDir)) return false;
        // Path: <pubOutDir>\<MD5>.pub
        // 2. 计算 MD5 指纹（Legacy MD5）并去掉冒号 → 文件名核心
        //std::string fp = MD5HexNoColons(Blob.data(), static_cast<DWORD>(Blob.size()));
        std::wstring outFile = pubOutDir + L"\\" + containerName + L".pub";
        return WriteStringToFile(outFile, pubKeyContent);
    }
    catch (...)
    {
        return false;
    }
}

bool sshagent::DeploymentChecker::IsDeployed()
{
    wchar_t *roaming = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) return false;
    std::wstring flag = std::wstring(roaming) + L"\\nCryptAgent\\deployed.flag";
    CoTaskMemFree(roaming);
    return GetFileAttributesW(flag.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void sshagent::DeploymentChecker::MarkDeployed()
{
    wchar_t *roaming = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)))
    {
        std::wstring dir = std::wstring(roaming) + L"\\nCryptAgent";
        CreateDirectoryW(dir.c_str(), nullptr);
        std::wstring flag = dir + L"\\deployed.flag";
        WriteBlobToFile(flag, std::vector<BYTE>{});  // 写一个空文件
        CoTaskMemFree(roaming);
    }
}

void sshagent::DeploymentChecker::ClearDeployment(const std::wstring &containerName)
{
    wchar_t *roaming = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)))
    {
        std::wstring flag = std::wstring(roaming) + L"\\nCryptAgent\\deployed.flag";
        DeleteFileW(flag.c_str());
        std::wstring dir = std::wstring(roaming) + L"\\nCryptAgent";
        // 删除公钥文件
        std::wstring pubKey = dir + L"\\" + containerName + L".pub";
        DeleteFileW(pubKey.c_str());

        // 删除证书文件
        std::wstring cert = dir + L"\\" + containerName + L"-cert.pub";
        DeleteFileW(cert.c_str());
        CoTaskMemFree(roaming);
        
    }
}

bool sshagent::UploadPublicKey(const std::vector<BYTE> &pubBlob, const std::string &principal,
                               const std::string &caEndpoint)
{
    return TRUE;
}

bool sshagent::DownloadSSHCertificate(const std::string &certUrl, const std::wstring &outPath)
{
    return TRUE;
}



std::vector<sshagent::KeyManagerAgent::Identity> sshagent::KeyManagerAgent::ExportAllIdentities()
{
    std::lock_guard<std::mutex> lk(identities_mutex_);
    return identities_;  // 返回当前缓存
}

const DWORD sshagent::KeyManagerAgent::READ_BUF_SIZE = 64 * 1024;
const int sshagent::KeyManagerAgent::STABLE_CHECKS = 4;
const std::chrono::milliseconds sshagent::KeyManagerAgent::STABLE_DELAY(200);
const std::chrono::seconds sshagent::KeyManagerAgent::TOTAL_TIMEOUT(20);
bool sshagent::KeyManagerAgent::ReadFileToBuffer(const std::wstring &path, std::vector<char> &out)
{
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE){
        return false;
    }
    // 获取文件大小
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize)){
        CloseHandle(hFile);
        return false;
    }
    // 检查文件大小是否合理
    if (fileSize.QuadPart < 0 || fileSize.QuadPart > MAXDWORD){
        CloseHandle(hFile);
        return false;
    }
    DWORD fileSizeDW = static_cast<DWORD>(fileSize.QuadPart);
    // 调整缓冲区大小
    try
    {
        out.resize(fileSizeDW);
    }
    catch (const std::bad_alloc &)
    {
        CloseHandle(hFile);
        return false;
    }
    // 读取文件内容
    DWORD bytesRead = 0;
    BOOL success = FALSE;
    if (fileSizeDW > 0){
        success = ReadFile(hFile, out.data(), fileSizeDW, &bytesRead, NULL);
    }
    else{
        success = TRUE;  // 空文件
        bytesRead = 0;
    }
    CloseHandle(hFile);

    if (!success || bytesRead != fileSizeDW){
        return false;
    }

    return true;
}


HANDLE sshagent::KeyManagerAgent::TryOpenFileReadExclusive(const std::wstring &path)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ,  // 允许其他读者，如果写者拒绝共享则可能失败
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    return h;
}

bool sshagent::KeyManagerAgent::WaitFileStable(const std::wstring &fullpath,
                                     std::chrono::milliseconds perInterval, int stableNeeded,
                                     std::chrono::seconds timeout)
{
    using clock = std::chrono::steady_clock;
    auto deadline = clock::now() + timeout;
    long long prevSize = -1;
    int stableCount = 0;

    while (clock::now() < deadline)
    {
        // 尝试获取文件大小
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (!GetFileAttributesExW(fullpath.c_str(), GetFileExInfoStandard, &fad))
        {
            // 文件可能还不存在
            std::this_thread::sleep_for(perInterval);
            continue;
        }

        LARGE_INTEGER size;
        size.LowPart = fad.nFileSizeLow;
        size.HighPart = fad.nFileSizeHigh;
        long long curSize = static_cast<long long>(size.QuadPart);

        // 尝试打开文件以检测写者是否仍持有独占锁
        HANDLE h = TryOpenFileReadExclusive(fullpath);
        if (h == INVALID_HANDLE_VALUE){
            // 无法打开读取 - 可能仍在被写入（写者拒绝共享）
            stableCount = 0;
            std::this_thread::sleep_for(perInterval);
            continue;
        }
        else{
            // 打开成功 - 立即关闭
            CloseHandle(h);
        }

        if (curSize == prevSize)
        {
            stableCount++;
            if (stableCount >= stableNeeded)
            {
                return true;
            }
        }
        else
        {
            prevSize = curSize;
            stableCount = 0;
        }

        std::this_thread::sleep_for(perInterval);
    }

    return false;
}

bool sshagent::KeyManagerAgent::SignPubKeyWithCA(const std::wstring &pubPath,
                                       const std::vector<char> &pubContent,
                                       const std::string &principal)
{
    // 实现SSH CA签名逻辑 需要调用实际的SSH CA API
    try
    {
        // 1. 将公钥内容转换为适当格式
        std::string pubKeyStr(pubContent.begin(), pubContent.end());

        // 2. 调用SSH CA上传公钥并申请证书
        // 这里应该调用现有的sshagent::UploadPublicKey函数
        // if (!sshagent::UploadPublicKey(pubKeyStr, principal, sshCaUploadUrl_)) {
        //     return false;
        // }

        // 3. 下载证书并保存
        std::wstring certPath = pubPath.substr(0, pubPath.find_last_of(L'.')) + L"-cert.pub";
        // if (!sshagent::DownloadSSHCertificate(sshCaDownloadUrl_, certPath)) {
        //     return false;
        // }

        // 临时实现：标记处理成功
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

// 辅助：尝试创建 marker 文件（原子：CREATE_NEW）
// 返回 handle（非 INVALID_HANDLE_VALUE）表示创建成功，调用方负责 CloseHandle +
// DeleteFile(markerPath). 如果创建失败并且 GetLastError() == ERROR_FILE_EXISTS 则表示已有 marker。
static HANDLE CreateSigningMarker(const std::wstring &markerPath)
{
    HANDLE h = CreateFileW(markerPath.c_str(), GENERIC_WRITE,
                           0,  // 不允许共享，确保独占
                           NULL,
                           CREATE_NEW,  // 原子创建，如果已存在则失败
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    // 如果成功，返回 handle（会在后续 CloseHandle 即删除）
    return h;
}

// 删除 marker（若仍存在）
static void RemoveSigningMarkerHandle(HANDLE hMarker)
{
    if (hMarker != INVALID_HANDLE_VALUE)
    {
        // 如果 OPEN 时使用 FILE_FLAG_DELETE_ON_CLOSE，CloseHandle 会自动删除文件。
        CloseHandle(hMarker);
    }
}

// 检查 marker 是否存在并且是否为 stale（超过 timeout）
// 如果存在返回 true；同时如果超过 timeout 返回 true 并 sets staleOut true.
static bool IsSigningMarkerPresent(const std::wstring &markerPath, std::chrono::seconds timeout,
                                   bool &staleOut)
{
    staleOut = false;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(markerPath.c_str(), GetFileExInfoStandard, &fad))
    {
        return false;
    }
    // 获取最后写入时间
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
static bool WaitForMarkerRemovalOrCert(const std::wstring &markerPath, const std::wstring &certPath,
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


bool sshagent::KeyManagerAgent::MonitorAndSignPubKey(const std::wstring &watchDir,
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
        // 提取文件名（不是以null结尾的）
        std::wstring filename(pInfo->FileName,
                              pInfo->FileName + pInfo->FileNameLength / sizeof(WCHAR));

        // 检查是否是我们期望的文件
        if (filename == expectedFilename)
        {
            matchedFile = filename;
            found = true;

            // 如果是重命名新名称，认为是原子移动，可能已准备就绪
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

    // 完整路径
    std::wstring fullpath = watchDir + L"\\" + matchedFile;
    std::wstring certPath = fullpath.substr(0, fullpath.find_last_of(L'.')) + L"-cert.pub";
    std::wstring markerPath = fullpath + L".signing";

    // 等待文件稳定
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
            LOG_ERROR(
                "MonitorAndSignPubKey: signing reported success but cert did not "
                "appear/stabilize: " +
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


void sshagent::KeyManagerAgent::OpenProvider() {
    if (hProv_) return;
    SECURITY_STATUS st = NCryptOpenStorageProvider(&hProv_, ncrypt::MS_PLATFORM_PROVIDER, 0);
    if (FAILED(st)) throw NCryptException("NCryptOpenStorageProvider failed", st);
}

void sshagent::KeyManagerAgent::ReloadAllIdentities() {
    std::lock_guard<std::mutex> lk(identities_mutex_);
    identities_.clear();
    // 枚举所有持久化 key
    NCryptKeyName *pName = nullptr;
    PVOID enumState = nullptr;
    while (true)
    {
        SECURITY_STATUS st = NCryptEnumKeys(hProv_,   // openProvider 已经打开 hProv_
                                            nullptr,  // 无特殊 scope
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

bool sshagent::RevokeSSHCertificate(const std::string &caRevokeEndpoint,
                                    const std::wstring &certSerial)
{
    return false;
}

void sshagent::CleanupAll(const std::string &caRevokeEndpoint, const std::wstring &containerName) {}
