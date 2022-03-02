#ifndef _G_STRUTIL_H_
#define _G_STRUTIL_H_

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

class Base64Help
{
  public:
    enum ErrorCode
    {
        SUCCESS,
        BUFFER_TOO_SMALL,
        INVALID_BASE64
    };
    static std::string Encode(const char *data, size_t size)
    {
        static constexpr char sEncodingTable[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
                                                  'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
                                                  'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
                                                  'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
                                                  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'};

        size_t in_len  = size;
        size_t out_len = 4 * ((in_len + 2) / 3);
        std::string ret(out_len, '\0');
        size_t i;
        char *p = const_cast<char *>(ret.c_str());

        for (i = 0; i < in_len - 2; i += 3)
        {
            *p++ = sEncodingTable[(data[i] >> 2) & 0x3F];
            *p++ = sEncodingTable[((data[i] & 0x3) << 4) | ((int)(data[i + 1] & 0xF0) >> 4)];
            *p++ = sEncodingTable[((data[i + 1] & 0xF) << 2) | ((int)(data[i + 2] & 0xC0) >> 6)];
            *p++ = sEncodingTable[data[i + 2] & 0x3F];
        }
        if (i < in_len)
        {
            *p++ = sEncodingTable[(data[i] >> 2) & 0x3F];
            if (i == (in_len - 1))
            {
                *p++ = sEncodingTable[((data[i] & 0x3) << 4)];
                *p++ = '=';
            }
            else
            {
                *p++ = sEncodingTable[((data[i] & 0x3) << 4) | ((int)(data[i + 1] & 0xF0) >> 4)];
                *p++ = sEncodingTable[((data[i + 1] & 0xF) << 2)];
            }
            *p++ = '=';
        }

        return ret;
    }

    static int Decode(const std::string &input, char *buf, size_t &bufSize)
    {
        static constexpr unsigned char kDecodingTable[] = {
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63, 52, 53, 54, 55,
            56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64, 64, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
            13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64, 64, 26, 27, 28, 29, 30, 31, 32,
            33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64};

        size_t in_len = input.size();
        if (in_len % 4 != 0)
            return INVALID_BASE64;

        size_t out_len = in_len / 4 * 3;
        if (input[in_len - 1] == '=')
            out_len--;
        if (input[in_len - 2] == '=')
            out_len--;

        if (bufSize < out_len)
        {
            bufSize = out_len;
            return BUFFER_TOO_SMALL;
        }

        for (size_t i = 0, j = 0; i < in_len;)
        {
            uint32_t a = input[i] == '=' ? 0 & i++ : kDecodingTable[static_cast<int>(input[i++])];
            uint32_t b = input[i] == '=' ? 0 & i++ : kDecodingTable[static_cast<int>(input[i++])];
            uint32_t c = input[i] == '=' ? 0 & i++ : kDecodingTable[static_cast<int>(input[i++])];
            uint32_t d = input[i] == '=' ? 0 & i++ : kDecodingTable[static_cast<int>(input[i++])];

            uint32_t triple = (a << 3 * 6) + (b << 2 * 6) + (c << 1 * 6) + (d << 0 * 6);

            if (j < out_len)
                buf[j++] = (triple >> 2 * 8) & 0xFF;
            if (j < out_len)
                buf[j++] = (triple >> 1 * 8) & 0xFF;
            if (j < out_len)
                buf[j++] = (triple >> 0 * 8) & 0xFF;
        }

        return SUCCESS;
    }
};

class StrCoding
{
  public:
    StrCoding(void);
    ~StrCoding(void);

    // url_gb2312 ����
    string UrlGB2312(char *str);
    // url_utf8 ����
    string UrlUTF8(const char *str);
    // url_utf8 ����
    string UrlUTF8Decode(string str);
    // url_gb2312 ����
    string UrlGB2312Decode(string str);

  private:
    char CharToInt(char ch)
    {
        if (ch >= '0' && ch <= '9')
            return (char)(ch - '0');
        if (ch >= 'a' && ch <= 'f')
            return (char)(ch - 'a' + 10);
        if (ch >= 'A' && ch <= 'F')
            return (char)(ch - 'A' + 10);
        return -1;
    }
    char StrToBin(char *str)
    {
        char tempWord[2];
        char chn;

        tempWord[0] = CharToInt(str[0]);                // make the B to 11 -- 00001011
        tempWord[1] = CharToInt(str[1]);                // make the 0 to 0 -- 00000000
        chn         = (tempWord[0] << 4) | tempWord[1]; // to change the BO to 10110000

        return chn;
    }
};

bool HasEnding(std::string const &fullString, std::string const &ending);

namespace str
{
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

std::string StrToUTF8(const std::string &str);
inline std::wstring utf82w(const std::string &str)
{
    std::string tmp = StrToUTF8(str);
    static std::wstring_convert<std::codecvt_utf8<wchar_t>> strCnv;
    return strCnv.from_bytes(tmp);
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

inline std::string StrToUTF8(const std::string &str)
{
    if (!str::IsUTF8(str.c_str()))
        return str::w2utf8(str::ansi2w(str));
    return str;
}
}; // namespace str

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
#endif
