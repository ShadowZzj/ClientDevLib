#ifndef _UTIL_CONVERT
#define _UTIL_CONVENT

#include <string>

inline std::string IntIPToStr(uint32_t ip)
{
    std::string ret;
    char *p = (char*)&ip;
    for (int i = 0; i < 4; i++)
    {
        ret += std::to_string((int)*p);
        p++;
        if (i < 3)
            ret += ".";
    }
    return ret;
}
#endif