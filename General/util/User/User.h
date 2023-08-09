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
    std::map<std::string, std::string> groupInfo;
    std::string homeDirectory;

#ifdef _WIN32
#else
    std::string uid;
    std::string gid;
#endif
    static std::optional<UserInfo> GetActiveUserInfo();
    static std::optional<UserInfo> GetUserInfoBySessionId(const std::string &sessionId);
    static std::optional<UserInfo> GetUserInfoByUserName(const std::string &userName);
    static std::vector<UserInfo> GetComputerUserInfos();
};
} // namespace zzj