#include "System.h"
#include "StrUtil.h"
#ifdef _WIN32
#include <Windows/WinUtilLib/WindowsUtilLib/ProcessHelper.h>
#include <Windows/WinUtilLib/WindowsUtilLib/StrUtil.h>
#include <Windows/WinUtilLib/WindowsUtilLib/SystemHelper.h>
#else
#include <MacOS/util/SystemUtil.h>
#include <MacOS/util/Process.h>
#endif

std::string GetActiveConsoleUserName()
{
#ifdef _WIN32
    return zzj::SystemInfo::GetActiveConsoleUserName();
#else
    return zzj::Computer::GetCurrentUserName();
#endif // _WIN32
}

std::string GetActiveConsoleSessionId()
{
#ifdef _WIN32
    return zzj::SystemInfo::GetActiveConsoleSessionId();
#else
    return zzj::Computer::GetActiveConsoleSessionId();
#endif // _WIN32
}
std::string GetActiveUserSid()
{
#ifdef _WIN32
    zzj::ActiveExplorerInfo explorerInfo;
    bool res = zzj::Process::GetActiveExplorerInfo(&explorerInfo);
    if (!res)
        return "";
    return str::Wstr2Str(explorerInfo.UserSid);
#else
    std::vector<std::string> args;
    args.push_back("localhost");
    args.push_back("-read");
    args.push_back("/Search/Users/"+GetActiveConsoleUserName());
    args.push_back("|");
    args.push_back("grep");
    args.push_back("GeneratedUID");
    args.push_back("|");
    args.push_back("cut");
    args.push_back("-c15-");
    std::wstring out;
    int result=zzj::Process::CreateUserProcess("/usr/bin/dscl", GetActiveConsoleUserName().c_str(), args, out);
    if ( 0!=result)
        return "";
    
    if(auto iter = out.find(L"\n"); iter != std::wstring::npos)
        out.erase(iter);
    std::string ret =WstrToUTF8Str(out);
    //libconfig must begin with letter.
    ret = std::string("p-")+ret;
    return ret;
#endif // _WIN32
}