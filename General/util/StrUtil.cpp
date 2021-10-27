#include "StrUtil.h"
// 把str编码为网页中的 UTF-8 url encode ,英文不变，汉字三字节 如%3D%AE%88
std::string StrCoding::UrlUTF8(const char *str)
{
    string tt = str;
    string dd;

    size_t len = tt.length();
    for (size_t i = 0; i < len; i++)
    {
        if (isalnum((unsigned char)tt.at(i)))
        {
            char tempbuff[2] = {0};
            sprintf(tempbuff, "%c", (unsigned char)tt.at(i));
            dd.append(tempbuff);
        }
        else if (isspace((unsigned char)tt.at(i)))
        {
            dd.append("+");
        }
        else
        {
            char tempbuff[4];
            sprintf(tempbuff, "%%%X%X", ((unsigned char)tt.at(i)) >> 4, ((unsigned char)tt.at(i)) % 16);
            dd.append(tempbuff);
        }
    }
    return dd;
}

bool HasEnding(std::string const &fullString, std::string const &ending)
{
    if (fullString.length() >= ending.length())
    {
        return (0 == fullString.compare(fullString.length() - ending.length(), ending.length(), ending));
    }
    else
    {
        return false;
    }
}