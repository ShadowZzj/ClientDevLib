#include "Http.h"
#include <General/util/BaseUtil.hpp>
#include <General/util/StrUtil.h>
#include <fstream>
#include <json.hpp>
#include <spdlog/spdlog.h>
#include <stdio.h>
#include <string>
#include <boost/filesystem.hpp>
#ifdef _WIN32
#include <curl/win/curl.h>
#else
#include <curl/mac/curl.h>
#endif
size_t WriteDataFile(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    size_t written = fwrite(ptr, size, nmemb, stream);
    return written;
}

size_t WriteData(void *ptr, size_t size, size_t nmemb, void *stream)
{
    std::string *str = (std::string *)stream;
    (*str).append((char *)ptr, size * nmemb);
    return size * nmemb;
}
size_t WriteHeader(void *ptr, size_t size, size_t nmemb, void *stream)
{
    std::string header;
    header.append((char *)ptr, size * nmemb);
    nlohmann::json *json = (nlohmann::json *)stream;
    std::string key      = header.substr(0, header.find(":"));
    std::string value    = header.substr(header.find(":") + 1);
    if (json->find(key) != json->end())
    {
        // if the value type is string, then make it array
        if ((*json)[key].is_string())
        {
            nlohmann::json tmp = (*json)[key];
            (*json)[key]       = nlohmann::json::array();
            (*json)[key].push_back(tmp);
        }
        (*json)[key].push_back(value);
    }
    else
    {
        (*json)[key] = value;
    }
    return size * nmemb;
}
int zzj::Http::Put(const char *apiPath, const char *str, std::string &ret)
{
    CURL *curl;
    CURLcode res;
    curl = curl_easy_init();
    if (curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, apiPath);
        curl_easy_setopt(curl, CURLOPT_PUT, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, str);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteData);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ret);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 60);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60);
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    return res;
}

std::string zzj::Http::DecodeUri(const std::string &uri)
{
    std::string decoded;
    std::istringstream in(uri);
    char ch;

    while (in.get(ch))
    {
        if (ch == '%')
        {
            int32_t n;
            in >> std::hex >> n;
            decoded += static_cast<char>(n);
            in.ignore(1);
        }
        else
        {
            decoded += ch;
        }
    }

    return decoded;
}

std::string zzj::Http::DownloadFromUrl(std::string url, std::string path, int connectionTimeOut, int timeout)
{
    CURL *curl;
    FILE *fp;
    CURLcode res;
    std::string ret         = "";
    std::string outFileName = url.substr(url.find_last_of("/") + 1);
    boost::filesystem::path outFileFullPath = path;
    outFileFullPath                         = outFileFullPath / outFileName;
    curl                    = curl_easy_init();
    if (curl)
    {
        fp = fopen(outFileFullPath.string().c_str(), "wb");
        if (!fp)
        {
            spdlog::error("DownloadFromUrl open file error");
            ret = "";
            goto exit;
        }
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteDataFile);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connectionTimeOut);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
        // curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, setSSL);
        res = curl_easy_perform(curl);
        /* always cleanup */
        char *curlVer = curl_version();
        if (res != CURLE_OK)
        {
            spdlog::error("curl_easy_perform() failed: {}", curl_easy_strerror(res));
            ret = "";
            goto exit;
        }
    }
    ret = outFileFullPath.string().c_str();
exit:
    curl_easy_cleanup(curl);
    if (fp)
        fclose(fp);

    return ret;
}
#include <iostream>
int zzj::Http::PostWithJsonSetting(const std::string &jsonSetting, std::string &retString)
{
    try
    {
        CURL *curl           = curl_easy_init();
        curl_mime *form      = NULL;
        curl_mimepart *field = NULL;
        CURLcode res;
        int result = 0;
        char errBuf[CURL_ERROR_SIZE];
        struct curl_slist *http_headers = NULL;
        retString                       = "";
        std::string postRetContent;
        std::string bodyData;
        std::string cert;
        std::string keypasswd;

        DEFER
        {
            if (0 != result)
                spdlog::error("Http post result {},ret :{} ", result, errBuf);
            curl_slist_free_all(http_headers);
            curl_easy_cleanup(curl);
            curl_mime_free(form);
        };
        if (!curl)
        {
            result = -1;
            return result;
        }

        nlohmann::json setting = nlohmann::json::parse(jsonSetting);
        if (setting.find("url") == setting.end())
        {
            spdlog::error("Http post json parse error, no url");
            result = -3;
            return result;
        }
        std::string url = setting["url"];
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        if (setting.find("headers") != setting.end())
        {
            nlohmann::json headers = setting["headers"];
            for (auto it = headers.begin(); it != headers.end(); it++)
            {
                if (!it.value().is_string())
                {
                    spdlog::error("Http post json parse error, header value is not string");
                    result = -3;
                    return result;
                }
                std::string header = it.key() + ":" + it.value().get<std::string>();
                http_headers       = curl_slist_append(http_headers, header.c_str());
            }
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, http_headers);
        }
        if (setting.find("nobody") != setting.end())
        {
            int nobody = setting["nobody"];
            curl_easy_setopt(curl, CURLOPT_NOBODY, nobody);
        }
        if (setting.find("body") != setting.end())
        {
            nlohmann::json body = setting["body"];
            if (body.find("type") == body.end())
            {
                spdlog::error("Http post json parse error, no body type");
                result = -3;
                return result;
            }
            std::string bodyType = body["type"];
            if (bodyType == "json")
            {
                if (body.find("data") == body.end())
                {
                    bodyData = "";
                }
                else
                {
                    bodyData = body["data"];
                }
                curl_easy_setopt(curl, CURLOPT_POST, 1);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyData.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, bodyData.size());
            }
            else if (bodyType == "form")
            {
                if (body.find("data") == body.end())
                {
                    spdlog::error("Http post json parse error, no body data");
                    result = -3;
                    return result;
                }
                nlohmann::json files  = body["data"]["files"];
                nlohmann::json fields = body["data"]["fields"];
                form                  = curl_mime_init(curl);
                for (auto it = files.begin(); it != files.end(); it++)
                {
                    if (!it.value().is_string())
                    {
                        spdlog::error("Http post json parse error, file value is not string");
                        result = -3;
                        return result;
                    }
                    std::string fileName = it.value().get<std::string>();
                    field                = curl_mime_addpart(form);
                    curl_mime_name(field, it.key().c_str());
                    curl_mime_filedata(field, fileName.c_str());
                }
                for (auto it = fields.begin(); it != fields.end(); it++)
                {
                    if (!it.value().is_string())
                    {
                        spdlog::error("Http post json parse error, field value is not string");
                        result = -3;
                        return result;
                    }
                    std::string fieldValue = it.value().get<std::string>();
                    field                  = curl_mime_addpart(form);
                    curl_mime_name(field, it.key().c_str());
                    curl_mime_data(field, fieldValue.c_str(), CURL_ZERO_TERMINATED);
                }
                curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
            }
            else
            {
                spdlog::error("Http post json parse error, body type is not support");
                result = -3;
                return result;
            }
        }
        if (setting.find("timeout") != setting.end())
        {
            int timeout = setting["timeout"];
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeout);
        }
        else
        {
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 60);
        }
        if (setting.find("followlocation") != setting.end())
        {
            int followlocation = setting["followlocation"];
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, followlocation);
        }
        else
        {
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
        }
        if (setting.find("ssl") != setting.end())
        {
            nlohmann::json ssl = setting["ssl"];
            if (ssl.find("verifypeer") != ssl.end())
            {
                bool verifypeer = ssl["verifypeer"];
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, verifypeer);
            }
            if (ssl.find("verifyhost") != ssl.end())
            {
                bool verifyhost = ssl["verifyhost"];
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, verifyhost);
            }
            if (ssl.find("cert") != ssl.end())
            {
                cert = ssl["cert"];
                curl_easy_setopt(curl, CURLOPT_SSLCERT, cert.c_str());
                curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "P12");
            }
            if (ssl.find("keypasswd") != ssl.end())
            {
                keypasswd = ssl["keypasswd"];
                curl_easy_setopt(curl, CURLOPT_KEYPASSWD, keypasswd.c_str());
            }
        }
        int responseCode = 0;
        nlohmann::json postRetHeader;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteData);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &postRetContent);
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errBuf);
        // get response header
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, WriteHeader);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &postRetHeader);
        res = curl_easy_perform(curl);

        if (res != CURLE_OK)
        {
            spdlog::error("Http post error {}", errBuf);
            result = -3;
            return result;
        }
        double time;
        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &time);
        time = time * 1000;

        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
        nlohmann::json retJson;
        retJson["code"]          = responseCode;
        retJson["header"]        = postRetHeader;
        retJson["body"]          = postRetContent;
        retJson["extra"]["time"] = time;
        retString                = retJson.dump();
        return result;
    }
    catch (const std::exception &e)
    {
        spdlog::error("Http post json error {}", e.what());
        return -2;
    }
}
int zzj::Http::GetWithJsonSetting(const std::string &jsonSetting, std::string &retString)
{
    try
    {
        CURL *curl = curl_easy_init();
        curl_mime *form = NULL;
        curl_mimepart *field = NULL;
        CURLcode res;
        int result = 0;
        char errBuf[CURL_ERROR_SIZE];
        struct curl_slist *http_headers = NULL;
        retString                       = "";
        std::string postRetContent;
        std::string bodyData;
        std::string cert;
        std::string keypasswd;
        DEFER
        {
            if (0 != result)
                spdlog::error("Http get result {},ret :{} ", result, errBuf);
            curl_slist_free_all(http_headers);
            curl_easy_cleanup(curl);
            curl_mime_free(form);
        };
        if (!curl)
        {
            result = -1;
            return result;
        }
        nlohmann::json setting = nlohmann::json::parse(jsonSetting);
        if (setting.find("url") == setting.end())
        {
            spdlog::error("Http get json parse error, no url");
            result = -3;
            return result;
        }
        std::string url = setting["url"];
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        if (setting.find("body") != setting.end())
        {
            nlohmann::json body = setting["body"];
            if (body.find("type") == body.end())
            {
                spdlog::error("Http post json parse error, no body type");
                result = -3;
                return result;
            }
            std::string bodyType = body["type"];
            if (bodyType == "json")
            {
                if (body.find("data") == body.end())
                {
                    bodyData = "";
                }
                else
                {
                    bodyData = body["data"];
                }
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyData.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, bodyData.size());
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
            }
            else if (bodyType == "form")
            {
                if (body.find("data") == body.end())
                {
                    spdlog::error("Http post json parse error, no body data");
                    result = -3;
                    return result;
                }
                nlohmann::json files  = body["data"]["files"];
                nlohmann::json fields = body["data"]["fields"];
                form                  = curl_mime_init(curl);
                for (auto it = files.begin(); it != files.end(); it++)
                {
                    if (!it.value().is_string())
                    {
                        spdlog::error("Http post json parse error, file value is not string");
                        result = -3;
                        return result;
                    }
                    std::string fileName = it.value().get<std::string>();
                    field                = curl_mime_addpart(form);
                    curl_mime_name(field, it.key().c_str());
                    curl_mime_filedata(field, fileName.c_str());
                }
                for (auto it = fields.begin(); it != fields.end(); it++)
                {
                    if (!it.value().is_string())
                    {
                        spdlog::error("Http post json parse error, field value is not string");
                        result = -3;
                        return result;
                    }
                    std::string fieldValue = it.value().get<std::string>();
                    field                  = curl_mime_addpart(form);
                    curl_mime_name(field, it.key().c_str());
                    curl_mime_data(field, fieldValue.c_str(), CURL_ZERO_TERMINATED);
                }
                curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
            }
            else
            {
                spdlog::error("Http post json parse error, body type is not support");
                result = -3;
                return result;
            }
        }
        if (setting.find("headers") != setting.end())
        {
            nlohmann::json headers = setting["headers"];
            for (auto it = headers.begin(); it != headers.end(); it++)
            {
                if (!it.value().is_string())
                {
                    spdlog::error("Http get json parse error, header value is not string");
                    result = -3;
                    return result;
                }
                std::string header = it.key() + ":" + it.value().get<std::string>();
                http_headers       = curl_slist_append(http_headers, header.c_str());
            }
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, http_headers);
        }
        if (setting.find("timeout") != setting.end())
        {
            int timeout = setting["timeout"];
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeout);
        }
        else
        {
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 60);
        }
        if (setting.find("nobody") != setting.end())
        {
            int nobody = setting["nobody"];
            curl_easy_setopt(curl, CURLOPT_NOBODY, nobody);
        }
        if (setting.find("followlocation") != setting.end())
        {
            int followlocation = setting["followlocation"];
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, followlocation);
        }
        else
        {
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
        }
        if (setting.find("ssl") != setting.end())
        {
            nlohmann::json ssl = setting["ssl"];
            if (ssl.find("verifypeer") != ssl.end())
            {
                bool verifypeer = ssl["verifypeer"];
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, verifypeer);
            }
            if (ssl.find("verifyhost") != ssl.end())
            {
                bool verifyhost = ssl["verifyhost"];
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, verifyhost);
            }
            if (ssl.find("cert") != ssl.end())
            {
                cert = ssl["cert"];
                curl_easy_setopt(curl, CURLOPT_SSLCERT, cert.c_str());
            }
            if (ssl.find("keypasswd") != ssl.end())
            {
                keypasswd = ssl["keypasswd"];
                curl_easy_setopt(curl, CURLOPT_KEYPASSWD, keypasswd.c_str());
            }
        }
        int responseCode = -1;
        nlohmann::json postRetHeader;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteData);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &postRetContent);
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errBuf);
        // get response header
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, WriteHeader);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &postRetHeader);
        res = curl_easy_perform(curl);

        if (res != CURLE_OK)
        {
            spdlog::error("Http post error {}", errBuf);
            result = -3;
            return result;
        }
        double time;
        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &time);
        time = time * 1000;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
        char *ip = nullptr;
        curl_easy_getinfo(curl, CURLINFO_PRIMARY_IP, &ip);

        try
        {
            nlohmann::json retJson;
            retJson["code"]          = responseCode;
            retJson["header"]        = postRetHeader;
            retJson["body"]          = postRetContent;
            retJson["extra"]["time"] = time;
            retJson["extra"]["remoteIp"] = ip == nullptr ? "" :  std::string(ip);
            retString                = retJson.dump();
        }
        catch (std::exception &e)
        {
            spdlog::error("http json error with {}, do not return body", e.what());
            nlohmann::json retJson;
            retJson["code"]          = responseCode;
            retJson["header"]        = postRetHeader;
            retJson["extra"]["time"] = time;
            retJson["extra"]["remoteIp"] = ip == nullptr ? "" : std::string(ip);
            retString                = retJson.dump();
        }
        return result;
    }
    catch (const std::exception &e)
    {
        spdlog::error("Http post json error {}", e.what());
        return -2;
    }
}
int zzj::Http::PostFile(const std::string &apiPath, std::map<std::string, std::string> headers,
                        std::map<std::string, std::string> bodyParam, const std::string &fileKey,
                        const std::string &fileName, std::string &ret, bool setSSL, int timeout,
                        std::string personalCertificateFile, std::string passwd)
{
    CURL *curl;
    CURLcode res;

    char errBuf[CURL_ERROR_SIZE];
    curl_mime *form               = NULL;
    curl_mimepart *field          = NULL;
    struct curl_slist *headerlist = NULL;
    curl                          = curl_easy_init();
    if (curl)
    {
        /* Create the form */
        form = curl_mime_init(curl);
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "UTF-8");
        for (auto it = bodyParam.begin(); it != bodyParam.end(); it++)
        {
            field = curl_mime_addpart(form);
            std::string firstUtf8 = zzj::str::ansi2utf8(it->first);
            std::string secondUtf8 = zzj::str::ansi2utf8(it->second);
            curl_mime_name(field, firstUtf8.c_str());
            curl_mime_data(field, secondUtf8.c_str(), CURL_ZERO_TERMINATED);
        }

        /* Fill in the file upload field */
        field = curl_mime_addpart(form);
        curl_mime_name(field, fileKey.c_str());
        curl_mime_filedata(field, fileName.c_str());

        /* initialize custom header list */
        for (auto &header : headers)
        {
            std::string tmpheader = header.first + ": " + header.second;
            headerlist            = curl_slist_append(headerlist, tmpheader.c_str());
        }
        /* what URL that receives this POST */
        curl_easy_setopt(curl, CURLOPT_URL, apiPath.c_str());
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteData);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ret);
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errBuf);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeout);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, setSSL);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, setSSL);
        if (!personalCertificateFile.empty() && !passwd.empty())
        {
            curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "P12");
            curl_easy_setopt(curl, CURLOPT_SSLCERT, personalCertificateFile.c_str());
            curl_easy_setopt(curl, CURLOPT_SSLKEYPASSWD, passwd.c_str());
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);
        /* Perform the request, res will get the return code */
        res = curl_easy_perform(curl);
        /* Check for errors */
        if (res != CURLE_OK)
        {
            printf("%s", errBuf);
        }
        /* always cleanup */
        curl_easy_cleanup(curl);

        /* then cleanup the form */
        curl_mime_free(form);
        /* free slist */
        curl_slist_free_all(headerlist);
    }
    return res;
}

int zzj::Http::Post(const char *apiPath, const char *str, std::string &ret, bool setSSL)
{
    CURL *curl = curl_easy_init();
    CURLcode res;
    int result = 0;
    char errBuf[CURL_ERROR_SIZE];
    char *version = curl_version();

    struct curl_slist *http_headers = NULL;
    ret                             = "";
    if (!curl)
    {
        result = -1;
        goto exit;
    }
    http_headers = curl_slist_append(http_headers, "Accept: application/json");
    http_headers = curl_slist_append(http_headers, "Content-Type: application/json");
    http_headers = curl_slist_append(http_headers, "charsets: utf-8");
    http_headers = curl_slist_append(http_headers, "Authorization: Bearer 52371ee0a435aa1a1fe670585eb32958");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, http_headers);

    curl_easy_setopt(curl, CURLOPT_URL, apiPath);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, str);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ret);
    curl_easy_setopt(curl, CURLOPT_POST, 1);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errBuf);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 60);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, setSSL);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, setSSL);
    res = curl_easy_perform(curl);

    if (res != CURLE_OK)
    {
        // ret = "";
        switch (res)
        {
        case CURLE_UNSUPPORTED_PROTOCOL:
            result = -1;
        case CURLE_COULDNT_CONNECT:
            result = -2;
        case CURLE_HTTP_RETURNED_ERROR:
            result = -3;
        case CURLE_READ_ERROR:
            result = -4;
        default:
            result = res;
        }
        goto exit;
    }

exit:
    if (0 != result)
        spdlog::error("Http post result {},ret :{} ", result, errBuf);
    curl_slist_free_all(http_headers);
    curl_easy_cleanup(curl);
    return result;
}

int zzj::Http::PostMutualAuth(const char *apiPath, const char *str, std::string &ret,
                              std::string personalCertificateFile, std::string passwd)
{
    CURL *curl = curl_easy_init();
    CURLcode res;
    int result = 0;
    char errBuf[CURL_ERROR_SIZE]{0};
    char *version = curl_version();
    printf("libcurl version: %s\n", version);

    struct curl_slist *http_headers = NULL;
    ret                             = "";
    if (!curl)
    {
        result = -1;
        goto exit;
    }
    http_headers = curl_slist_append(http_headers, "Accept: application/json");
    http_headers = curl_slist_append(http_headers, "Content-Type: application/json");
    http_headers = curl_slist_append(http_headers, "charsets: utf-8");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, http_headers);

    curl_easy_setopt(curl, CURLOPT_URL, apiPath);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, str);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ret);
    curl_easy_setopt(curl, CURLOPT_POST, 1);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errBuf);

    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 60);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, true);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, true);
    curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "P12");
    curl_easy_setopt(curl, CURLOPT_SSLCERT, personalCertificateFile.c_str());
    curl_easy_setopt(curl, CURLOPT_KEYPASSWD, passwd.c_str());
    res = curl_easy_perform(curl);

    if (res != CURLE_OK)
    {
        ret = "";
        switch (res)
        {
        case CURLE_UNSUPPORTED_PROTOCOL:
            result = -1;
        case CURLE_COULDNT_CONNECT:
            result = -2;
        case CURLE_HTTP_RETURNED_ERROR:
            result = -3;
        case CURLE_READ_ERROR:
            result = -4;
        default:
            result = res;
        }

        spdlog::error("Http post result {},ret :{} ", result, errBuf);
        goto exit;
    }

exit:
    curl_slist_free_all(http_headers);
    curl_easy_cleanup(curl);
    return result;
}

int zzj::Http::Get(const char *apiPath, std::string &ret)
{
    auto curl = curl_easy_init();
    long response_code;
    CURLcode res;
    int result = 0;
    std::string response_string;
    std::string header_string;
    if (!curl)
    {
        result = -1;
        goto exit;
    }

    curl_easy_setopt(curl, CURLOPT_URL, apiPath);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    // curl_easy_setopt(curl, CURLOPT_USERPWD, "user:pass");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "curl/7.42.0");
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 50L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_string);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 60);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, true);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, true);
    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        ret = "";
        switch (res)
        {
        case CURLE_UNSUPPORTED_PROTOCOL:
            result = -1;
        case CURLE_COULDNT_CONNECT:
            result = -2;
        case CURLE_HTTP_RETURNED_ERROR:
            result = -3;
        case CURLE_READ_ERROR:
            result = -4;
        default:
            result = res;
        }
        goto exit;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    // curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &elapsed);
    // curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &url);
    if (response_code != 200)
    {
        result = response_code;
        goto exit;
    }

    ret = response_string;
exit:

    curl_easy_cleanup(curl);
    return result;
}
