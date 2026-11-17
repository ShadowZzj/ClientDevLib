#include "PEhelper.h"
#include <Windows/util/File/FileHelper.h>
#include <Windows/util/HandleHelper.h>
#include <General/util/StrUtil.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <General/util/Memory/Allocator/Allocator.hpp>
#include <stdexcept>
#include <exception>
#include <spdlog/spdlog.h>
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "wintrust.lib")
using namespace zzj;

bool zzj::PEFile::FileProperty::_queryValue(const std::string &valueName,
                                            const std::string &moduleName, std::string &RetStr)
{
    bool bSuccess = false;
    BYTE *m_lpVersionData = nullptr;
    DWORD m_dwLangCharset = 0;
    CHAR *tmpstr = nullptr;
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

        if (!::GetFileVersionInfoA((LPCSTR)moduleName.c_str(), dwHandle, dwDataSize,
                                   (void *)m_lpVersionData))
        {
            break;
        }

        UINT nQuerySize;
        DWORD *pTransTable;
        if (!::VerQueryValueA(m_lpVersionData, "\\VarFileInfo\\Translation", (void **)&pTransTable,
                              &nQuerySize))
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
        RetStr = (char *)lpData;
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

zzj::PEFile::FileProperty::FileProperty(const std::string &imageName) : m_imageName(imageName) {}

zzj::PEFile::FileProperty::~FileProperty() {}

std::string zzj::PEFile::FileProperty::GetFileDescription()
{
    std::string tempStr;
    if (_queryValue("FileDescription", m_imageName, tempStr))
        return tempStr;
    else
        return "";
}

std::string zzj::PEFile::FileProperty::GetFileVersion()
{
    std::string tempStr;
    if (_queryValue("FileVersion", m_imageName, tempStr))
        return tempStr;
    else
        return "";
}

std::string zzj::PEFile::FileProperty::GetInternalName()
{
    std::string tempStr;
    if (_queryValue("InternalName", m_imageName, tempStr))
        return tempStr;
    else
        return "";
}

std::string zzj::PEFile::FileProperty::GetCompanyName()
{
    std::string tempStr;
    if (_queryValue("CompanyName", m_imageName, tempStr))
        return tempStr;
    else
        return "";
}

std::string zzj::PEFile::FileProperty::GetLegalCopyright()
{
    std::string tempStr;
    if (_queryValue("LegalCopyright", m_imageName, tempStr))
        return tempStr;
    else
        return "";
}

std::string zzj::PEFile::FileProperty::GetOriginalFilename()
{
    std::string tempStr;
    if (_queryValue("OriginalFilename", m_imageName, tempStr))
        return tempStr;
    else
        return "";
}

std::string zzj::PEFile::FileProperty::GetProductName()
{
    std::string tempStr;
    if (_queryValue("ProductName", m_imageName, tempStr))
        return tempStr;
    else
        return "";
}

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
bool PEInfo::LoadModule(HMODULE Module) { return false; }

HMODULE PEInfo::GetModuleHandle(const char *name) { return ::GetModuleHandleA(name); }

HMODULE PEInfo::GetModuleHandle(const wchar_t *name) { return ::GetModuleHandleW(name); }

HMODULE PEInfo::GetEXEModuleHandle() { return ::GetModuleHandle(NULL); }

HMODULE PEInfo::GetCurrentModuleHandle()
{
    HMODULE ret;
    GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (PCTSTR)GetCurrentModuleHandle, &ret);

    return ret;
}

PEFile::PEFile(std::string _fileName) : File(_fileName)
{
    fileName = _fileName;
    ErrorCode error;
    error = LoadPE();
    ThrowExceptionIfFail(error);
}

zzj::PEFile::~PEFile() { UnloadPE(); }

PEFile::ErrorCode PEFile::LoadPE()
{
    ErrorCode error;
    if (fileName.empty()) return ErrorCode::INVALID_FILE_NAME;

    UnloadPE();
    // dosHeader points to temp buffer.
    dosHeader = zzj::Allocator::Allocate<IMAGE_DOS_HEADER>(1);
    if (!dosHeader) return ErrorCode::ZZJ_LIB_ERROR;

    error = ReadDosHeader(fileName, dosHeader);
    ReturnIfFail(error);

    ntHeader = Allocator::Allocate<IMAGE_NT_HEADERS>(1);
    if (!ntHeader) return ErrorCode::ZZJ_LIB_ERROR;

    error = ReadNTHeader(fileName, *dosHeader, ntHeader);
    ReturnIfFail(error);

    if (peImage)
    {
        Allocator::Deallocate(peImage);
        peImage = nullptr;
    }

    peImage = (char *)Allocator::AllocMemory(
        AlignData(ntHeader->OptionalHeader.SizeOfImage, ntHeader->OptionalHeader.SectionAlignment));
    if (!peImage) return ErrorCode::ZZJ_LIB_ERROR;

    bool ret = FileHelper::ReadFileAtOffset(fileName, peImage,
                                            ntHeader->OptionalHeader.SizeOfHeaders, FILE_BEGIN);
    if (!ret) return ErrorCode::ZZJ_LIB_ERROR;

    Allocator::Deallocate(dosHeader);
    Allocator::Deallocate(ntHeader);

    // dosHeader points in pe image.

    dosHeader = (IMAGE_DOS_HEADER *)peImage;
    ntHeader = (IMAGE_NT_HEADERS *)(peImage + dosHeader->e_lfanew);
    sectionHeader = IMAGE_FIRST_SECTION(ntHeader);

    // loop read section.
    for (int index = 0; index < ntHeader->FileHeader.NumberOfSections; index++)
    {
        ret = FileHelper::ReadFileAtOffset(fileName, &peImage[sectionHeader[index].VirtualAddress],
                                           sectionHeader[index].SizeOfRawData,
                                           sectionHeader[index].PointerToRawData);
        if (!ret) return ErrorCode::ZZJ_LIB_ERROR;
    }

    error = ReadDataDirectoryTable(peImage, ntHeader, dataDirectoryTable);
    ReturnIfFail(error);

    error = ReadImportLibrary(peImage, dataDirectoryTable, importLibrary, numOfImportLibrary);
    ReturnIfFail(error);

    error = ReadRelocation(peImage, dataDirectoryTable, baseRelocation);
    ReturnIfFail(error);

    fileSize = FileHelper::GetFileInstance(fileName).GetFileInfo().GetFileSize();
    if (fileSize == -1) return ErrorCode::ZZJ_LIB_ERROR;

    IMAGE_SECTION_HEADER *lastSection = &sectionHeader[ntHeader->FileHeader.NumberOfSections - 1];
    extraDataSize = fileSize - (lastSection->PointerToRawData + lastSection->SizeOfRawData);

    if (extraDataSize == 0) return ErrorCode::SUCCESS;

    if (extraData)
    {
        Allocator::Deallocate(extraData);
        extraData = nullptr;
    }

    extraData = (char *)Allocator::AllocMemory(extraDataSize);
    ret = FileHelper::ReadFileAtOffset(fileName, extraData, extraDataSize,
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
        extraData = nullptr;
    }
    if (peImage)
    {
        Allocator::Deallocate(peImage);
        peImage = nullptr;
    }

    return ErrorCode::SUCCESS;
}

std::uintptr_t zzj::PEFile::RVAToVA(char *imageBase, DWORD rva)
{
    if (imageBase) return (std::uintptr_t)imageBase + rva;
    return -1;
}
std::uintptr_t zzj::PEFile::RVAToOffset(char *imageBase, DWORD rva)
{
    IMAGE_DOS_HEADER *dosHeader = (IMAGE_DOS_HEADER *)imageBase;
    IMAGE_NT_HEADERS *ntHeader = (IMAGE_NT_HEADERS *)(imageBase + dosHeader->e_lfanew);
    IMAGE_SECTION_HEADER *sectionHeader = IMAGE_FIRST_SECTION(ntHeader);

    for (int index = 0; index < ntHeader->FileHeader.NumberOfSections; index++)
    {
        if (rva >= sectionHeader[index].VirtualAddress &&
            rva < sectionHeader[index].VirtualAddress + sectionHeader[index].SizeOfRawData)
        {
            return sectionHeader[index].PointerToRawData + rva -
                   sectionHeader[index].VirtualAddress;
        }
    }

    return -1;
}

PEFile::ErrorCode PEFile::IsValidPE(std::string fileName) { return SUCCESS; }

bool PEFile::IsOperationSucceed(ErrorCode error)
{
    if (error == SUCCESS) return true;
    return false;
}

PEFile::ErrorCode PEFile::ReadDosHeader(std::string fileName, IMAGE_DOS_HEADER *dosHeader)
{
    if (!dosHeader) return ErrorCode::INVALID_PARAMETER;

    bool ret = FileHelper::ReadFileAtOffset(fileName, dosHeader, sizeof(*dosHeader), FILE_BEGIN);
    if (!ret) return ZZJ_LIB_ERROR;

    return ErrorCode::SUCCESS;
}

PEFile::ErrorCode zzj::PEFile::ReadNTHeader(std::string fileName, const IMAGE_DOS_HEADER &dosHeader,
                                            IMAGE_NT_HEADERS *ntHeader)
{
    if (!ntHeader) return ErrorCode::INVALID_PARAMETER;
    bool ret =
        FileHelper::ReadFileAtOffset(fileName, ntHeader, sizeof(*ntHeader), dosHeader.e_lfanew);
    if (!ret) return ZZJ_LIB_ERROR;

    return ErrorCode::SUCCESS;
}

PEFile::ErrorCode zzj::PEFile::ReadDataDirectoryTable(char *imageBase, IMAGE_NT_HEADERS *ntheader,
                                                      IMAGE_DATA_DIRECTORY *&dataDirectoryTable)
{
    dataDirectoryTable = &ntheader->OptionalHeader.DataDirectory[0];
    return ErrorCode::SUCCESS;
}

PEFile::ErrorCode zzj::PEFile::ReadImportLibrary(char *imageBase,
                                                 IMAGE_DATA_DIRECTORY *dataDirectoryTable,
                                                 IMAGE_IMPORT_DESCRIPTOR *&importLibrary,
                                                 DWORD &numOfImportLibrary)
{
    importLibrary = (IMAGE_IMPORT_DESCRIPTOR *)RVAToVA(
        imageBase, dataDirectoryTable[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

    numOfImportLibrary = 0;
    while ((&importLibrary[numOfImportLibrary])->Name != 0) numOfImportLibrary++;

    return ErrorCode::SUCCESS;
}
DWORD zzj::PEFile::GetRelocationTableEntryNum()
{
    DWORD ret = 0;
    DWORD maxSize = ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size;
    DWORD maxOffset =
        ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress +
        maxSize;
    IMAGE_BASE_RELOCATION *baseRelocation = (IMAGE_BASE_RELOCATION *)RVAToVA(
        peImage,
        ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);
    DWORD currentOffset =
        ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress;
    while (currentOffset < maxOffset && baseRelocation->SizeOfBlock > 0 &&
           baseRelocation->VirtualAddress > 0)
    {
        ret++;
        currentOffset += baseRelocation->SizeOfBlock;
        baseRelocation =
            (IMAGE_BASE_RELOCATION *)((char *)baseRelocation + baseRelocation->SizeOfBlock);
    }
    return ret;
}
IMAGE_BASE_RELOCATION *zzj::PEFile::GetRelocationTableEntry(int index) { return nullptr; }
PEFile::ErrorCode zzj::PEFile::ReadRelocation(char *imageBase,
                                              IMAGE_DATA_DIRECTORY *dataDirectoryTable,
                                              IMAGE_BASE_RELOCATION *&baseRelocation)
{
    baseRelocation = (IMAGE_BASE_RELOCATION *)RVAToVA(
        imageBase, dataDirectoryTable[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);
    return ErrorCode::SUCCESS;
}

DWORD zzj::PEFile::AlignData(DWORD data, DWORD alignment)
{
    DWORD remainder = data % alignment;
    DWORD quotient = data / alignment;
    if (remainder > 0) return alignment * (quotient + 1);
    return data;
}
DWORD zzj::PEFile::GetSectionNum() { return ntHeader->FileHeader.NumberOfSections; }
IMAGE_SECTION_HEADER *zzj::PEFile::GetSection(int index)
{
    if (index < 0 || index >= ntHeader->FileHeader.NumberOfSections) return nullptr;
    return &sectionHeader[index];
}

LPWSTR AllocateAndCopyWideString(LPCWSTR inputString)
{
    LPWSTR outputString = NULL;
    outputString = (LPWSTR)LocalAlloc(LPTR, (wcslen(inputString) + 1) * sizeof(WCHAR));
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
zzj::PEFile::FileSign::~FileSign() {}

#define ENCODING (X509_ASN_ENCODING | PKCS_7_ASN_ENCODING)
bool zzj::PEFile::FileSign::_getProgAndPublisherInfo(PCMSG_SIGNER_INFO pSignerInfo,
                                                     PSPROG_PUBLISHERINFO Info)
{
    BOOL fReturn = FALSE;
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
                                            pSignerInfo->AuthAttrs.rgAttr[n].rgValue[0].cbData, 0,
                                            NULL, &dwData);
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
                                            pSignerInfo->AuthAttrs.rgAttr[n].rgValue[0].cbData, 0,
                                            OpusInfo, &dwData);
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
                            Info->lpszPublisherLink =
                                AllocateAndCopyWideString(OpusInfo->pPublisherInfo->pwszUrl);
                            break;
                        case SPC_FILE_LINK_CHOICE:
                            Info->lpszPublisherLink =
                                AllocateAndCopyWideString(OpusInfo->pPublisherInfo->pwszFile);
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
                            Info->lpszMoreInfoLink =
                                AllocateAndCopyWideString(OpusInfo->pMoreInfo->pwszUrl);
                            break;
                        case SPC_FILE_LINK_CHOICE:
                            Info->lpszMoreInfoLink =
                                AllocateAndCopyWideString(OpusInfo->pMoreInfo->pwszFile);
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
                break;  // Break from for loop.
            }  // lstrcmp SPC_SP_OPUS_INFO_OBJID
        }  // for
    }
    __finally
    {
        if (OpusInfo != NULL) LocalFree(OpusInfo);
    }
    return fReturn;
}

bool zzj::PEFile::FileSign::GetProgAndPublisherInfo(PSPROG_PUBLISHERINFO pProgPubInfo)
{
    HCERTSTORE hStore = NULL;
    HCRYPTMSG hMsg = NULL;
    BOOL fResult;
    DWORD dwEncoding, dwContentType, dwFormatType;
    PCMSG_SIGNER_INFO pSignerInfo = NULL;
    DWORD dwSignerInfo;
    CERT_INFO CertInfo;

    __try
    {
        // Get message handle and store handle from the signed file.
        fResult = CryptQueryObject(CERT_QUERY_OBJECT_FILE, m_imageName.c_str(),
                                   CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                                   CERT_QUERY_FORMAT_FLAG_BINARY, 0, &dwEncoding, &dwContentType,
                                   &dwFormatType, &hStore, &hMsg, NULL);
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
        fResult =
            CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, (PVOID)pSignerInfo, &dwSignerInfo);
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
        if (hStore != NULL) CertCloseStore(hStore, 0);
        if (hMsg != NULL) CryptMsgClose(hMsg);
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
            fResult = CryptDecodeObject(
                ENCODING, szOID_RSA_signingTime, pSignerInfo->AuthAttrs.rgAttr[n].rgValue[0].pbData,
                pSignerInfo->AuthAttrs.rgAttr[n].rgValue[0].cbData, 0, (PVOID)&ft, &dwData);
            if (!fResult)
            {
                break;
            }

            // Convert to local time.
            FileTimeToLocalFileTime(&ft, &lft);
            FileTimeToSystemTime(&lft, st);
            fReturn = TRUE;
            break;  // Break from for loop.
        }  // lstrcmp szOID_RSA_signingTime
    }  // for
    return fReturn;
}
bool zzj::PEFile::FileSign::_getTimeStampSignerInfo(PCMSG_SIGNER_INFO pSignerInfo,
                                                    PCMSG_SIGNER_INFO *pCounterSignerInfo)
{
    PCCERT_CONTEXT pCertContext = NULL;
    BOOL fReturn = FALSE;
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
                fResult = CryptDecodeObject(ENCODING, PKCS7_SIGNER_INFO,
                                            pSignerInfo->UnauthAttrs.rgAttr[n].rgValue[0].pbData,
                                            pSignerInfo->UnauthAttrs.rgAttr[n].rgValue[0].cbData, 0,
                                            NULL, &dwSize);
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
                fResult = CryptDecodeObject(ENCODING, PKCS7_SIGNER_INFO,
                                            pSignerInfo->UnauthAttrs.rgAttr[n].rgValue[0].pbData,
                                            pSignerInfo->UnauthAttrs.rgAttr[n].rgValue[0].cbData, 0,
                                            (PVOID)*pCounterSignerInfo, &dwSize);
                if (!fResult)
                {
                    __leave;
                }
                fReturn = TRUE;
                break;  // Break from for loop.
            }
        }
    }
    __finally
    {
        if (pCertContext != NULL) CertFreeCertificateContext(pCertContext);
    }
    return fReturn;
}

bool zzj::PEFile::FileSign::GetTimeStampSignerInfo(SYSTEMTIME *st)
{
    HCERTSTORE hStore = NULL;
    HCRYPTMSG hMsg = NULL;
    BOOL fResult;
    DWORD dwEncoding, dwContentType, dwFormatType;
    PCMSG_SIGNER_INFO pSignerInfo = NULL;
    PCMSG_SIGNER_INFO pCounterSignerInfo = NULL;
    DWORD dwSignerInfo;
    CERT_INFO CertInfo;
    SPROG_PUBLISHERINFO ProgPubInfo;
    ZeroMemory(&ProgPubInfo, sizeof(ProgPubInfo));

    __try
    {
        // Get message handle and store handle from the signed file.
        fResult = CryptQueryObject(CERT_QUERY_OBJECT_FILE, m_imageName.c_str(),
                                   CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                                   CERT_QUERY_FORMAT_FLAG_BINARY, 0, &dwEncoding, &dwContentType,
                                   &dwFormatType, &hStore, &hMsg, NULL);
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
        fResult =
            CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, (PVOID)pSignerInfo, &dwSignerInfo);
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
        if (pSignerInfo != NULL) LocalFree(pSignerInfo);
        if (pCounterSignerInfo != NULL) LocalFree(pCounterSignerInfo);
        if (hStore != NULL) CertCloseStore(hStore, 0);
        if (hMsg != NULL) CryptMsgClose(hMsg);
    }
    return fResult;
}

LPWSTR zzj::PEFile::FileSign::_getCertNameString(PCCERT_CONTEXT pCertContext, DWORD dwType,
                                                 DWORD dwFlags)
{
    DWORD dwSize = CertGetNameStringW(pCertContext, dwType, dwFlags, NULL, NULL, 0);
    if (dwSize <= 1) return NULL;

    LPWSTR pszName = (LPWSTR)LocalAlloc(LPTR, dwSize * sizeof(WCHAR));
    if (!pszName) return NULL;

    if (!CertGetNameStringW(pCertContext, dwType, dwFlags, NULL, pszName, dwSize))
    {
        LocalFree(pszName);
        return NULL;
    }

    return pszName;
}

void zzj::PEFile::FileSign::_freeCertInfo(PSCERT_INFO pCertInfo)
{
    if (pCertInfo)
    {
        if (pCertInfo->lpszSubject)
        {
            LocalFree(pCertInfo->lpszSubject);
            pCertInfo->lpszSubject = NULL;
        }
        if (pCertInfo->lpszIssuer)
        {
            LocalFree(pCertInfo->lpszIssuer);
            pCertInfo->lpszIssuer = NULL;
        }
        if (pCertInfo->lpszSerialNumber)
        {
            LocalFree(pCertInfo->lpszSerialNumber);
            pCertInfo->lpszSerialNumber = NULL;
        }
    }
}

bool zzj::PEFile::FileSign::_getCertificateInfo(PCCERT_CONTEXT pCertContext, PSCERT_INFO pCertInfo)
{
    if (!pCertContext || !pCertInfo) return false;

    ZeroMemory(pCertInfo, sizeof(SCERT_INFO));

    pCertInfo->lpszSubject = _getCertNameString(pCertContext, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0);

    pCertInfo->lpszIssuer =
        _getCertNameString(pCertContext, CERT_NAME_SIMPLE_DISPLAY_TYPE, CERT_NAME_ISSUER_FLAG);

    DWORD dwSize = pCertContext->pCertInfo->SerialNumber.cbData;
    if (dwSize > 0)
    {
        WCHAR szSerialNumber[256] = {0};
        for (DWORD i = 0; i < dwSize; i++)
        {
            if (i > 0) wcscat_s(szSerialNumber, L" ");

            WCHAR szByte[4];
            swprintf_s(szByte, L"%02X",
                       pCertContext->pCertInfo->SerialNumber.pbData[dwSize - 1 - i]);
            wcscat_s(szSerialNumber, szByte);
        }
        pCertInfo->lpszSerialNumber = AllocateAndCopyWideString(szSerialNumber);
    }

    FileTimeToSystemTime(&pCertContext->pCertInfo->NotBefore, &pCertInfo->notBefore);
    FileTimeToSystemTime(&pCertContext->pCertInfo->NotAfter, &pCertInfo->notAfter);

    return true;
}

bool zzj::PEFile::FileSign::GetCertificateInfo(SignatureInfo &signatureInfo)
{
    SCERT_INFO certInfo = {0};
    HCERTSTORE hStore = NULL;
    HCRYPTMSG hMsg = NULL;
    PCCERT_CONTEXT pCertContext = NULL;
    PCMSG_SIGNER_INFO pSignerInfo = NULL;
    BOOL fResult = FALSE;
    DWORD dwEncoding, dwContentType, dwFormatType;
    DWORD dwSignerInfo;
    CERT_INFO CertInfo;

    try
    {

        fResult = CryptQueryObject(CERT_QUERY_OBJECT_FILE, m_imageName.c_str(),
                                   CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                                   CERT_QUERY_FORMAT_FLAG_BINARY, 0, &dwEncoding, &dwContentType,
                                   &dwFormatType, &hStore, &hMsg, NULL);
        if (!fResult)
        {
            throw std::runtime_error("Failed to query crypto object");
        }


        fResult = CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, NULL, &dwSignerInfo);
        if (!fResult)
        {
            throw std::runtime_error("Failed to get signer info size");
        }


        pSignerInfo = (PCMSG_SIGNER_INFO)LocalAlloc(LPTR, dwSignerInfo);
        if (!pSignerInfo)
        {
            throw std::bad_alloc();
        }


        fResult =
            CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, (PVOID)pSignerInfo, &dwSignerInfo);
        if (!fResult)
        {
            throw std::runtime_error("Failed to get signer info");
        }


        CertInfo.Issuer = pSignerInfo->Issuer;
        CertInfo.SerialNumber = pSignerInfo->SerialNumber;


        pCertContext = CertFindCertificateInStore(hStore, ENCODING, 0, CERT_FIND_SUBJECT_CERT,
                                                  (PVOID)&CertInfo, NULL);
        if (!pCertContext)
        {
            throw std::runtime_error("Failed to find certificate in store");
        }


        fResult = _getCertificateInfo(pCertContext, &certInfo);
        if (!fResult)
        {
            throw std::runtime_error("Failed to extract certificate info");
        }


        signatureInfo.subject = zzj::str::w2utf8(certInfo.lpszSubject);
        signatureInfo.issuer = zzj::str::w2utf8(certInfo.lpszIssuer);
        signatureInfo.serialNumber = zzj::str::w2utf8(certInfo.lpszSerialNumber);
        signatureInfo.notBefore = certInfo.notBefore;
        signatureInfo.notAfter = certInfo.notAfter;
    }
    catch (const std::exception &e)
    {

        spdlog::warn("Certificate info extraction failed: {}", e.what());
        fResult = FALSE;
    }
    catch (...)
    {

        spdlog::warn("Unknown exception occurred during certificate info extraction");
        fResult = FALSE;
    }


    if (pSignerInfo != NULL)
    {
        LocalFree(pSignerInfo);
    }
    if (pCertContext != NULL)
    {
        CertFreeCertificateContext(pCertContext);
    }
    if (hStore != NULL)
    {
        CertCloseStore(hStore, 0);
    }
    if (hMsg != NULL)
    {
        CryptMsgClose(hMsg);
    }

    _freeCertInfo(&certInfo);

    return fResult == TRUE;
}

bool zzj::PEFile::FileSign::IsFileSigned()
{
    HCERTSTORE hStore = NULL;
    HCRYPTMSG hMsg = NULL;
    BOOL fResult;
    DWORD dwEncoding, dwContentType, dwFormatType;

    fResult =
        CryptQueryObject(CERT_QUERY_OBJECT_FILE, m_imageName.c_str(),
                         CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED, CERT_QUERY_FORMAT_FLAG_BINARY,
                         0, &dwEncoding, &dwContentType, &dwFormatType, &hStore, &hMsg, NULL);

    if (hStore != NULL) CertCloseStore(hStore, 0);
    if (hMsg != NULL) CryptMsgClose(hMsg);

    return fResult != FALSE;
}

zzj::PEFile::FileSign::SignatureStatus zzj::PEFile::FileSign::VerifySignature()
{
    WINTRUST_FILE_INFO FileData;
    ZeroMemory(&FileData, sizeof(FileData));
    FileData.cbStruct = sizeof(WINTRUST_FILE_INFO);
    FileData.pcwszFilePath = m_imageName.c_str();
    FileData.hFile = NULL;
    FileData.pgKnownSubject = NULL;

    WINTRUST_DATA WinTrustData;
    ZeroMemory(&WinTrustData, sizeof(WinTrustData));
    WinTrustData.cbStruct = sizeof(WinTrustData);
    WinTrustData.pPolicyCallbackData = NULL;
    WinTrustData.pSIPClientData = NULL;
    WinTrustData.dwUIChoice = WTD_UI_NONE;
    WinTrustData.fdwRevocationChecks = WTD_REVOKE_NONE;
    WinTrustData.dwUnionChoice = WTD_CHOICE_FILE;
    WinTrustData.dwStateAction = WTD_STATEACTION_VERIFY;
    WinTrustData.hWVTStateData = NULL;
    WinTrustData.pwszURLReference = NULL;
    WinTrustData.dwUIContext = 0;
    WinTrustData.pFile = &FileData;

    WinTrustData.dwProvFlags =
        WTD_REVOCATION_CHECK_NONE | WTD_LIFETIME_SIGNING_FLAG | WTD_CACHE_ONLY_URL_RETRIEVAL;

    GUID WVTPolicyGUID = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG lStatus = WinVerifyTrust(NULL, &WVTPolicyGUID, &WinTrustData);

    WinTrustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(NULL, &WVTPolicyGUID, &WinTrustData);

    switch (lStatus)
    {
        case ERROR_SUCCESS:
            return SIGNATURE_VALID;
        case TRUST_E_NOSIGNATURE:
            return SIGNATURE_NOT_SIGNED;
        case TRUST_E_EXPLICIT_DISTRUST:
        case TRUST_E_SUBJECT_NOT_TRUSTED:
        case CRYPT_E_SECURITY_SETTINGS:
            return SIGNATURE_NOT_TRUSTED;

        case CERT_E_EXPIRED:
        case CERT_E_VALIDITYPERIODNESTING:
        case TRUST_E_TIME_STAMP:
            return SIGNATURE_VALID;
        default:
            return SIGNATURE_ERROR;
    }
}