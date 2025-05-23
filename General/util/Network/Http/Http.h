#ifndef _G_HTTP_H_
#define _G_HTTP_H_

#include <map>
#include <string>
#include <vector>
#include <curl/curl.h>
namespace zzj
{
class Http
{
   public:
    static std::string DecodeUri(const std::string &uri);
    static std::string DownloadFromUrl(std::string url, std::string path,
                                       int connectionTimeOut = 60, int timeout = 300);
    static int Post(const char *apiPath, const char *str, std::string &ret, bool setSSL = false);
    static int PostFile(const std::string &apiPath, std::map<std::string, std::string> headers,
                        std::map<std::string, std::string> bodyParam, const std::string &fileKey,
                        const std::string &fileName, std::string &ret, bool setSSL = false,
                        int timeout = 60, std::string personalCertificateFile = "",
                        std::string passwd = "");
    static int PostMutualAuth(const char *apiPath, const char *str, std::string &ret,
                              std::string personalCertificateFile, std::string passwd);
    static int Get(const char *apiPath, std::string &ret);
    static int Put(const char *apiPath, const char *str, std::string &ret);
    static int PostWithJsonSetting(const std::string &jsonSetting, std::string &retString);
    static int GetWithJsonSetting(const std::string &jsonSetting, std::string &retString);
    static CURLsslset CurlSelectSSLBackend(curl_sslbackend backend);

   private:
};
};  // namespace zzj

#endif
