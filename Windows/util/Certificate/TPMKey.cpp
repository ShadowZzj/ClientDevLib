#include "TPMKey.hpp"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <ncrypt.h>
#include <tchar.h>
#include <vector>
#include <wincrypt.h>
#include <General/util/Crypto/Base64.hpp>
#include <General/util/StrUtil.h>
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ncrypt.lib")
int zzj::TPMKey::Create(const std::string &keycontainer, LPCWSTR algorithm, DWORD keyLength,
                        DWORD flags)
{
    SECURITY_STATUS status = 0;
    status = NCryptOpenStorageProvider(&_hProv, MS_PLATFORM_KEY_STORAGE_PROVIDER, 0);
    if (status != ERROR_SUCCESS) return status;

    std::wstring keycontainerW = zzj::str::utf82w(keycontainer);
    status = NCryptCreatePersistedKey(_hProv, &_hKey, algorithm, keycontainerW.c_str(), 0, flags);
    if (status != ERROR_SUCCESS)
    {
        NCryptFreeObject(_hProv);
        _hProv = NULL;
        return status;
    }

    DWORD _keyLength = keyLength;
    status =
        NCryptSetProperty(_hKey, NCRYPT_LENGTH_PROPERTY, (PBYTE)&_keyLength, sizeof(_keyLength), 0);
    if (status != ERROR_SUCCESS)
    {
        NCryptFreeObject(_hKey);
        NCryptFreeObject(_hProv);
        _hKey = NULL;
        _hProv = NULL;
        return status;
    }

    DWORD dwKeyUsage = NCRYPT_ALLOW_ALL_USAGES;
    status = NCryptSetProperty(_hKey, NCRYPT_KEY_USAGE_PROPERTY, (PBYTE)&dwKeyUsage,
                               sizeof(dwKeyUsage), 0);
    if (status != ERROR_SUCCESS)
    {
        NCryptFreeObject(_hKey);
        NCryptFreeObject(_hProv);
        _hKey = NULL;
        _hProv = NULL;
        return status;
    }
    return 0;
}
int zzj::TPMKey::Delete()
{
    if (_hKey == NULL)
    {
        return -1;
    }
    NCryptDeleteKey(_hKey, 0);
    return 0;
}
int zzj::TPMKey::Finalize()
{
    if (_hKey == NULL) return -1;
    return NCryptFinalizeKey(_hKey, 0);
}

int zzj::TPMKey::Open(const std::string &keycontainer)
{
    if (_hKey != NULL)
    {
        NCryptFreeObject(_hKey);
        _hKey = NULL;
    }
    if (_hProv != NULL)
    {
        NCryptFreeObject(_hProv);
        _hProv = NULL;
    }
    SECURITY_STATUS status = 0;
    status = NCryptOpenStorageProvider(&_hProv, MS_PLATFORM_KEY_STORAGE_PROVIDER, 0);
    if (status != ERROR_SUCCESS) return status;

    std::wstring keycontainerW = zzj::str::utf82w(keycontainer);
    status = NCryptOpenKey(_hProv, &_hKey, keycontainerW.c_str(), 0, 0);
    if (status != ERROR_SUCCESS)
    {
        NCryptFreeObject(_hProv);
        _hProv = NULL;
        return status;
    }
    return 0;
}

int zzj::TPMKey::ExportPublicKey(PCERT_PUBLIC_KEY_INFO *publicKeyInfo)
{
    if (_hKey == NULL) return -1;

    DWORD cbPublicKeyInfo = 0;
    BOOL res = 0;
    res = CryptExportPublicKeyInfoEx(_hKey, 0, X509_ASN_ENCODING, NULL, 0, NULL, NULL,
                                     &cbPublicKeyInfo);
    if (!res) return GetLastError();

    PCERT_PUBLIC_KEY_INFO pPublicKeyInfo = (PCERT_PUBLIC_KEY_INFO)LocalAlloc(LPTR, cbPublicKeyInfo);
    res = CryptExportPublicKeyInfoEx(_hKey, 0, X509_ASN_ENCODING, NULL, 0, NULL, pPublicKeyInfo,
                                     &cbPublicKeyInfo);
    if (!res) return GetLastError();

    *publicKeyInfo = pPublicKeyInfo;

    return 0;
}

int zzj::TPMKey::GenerateCsrDerFormat(const std::string &certSubjectNameFullQualified,
                                      std::vector<BYTE> &csrDer)
{
    std::wstring certSubjectNameFullQualifiedW = zzj::str::utf82w(certSubjectNameFullQualified);
    CERT_NAME_BLOB certNameBlob;

    if (!CertStrToName(X509_ASN_ENCODING, certSubjectNameFullQualifiedW.c_str(), CERT_X500_NAME_STR,
                       NULL, NULL, &certNameBlob.cbData, NULL))
        return GetLastError();

    certNameBlob.pbData = (BYTE *)LocalAlloc(LPTR, certNameBlob.cbData);
    DEFER { LocalFree(certNameBlob.pbData); };

    if (!CertStrToName(X509_ASN_ENCODING, certSubjectNameFullQualifiedW.c_str(), CERT_X500_NAME_STR,
                       NULL, certNameBlob.pbData, &certNameBlob.cbData, NULL))
        return GetLastError();
    PCERT_PUBLIC_KEY_INFO publicKey = nullptr;
    auto status = ExportPublicKey(&publicKey);
    if (status != 0) return status;
    DEFER { LocalFree(publicKey); };

    CERT_REQUEST_INFO requestInfo = {0};
    requestInfo.SubjectPublicKeyInfo = *publicKey;
    requestInfo.Subject.pbData = certNameBlob.pbData;
    requestInfo.Subject.cbData = certNameBlob.cbData;
    requestInfo.dwVersion = CERT_REQUEST_V1;

    CRYPT_ALGORITHM_IDENTIFIER signatureAlgorithm = {szOID_RSA_SHA256RSA, {0, NULL}};
    CRYPT_DATA_BLOB requestBlob = {0};
    if (!CryptSignAndEncodeCertificate(_hKey, AT_SIGNATURE, X509_ASN_ENCODING,
                                       X509_CERT_REQUEST_TO_BE_SIGNED, &requestInfo,
                                       &signatureAlgorithm, NULL, NULL, &requestBlob.cbData))
        return GetLastError();

    requestBlob.pbData = (BYTE *)LocalAlloc(LPTR, requestBlob.cbData);
    DEFER { LocalFree(requestBlob.pbData); };

    if (!CryptSignAndEncodeCertificate(
            _hKey, AT_SIGNATURE, X509_ASN_ENCODING, X509_CERT_REQUEST_TO_BE_SIGNED, &requestInfo,
            &signatureAlgorithm, NULL, requestBlob.pbData, &requestBlob.cbData))
        return GetLastError();

    csrDer.assign(requestBlob.pbData, requestBlob.pbData + requestBlob.cbData);

    return 0;
}

int zzj::TPMKey::GenerateCsrPemFormat(const std::string &certSubjectNameFullQualified,
                                      std::string &csrPem)
{
    int status = 0;
    std::vector<BYTE> csrDer;

    status = GenerateCsrDerFormat(certSubjectNameFullQualified, csrDer);
    if (status != 0) return status;

    std::string base64String = zzj::Base64Help::Encode((const char *)csrDer.data(), csrDer.size());

    // Insert line breaks every 64 characters
    const size_t lineLength = 64;
    std::string base64WithLineBreaks;
    for (size_t i = 0; i < base64String.length(); i += lineLength)
    {
        base64WithLineBreaks += base64String.substr(i, lineLength);
        base64WithLineBreaks += "\n";
    }

    std::string pemCSR = "-----BEGIN CERTIFICATE REQUEST-----\n";
    pemCSR += base64WithLineBreaks;
    pemCSR += "-----END CERTIFICATE REQUEST-----\n";

    csrPem = pemCSR;
    return 0;
}

int zzj::TPMKey::SignDataSha256(const std::vector<char> &data, std::vector<char> &signature)
{
    // 1) Query which algorithm group the key belongs to (RSA or ECC).
    WCHAR algoGroup[32] = {0};
    DWORD cbProp = 0;
    NTSTATUS nts =
        NCryptGetProperty(_hKey, NCRYPT_ALGORITHM_GROUP_PROPERTY,
                          reinterpret_cast<PBYTE>(algoGroup), sizeof(algoGroup), &cbProp, 0);
    if (nts != ERROR_SUCCESS) return nts;

    // 2) Decide whether we need padding info (for RSA) or not (ECC).
    PVOID pPaddingInfo = nullptr;
    DWORD dwFlags = 0;

    BCRYPT_PKCS1_PADDING_INFO pi;
    ZeroMemory(&pi, sizeof(pi));
    // If the key is RSA, use PKCS#1 padding (as an example).
    // If the key is ECC, use no padding, etc.
    if (!_wcsicmp(algoGroup, NCRYPT_RSA_ALGORITHM_GROUP))
    {
        // Typically should match the hash algorithm actually used
        pi.pszAlgId = BCRYPT_SHA256_ALGORITHM;
        pPaddingInfo = &pi;
        dwFlags = BCRYPT_PAD_PKCS1;
    }
    else if (!_wcsicmp(algoGroup, NCRYPT_ECDSA_ALGORITHM_GROUP))
    {
        // ECC => no padding needed
        pPaddingInfo = nullptr;
        dwFlags = 0;
    }
    else
    {
        // Unsupported algorithm group
        return -1;
    }

    // 3) Call CryptHashCertificate2 to hash the data
    std::vector<BYTE> hashValue(32);
    ULONG cb = hashValue.size();
    if (!CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM, 0, 0, (const BYTE*)data.data(), data.size(),
                               hashValue.data(), &cb))
    {
        return GetLastError();
    }

    // 4) Call NCryptSignHash to sign the hash

    BYTE *pbSignature = nullptr;
    DWORD cbSignature = 0;

    while (true)
    {
        nts = NCryptSignHash(_hKey, pPaddingInfo, hashValue.data(),
                             cb,  // size of data
                             pbSignature, cbSignature, &cbSignature, dwFlags);

        if (!BCRYPT_SUCCESS(nts))
        {
            if (pbSignature)
            {
                delete[] pbSignature;
                pbSignature = nullptr;
            }
            return nts;
        }

        if (pbSignature)
        {
            // We have the signature
            signature.resize(cbSignature);
            memcpy(signature.data(), pbSignature, cbSignature);
            delete[] pbSignature;  // cleanup
            pbSignature = nullptr;
            break;
        }
        else
        {
            // First pass: pbSignature == NULL => we got the required cbSignature
            pbSignature = new BYTE[cbSignature];
            ZeroMemory(pbSignature, cbSignature);
        }
    }

    return 0;
}

int zzj::TPMKey::AssociateCertificate(PCCERT_CONTEXT pCertContext, const std::string &keycontainer)
{
    std::wstring keycontainerW = zzj::str::utf82w(keycontainer);
    CRYPT_KEY_PROV_INFO keyProvInfo = {0};
    keyProvInfo.pwszContainerName = (LPWSTR)keycontainerW.c_str();
    keyProvInfo.pwszProvName = MS_PLATFORM_CRYPTO_PROVIDER;
    keyProvInfo.dwProvType = 0;
    keyProvInfo.dwFlags = 0;
    keyProvInfo.cProvParam = 0;
    keyProvInfo.rgProvParam = NULL;
    keyProvInfo.dwKeySpec = 0;
    if (!CertSetCertificateContextProperty(pCertContext, CERT_KEY_PROV_INFO_PROP_ID, 0,
                                           &keyProvInfo))
        return GetLastError();

    return 0;
}
std::shared_ptr<zzj::TPMKey> zzj::TPMKey::OpenTpmKeyFromCertificate(PCCERT_CONTEXT pCertContext)
{
    // 1) Retrieve the size needed for CERT_KEY_PROV_INFO_PROP_ID
    DWORD cbData = 0;
    if (!CertGetCertificateContextProperty(pCertContext, CERT_KEY_PROV_INFO_PROP_ID, nullptr,
                                           &cbData))
    {
        return nullptr;
    }

    // 2) Allocate buffer and get the property
    std::unique_ptr<BYTE[]> buffer(new BYTE[cbData]);
    CRYPT_KEY_PROV_INFO *pKeyProvInfo = reinterpret_cast<CRYPT_KEY_PROV_INFO *>(buffer.get());

    if (!CertGetCertificateContextProperty(pCertContext, CERT_KEY_PROV_INFO_PROP_ID, pKeyProvInfo,
                                           &cbData))
    {
        return nullptr;
    }

    // Extract the container name and provider name
    std::wstring containerName =
        pKeyProvInfo->pwszContainerName ? pKeyProvInfo->pwszContainerName : L"";
    std::wstring providerName = pKeyProvInfo->pwszProvName ? pKeyProvInfo->pwszProvName : L"";

    std::shared_ptr<TPMKey> tpmKey(new TPMKey());
    int keyOpenRes = tpmKey->Open(zzj::str::w2utf8(containerName));
    if (keyOpenRes != 0)
    {
        return nullptr;
    }

    return tpmKey;
}

std::tuple<int, bool> zzj::TPMKey::VerifyDataSha256(const std::vector<char> &data,
                                                    const std::vector<char> &signature)
{
    // 1) Query which algorithm group the key belongs to (RSA or ECC).
    WCHAR algoGroup[32] = {0};
    DWORD cbProp = 0;
    NTSTATUS nts =
        NCryptGetProperty(_hKey, NCRYPT_ALGORITHM_GROUP_PROPERTY,
                          reinterpret_cast<PBYTE>(algoGroup), sizeof(algoGroup), &cbProp, 0);
    if (nts != ERROR_SUCCESS) return {nts, false};

    // 2) Decide whether we need padding info (for RSA) or not (ECC).
    PVOID pPaddingInfo = nullptr;
    DWORD dwFlags = 0;

    BCRYPT_PKCS1_PADDING_INFO pi;
    ZeroMemory(&pi, sizeof(pi));
    // If the key is RSA, use PKCS#1 padding (as an example).
    // If the key is ECC, use no padding, etc.
    if (!_wcsicmp(algoGroup, NCRYPT_RSA_ALGORITHM_GROUP))
    {
        // Typically should match the hash algorithm actually used
        pi.pszAlgId = BCRYPT_SHA256_ALGORITHM;
        pPaddingInfo = &pi;
        dwFlags = BCRYPT_PAD_PKCS1;
    }
    else if (!_wcsicmp(algoGroup, NCRYPT_ECDSA_ALGORITHM_GROUP))
    {
        // ECC => no padding needed
        pPaddingInfo = nullptr;
        dwFlags = 0;
    }
    else
    {
        // Unsupported algorithm group
        return {-1, false};
    }

    // 3) Call CryptHashCertificate2 to hash the data
    std::vector<BYTE> hashValue(32);
    ULONG cb = hashValue.size();
    if (!CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM, 0, 0, (const BYTE*)data.data(), data.size(),
                               hashValue.data(), &cb))
    {
        return {GetLastError(), false};
    }

    // 4) Export the public key
    DWORD cbPublicKey = 0;
    nts = NCryptExportKey(_hKey, NULL, BCRYPT_RSAPUBLIC_BLOB, NULL, NULL, 0, &cbPublicKey, 0);
    if (!BCRYPT_SUCCESS(nts))
    {
        return {nts, false};
    }

    // Allocate memory to hold the public key
    BYTE *pbPublicKey = new BYTE[cbPublicKey];
    if (pbPublicKey == NULL)
    {
        return {ERROR_OUTOFMEMORY, false};
    }
    DEFER { 
        if (pbPublicKey != NULL)
            delete[] pbPublicKey; 
    };
    // Now export the public key into the allocated buffer
    nts = NCryptExportKey(_hKey, NULL, BCRYPT_RSAPUBLIC_BLOB, NULL, pbPublicKey, cbPublicKey,
                          &cbPublicKey, 0);
    if (!BCRYPT_SUCCESS(nts))
    {
        return {nts, false};
    }

    // You now have the public key in pbPublicKey. You can use this to verify the signature.

    NCRYPT_KEY_HANDLE hPublicKey = NULL;
    // Use pbPublicKey to create a new NCRYPT_KEY_HANDLE for verification if needed
    nts = NCryptImportKey(_hProv, NULL, BCRYPT_RSAPUBLIC_BLOB, NULL, &hPublicKey, pbPublicKey,
                          cbPublicKey, 0);
    if (!BCRYPT_SUCCESS(nts))
    {
        return {nts, false};
    }
    DEFER
    {
        if (hPublicKey != NULL) NCryptFreeObject(hPublicKey);
    };
    // 5) Call NCryptVerifySignature to verify the signature
    nts = NCryptVerifySignature(hPublicKey, pPaddingInfo, hashValue.data(), hashValue.size(),
                                (unsigned char*)signature.data(), signature.size(), dwFlags);
    if (BCRYPT_SUCCESS(nts))
    {
        return {0, true};
    }
    else if (nts == NTE_BAD_SIGNATURE)
    {
        return {0, false};
    }
    else
    {
        return {nts, false};
    }
}
