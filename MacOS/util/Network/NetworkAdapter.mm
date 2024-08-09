#include <General/util/BaseUtil.hpp>
#include <General/util/Network/NetworkAdapter.h>
#include <General/util/StrUtil.h>
#import <SystemConfiguration/SystemConfiguration.h>
#import <CoreFoundation/CoreFoundation.h>
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
#include <spdlog/spdlog.h>
using namespace zzj;
int SetDNSAutomatic(const std::string &interfaceName)
{
    SCPreferencesRef prefs = SCPreferencesCreate(NULL, CFSTR("SetDNSAutomatic"), NULL);
    if (prefs == NULL)
    {
        spdlog::error("Failed to create SCPreferencesRef");
        return -1;
    }
    DEFER{CFRelease(prefs);};
    // 获取网络服务
    CFArrayRef services = SCNetworkServiceCopyAll(prefs);
    if (services == NULL)
    {
        spdlog::error("Failed to copy network services");
        return -2;
    }
    DEFER{CFRelease(services);};

    CFIndex count = CFArrayGetCount(services);
    for (CFIndex i = 0; i < count; i++)
    {
        SCNetworkServiceRef service = (SCNetworkServiceRef)CFArrayGetValueAtIndex(services, i);

        // 获取接口
        SCNetworkInterfaceRef interfaceRef = SCNetworkServiceGetInterface(service);
        if (interfaceRef == NULL)
        {
            continue;
        }
        
        // 获取接口名
        CFStringRef bsdName = SCNetworkInterfaceGetBSDName(interfaceRef);
        if (bsdName == NULL)
        {
            continue;
        }
        char buffer[256];
        if (CFStringGetCString(bsdName, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
            std::string currentInterfaceName = std::string(buffer);
            if (currentInterfaceName != interfaceName)
            {
                continue;
            }
        } else {
            // 处理无法转换为 C 字符串的情况
            continue;
        }

        // 获取 IPv4 配置
        SCNetworkProtocolRef protocol =
            SCNetworkServiceCopyProtocol(service, kSCNetworkProtocolTypeDNS);
        if (protocol == NULL)
        {
            spdlog::error("Failed to get DNS protocol");
            continue;
        }
        DEFER{CFRelease(protocol);};
        // 设置 DNS 为自动获取
        CFDictionaryRef defaultConfig = SCNetworkProtocolGetConfiguration(protocol);
        if (defaultConfig != NULL)
        {
            CFMutableDictionaryRef newConfig =
                CFDictionaryCreateMutableCopy(NULL, 0, defaultConfig);
            CFDictionaryRemoveValue(newConfig, kSCPropNetDNSServerAddresses);
            SCNetworkProtocolSetConfiguration(protocol, newConfig);
            CFRelease(newConfig);
        }
        else
        {
            CFMutableDictionaryRef newConfig = CFDictionaryCreateMutable(NULL, 0, NULL, NULL);
            SCNetworkProtocolSetConfiguration(protocol, newConfig);
            CFRelease(newConfig);
        }

        // 保存更改
        if (!SCPreferencesCommitChanges(prefs))
        {
            spdlog::error("Failed to commit changes");
        }
        if (!SCPreferencesApplyChanges(prefs))
        {
            spdlog::error("Failed to apply changes");
        }
    }
    
    return 0;
}
std::string stdStringFromCF(CFStringRef s)
{
    if (auto fastCString = CFStringGetCStringPtr(s, kCFStringEncodingUTF8))
    {
        return std::string(fastCString);
    }
    auto utf16length = CFStringGetLength(s);
    auto maxUtf8len = CFStringGetMaximumSizeForEncoding(utf16length, kCFStringEncodingUTF8);
    std::string converted(maxUtf8len, '\0');

    CFStringGetCString(s, converted.data(), maxUtf8len, kCFStringEncodingUTF8);
    converted.resize(std::strlen(converted.data()));

    return converted;
}
std::string NetworkAdapter::GetDefaultGatewayAdapterName()
{
    FILE *fp = popen("netstat -rn", "r");
    if (fp == nullptr)
    {
        return "";
    }

    char line[256];
    while (fgets(line, sizeof(line), fp) != nullptr)
    {
        if (strstr(line, "default") || strstr(line, "0.0.0.0"))
        {
            std::istringstream iss(line);
            std::string gateway, dest, flags, iface;
            iss >> dest >> gateway >> flags >> iface;
            if (!iface.empty())
            {
                pclose(fp);
                return iface;
            }
        }
    }
    pclose(fp);
    return "";
}
std::vector<std::string> ConvertCFArrayToStdVector(CFArrayRef cfArray)
{
    std::vector<std::string> stdVector;
    
    if (cfArray) {
        CFIndex count = CFArrayGetCount(cfArray);
        
        for (CFIndex i = 0; i < count; ++i) {
            CFStringRef cfString = (CFStringRef)CFArrayGetValueAtIndex(cfArray, i);
            if (cfString) {
                const char* cStr = CFStringGetCStringPtr(cfString, kCFStringEncodingUTF8);
                if (!cStr) {
                    // If direct conversion fails, we can create a temporary buffer
                    char buffer[256];
                    if (CFStringGetCString(cfString, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
                        stdVector.push_back(buffer);
                    }
                } else {
                    stdVector.push_back(cStr);
                }
            }
        }
    }
    
    return stdVector;
}
void NetworkAdapter::GetDnsInfo(SCNetworkServiceRef service)
{
    CFArrayRef result = NULL;
    CFStringRef interfaceServiceID = SCNetworkServiceGetServiceID(service);
    // If the user has added custom DNS servers, then we have to use this path to find them:
    CFStringRef servicePath = CFStringCreateWithFormat(NULL, NULL, CFSTR("Setup:/Network/Service/%@/DNS"),
                                                       interfaceServiceID);
    SCDynamicStoreRef dynamicStoreRef = SCDynamicStoreCreate(kCFAllocatorSystemDefault,
                                                             CFSTR("your.app.name.here"), NULL, NULL);
    CFDictionaryRef propList = (CFDictionaryRef)SCDynamicStoreCopyValue(dynamicStoreRef, servicePath);
    CFRelease(servicePath);
    
    if (!propList) {
        // In this case, the user has not added custom DNS servers and we use this path to
        // get the default DNS servers:
        isDynamicDns = true;
        servicePath = CFStringCreateWithFormat(NULL, NULL, CFSTR("State:/Network/Service/%@/DNS"),
                                               interfaceServiceID);
        propList = (CFDictionaryRef)SCDynamicStoreCopyValue(dynamicStoreRef, servicePath);
        CFRelease(servicePath);
    }
    
    if (propList) {
        result = (CFArrayRef)CFDictionaryGetValue(propList, CFSTR("ServerAddresses"));
        //convert CFArrayRef to a std::vector<std::string> 
        ipv4DnsAddrs =  ConvertCFArrayToStdVector(result);
        CFRelease(propList);
    }
    
    CFRelease(dynamicStoreRef);
    
    return;
}
std::string GetNetworkServiceGateway(SCNetworkServiceRef service)
{
    CFStringRef result = NULL;
    CFStringRef interfaceServiceID = SCNetworkServiceGetServiceID(service);
    CFStringRef servicePath = CFStringCreateWithFormat(
        NULL, NULL, CFSTR("State:/Network/Service/%@/IPv4"), interfaceServiceID);
    SCDynamicStoreRef dynamicStoreRef =
        SCDynamicStoreCreate(kCFAllocatorSystemDefault, CFSTR("ClientDevLib"), NULL, NULL);
    CFDictionaryRef propList =
        (CFDictionaryRef)SCDynamicStoreCopyValue(dynamicStoreRef, servicePath);
    
    std::string res;
    if (propList)
    {
        result = (CFStringRef)CFDictionaryGetValue(propList, CFSTR("Router"));
        if(result)
            res = stdStringFromCF(result);
        CFRelease(propList);
    }

    CFRelease(servicePath);
    CFRelease(dynamicStoreRef);

    
    return res;
}


std::vector<NetworkAdapter> NetworkAdapter::SCGetNetworkAdapters()
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
    DEFER{CFRelease(list);};
    count = CFArrayGetCount(list);
    SCPreferencesRef prefs = SCPreferencesCreate(NULL, CFSTR("ClientDevLib"), NULL);
    SCNetworkSetRef currentSet = SCNetworkSetCopyCurrent(prefs);
    CFArrayRef services = SCNetworkSetCopyServices(currentSet);
    DEFER
    {
        CFRelease(services);
        CFRelease(currentSet);
        CFRelease(prefs);
    };
    for (CFIndex j = 0; j < CFArrayGetCount(services); j++)
    {
        SCNetworkServiceRef service = (SCNetworkServiceRef)CFArrayGetValueAtIndex(services, j);
        SCNetworkInterfaceRef this_if = SCNetworkServiceGetInterface(service);
        CFStringRef this_if_name;
        NetworkAdapter adapter;

        this_if_name = SCNetworkInterfaceGetBSDName(this_if);
        if (this_if_name) adapter.name = stdStringFromCF(this_if_name);

        CFStringRef interfaceType = SCNetworkInterfaceGetInterfaceType(this_if);
        if (CFStringCompare(interfaceType, kSCNetworkInterfaceTypeEthernet, 0) == kCFCompareEqualTo)
            adapter.adapterType = NetworkAdapter::Type::Wired;
        else if (CFStringCompare(interfaceType, kSCNetworkInterfaceTypeIEEE80211, 0) ==
                 kCFCompareEqualTo)
            adapter.adapterType = NetworkAdapter::Type::Wireless;
        else
            adapter.adapterType = NetworkAdapter::Type::Other;

        CFStringRef localizedName = SCNetworkInterfaceGetLocalizedDisplayName(this_if);
        adapter.friendlyName = stdStringFromCF(localizedName);

        CFStringRef hardwareAddress = SCNetworkInterfaceGetHardwareAddressString(this_if);
        if (hardwareAddress != NULL) adapter.macAddr = stdStringFromCF(hardwareAddress);

        int mtuCur;
        int mtuMin;
        int mtuMax;
        bool bRet = SCNetworkInterfaceCopyMTU(this_if, &mtuCur, &mtuMin, &mtuMax);
        if (bRet) adapter.mtu = mtuCur;

        // get the adapter  gateway setting
        std::string gateWay = GetNetworkServiceGateway(service);
        if(!gateWay.empty())
            adapter.ipv4GatewayAddrs.push_back(gateWay);
        adapter.GetDnsInfo(service);
        ret.push_back(adapter);
    }
    return ret;
}
std::vector<NetworkAdapter> NetworkAdapter::GetNetworkAdapters()
{
    std::vector<NetworkAdapter> ret = SCGetNetworkAdapters();
    if (ret.size() == 0) return {};

    int success = 0;
    struct ifaddrs *interfaces = NULL;
    struct ifaddrs *temp_addr = NULL;
    success = getifaddrs(&interfaces);
    // Get all ip adresses
    if (success == 0)
    {
        temp_addr = interfaces;
        while (temp_addr != NULL)
        {
            if (!temp_addr->ifa_name)
            {
                temp_addr = temp_addr->ifa_next;
                continue;
            }
            if (temp_addr->ifa_addr->sa_family != AF_INET)
            {
                temp_addr = temp_addr->ifa_next;
                continue;
            }
            if (auto iter =
                    std::find_if(ret.begin(), ret.end(), [temp_addr](const NetworkAdapter &adapter)
                                 { return adapter.name == std::string(temp_addr->ifa_name); });
                iter != ret.end())
            {
                if (temp_addr->ifa_addr != nullptr)
                {
                    iter->ipv4UniAddrs.push_back(std::string(
                        inet_ntoa(((struct sockaddr_in *)temp_addr->ifa_addr)->sin_addr)));
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
    timeout.tv_sec = 2;   // Timeout in seconds
    timeout.tv_usec = 0;  // Not setting microseconds

    destination_address = toIP.c_str();
    Addr = {0};
    addr = inet_addr(destination_address);
    Addr.sin_addr.s_addr = addr;
    Addr.sin_family = AF_INET;
    Addr.sin_port = htons(9);  // 9 is discard port
    sockHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
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
    AddrLen = sizeof(Addr);
    result = connect(sockHandle, (sockaddr *)&Addr, AddrLen);
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
    ipAddr = source_address;
exit:
    if (sockHandle > 0) close(sockHandle);
    return result;
}
int NetworkHelper::GetAllIpv4(std::vector<std::string> &ipv4List)
{
    int result = 0;
    struct ifaddrs *interfaces = NULL;
    struct ifaddrs *temp_addr = NULL;
    int success = 0;
    success = getifaddrs(&interfaces);
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

int NetworkAdapter::SetDynamicDns() const 
{
    if(isDynamicDns)
        return 0;
    return SetDNSAutomatic(name);
}
