#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace zzj
{
class UserInfo
{
  public:
    std::string userName;
    std::string sid;
    std::string homeDirectory;

#ifdef _WIN32
    std::optional<std::string> domainGUID;
    static std::optional<std::map<std::string, std::string>> GetUserGlobalGourpNameAndSidByName(
        const std::string &userName);
#else
    std::string uid;
    std::string gid;
#endif
    static std::optional<UserInfo> GetActiveUserInfo();
    static std::optional<UserInfo> GetUserInfoBySessionId(const std::string &sessionId);
    static std::optional<UserInfo> GetUserInfoByUserName(const std::string &userName);
    static std::vector<UserInfo> GetComputerUserInfos();
    static std::optional<std::map<std::string, std::string>> GetUserLocalGourpNameAndSidByName(
        const std::string &userName);
};
} // namespace zzj