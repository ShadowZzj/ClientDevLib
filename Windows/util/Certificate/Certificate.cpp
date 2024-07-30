#include <General/util/BaseUtil.hpp>
#include <General/util/Certificate/Certificate.h>
#include <General/util/StrUtil.h>
#include <Windows.h>
#include <Windows/util/Process/ProcessHelper.h>
#include <Windows/util/Process/ThreadHelper.h>
#include <boost/filesystem.hpp>

using namespace zzj;

std::tuple<int, Certificate> Certificate::GetCertificate(PCCERT_CONTEXT pCertContext)
{
    Certificate cert;
    int result;
    char szCertName[256]{0};
    result = CertGetNameStringA(pCertContext, CERT_NAME_SIMPLE_DISPLAY_TYPE, CERT_NAME_ISSUER_FLAG,
                                NULL, szCertName, 256);
    if (0 == result)
    {
        return {-4, {}};
    }
    cert._issuer = szCertName;

    result =
        CertGetNameStringA(pCertContext, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, szCertName, 256);
    if (0 == result)
    {
        return {-5, {}};
    }
    cert._name = szCertName;

    std::string serialNumber;
    std::string tmpStr;
    PCERT_INFO pCertInfo = pCertContext->pCertInfo;
    BYTE *pbData = pCertInfo->SerialNumber.pbData;
    DWORD cbData = pCertInfo->SerialNumber.cbData;
    char buf[3]{0};
    if (cbData <= 0)
    {
        return {-6, {}};
    }
    for (int i = 0; i < cbData; i++)
    {
        BYTE bb = (BYTE)pbData[i];
        sprintf(buf, "%02X", bb);
        tmpStr += buf;
    }
    std::reverse(tmpStr.begin(), tmpStr.end());
    for (int i = 0; i < tmpStr.length(); i = i + 2)
    {
        char a = tmpStr[i + 1];
        char b = tmpStr[i];
        serialNumber += a;
        serialNumber += b;
    }
    result = str::HexStrToDecStr(serialNumber, serialNumber);
    if (result != 0)
    {
        return {-7, {}};
    }
    cert._sequence = serialNumber;
    return {0, cert};
}

std::tuple<int, std::vector<Certificate>> Certificate::GetCerticifateTemplate(
    const Certificate::StoreType &storeType, CertLocation certLocation,
    std::function<bool(Certificate)> predicate, OperateType operateType)
{
    bool result = false;
    HCERTSTORE myCertStore = NULL;
    PCCERT_CONTEXT pCertContext = NULL;
    HANDLE hToken = NULL;
    DEFER
    {
        if (myCertStore) CertCloseStore(myCertStore, NULL);
        if (pCertContext) CertFreeCertificateContext(pCertContext);
        if (hToken) Thread::RevertToCurrentUser(hToken);
    };
    char szCertName[256] = {0};
    std::wstring certLocationString;
    switch (certLocation)
    {
        case CertLocation::User:
            certLocationString = L"My";
            break;
        case CertLocation::Root:
            certLocationString = L"Root";
            break;
        default:
            break;
    }

    Process currentProcess;
    switch (storeType)
    {
        case StoreType::LocalMachine:
            myCertStore =
                CertOpenStore(CERT_STORE_PROV_SYSTEM,  // the store provider type
                              0,                       // the encoding type is not needed
                              NULL,                    // use the default HCRYPTPROV
                              CERT_SYSTEM_STORE_LOCAL_MACHINE,
                              // set the store location in a
                              // registry location
                              certLocationString.c_str()  // the store name as a Unicode string
                );
            break;
        case StoreType::CurrentUser: {
            auto [result, isService] = currentProcess.IsServiceProcess();
            if (result != 0)
            {
                return {-1, {}};
            }
            if (isService)
            {
                hToken = zzj::Thread::ImpersonateCurrentUser();
                if (NULL == hToken)
                {
                    return {-2, {}};
                }
            }
            myCertStore =
                CertOpenStore(CERT_STORE_PROV_SYSTEM,  // the store provider type
                              0,                       // the encoding type is not needed
                              NULL,                    // use the default HCRYPTPROV
                              CERT_SYSTEM_STORE_CURRENT_USER,
                              // set the store location in a
                              // registry location
                              certLocationString.c_str()  // the store name as a Unicode string
                );
            break;
        }
        default:
            break;
    }

    if (NULL == myCertStore)
    {
        return {-3, {}};
    }

    std::vector<Certificate> certs;
    while (pCertContext = CertEnumCertificatesInStore(myCertStore, pCertContext))
    {
        auto [result, cert] = GetCertificate(pCertContext);
        if (result != 0)
        {
            return {-4, {}};
        }
        if (predicate(cert))
        {
            certs.push_back(cert);
        }

        switch (operateType)
        {
            case OperateType::Delete:
                result = CertDeleteCertificateFromStore(pCertContext);
                if (!result)
                {
                    return {-5, {}};
                }
                break;
            case OperateType::Query:
                break;
            default:
                break;
        }
    }
    return {0, certs};
}
std::tuple<int, std::vector<Certificate>> Certificate::ImportCertificateTemplate(
    const std::string &content, const std::string &passwd, const Certificate::StoreType &storeType,
    CertLocation certLocation, std::function<bool(Certificate)> predicate, OperateType operateType)
{
    bool result = false;
    int ret = 0;
    CRYPT_DATA_BLOB blob = {0};
    HCERTSTORE myCertStore = NULL;
    HCERTSTORE memCertStore = NULL;
    HCERTSTORE caCertStore = NULL;
    PCCERT_CONTEXT pCertContext = NULL;
    HANDLE hToken = NULL;
    std::wstring password;
    DEFER
    {
        if (myCertStore) CertCloseStore(myCertStore, NULL);
        if (pCertContext) CertFreeCertificateContext(pCertContext);
        if (hToken) Thread::RevertToCurrentUser(hToken);
        if (memCertStore) CertCloseStore(memCertStore, NULL);
        if (caCertStore) CertCloseStore(caCertStore, NULL);
    };
    char nameBuf[256]{0};

    std::wstring certLocationString;
    switch (certLocation)
    {
        case CertLocation::User:
            certLocationString = L"My";
            break;
        case CertLocation::Root:
            certLocationString = L"Root";
            break;
        default:
            break;
    }

    Process currentProcess;
    switch (storeType)
    {
        case StoreType::LocalMachine:
            myCertStore =
                CertOpenStore(CERT_STORE_PROV_SYSTEM,  // the store provider type
                              0,                       // the encoding type is not needed
                              NULL,                    // use the default HCRYPTPROV
                              CERT_SYSTEM_STORE_LOCAL_MACHINE,
                              // set the store location in a
                              // registry location
                              certLocationString.c_str()  // the store name as a Unicode string
                );
            caCertStore = CertOpenStore(CERT_STORE_PROV_SYSTEM,  // the store provider type
                                        0,                       // the encoding type is not needed
                                        NULL,                    // use the default HCRYPTPROV
                                        CERT_SYSTEM_STORE_LOCAL_MACHINE,
                                        // set the store location in a
                                        // registry location
                                        L"Root"  // the store name as a Unicode string
            );
            break;
        case StoreType::CurrentUser: {
            auto [result, isService] = currentProcess.IsServiceProcess();
            if (result != 0)
            {
                return {-1, {}};
            }
            if (isService)
            {
                hToken = zzj::Thread::ImpersonateCurrentUser();
                if (NULL == hToken)
                {
                    return {-2, {}};
                }
            }
            myCertStore =
                CertOpenStore(CERT_STORE_PROV_SYSTEM,  // the store provider type
                              0,                       // the encoding type is not needed
                              NULL,                    // use the default HCRYPTPROV
                              CERT_SYSTEM_STORE_CURRENT_USER,
                              // set the store location in a
                              // registry location
                              certLocationString.c_str()  // the store name as a Unicode string
                );
            caCertStore = CertOpenStore(CERT_STORE_PROV_SYSTEM,  // the store provider type
                                        0,                       // the encoding type is not needed
                                        NULL,                    // use the default HCRYPTPROV
                                        CERT_SYSTEM_STORE_CURRENT_USER,
                                        // set the store location in a
                                        // registry location
                                        L"Root"  // the store name as a Unicode string
            );
            break;
        }
        default:
            break;
    }

    if (NULL == myCertStore || NULL == caCertStore)
    {
        return {-3, {}};
    }

    blob.cbData = content.size();
    blob.pbData = (BYTE *)content.data();
    password = str::UTF8Str2Wstr(passwd.c_str());

    // import certificate to mem store.
    memCertStore = PFXImportCertStore(&blob, password.c_str(), PKCS12_INCLUDE_EXTENDED_PROPERTIES);
    if (NULL == memCertStore)
    {
        return {-4, {}};
    }

    std::vector<Certificate> certs;
    // get certificate from mem store,  if signer name equals to subject name,
    // store the cert to Trusted.
    while (pCertContext =
               CertFindCertificateInStore(memCertStore, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0,
                                          CERT_FIND_ANY, NULL, pCertContext))
    {
        std::string signerName;
        std::string subjectName;
        HCERTSTORE saveStore;

        auto [result, cert] = GetCertificate(pCertContext);
        if (result != 0)
        {
            return {-5, {}};
        }

        if (!predicate(cert))
        {
            continue;
        }

        saveStore = cert._issuer == cert._name ? caCertStore : myCertStore;
        switch (operateType)
        {
            case OperateType::Query:
                break;
            case OperateType::Add:
                result = CertAddCertificateContextToStore(saveStore, pCertContext,
                                                          CERT_STORE_ADD_REPLACE_EXISTING, NULL);
                if (!result)
                {
                    return {-6, {}};  // add cert failed.
                }
                break;
            default:
                break;
        }
        certs.push_back(cert);
    }
    return {0, certs};
}
std::tuple<int, std::vector<Certificate>> Certificate::GetCerticifateByIssuer(
    const std::string &issuer, const StoreType &storeType, CertLocation certLocation)
{
    return GetCerticifateTemplate(
        storeType, certLocation, [issuer](Certificate cert) { return cert._issuer == issuer; },
        OperateType::Query);
}
std::tuple<int, std::vector<Certificate>> zzj::Certificate::GetCerticifateByName(
    const std::string &name, const StoreType &storeType, CertLocation certLocation)
{
    return GetCerticifateTemplate(storeType, certLocation,
                                  [name](Certificate cert)
                                  { return cert._name.find(name) != std::string::npos; });
}
std::tuple<int, std::vector<Certificate>> zzj::Certificate::GetCerticifateBySequence(
    const std::string &sequence, const StoreType &storeType, CertLocation certLocation)
{
    return GetCerticifateTemplate(storeType, certLocation,
                                  [sequence](Certificate cert)
                                  { return cert._sequence == sequence; });
}
std::tuple<int, std::vector<Certificate>> zzj::Certificate::AddFromFile(const std::string &filePath,
                                                                        const std::string &passwd,
                                                                        StoreType storeType,
                                                                        CertLocation certLocation)
{
    boost::filesystem::path path(filePath);
    if (!boost::filesystem::exists(path))
    {
        return {-1, {}};
    }
    std::ifstream ifs(filePath, std::ios::binary);
    if (!ifs.is_open())
    {
        return {-2, {}};
    }
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    return AddFromContent(content, passwd, storeType, certLocation);
}
std::tuple<int, std::vector<Certificate>> zzj::Certificate::AddFromContent(
    const std::string &content, const std::string &passwd, StoreType storeType,
    CertLocation certLocation)
{
    return ImportCertificateTemplate(
        content, passwd, storeType, certLocation, [](Certificate cert) { return true; },
        OperateType::Add);
}
std::tuple<int, std::vector<Certificate>> zzj::Certificate::ReadFromFile(
    const std::string &filePath, const std::string &passwd, StoreType storeType,
    CertLocation certLocation)
{
    boost::filesystem::path path(filePath);
    if (!boost::filesystem::exists(path))
    {
        return {-1, {}};
    }
    std::ifstream ifs(filePath, std::ios::binary);
    if (!ifs.is_open())
    {
        return {-2, {}};
    }
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    return ReadFromContent(content, passwd, storeType, certLocation);
}
std::tuple<int, std::vector<Certificate>> zzj::Certificate::ReadFromContent(
    const std::string &content, const std::string &passwd, StoreType storeType,
    CertLocation certLocation)
{
    return ImportCertificateTemplate(
        content, passwd, storeType, certLocation, [](Certificate cert) { return true; },
        OperateType::Query);
}

int zzj::Certificate::Delete(const StoreType &storeType, CertLocation certLocation)
{
    auto [result, certs] = GetCerticifateTemplate(
        storeType, certLocation,
        [this](Certificate cert)
        { return cert._sequence == _sequence && cert._name == _name && cert._issuer == _issuer; },
        OperateType::Query);
    return result;
}