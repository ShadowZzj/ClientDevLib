#pragma once
#include <General/util/BaseUtil.hpp>
#include <General/util/Memory/Allocator/Allocator.hpp>
#include <codecvt>
#include <iostream>
#include <locale>
#include <sstream>
#include <string>

using std::string;

template <class Facet> struct deletable_facet : Facet
{
    template <class... Args> deletable_facet(Args &&...args) : Facet(std::forward<Args>(args)...)
    {
    }
    ~deletable_facet()
    {
    }
};
namespace zzj
{
namespace str
{
inline char *Dup(const char *s)
{
    return s ? strdup(s) : nullptr;
}
inline wchar_t *Dup(const wchar_t *s)
{
    if (!s)
        return nullptr;
    wchar_t *dst;
    size_t size = wcslen(s) * sizeof(wchar_t);
    dst         = (wchar_t *)malloc(size + sizeof(wchar_t));
    memcpy(dst, s, size);
    return dst;
}
inline void Free(void *s)
{
    if (s)
        free(s);
}
inline char *FmtV(const char *fmt, va_list args)
{
    char message[256];
    size_t bufCchSize = zzj::GetDim(message);
    char *buf         = message;
    for (;;)
    {
        int count = vsnprintf(buf, bufCchSize, fmt, args);
        if ((count >= 0) && ((size_t)count < bufCchSize))
            break;
        /* we have to make the buffer bigger. The algorithm used to calculate
           the new size is arbitrary*/
        if (buf != message)
            free(buf);
        if (bufCchSize < 4 * 1024)
            bufCchSize += bufCchSize;
        else
            bufCchSize += 1024;
        buf = (char *)Allocator::AllocMemory(bufCchSize);
        if (!buf)
            break;
    }

    if (buf == message)
        buf = zzj::str::Dup(message);

    return buf;
}
inline bool IsUTF8(const char *string)
{
    if (!string)
        return true;

    const unsigned char *bytes = (const unsigned char *)string;
    unsigned int cp;
    int num;

    while (*bytes != 0x00)
    {
        if ((*bytes & 0x80) == 0x00)
        {
            // U+0000 to U+007F
            cp  = (*bytes & 0x7F);
            num = 1;
        }
        else if ((*bytes & 0xE0) == 0xC0)
        {
            // U+0080 to U+07FF
            cp  = (*bytes & 0x1F);
            num = 2;
        }
        else if ((*bytes & 0xF0) == 0xE0)
        {
            // U+0800 to U+FFFF
            cp  = (*bytes & 0x0F);
            num = 3;
        }
        else if ((*bytes & 0xF8) == 0xF0)
        {
            // U+10000 to U+10FFFF
            cp  = (*bytes & 0x07);
            num = 4;
        }
        else
            return false;

        bytes += 1;
        for (int i = 1; i < num; ++i)
        {
            if ((*bytes & 0xC0) != 0x80)
                return false;
            cp = (cp << 6) | (*bytes & 0x3F);
            bytes += 1;
        }

        if ((cp > 0x10FFFF) || ((cp >= 0xD800) && (cp <= 0xDFFF)) || ((cp <= 0x007F) && (num != 1)) ||
            ((cp >= 0x0080) && (cp <= 0x07FF) && (num != 2)) || ((cp >= 0x0800) && (cp <= 0xFFFF) && (num != 3)) ||
            ((cp >= 0x10000) && (cp <= 0x1FFFFF) && (num != 4)))
            return false;
    }

    return true;
}
inline std::string w2utf8(const std::wstring &str)
{
    static std::wstring_convert<std::codecvt_utf8<wchar_t>> strCnv;
    return strCnv.to_bytes(str);
}

inline std::string w2ansi(const std::wstring &str, const std::string &locale = "chs")
{
    typedef deletable_facet<std::codecvt_byname<wchar_t, char, std::mbstate_t>> F;
    static std::wstring_convert<F> strCnv(new F(locale));
    return strCnv.to_bytes(str);
}

inline std::wstring ansi2w(const std::string &str, const std::string &locale = "chs")
{
    typedef deletable_facet<std::codecvt_byname<wchar_t, char, std::mbstate_t>> F;
    static std::wstring_convert<F> strCnv(new F(locale));
    return strCnv.from_bytes(str);
}

inline std::string ansi2utf8(const std::string &str)
{
    if (!str::IsUTF8(str.c_str()))
        return str::w2utf8(str::ansi2w(str));
    return str;
}

inline std::wstring utf82w(const std::string &str)
{
    std::string tmp = ansi2utf8(str);
    static std::wstring_convert<std::codecvt_utf8<wchar_t>> strCnv;
    return strCnv.from_bytes(tmp);
}

inline int HexStrToDecStr(std::string inStr, std::string &outStr)
{
    int result             = 0;
    char *buf              = nullptr;
    unsigned long long sum = 0;
    buf                    = (char *)malloc(inStr.length() * 2);
    if (nullptr == buf)
    {
        result = -1;
        goto exit;
    }
    sum = strtoull(inStr.c_str(), NULL, 16);
    sprintf(buf, "%lld", sum);
    outStr = buf;
exit:
    if (buf)
        free(buf);
    return result;
}
template <class T> inline std::string ToHex(const T &data, bool addPrefix)
{
    std::stringstream sstream;
    sstream << std::hex;
    std::string ret;
    if (typeid(T) == typeid(char) || typeid(T) == typeid(unsigned char) || sizeof(T) == 1)
    {
        sstream << static_cast<int>(data);
        ret = sstream.str();
        if (ret.length() > 2)
        {
            ret = ret.substr(ret.length() - 2, 2);
        }
        else if (ret.length() == 1)
            ret = u8"0" + ret;
    }
    else
    {
        sstream << data;
        ret = sstream.str();
    }
    return (addPrefix ? u8"0x" : u8"") + ret;
}

inline std::string WstrToUTF8Str(const std::wstring &wstr)
{
    return str::w2utf8(wstr);
}

inline std::wstring UTF8Str2Wstr(const std::string &str)
{
    return str::utf82w(str);
}
} // namespace str
} // namespace zzj
