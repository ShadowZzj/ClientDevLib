#include "Http.h"
#include <stdio.h>
#include <string>
#include <spdlog/spdlog.h>
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

std::string zzj::Http::DownloadFromUrl(std::string url, std::string path)
{
    CURL *curl;
    FILE *fp;
    CURLcode res;
    std::string ret         = "";
    std::string outFileName = url.substr(url.find_last_of("/") + 1);
    outFileName             = path + outFileName;
    curl                    = curl_easy_init();
    if (curl)
    {
        fp = fopen(outFileName.c_str(), "wb");
        if (!fp)
        {
            ret = "";
            goto exit;
        }
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteDataFile);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2);
        // curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, setSSL);
        res = curl_easy_perform(curl);
        /* always cleanup */
        char *curlVer = curl_version();
        if (res != CURLE_OK)
        {
            ret = "";
            goto exit;
        }
    }
    ret = outFileName;
exit:
    curl_easy_cleanup(curl);
    if (fp)
        fclose(fp);

    return ret;
}

int zzj::Http::Post(const char *apiPath, const char *str, std::string &ret, bool setSSL)
{
    CURL *curl = curl_easy_init();
    CURLcode res;
    int result = 0;
    char errBuf[CURL_ERROR_SIZE];
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
        //ret = "";
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
    if(0 != result)
        spdlog::error("Http post result {},ret :{} ", result,ret);
    curl_slist_free_all(http_headers);
    curl_easy_cleanup(curl);
    return result;
}

int zzj::Http::PostMutualAuth(const char *apiPath, const char *str, std::string &ret,
                              std::string personalCertificateFile,std::string passwd)
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
