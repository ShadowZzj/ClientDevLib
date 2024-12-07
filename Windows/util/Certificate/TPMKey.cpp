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

#pragma comment(lib, "ncrypt.lib")
#pragma comment(lib, "crypt32.lib")

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
