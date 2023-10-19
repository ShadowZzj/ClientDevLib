#import <Collaboration/Collaboration.h>
#import <CoreServices/CoreServices.h>
#import <Foundation/Foundation.h>
#include <General/util/StrUtil.h>
#include <General/util/System/System.h>
#include <General/util/User/User.h>
#include <MacOS/util/Process.h>
#include <MacOS/util/SystemUtil.h>
#import <OpenDirectory/OpenDirectory.h>
#import <Security/SecBase.h>
#import <grp.h>
#include <optional>
#include <pwd.h>
#include <spdlog/spdlog.h>
#include <uuid/uuid.h>
namespace zzj
{
std::optional<std::map<std::string, std::string>> UserInfo::GetUserLocalGourpNameAndSidByName(
    const std::string &userName)
{
    gid_t primary_gid;
    int ngroups = 100;

    gid_t *groups = (gid_t *)malloc(ngroups * sizeof(gid_t));
    struct passwd *pw;
    pw = getpwnam(userName.c_str());
    // 获取用户所属的所有组的ID
    getgrouplist(userName.c_str(), pw->pw_gid, (int *)groups, &ngroups);
    std::map<std::string, std::string> ret;
    for (int i = 0; i < ngroups; i++)
    {
        struct group *grp                = getgrgid(groups[i]);
        ret[std::to_string(grp->gr_gid)] = grp->gr_name;
    }

    free(groups);
    return ret;
}

std::optional<std::string> GetSidByName(const std::string &name)
{
    @autoreleasepool
    {
        NSError *error = nil;

        ODSession *session = [ODSession defaultSession];
        ODNode *node       = [ODNode nodeWithSession:session name:@"/Search" error:&error];

        if (error)
        {
            auto errorDes = [error localizedDescription];
            spdlog::error("Error creating node: {}", [errorDes UTF8String]);
            return std::nullopt;
        }

        ODQuery *query = [ODQuery queryWithNode:node
                                 forRecordTypes:kODRecordTypeUsers
                                      attribute:kODAttributeTypeRecordName
                                      matchType:kODMatchEqualTo
                                    queryValues:[NSString stringWithUTF8String:name.c_str()]
                               returnAttributes:@"dsAttrTypeStandard:GeneratedUID"
                                 maximumResults:0
                                          error:&error];

        if (error)
        {
            spdlog::error("Error creating query: {}", [error.localizedDescription UTF8String]);
            return std::nullopt;
        }

        NSArray *results = [query resultsAllowingPartial:NO error:&error];

        if (error)
        {
            spdlog::error("Error executing query: {}", [error.localizedDescription UTF8String]);
            return std::nullopt;
        }

        if (results.count == 0)
        {
            return std::nullopt;
        }

        ODRecord *record   = results[0];
        NSArray *uidValues = [record valuesForAttribute:@"dsAttrTypeStandard:GeneratedUID" error:nil];

        if (uidValues.count > 0)
        {
            return [uidValues[0] UTF8String];
        }

        return std::nullopt;
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
    {
        spdlog::info("GetActiveSessionId sessionId is empty");
        return {};
    }
}
std::optional<UserInfo> UserInfo::GetUserInfoBySessionId(const std::string &sessionId)
{
    @autoreleasepool
    {
        if (sessionId.empty())
            return {};
        struct passwd *pwd = getpwuid(atoi(sessionId.c_str()));
        if (!pwd)
        {
            spdlog::error("getpwuid failed, sessionId: {}", sessionId);
            return {};
        }

        std::string userName = pwd->pw_name;
        return GetUserInfoByUserName(userName);
    }
}
std::optional<UserInfo> UserInfo::GetUserInfoByUserName(const std::string &userName)
{
    UserInfo ret;
    ret.userName = userName;
    auto tmp     = GetSidByName(userName);
    if (!tmp.has_value())
    {
        spdlog::error("GetSidByName failed, userName: {}", userName);
        return {};
    }
    ret.sid = *tmp;

    tmp = GetUserHomeDirectoryByName(userName);
    if (!tmp.has_value())
    {
        spdlog::error("GetUserHomeDirectoryByName failed, userName: {}", userName);
        return {};
    }

    ret.homeDirectory = *tmp;

    struct passwd *pwd = getpwnam(userName.c_str());
    if (!pwd)
    {
        spdlog::error("getpwnam failed, userName: {}", userName);
        return {};
    }
    ret.uid = std::to_string(pwd->pw_uid);
    ret.gid = std::to_string(pwd->pw_gid);
    return ret;
}
} // namespace zzj
