#ifndef _G_SIGN_H_
#define _G_SIGN_H_

#include <map>
#include <string>
#include "StrUtil.h"
#include "Md5.h"
inline std::string GetUrlSign(const char *url, std::map<std::string, std::string> args)
{
    std::string structedStr = url;
    StrCoding urlCoding;
    MD5 md5;
    std::string ret;
    
    for(auto iter:args)
        structedStr+=urlCoding.UrlUTF8(iter.second.c_str());
    
    md5.update(structedStr);
    
    return md5.toString();
}

#endif