#pragma once
#include <ostream>
#include <string>
#include <vector>
namespace zzj
{
class NetworkAdapter
{
  public:
    enum class Type
    {
        Wireless,
        Wired,
        Loopback,
        Other
    };
    enum class NetworkStatus:unsigned long
    {
        Up = 1,
        DOWN
    };
    static std::vector<NetworkAdapter> GetNetworkAdapters();

    std::vector<std::string> ipv4UniAddrs;
    std::vector<std::string> ipv4AnyAddrs;
    std::vector<std::string> ipv4MultAddrs;
    std::vector<std::string> ipv6UniAddrs;
    std::vector<std::string> ipv6AnyAddrs;
    std::vector<std::string> ipv6MultAddrs;
    std::vector<std::string> ipv4DnsAddrs;
    std::vector<std::string> ipv6DnsAddrs;
    std::vector<std::string> ipv4GatewayAddrs;
    std::vector<std::string> ipv6GatewayAddrs;

    unsigned long flags;
    unsigned long mtu;
    NetworkStatus status;
    std::string dnsSuffixes;
    std::string macAddr;
    std::string name;
    std::string friendlyName;
    std::string description;
    Type adapterType;
};
class NetworkHelper
{
    public:
        static int GetOutIpAddress(std::string &ipAddr,const std::string& toIP = "8.8.8.8");
};
} // namespace zzj