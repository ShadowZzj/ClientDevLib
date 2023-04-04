#pragma once
#include <string>
#include <tuple>
#include <vector>


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
    Certificate(){}
    ~Certificate(){}

    static std::tuple<int, std::vector<Certificate>> GetCerticifateByIssuer(const std::string &issuer,
                                                                            const StoreType &storeType);
    static std::tuple<int, std::vector<Certificate>> GetCerticifateByName(const std::string &name,
                                                                          const StoreType &storeType);
    static std::tuple<int, std::vector<Certificate>> GetCerticifateBySequence(const std::string &sequence,
                                                                              const StoreType &storeType);
    
    int Delete();
    static std::tuple<int,std::vector<Certificate>> AddFromFile(const std::string& filePath,const std::string& passwd,StoreType storeType);
    static std::tuple<int,std::vector<Certificate>> AddFromContent(const std::string& content,const std::string& passwd,StoreType storeType);
    static std::tuple<int,std::vector<Certificate>> ReadFromFile(const std::string& filePath,const std::string& passwd,StoreType storeType);
    static std::tuple<int,std::vector<Certificate>> ReadFromContent(const std::string& content,const std::string& passwd,StoreType storeType);
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

  private:
    std::string _name;
    std::string _sequence;
    std::string _issuer;

#ifdef _WIN32
#else
    enum class CertificateTemplateType
    {
        Issuer,
        Sequence,
        Name,
    };
    enum class OperateType
    {
        Add,
        Delete,
        Query,
    };
    static std::tuple<int, std::vector<Certificate>> GetCerticifateTemplate( const Certificate::StoreType &storeType, std::vector<std::function<bool(std::string)>> predicate,
        CertificateTemplateType templateType,OperateType operateType = OperateType::Query);
    static std::tuple<int,std::vector<Certificate>> ImportCertificateTemplate(const std::string& content,const std::string& passwd,const Certificate::StoreType &storeType, std::vector<std::function<bool(Certificate)>> predicate,OperateType operateType = OperateType::Query);
    static std::tuple<int,std::vector<Certificate>> DoFromFileTemplate(const std::string &filePath, const std::string &passwd,zzj::Certificate::StoreType storeType,std::function<std::tuple<int,std::vector<Certificate>>(const std::string &filePath, const std::string &passwd,zzj::Certificate::StoreType storeType)> func);
    friend std::tuple<int,Certificate> GetCertificateInfo(SecCertificateRef certificate);
#endif
};
} // namespace zzj
