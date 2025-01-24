#pragma once
#include <windows.h>
#include <ncrypt.h>
#include <string>
#include <tchar.h>
#include <vector>
#include <wincrypt.h>
#include <memory>
#include <tuple>
namespace zzj
{
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
    int ExportPublicKey(PCERT_PUBLIC_KEY_INFO *publicKeyInfo);
    template <typename T>
    int SetProperty(const std::wstring &property, const T &value)
    {
        return NCryptSetProperty(_hKey, (PWSTR)property.c_str(), (PBYTE)&value, sizeof(T), 0);
    }

    int GenerateCsrDerFormat(const std::string &certSubjectNameFullQualified,
                             std::vector<BYTE> &csrDer);
    int GenerateCsrPemFormat(const std::string &certSubjectNameFullQualified, std::string &csrPem);
    int SignDataSha256(const std::vector<BYTE> &data, std::vector<BYTE> &signature);
    std::tuple<int,bool> VerifyDataSha256(const std::vector<BYTE> &data, const std::vector<BYTE> &signature);
    static int AssociateCertificate(PCCERT_CONTEXT pCertContext, const std::string &keycontainer);
    static std::shared_ptr<TPMKey> OpenTpmKeyFromCertificate(PCCERT_CONTEXT pCertContext);
   private:
    NCRYPT_KEY_HANDLE _hKey = NULL;
    NCRYPT_PROV_HANDLE _hProv = NULL;
};

};  // namespace zzj