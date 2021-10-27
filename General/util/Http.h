#ifndef _G_HTTP_H_
#define _G_HTTP_H_

#include <curl/curl.h>
#include <string>
namespace zzj
{
class Http
{
  public:
    static std::string DownloadFromUrl(std::string url, std::string path);
    static int Post(const char* apiPath,const char* str,std::string& ret,bool setSSL = false);
    static int PostMutualAuth(const char *apiPath, const char *str, std::string &ret,
                              std::string personalCertificateFile,std::string passwd);
    static int Get(const char* apiPath,std::string& ret);
  private:
};
}; // namespace zzj

#endif
