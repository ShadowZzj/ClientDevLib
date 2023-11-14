#include <General/util/BaseUtil.hpp>
#include <General/util/Network/NetworkAdapter.h>
#include <General/util/StrUtil.h>
#import <SystemConfiguration/SystemConfiguration.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

using namespace zzj;
std::string stdStringFromCF(CFStringRef s)
{
    if (auto fastCString = CFStringGetCStringPtr(s, kCFStringEncodingUTF8))
    {
        return std::string(fastCString);
    }
    auto utf16length = CFStringGetLength(s);
    auto maxUtf8len  = CFStringGetMaximumSizeForEncoding(utf16length, kCFStringEncodingUTF8);
    std::string converted(maxUtf8len, '\0');

    CFStringGetCString(s, converted.data(), maxUtf8len, kCFStringEncodingUTF8);
    converted.resize(std::strlen(converted.data()));

    return converted;
}

std::vector<NetworkAdapter> SCGetNetworkAdapters()
{
    long count = 0;
    long i;
    std::vector<NetworkAdapter> ret;
    CFArrayRef list;

    list = SCNetworkInterfaceCopyAll();
    if (list == NULL)
    {
        return {};
    }
    count = CFArrayGetCount(list);

    for (i = 0; i < count; i++)
    {
        NetworkAdapter adapter;
        SCNetworkInterfaceRef this_if;
        CFStringRef this_if_name;

        this_if      = (SCNetworkInterfaceRef)CFArrayGetValueAtIndex(list, i);
        this_if_name = SCNetworkInterfaceGetBSDName(this_if);
        if (this_if_name)
            adapter.name = stdStringFromCF(this_if_name);
        
        CFStringRef interfaceType = SCNetworkInterfaceGetInterfaceType(this_if);
        if (CFStringCompare(interfaceType, kSCNetworkInterfaceTypeEthernet, 0) == kCFCompareEqualTo)
            adapter.adapterType = NetworkAdapter::Type::Wired;
        else if (CFStringCompare(interfaceType, kSCNetworkInterfaceTypeIEEE80211, 0) == kCFCompareEqualTo)
            adapter.adapterType = NetworkAdapter::Type::Wireless;
        else
            adapter.adapterType = NetworkAdapter::Type::Other;

        CFStringRef localizedName = SCNetworkInterfaceGetLocalizedDisplayName(this_if);
        adapter.friendlyName = stdStringFromCF(localizedName);
        
        CFStringRef hardwareAddress = SCNetworkInterfaceGetHardwareAddressString(this_if);
        if (hardwareAddress != NULL)
            adapter.macAddr = stdStringFromCF(hardwareAddress);
        
        int mtuCur;
        int mtuMin;
        int mtuMax;
        bool bRet =SCNetworkInterfaceCopyMTU(this_if,&mtuCur,&mtuMin,&mtuMax);
        if(bRet)
            adapter.mtu = mtuCur;
        ret.push_back(adapter);
    }
    
    CFRelease(list);
    return ret;
}
std::vector<NetworkAdapter> NetworkAdapter::GetNetworkAdapters()
{
    std::vector<NetworkAdapter> ret = SCGetNetworkAdapters();
    if(ret.size() == 0)
        return {};
    int success                = 0;
    struct ifaddrs *interfaces = NULL;
    struct ifaddrs *temp_addr  = NULL;
    success                    = getifaddrs(&interfaces);
    // Get all ip adresses
    if (success == 0)
    {
        temp_addr = interfaces;
        while (temp_addr != NULL)
        {
            if(!temp_addr->ifa_name)
            {
                temp_addr = temp_addr->ifa_next;
                continue;
            }
            if(temp_addr->ifa_addr->sa_family != AF_INET)
            {
                temp_addr = temp_addr->ifa_next;
                continue;
            }
            if(auto iter = std::find_if(ret.begin(), ret.end(), [temp_addr](const NetworkAdapter& adapter){return adapter.name == std::string(temp_addr->ifa_name);});iter != ret.end())
            {
                if (temp_addr->ifa_addr != nullptr)
                {
                    
                    iter->ipv4UniAddrs.push_back(
                        std::string(inet_ntoa(((struct sockaddr_in *)temp_addr->ifa_addr)->sin_addr)));
                    iter->status = zzj::NetworkAdapter::NetworkStatus::Up;
                }
            }
            
            
            temp_addr = temp_addr->ifa_next;
        }
        freeifaddrs(interfaces);
    }
    
    return ret;
}
int NetworkHelper::GetOutIpAddress(std::string &ipAddr, const std::string &toIP)
{
    int result = 0;
    const char *destination_address;
    sockaddr_in Addr;
    unsigned long addr;
    char *source_address;
    int sockHandle = 0;
    int AddrLen;
    struct timeval timeout;
    timeout.tv_sec = 2;  // Timeout in seconds
    timeout.tv_usec = 0;          // Not setting microseconds

    destination_address  = toIP.c_str();
    Addr                 = {0};
    addr                 = inet_addr(destination_address);
    Addr.sin_addr.s_addr = addr;
    Addr.sin_family      = AF_INET;
    Addr.sin_port        = htons(9); // 9 is discard port
    sockHandle           = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockHandle < 0)
    {
        result = -1;
        goto exit;
    }
    if (setsockopt(sockHandle, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
    {
        result = -2;
        goto exit;
    }
    AddrLen              = sizeof(Addr);
    result               = connect(sockHandle, (sockaddr *)&Addr, AddrLen);
    if (0 != result)
    {
        result = -3;
        goto exit;
    }
    result = getsockname(sockHandle, (sockaddr *)&Addr, (socklen_t *)&AddrLen);
    if (0 != result)
    {
        result = -4;
        goto exit;
    }
    source_address = inet_ntoa(((struct sockaddr_in *)&Addr)->sin_addr);
    ipAddr         = source_address;
exit:
    if (sockHandle > 0)
        close(sockHandle);
    return result;
}
int NetworkHelper::GetAllIpv4(std::vector<std::string> &ipv4List)
{
        int result                 = 0;
        struct ifaddrs *interfaces = NULL;
        struct ifaddrs *temp_addr  = NULL;
        int success                = 0;
        success                    = getifaddrs(&interfaces);
        std::string ipAddr;
        if (success != 0)
        {
            result = -1;
            goto exit;
        }
        temp_addr = interfaces;
        while (temp_addr != NULL)
        {
            if (temp_addr->ifa_addr->sa_family == AF_INET && !(temp_addr->ifa_flags & IFF_LOOPBACK))
            {
                ipAddr = inet_ntoa(((struct sockaddr_in *)temp_addr->ifa_addr)->sin_addr);
                ipv4List.push_back(ipAddr);
            }
            temp_addr = temp_addr->ifa_next;
        }
        freeifaddrs(interfaces);
    exit:
        return result;
}
