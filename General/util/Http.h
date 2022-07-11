#ifndef _G_HTTP_H_
#define _G_HTTP_H_

#include <curl/curl.h>
#include <map>
#include <string>
#include <vector>
namespace zzj
{
class Http
{
  public:
    static std::string DownloadFromUrl(std::string url, std::string path, int connectionTimeOut = 60,
                                       int timeout = 300);
    static int Post(const char *apiPath, const char *str, std::string &ret, bool setSSL = false);
    static int PostFile(const std::string &apiPath, std::map<std::string, std::string> headers,
                        std::map<std::string, std::string> bodyParam, const std::string &fileKey,
                        const std::string &fileName, std::string &ret, bool setSSL = false, int timeout = 60,
                        std::string personalCertificateFile = "", std::string passwd = "");
    static int PostMutualAuth(const char *apiPath, const char *str, std::string &ret,
                              std::string personalCertificateFile, std::string passwd);
    static int Get(const char *apiPath, std::string &ret);
    static int Put(const char *apiPath, const char *str, std::string &ret);

  private:
};
}; // namespace zzj

#endif
