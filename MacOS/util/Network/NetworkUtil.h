#pragma once
#include <set>
#include <string>
namespace zzj
{
class NetworkUtil
{
  public:
    static std::set<std::string> GetDNSServers();
};
} // namespace zzj