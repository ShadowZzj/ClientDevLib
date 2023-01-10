#ifndef _G_TIME_H_
#define _G_TIME_H_

#include <ctime>
#include <iostream>
#include <string>

inline std::string GetCurrentTimeString()
{

    time_t now      = time(0);
    char *dt        = ctime(&now);
    std::string ret = dt;
    ret             = ret.substr(0, ret.find("\n"));
    return ret;
}

#endif