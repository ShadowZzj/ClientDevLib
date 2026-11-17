#pragma once
#include <General/util/BaseUtil.hpp>
#include <Windows/util/File/File.h>
#include <stdlib.h>
#include <string>
#include <windows.h>
#include <wincrypt.h>

struct _CMSG_SIGNER_INFO;
typedef struct _CMSG_SIGNER_INFO *PCMSG_SIGNER_INFO;
struct _CERT_CONTEXT;
typedef const struct _CERT_CONTEXT *PCCERT_CONTEXT;

class PEInfo
{
   public:
    explicit PEInfo(HMODULE _module)
    {
        module = _module;
        LoadModule(module);
    }
    bool LoadModule(HMODULE Module);
    static HMODULE GetModuleHandle(const char *name);
    static HMODULE GetModuleHandle(const wchar_t *name);
    // if the code is in dll, you can use this function to get exe module
    static HMODULE GetEXEModuleHandle();
    // if the code is in dll, you can use this function to get dll module.
    static HMODULE GetCurrentModuleHandle();

   private:
    HMODULE module;
};

namespace zzj
{

class PEFile : public File
{
#define ThrowExceptionIfFail(error) \
    if (!IsOperationSucceed(error)) throw error;
#define ReturnIfFail(error) \
    if (!IsOperationSucceed(error)) return error;

   public:
    class FileProperty
    {
       public:
        FileProperty(const std::string &imageName);
        ~FileProperty();

       public:
        std::string GetFileDescription();
        std::string GetFileVersion();
        std::string GetInternalName();
        std::string GetCompanyName();
        std::string GetLegalCopyright();
        std::string GetOriginalFilename();
        std::string GetProductName();
        std::string GetProductVersion();

       private:
        bool _queryValue(const std::string &valueName, const std::string &moduleName,
                         std::string &RetStr);

       private:
        std::string m_imageName;
    };

    class FileSign
    {
       public:

        class SPROG_PUBLISHERINFO
        {
          public:
            LPWSTR lpszProgramName;
            LPWSTR lpszPublisherLink;
            LPWSTR lpszMoreInfoLink;
        };

        using PSPROG_PUBLISHERINFO = SPROG_PUBLISHERINFO *;

        class SCERT_INFO
        {
          public:
            LPWSTR lpszSubject;
            LPWSTR lpszIssuer;
            LPWSTR lpszSerialNumber;
            SYSTEMTIME notBefore = {0};
            SYSTEMTIME notAfter = {0};
        };

        using PSCERT_INFO = SCERT_INFO *;

        class SignatureInfo
        {
          public:
            std::string subject;
            std::string issuer;
            std::string serialNumber;
            SYSTEMTIME notBefore = {0};
            SYSTEMTIME notAfter = {0};
        };

        using PSignatureInfo = SignatureInfo *;

        enum SignatureStatus
        {
            SIGNATURE_VALID = 0,
            SIGNATURE_INVALID,
            SIGNATURE_NOT_TRUSTED,
            SIGNATURE_EXPIRED,
            SIGNATURE_NOT_SIGNED,
            SIGNATURE_ERROR,
            SIGNATURE_UNKNOWN
        };

       public:
        FileSign(const std::string &fileName);
        ~FileSign();
 
        bool GetProgAndPublisherInfo(PSPROG_PUBLISHERINFO pProgPubInfo);
        bool GetTimeStampSignerInfo(SYSTEMTIME *st);

        bool GetCertificateInfo(SignatureInfo &signatureInfo);

        SignatureStatus VerifySignature();

        bool IsFileSigned();

       private:
        bool _getProgAndPublisherInfo(PCMSG_SIGNER_INFO pSignerInfo, PSPROG_PUBLISHERINFO Info);
        bool _getTimeStampSignerInfo(PCMSG_SIGNER_INFO pSignerInfo,
                                     PCMSG_SIGNER_INFO *pCounterSignerInfo);
        bool _getDateOfTimeStamp(PCMSG_SIGNER_INFO pSignerInfo, SYSTEMTIME *st);

        bool _getCertificateInfo(PCCERT_CONTEXT pCertContext, PSCERT_INFO pCertInfo);
        LPWSTR _getCertNameString(PCCERT_CONTEXT pCertContext, DWORD dwType, DWORD dwFlags);
        void _freeCertInfo(PSCERT_INFO pCertInfo);

       private:
        std::wstring m_imageName;
    };

    enum ErrorCode
    {
        SUCCESS = 1,
        INVALID_PEFILE,
        LOAD_PE_ERROR,
        INVALID_FILE_NAME,
        WINAPI_ERROR,
        ZZJ_LIB_ERROR,
        INVALID_PARAMETER
    };

    explicit PEFile(std::string _fileName);
    ~PEFile();
    ErrorCode LoadPE();
    ErrorCode UnloadPE();
    DWORD GetSectionNum();
    IMAGE_SECTION_HEADER *GetSection(int index);

    DWORD GetRelocationTableEntryNum();
    IMAGE_BASE_RELOCATION *GetRelocationTableEntry(int index);
    static std::uintptr_t RVAToVA(char *imageBase, DWORD rva);
    static std::uintptr_t RVAToOffset(char *imageBase, DWORD rva);
    static ErrorCode IsValidPE(std::string _fileName);
    static bool IsOperationSucceed(ErrorCode error);
    static ErrorCode ReadDosHeader(std::string fileName, IMAGE_DOS_HEADER *dosHeader);
    static ErrorCode ReadNTHeader(std::string fileName, const IMAGE_DOS_HEADER &dosHeader,
                                  IMAGE_NT_HEADERS *ntHeader);
    static ErrorCode ReadDataDirectoryTable(char *imageBase, IMAGE_NT_HEADERS *ntheader,
                                            IMAGE_DATA_DIRECTORY *&dataDirectoryTable);
    static ErrorCode ReadImportLibrary(char *imageBase, IMAGE_DATA_DIRECTORY *dataDirectoryTable,
                                       IMAGE_IMPORT_DESCRIPTOR *&importLibrary,
                                       DWORD &numOfImportLibrary);
    static ErrorCode ReadRelocation(char *imageBase, IMAGE_DATA_DIRECTORY *dataDirectoryTable,
                                    IMAGE_BASE_RELOCATION *&baseRelocation);
    static DWORD AlignData(DWORD data, DWORD alignment);
    FileProperty GetFileProperty();
    std::string fileName;
    DWORD fileSize;

    IMAGE_DOS_HEADER *dosHeader;
    IMAGE_NT_HEADERS *ntHeader;
    IMAGE_DATA_DIRECTORY *dataDirectoryTable;
    IMAGE_SECTION_HEADER *sectionHeader;
    IMAGE_BASE_RELOCATION *baseRelocation;

    IMAGE_IMPORT_DESCRIPTOR *importLibrary;
    DWORD numOfImportLibrary = 0;

    DWORD extraDataSize;
    char *extraData = nullptr;

   private:
    char *peImage = nullptr;
};
}  // namespace zzj