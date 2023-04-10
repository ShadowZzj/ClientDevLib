#pragma once
#include <functional>
#include <string>
#include <tuple>
#include <vector>
#ifdef _WIN32
#include <Windows.h>
#include <wincrypt.h>
#else
#include <Foundation/Foundation.h>
#endif
namespace zzj
{

class Certificate
{
  public:
    
    enum class StoreType
    {
        CurrentUser,
        LocalMachine
    };
    enum class CertLocation
    {
        User,
        Root,
    };
    Certificate()
    {
    }
    ~Certificate()
    {
    }

    std::string name() const
    {
        return _name;
    }
    std::string sequence() const
    {
        return _sequence;
    }
    std::string issuer() const
    {
        return _issuer;
    }

#ifdef _WIN32
    int Delete(const StoreType &storeType, CertLocation certLocation);
    static std::tuple<int, std::vector<Certificate>> GetCerticifateByIssuer(const std::string &issuer,
                                                                            const StoreType &storeType,
                                                                            CertLocation certLocation);
    static std::tuple<int, std::vector<Certificate>> GetCerticifateByName(const std::string &name,
                                                                          const StoreType &storeType,
                                                                          CertLocation certLocation);
    static std::tuple<int, std::vector<Certificate>> GetCerticifateBySequence(const std::string &sequence,
                                                                              const StoreType &storeType,
                                                                              CertLocation certLocation);

    static std::tuple<int, std::vector<Certificate>> AddFromFile(const std::string &filePath, const std::string &passwd,
                                                                 StoreType storeType, CertLocation certLocation);
    static std::tuple<int, std::vector<Certificate>> AddFromContent(const std::string &content,
                                                                    const std::string &passwd, StoreType storeType,
                                                                    CertLocation certLocation);
    static std::tuple<int, std::vector<Certificate>> ReadFromFile(const std::string &filePath,
                                                                  const std::string &passwd, StoreType storeType,
                                                                  CertLocation certLocation);
    static std::tuple<int, std::vector<Certificate>> ReadFromContent(const std::string &content,
                                                                     const std::string &passwd, StoreType storeType,
                                                                     CertLocation certLocation);
#else
    int Delete();
    static std::tuple<int, std::vector<Certificate>> GetCerticifateByIssuer(const std::string &issuer,
                                                                            const StoreType &storeType);
    static std::tuple<int, std::vector<Certificate>> GetCerticifateByName(const std::string &name,
                                                                          const StoreType &storeType);
    static std::tuple<int, std::vector<Certificate>> GetCerticifateBySequence(const std::string &sequence,
                                                                              const StoreType &storeType);

    static std::tuple<int, std::vector<Certificate>> AddFromFile(const std::string &filePath, const std::string &passwd,
                                                                 StoreType storeType);
    static std::tuple<int, std::vector<Certificate>> AddFromContent(const std::string &content,
                                                                    const std::string &passwd, StoreType storeType);
    static std::tuple<int, std::vector<Certificate>> ReadFromFile(const std::string &filePath,
                                                                  const std::string &passwd, StoreType storeType);
    static std::tuple<int, std::vector<Certificate>> ReadFromContent(const std::string &content,
                                                                     const std::string &passwd, StoreType storeType);
#endif
  private:
    std::string _name;
    std::string _sequence;
    std::string _issuer;

    enum class OperateType
    {
        Add,
        Delete,
        Query,
    };
#ifdef _WIN32
    static std::tuple<int, std::vector<Certificate>> GetCerticifateTemplate(
        const Certificate::StoreType &storeType, CertLocation certLocation, std::function<bool(Certificate)> predicate,
        OperateType operateType = OperateType::Query);
    static std::tuple<int, std::vector<Certificate>> ImportCertificateTemplate(
        const std::string &content, const std::string &passwd, const Certificate::StoreType &storeType,
        CertLocation certLocation, std::function<bool(Certificate)> predicate,
        OperateType operateType = OperateType::Query);
    static std::tuple<int, Certificate> GetCertificate(PCCERT_CONTEXT pCertContext);
#else
    enum class CertificateTemplateType
    {
        Issuer,
        Sequence,
        Name,
    };
    static std::tuple<int, std::vector<Certificate>> GetCerticifateTemplate(
        const Certificate::StoreType &storeType, std::vector<std::function<bool(std::string)>> predicate,
        CertificateTemplateType templateType, OperateType operateType = OperateType::Query);
    static std::tuple<int, std::vector<Certificate>> ImportCertificateTemplate(
        const std::string &content, const std::string &passwd, const Certificate::StoreType &storeType,
        std::vector<std::function<bool(Certificate)>> predicate, OperateType operateType = OperateType::Query);
    static std::tuple<int, std::vector<Certificate>> DoFromFileTemplate(
        const std::string &filePath, const std::string &passwd, zzj::Certificate::StoreType storeType,
        std::function<std::tuple<int, std::vector<Certificate>>(const std::string &filePath, const std::string &passwd,
                                                                zzj::Certificate::StoreType storeType)>
            func);
    static std::tuple<int, Certificate> GetCertificateInfo(SecCertificateRef certificate);
#endif
};
} // namespace zzj
