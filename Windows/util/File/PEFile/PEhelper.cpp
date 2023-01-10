#include "PEhelper.h"
#include <Windows/util/File/FileHelper.h>
#include <Windows/util/HandleHelper.h>
#include <General/util/StrUtil.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <General/util/Memory/Allocator/Allocator.hpp>
#pragma comment(lib, "crypt32.lib")
using namespace zzj;

bool zzj::PEFile::FileProperty::_queryValue(const std::string &valueName, const std::string &moduleName,
                                            std::string &RetStr)
{
    bool bSuccess         = false;
    BYTE *m_lpVersionData = nullptr;
    DWORD m_dwLangCharset = 0;
    CHAR *tmpstr          = nullptr;
    do
    {
        if (valueName.empty() || moduleName.empty())
        {
            break;
        }

        DWORD dwHandle;
        DWORD dwDataSize = ::GetFileVersionInfoSizeA((LPCSTR)moduleName.c_str(), &dwHandle);
        if (dwDataSize == 0)
        {
            break;
        }

        m_lpVersionData = new (std::nothrow) BYTE[dwDataSize];
        if (nullptr == m_lpVersionData)
        {
            break;
        }

        if (!::GetFileVersionInfoA((LPCSTR)moduleName.c_str(), dwHandle, dwDataSize, (void *)m_lpVersionData))
        {
            break;
        }

        UINT nQuerySize;
        DWORD *pTransTable;
        if (!::VerQueryValueA(m_lpVersionData, "\\VarFileInfo\\Translation", (void **)&pTransTable, &nQuerySize))
        {
            break;
        }

        m_dwLangCharset = MAKELONG(HIWORD(pTransTable[0]), LOWORD(pTransTable[0]));
        if (nullptr == m_lpVersionData)
        {
            break;
        }

        tmpstr = new (std::nothrow) CHAR[128];
        if (nullptr == tmpstr)
        {
            break;
        }

        sprintf_s(tmpstr, 128, "\\StringFileInfo\\%08lx\\%s", m_dwLangCharset, valueName.c_str());
        LPVOID lpData;
        if (!::VerQueryValueA((void *)m_lpVersionData, tmpstr, &lpData, &nQuerySize))
        {
            break;
        }
        RetStr   = (char *)lpData;
        bSuccess = true;
    } while (false);

    if (m_lpVersionData)
    {
        delete[] m_lpVersionData;
        m_lpVersionData = nullptr;
    }
    if (tmpstr)
    {
        delete[] tmpstr;
        tmpstr = nullptr;
    }
    return bSuccess;
}

zzj::PEFile::FileProperty::FileProperty(const std::string &imageName) : m_imageName(imageName)
{
}

zzj::PEFile::FileProperty::~FileProperty()
{
}

// ��ȡ�ļ�����
std::string zzj::PEFile::FileProperty::GetFileDescription()
{
    std::string tempStr;
    if (_queryValue("FileDescription", m_imageName, tempStr))
        return tempStr;
    else
        return "";
}

// ��ȡ�ļ��汾
std::string zzj::PEFile::FileProperty::GetFileVersion()
{
    std::string tempStr;
    if (_queryValue("FileVersion", m_imageName, tempStr))
        return tempStr;
    else
        return "";
}

// ��ȡ�ڲ�����
std::string zzj::PEFile::FileProperty::GetInternalName()
{
    std::string tempStr;
    if (_queryValue("InternalName", m_imageName, tempStr))
        return tempStr;
    else
        return "";
}

// ��ȡ��˾����
std::string zzj::PEFile::FileProperty::GetCompanyName()
{
    std::string tempStr;
    if (_queryValue("CompanyName", m_imageName, tempStr))
        return tempStr;
    else
        return "";
}

// ��ȡ��Ȩ
std::string zzj::PEFile::FileProperty::GetLegalCopyright()
{
    std::string tempStr;
    if (_queryValue("LegalCopyright", m_imageName, tempStr))
        return tempStr;
    else
        return "";
}

// ��ȡԭʼ�ļ���
std::string zzj::PEFile::FileProperty::GetOriginalFilename()
{
    std::string tempStr;
    if (_queryValue("OriginalFilename", m_imageName, tempStr))
        return tempStr;
    else
        return "";
}

// ��ȡ��Ʒ����
std::string zzj::PEFile::FileProperty::GetProductName()
{
    std::string tempStr;
    if (_queryValue("ProductName", m_imageName, tempStr))
        return tempStr;
    else
        return "";
}

// ��ȡ��Ʒ�汾
std::string zzj::PEFile::FileProperty::GetProductVersion()
{
    std::string tempStr;
    if (_queryValue("ProductVersion", m_imageName, tempStr))
        return tempStr;
    else
        return "";
}

zzj::PEFile::FileProperty PEFile::GetFileProperty()
{
    return zzj::PEFile::FileProperty::FileProperty(fileName);
}
bool PEInfo::LoadModule(HMODULE Module)
{
    return false;
}

HMODULE PEInfo::GetModuleHandle(const char *name)
{
    return ::GetModuleHandleA(name);
}

HMODULE PEInfo::GetModuleHandle(const wchar_t *name)
{
    return ::GetModuleHandleW(name);
}

HMODULE PEInfo::GetEXEModuleHandle()
{
    return ::GetModuleHandle(NULL);
}

HMODULE PEInfo::GetCurrentModuleHandle()
{
    HMODULE ret;
    GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (PCTSTR)GetCurrentModuleHandle, &ret);

    return ret;
}

PEFile::PEFile(std::string _fileName) : File(fileName)
{
    fileName = _fileName;
    ErrorCode error;
    error = LoadPE();
    ThrowExceptionIfFail(error);
}

zzj::PEFile::~PEFile()
{
    UnloadPE();
}

PEFile::ErrorCode PEFile::LoadPE()
{
    ErrorCode error;
    if (fileName.empty())
        return ErrorCode::INVALID_FILE_NAME;

    UnloadPE();
    // dosHeader points to temp buffer.
    dosHeader = zzj::Allocator::Allocate<IMAGE_DOS_HEADER>(1);
    if (!dosHeader)
        return ErrorCode::ZZJ_LIB_ERROR;

    error = ReadDosHeader(fileName, dosHeader);
    ReturnIfFail(error);

    ntHeader = Allocator::Allocate<IMAGE_NT_HEADERS>(1);
    if (!ntHeader)
        return ErrorCode::ZZJ_LIB_ERROR;

    error = ReadNTHeader(fileName, *dosHeader, ntHeader);
    ReturnIfFail(error);

    if (peImage)
    {
        Allocator::Deallocate(peImage);
        peImage = nullptr;
    }

    peImage = (char *)Allocator::AllocMemory(
        AlignData(ntHeader->OptionalHeader.SizeOfImage, ntHeader->OptionalHeader.SectionAlignment));
    if (!peImage)
        return ErrorCode::ZZJ_LIB_ERROR;

    bool ret = FileHelper::ReadFileAtOffset(fileName, peImage, ntHeader->OptionalHeader.SizeOfHeaders, FILE_BEGIN);
    if (!ret)
        return ErrorCode::ZZJ_LIB_ERROR;

    Allocator::Deallocate(dosHeader);
    Allocator::Deallocate(ntHeader);

    // dosHeader points in pe image.

    dosHeader     = (IMAGE_DOS_HEADER *)peImage;
    ntHeader      = (IMAGE_NT_HEADERS *)(peImage + dosHeader->e_lfanew);
    sectionHeader = IMAGE_FIRST_SECTION(ntHeader);

    // loop read section.
    for (int index = 0; index < ntHeader->FileHeader.NumberOfSections; index++)
    {
        ret = FileHelper::ReadFileAtOffset(fileName, &peImage[sectionHeader[index].VirtualAddress],
                                           sectionHeader[index].SizeOfRawData, sectionHeader[index].PointerToRawData);
        if (!ret)
            return ErrorCode::ZZJ_LIB_ERROR;
    }

    error = ReadDataDirectoryTable(peImage, ntHeader, dataDirectoryTable);
    ReturnIfFail(error);

    error = ReadImportLibrary(peImage, dataDirectoryTable, importLibrary, numOfImportLibrary);
    ReturnIfFail(error);

    fileSize = FileHelper::GetFileInstance(fileName).GetFileInfo().GetFileSize();
    if (fileSize == -1)
        return ErrorCode::ZZJ_LIB_ERROR;

    IMAGE_SECTION_HEADER *lastSection = &sectionHeader[ntHeader->FileHeader.NumberOfSections - 1];
    extraDataSize                     = fileSize - (lastSection->PointerToRawData + lastSection->SizeOfRawData);

    if (extraDataSize == 0)
        return ErrorCode::SUCCESS;

    if (extraData)
    {
        Allocator::Deallocate(extraData);
        extraData = nullptr;
    }

    extraData = (char *)Allocator::AllocMemory(extraDataSize);
    ret       = FileHelper::ReadFileAtOffset(fileName, extraData, extraDataSize,
                                             lastSection->PointerToRawData + lastSection->SizeOfRawData);
    if (!ret)
    {
        Allocator::Deallocate(extraData);
        extraData = nullptr;
        return ErrorCode::ZZJ_LIB_ERROR;
    }

    return ErrorCode::SUCCESS;
}

PEFile::ErrorCode PEFile::UnloadPE()
{
    if (extraData)
    {
        Allocator::Deallocate(extraData);
        extraData == nullptr;
    }
    if (peImage)
    {
        Allocator::Deallocate(peImage);
        peImage = nullptr;
    }

    return ErrorCode::SUCCESS;
}

DWORD zzj::PEFile::RVAToVA(char *imageBase, DWORD rva)
{
    if (imageBase)
        return (DWORD)imageBase + rva;
    return -1;
}

PEFile::ErrorCode PEFile::IsValidPE(std::string fileName)
{
    return SUCCESS;
}

bool PEFile::IsOperationSucceed(ErrorCode error)
{
    if (error == SUCCESS)
        return true;
    return false;
}

PEFile::ErrorCode PEFile::ReadDosHeader(std::string fileName, IMAGE_DOS_HEADER *dosHeader)
{

    if (!dosHeader)
        return ErrorCode::INVALID_PARAMETER;

    bool ret = FileHelper::ReadFileAtOffset(fileName, dosHeader, sizeof(*dosHeader), FILE_BEGIN);
    if (!ret)
        return ZZJ_LIB_ERROR;

    return ErrorCode::SUCCESS;
}

PEFile::ErrorCode zzj::PEFile::ReadNTHeader(std::string fileName, const IMAGE_DOS_HEADER &dosHeader,
                                            IMAGE_NT_HEADERS *ntHeader)
{
    if (!ntHeader)
        return ErrorCode::INVALID_PARAMETER;
    bool ret = FileHelper::ReadFileAtOffset(fileName, ntHeader, sizeof(*ntHeader), dosHeader.e_lfanew);
    if (!ret)
        return ZZJ_LIB_ERROR;

    return ErrorCode::SUCCESS;
}

PEFile::ErrorCode zzj::PEFile::ReadDataDirectoryTable(char *imageBase, IMAGE_NT_HEADERS *ntheader,
                                                      IMAGE_DATA_DIRECTORY *&dataDirectoryTable)
{
    dataDirectoryTable = &ntheader->OptionalHeader.DataDirectory[0];
    return ErrorCode::SUCCESS;
}

PEFile::ErrorCode zzj::PEFile::ReadImportLibrary(char *imageBase, IMAGE_DATA_DIRECTORY *dataDirectoryTable,
                                                 IMAGE_IMPORT_DESCRIPTOR *&importLibrary, DWORD &numOfImportLibrary)
{

    importLibrary =
        (IMAGE_IMPORT_DESCRIPTOR *)RVAToVA(imageBase, dataDirectoryTable[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

    numOfImportLibrary = 0;
    while ((&importLibrary[numOfImportLibrary])->Name != 0)
        numOfImportLibrary++;

    return ErrorCode::SUCCESS;
}

DWORD zzj::PEFile::AlignData(DWORD data, DWORD alignment)
{
    DWORD remainder = data % alignment;
    DWORD quotient  = data / alignment;
    if (remainder > 0)
        return alignment * (quotient + 1);
    return data;
}

LPWSTR AllocateAndCopyWideString(LPCWSTR inputString)
{
    LPWSTR outputString = NULL;
    outputString        = (LPWSTR)LocalAlloc(LPTR, (wcslen(inputString) + 1) * sizeof(WCHAR));
    if (outputString != NULL)
    {
        lstrcpyW(outputString, inputString);
    }
    return outputString;
}

zzj::PEFile::FileSign::FileSign(const std::string &fileName)
{
    m_imageName = zzj::str::ansi2w(fileName);
}
zzj::PEFile::FileSign::~FileSign()
{
}

#define ENCODING (X509_ASN_ENCODING | PKCS_7_ASN_ENCODING)
bool zzj::PEFile::FileSign::_getProgAndPublisherInfo(PCMSG_SIGNER_INFO pSignerInfo, PSPROG_PUBLISHERINFO Info)
{
    BOOL fReturn               = FALSE;
    PSPC_SP_OPUS_INFO OpusInfo = NULL;
    DWORD dwData;
    BOOL fResult;
    __try
    {
        // Loop through authenticated attributes and find
        // SPC_SP_OPUS_INFO_OBJID OID.
        for (DWORD n = 0; n < pSignerInfo->AuthAttrs.cAttr; n++)
        {
            if (lstrcmpA(SPC_SP_OPUS_INFO_OBJID, pSignerInfo->AuthAttrs.rgAttr[n].pszObjId) == 0)
            {
                // Get Size of SPC_SP_OPUS_INFO structure.
                fResult = CryptDecodeObject(ENCODING, SPC_SP_OPUS_INFO_OBJID,
                                            pSignerInfo->AuthAttrs.rgAttr[n].rgValue[0].pbData,
                                            pSignerInfo->AuthAttrs.rgAttr[n].rgValue[0].cbData, 0, NULL, &dwData);
                if (!fResult)
                {
                    __leave;
                }

                // Allocate memory for SPC_SP_OPUS_INFO structure.
                OpusInfo = (PSPC_SP_OPUS_INFO)LocalAlloc(LPTR, dwData);
                if (!OpusInfo)
                {
                    __leave;
                }

                // Decode and get SPC_SP_OPUS_INFO structure.
                fResult = CryptDecodeObject(ENCODING, SPC_SP_OPUS_INFO_OBJID,
                                            pSignerInfo->AuthAttrs.rgAttr[n].rgValue[0].pbData,
                                            pSignerInfo->AuthAttrs.rgAttr[n].rgValue[0].cbData, 0, OpusInfo, &dwData);
                if (!fResult)
                {
                    __leave;
                }

                // Fill in Program Name if present.
                if (OpusInfo->pwszProgramName)
                {
                    Info->lpszProgramName = AllocateAndCopyWideString(OpusInfo->pwszProgramName);
                }
                else
                    Info->lpszProgramName = NULL;

                // Fill in Publisher Information if present.
                if (OpusInfo->pPublisherInfo)
                {
                    switch (OpusInfo->pPublisherInfo->dwLinkChoice)
                    {
                    case SPC_URL_LINK_CHOICE:
                        Info->lpszPublisherLink = AllocateAndCopyWideString(OpusInfo->pPublisherInfo->pwszUrl);
                        break;
                    case SPC_FILE_LINK_CHOICE:
                        Info->lpszPublisherLink = AllocateAndCopyWideString(OpusInfo->pPublisherInfo->pwszFile);
                        break;
                    default:
                        Info->lpszPublisherLink = NULL;
                        break;
                    }
                }
                else
                {
                    Info->lpszPublisherLink = NULL;
                }

                // Fill in More Info if present.
                if (OpusInfo->pMoreInfo)
                {
                    switch (OpusInfo->pMoreInfo->dwLinkChoice)
                    {
                    case SPC_URL_LINK_CHOICE:
                        Info->lpszMoreInfoLink = AllocateAndCopyWideString(OpusInfo->pMoreInfo->pwszUrl);
                        break;
                    case SPC_FILE_LINK_CHOICE:
                        Info->lpszMoreInfoLink = AllocateAndCopyWideString(OpusInfo->pMoreInfo->pwszFile);
                        break;
                    default:
                        Info->lpszMoreInfoLink = NULL;
                        break;
                    }
                }
                else
                {
                    Info->lpszMoreInfoLink = NULL;
                }

                fReturn = TRUE;
                break; // Break from for loop.
            }          // lstrcmp SPC_SP_OPUS_INFO_OBJID
        }              // for
    }
    __finally
    {
        if (OpusInfo != NULL)
            LocalFree(OpusInfo);
    }
    return fReturn;
}

bool zzj::PEFile::FileSign::GetProgAndPublisherInfo(PSPROG_PUBLISHERINFO pProgPubInfo)
{
    HCERTSTORE hStore = NULL;
    HCRYPTMSG hMsg    = NULL;
    BOOL fResult;
    DWORD dwEncoding, dwContentType, dwFormatType;
    PCMSG_SIGNER_INFO pSignerInfo = NULL;
    DWORD dwSignerInfo;
    CERT_INFO CertInfo;

    __try
    {
        // Get message handle and store handle from the signed file.
        fResult = CryptQueryObject(CERT_QUERY_OBJECT_FILE, m_imageName.c_str(),
                                   CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED, CERT_QUERY_FORMAT_FLAG_BINARY, 0,
                                   &dwEncoding, &dwContentType, &dwFormatType, &hStore, &hMsg, NULL);
        if (!fResult)
        {
            __leave;
        }

        // Get signer information size.
        fResult = CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, NULL, &dwSignerInfo);
        if (!fResult)
        {
            __leave;
        }

        // Allocate memory for signer information.
        pSignerInfo = (PCMSG_SIGNER_INFO)LocalAlloc(LPTR, dwSignerInfo);
        if (!pSignerInfo)
        {
            __leave;
        }

        // Get Signer Information.
        fResult = CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, (PVOID)pSignerInfo, &dwSignerInfo);
        if (!fResult)
        {
            __leave;
        }

        // Get program name and publisher information from
        // signer info structure.
        if (_getProgAndPublisherInfo(pSignerInfo, pProgPubInfo))
        {
            fResult = TRUE;
        }
        else
        {
            fResult = FALSE;
        }
    }
    __finally
    {
        if (hStore != NULL)
            CertCloseStore(hStore, 0);
        if (hMsg != NULL)
            CryptMsgClose(hMsg);
    }
    return fResult;
}

bool zzj::PEFile::FileSign::_getDateOfTimeStamp(PCMSG_SIGNER_INFO pSignerInfo, SYSTEMTIME *st)
{
    BOOL fResult;
    FILETIME lft, ft;
    DWORD dwData;
    BOOL fReturn = FALSE;
    // Loop through authenticated attributes and find
    // szOID_RSA_signingTime OID.
    for (DWORD n = 0; n < pSignerInfo->AuthAttrs.cAttr; n++)
    {
        if (lstrcmpA(szOID_RSA_signingTime, pSignerInfo->AuthAttrs.rgAttr[n].pszObjId) == 0)
        {
            // Decode and get FILETIME structure.
            dwData = sizeof(ft);
            fResult =
                CryptDecodeObject(ENCODING, szOID_RSA_signingTime, pSignerInfo->AuthAttrs.rgAttr[n].rgValue[0].pbData,
                                  pSignerInfo->AuthAttrs.rgAttr[n].rgValue[0].cbData, 0, (PVOID)&ft, &dwData);
            if (!fResult)
            {
                break;
            }

            // Convert to local time.
            FileTimeToLocalFileTime(&ft, &lft);
            FileTimeToSystemTime(&lft, st);
            fReturn = TRUE;
            break; // Break from for loop.
        }          // lstrcmp szOID_RSA_signingTime
    }              // for
    return fReturn;
}
bool zzj::PEFile::FileSign::_getTimeStampSignerInfo(PCMSG_SIGNER_INFO pSignerInfo,
                                                    PCMSG_SIGNER_INFO *pCounterSignerInfo)
{
    PCCERT_CONTEXT pCertContext = NULL;
    BOOL fReturn                = FALSE;
    BOOL fResult;
    DWORD dwSize;
    __try
    {
        *pCounterSignerInfo = NULL;
        for (DWORD n = 0; n < pSignerInfo->UnauthAttrs.cAttr; n++)
        {
            if (lstrcmpA(pSignerInfo->UnauthAttrs.rgAttr[n].pszObjId, szOID_RSA_counterSign) == 0)
            {
                // Get size of CMSG_SIGNER_INFO structure.
                fResult =
                    CryptDecodeObject(ENCODING, PKCS7_SIGNER_INFO, pSignerInfo->UnauthAttrs.rgAttr[n].rgValue[0].pbData,
                                      pSignerInfo->UnauthAttrs.rgAttr[n].rgValue[0].cbData, 0, NULL, &dwSize);
                if (!fResult)
                {
                    __leave;
                }

                // Allocate memory for CMSG_SIGNER_INFO.
                *pCounterSignerInfo = (PCMSG_SIGNER_INFO)LocalAlloc(LPTR, dwSize);
                if (!*pCounterSignerInfo)
                {
                    __leave;
                }

                // Decode and get CMSG_SIGNER_INFO structure
                // for timestamp certificate.
                fResult = CryptDecodeObject(
                    ENCODING, PKCS7_SIGNER_INFO, pSignerInfo->UnauthAttrs.rgAttr[n].rgValue[0].pbData,
                    pSignerInfo->UnauthAttrs.rgAttr[n].rgValue[0].cbData, 0, (PVOID)*pCounterSignerInfo, &dwSize);
                if (!fResult)
                {
                    __leave;
                }
                fReturn = TRUE;
                break; // Break from for loop.
            }
        }
    }
    __finally
    {
        if (pCertContext != NULL)
            CertFreeCertificateContext(pCertContext);
    }
    return fReturn;
}

bool zzj::PEFile::FileSign::GetTimeStampSignerInfo(SYSTEMTIME *st)
{
    HCERTSTORE hStore = NULL;
    HCRYPTMSG hMsg    = NULL;
    BOOL fResult;
    DWORD dwEncoding, dwContentType, dwFormatType;
    PCMSG_SIGNER_INFO pSignerInfo        = NULL;
    PCMSG_SIGNER_INFO pCounterSignerInfo = NULL;
    DWORD dwSignerInfo;
    CERT_INFO CertInfo;
    SPROG_PUBLISHERINFO ProgPubInfo;
    ZeroMemory(&ProgPubInfo, sizeof(ProgPubInfo));

    __try
    {
        // Get message handle and store handle from the signed file.
        fResult = CryptQueryObject(CERT_QUERY_OBJECT_FILE, m_imageName.c_str(),
                                   CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED, CERT_QUERY_FORMAT_FLAG_BINARY, 0,
                                   &dwEncoding, &dwContentType, &dwFormatType, &hStore, &hMsg, NULL);
        if (!fResult)
        {
            __leave;
        }

        // Get signer information size.
        fResult = CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, NULL, &dwSignerInfo);
        if (!fResult)
        {
            __leave;
        }

        // Allocate memory for signer information.
        pSignerInfo = (PCMSG_SIGNER_INFO)LocalAlloc(LPTR, dwSignerInfo);
        if (!pSignerInfo)
        {
            __leave;
        }

        // Get Signer Information.
        fResult = CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, (PVOID)pSignerInfo, &dwSignerInfo);
        if (!fResult)
        {
            __leave;
        }
        if (_getTimeStampSignerInfo(pSignerInfo, &pCounterSignerInfo))
        {
            // Find Date of timestamp.
            if (_getDateOfTimeStamp(pCounterSignerInfo, st))
            {
                fResult = true;
            }
            else
            {
                fResult = false;
            }
        }
    }
    __finally
    {
        if (pSignerInfo != NULL)
            LocalFree(pSignerInfo);
        if (pCounterSignerInfo != NULL)
            LocalFree(pCounterSignerInfo);
        if (hStore != NULL)
            CertCloseStore(hStore, 0);
        if (hMsg != NULL)
            CryptMsgClose(hMsg);
    }
    return fResult;
}