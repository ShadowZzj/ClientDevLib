#include "System.h"
#include <map>
#include <General/util/StrUtil.h>
#ifdef _WIN32
#include <Windows/util/Process/ProcessHelper.h>
#include <Windows/util/System/SystemHelper.h>
#include <Windows/util/User/UserInfo.h>
#else
#include <MacOS/util/SystemUtil.h>
#include <MacOS/util/Process.h>
#endif

std::string GetActiveConsoleUserName()
{
#ifdef _WIN32
    zzj::UserInfo userInfo = zzj::UserInfo::GetActiveUserInfo();
    return userInfo.userName;
#else
    return zzj::Computer::GetCurrentUserName();
#endif // _WIN32
}

std::string GetActiveConsoleSessionId()
{
#ifdef _WIN32
    zzj::UserInfo userInfo = zzj::UserInfo::GetActiveUserInfo();
    return userInfo.sid;
#else
    return zzj::Computer::GetActiveConsoleSessionId();
#endif // _WIN32
}

std::string GetActiveUserSid()
{
#ifdef _WIN32
    zzj::UserInfo userInfo = zzj::UserInfo::GetActiveUserInfo();
    return userInfo.sid;
#else
    
    static std::mutex lck;
    std::lock_guard<std::mutex> guard(lck);
    static std::map<std::string ,std::string> userSidMap;
    std::string activeUserName = GetActiveConsoleUserName();
    
    if(activeUserName.empty())
        return "";

    auto it = userSidMap.find(activeUserName);
    if(it == userSidMap.end())
    {
        std::vector<std::string> args;
        args.push_back("localhost");
        args.push_back("-read");
        args.push_back("/Search/Users/" + activeUserName);
        args.push_back("GeneratedUID");
        args.push_back("|");
        args.push_back("cut");
        args.push_back("-c15-");
        std::wstring out;
        int result = zzj::Process::CreateUserProcess("/usr/bin/dscl", activeUserName.c_str(), args, out);
        if (0 != result)
            return "";

        if (auto iter = out.find(L"\n"); iter != std::wstring::npos)
            out.erase(iter);
        std::string ret = str::w2utf8(out);
        ret = std::string("p-") + ret;
        
        
        userSidMap.emplace(activeUserName, ret);
        return ret;
    }
    else
    {
        return it->second;
    }
    
#endif // _WIN32
}