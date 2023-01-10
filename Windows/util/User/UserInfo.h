#pragma once
#include <string>

namespace zzj
{
class UserInfo
{
  public:
    std::string userName;
    std::string domainName;
    std::string sid;
    static UserInfo GetActiveUserInfo();
};
};