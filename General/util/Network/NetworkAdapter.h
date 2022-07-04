#pragma once
#include <ostream>
#include <string>
#include <vector>
namespace zzj
{
class NetworkAdapter
{
  public:
    static std::vector<NetworkAdapter> GetNetworkAdapters();

    std::vector<std::string> ipAddrs;
    std::string macAddr;
    std::string name;
    std::string description;
};
class NetworkHelper
{
    public:
        static int GetOutIpAddress(std::string &ipAddr,const std::string& toIP = "8.8.8.8");
};
} // namespace zzj