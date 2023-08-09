#import <Collaboration/Collaboration.h>
#import <CoreServices/CoreServices.h>
#import <Foundation/Foundation.h>
#include <General/util/System/System.h>
#include <General/util/User/User.h>
#include <General/util/StrUtil.h>
#include <MacOS/util/Process.h>
#include <MacOS/util/SystemUtil.h>
#import <Security/SecBase.h>
#import <grp.h>
#include <optional>
#import <pwd.h>

namespace zzj
{
std::optional<std::map<std::string, std::string>> GetUserGourpNameAndSidByName(const std::string &userName)
{
    gid_t primary_gid;
    int ngroups = 100;
    
    gid_t *groups = (gid_t *) malloc(ngroups * sizeof(gid_t));
    if (groups == NULL) {
        return {};
    }
    struct passwd* pw;
    pw = getpwnam(userName.c_str());
    // 获取用户所属的所有组的ID
    getgrouplist(userName.c_str(), pw->pw_gid, (int*)groups, &ngroups);
    std::map<std::string, std::string> ret;
    for (int i = 0; i < ngroups; i++) {
        struct group *grp = getgrgid(groups[i]);
        ret[std::to_string(grp->gr_gid)] = grp->gr_name;
    }
    
    free(groups);
    return ret;
}
std::optional<std::string> GetSidByName(const std::string &name)
{
    static std::mutex lck;
    std::lock_guard<std::mutex> guard(lck);
    static std::map<std::string, std::string> userSidMap;

    auto it = userSidMap.find(name);
    if (it == userSidMap.end())
    {
        std::vector<std::string> args;
        args.push_back("localhost");
        args.push_back("-read");
        args.push_back("/Search/Users/" + name);
        args.push_back("GeneratedUID");
        args.push_back("|");
        args.push_back("cut");
        args.push_back("-c15-");
        std::wstring out;
        int result = zzj::Process::CreateUserProcess("/usr/bin/dscl", name.c_str(), args, out);
        if (0 != result)
            return {};

        if (auto iter = out.find(L"\n"); iter != std::wstring::npos)
            out.erase(iter);
        std::string ret = zzj::str::w2utf8(out);

        userSidMap.emplace(name, ret);
        return ret;
    }
    else
    {
        return it->second;
    }
}
std::optional<std::string> GetUserHomeDirectoryByName(const std::string &userName)
{
    @autoreleasepool
    {
        std::optional<std::string> ret;
        auto homeDir = NSHomeDirectoryForUser([NSString stringWithUTF8String:userName.c_str()]);
        if (homeDir != nil)
            ret = [homeDir UTF8String];
        return ret;
    }
}
std::optional<UserInfo> UserInfo::GetActiveUserInfo()
{
    auto sessionId = Session::GetActiveSessionId();
    if (sessionId.has_value())
        return GetUserInfoBySessionId(*sessionId);
    else
        return {};
}
std::optional<UserInfo> UserInfo::GetUserInfoBySessionId(const std::string &sessionId)
{
    @autoreleasepool
    {
        if(sessionId.empty())
            return {};
        struct passwd *pwd = getpwuid(atoi(sessionId.c_str()));
        if (!pwd)
            return {};

        std::string userName = pwd->pw_name;
        return GetUserInfoByUserName(userName);
    }
}
std::optional<UserInfo> UserInfo::GetUserInfoByUserName(const std::string &userName)
{
    UserInfo ret;
    ret.userName      = userName;
    auto tmp =GetSidByName(userName);
    if(!tmp.has_value())
        return {};
    ret.sid           = *tmp;
    
    tmp =GetUserHomeDirectoryByName(userName);
    if(!tmp.has_value())
        return {};
    
    ret.homeDirectory = *tmp;
    
    auto tmp2 = GetUserGourpNameAndSidByName(userName);
    if(!tmp2.has_value())
        return {};
    
    ret.groupInfo     = *tmp2;
    
    struct passwd *pwd = getpwnam(userName.c_str());
    if (!pwd)
    {
        return {};
    }
    ret.uid = std::to_string(pwd->pw_uid);
    ret.gid = std::to_string(pwd->pw_gid);
    return ret;
}
} // namespace zzj
