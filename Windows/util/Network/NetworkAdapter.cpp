#include <General/util/Network/NetworkAdapter.h>
#include <General/util/BaseUtil.hpp>
#include <General/util/StrUtil.h>
#include <Winsock2.h>
#include <ws2tcpip.h>
#include <IPTypes.h>
#include <iphlpapi.h>
#include <Windows.h>
#include <vector>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32")
using namespace zzj;

std::vector<NetworkAdapter> NetworkAdapter::GetNetworkAdapters()
{
    std::vector<NetworkAdapter> adapters;
    ULONG size              = 1024 * 15;
    PIP_ADAPTER_ADDRESSES p = (IP_ADAPTER_ADDRESSES *)HeapAlloc(GetProcessHeap(), 0, size);
    if (!p)
        return {};
    DEFER
    {
        HeapFree(GetProcessHeap(), 0, p);
    };
    ULONG ret;
    do
    {
        ret = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, p, &size);
        if (ret != ERROR_BUFFER_OVERFLOW)
            break;

        PIP_ADAPTER_ADDRESSES newp = (IP_ADAPTER_ADDRESSES *)HeapReAlloc(GetProcessHeap(), 0, p, size);
        if (!newp)
        {
            return {};
        }

        p = newp;
    } while (true);

    if (ret != NO_ERROR)
    {
        return {};
    }

    for (PIP_ADAPTER_ADDRESSES tp = p; tp != NULL; tp = tp->Next)
    {
        NetworkAdapter adapter;
        adapter.name = tp->AdapterName;
        if (tp->FriendlyName)
            adapter.friendlyName = str::w2utf8(tp->FriendlyName);
        if (tp->Description)
            adapter.description = str::w2utf8(tp->Description);
        switch (tp->IfType)
        {
        case IF_TYPE_SOFTWARE_LOOPBACK:
            adapter.adapterType = NetworkAdapter::Type::Loopback;
            break;
        case IF_TYPE_ETHERNET_CSMACD:
            adapter.adapterType = NetworkAdapter::Type::Wired;
            break;
        case IF_TYPE_IEEE80211:
            adapter.adapterType = NetworkAdapter::Type::Wireless;
            break;
        default:
            adapter.adapterType = NetworkAdapter::Type::Other;
            break;
        }

        for (PIP_ADAPTER_UNICAST_ADDRESS pu = tp->FirstUnicastAddress; pu != NULL; pu = pu->Next)
        {
            // if ipv4
            if (pu->Address.lpSockaddr->sa_family == AF_INET)
            {
                sockaddr_in *si         = (sockaddr_in *)(pu->Address.lpSockaddr);
                char a[INET_ADDRSTRLEN] = {};
                if (inet_ntop(AF_INET, &(si->sin_addr), a, sizeof(a)))
                    adapter.ipv4UniAddrs.push_back(a);
            }
            // if ipv6
            else if (pu->Address.lpSockaddr->sa_family == AF_INET6)
            {
                sockaddr_in6 *si         = (sockaddr_in6 *)(pu->Address.lpSockaddr);
                char a[INET6_ADDRSTRLEN] = {};
                if (inet_ntop(AF_INET6, &(si->sin6_addr), a, sizeof(a)))
                    adapter.ipv6UniAddrs.push_back(a);
            }
        }
        for (PIP_ADAPTER_ANYCAST_ADDRESS pa = tp->FirstAnycastAddress; pa != NULL; pa = pa->Next)
        {
            // if ipv4
            if (pa->Address.lpSockaddr->sa_family == AF_INET)
            {
                sockaddr_in *si         = (sockaddr_in *)(pa->Address.lpSockaddr);
                char a[INET_ADDRSTRLEN] = {};
                if (inet_ntop(AF_INET, &(si->sin_addr), a, sizeof(a)))
                    adapter.ipv4AnyAddrs.push_back(a);
            }
            // if ipv6
            else if (pa->Address.lpSockaddr->sa_family == AF_INET6)
            {
                sockaddr_in6 *si         = (sockaddr_in6 *)(pa->Address.lpSockaddr);
                char a[INET6_ADDRSTRLEN] = {};
                if (inet_ntop(AF_INET6, &(si->sin6_addr), a, sizeof(a)))
                    adapter.ipv6AnyAddrs.push_back(a);
            }
        }

        for (PIP_ADAPTER_MULTICAST_ADDRESS pm = tp->FirstMulticastAddress; pm != NULL; pm = pm->Next)
        {
            // if ipv4
            if (pm->Address.lpSockaddr->sa_family == AF_INET)
            {
                sockaddr_in *si         = (sockaddr_in *)(pm->Address.lpSockaddr);
                char a[INET_ADDRSTRLEN] = {};
                if (inet_ntop(AF_INET, &(si->sin_addr), a, sizeof(a)))
                    adapter.ipv4MultAddrs.push_back(a);
            }
            // if ipv6
            else if (pm->Address.lpSockaddr->sa_family == AF_INET6)
            {
                sockaddr_in6 *si         = (sockaddr_in6 *)(pm->Address.lpSockaddr);
                char a[INET6_ADDRSTRLEN] = {};
                if (inet_ntop(AF_INET6, &(si->sin6_addr), a, sizeof(a)))
                    adapter.ipv6MultAddrs.push_back(a);
            }
        }
        for (PIP_ADAPTER_DNS_SERVER_ADDRESS pd = tp->FirstDnsServerAddress; pd != NULL; pd = pd->Next)
        {
            // if ipv4
            if (pd->Address.lpSockaddr->sa_family == AF_INET)
            {
                sockaddr_in *si         = (sockaddr_in *)(pd->Address.lpSockaddr);
                char a[INET_ADDRSTRLEN] = {};
                if (inet_ntop(AF_INET, &(si->sin_addr), a, sizeof(a)))
                    adapter.ipv4DnsAddrs.push_back(a);
            }
            // if ipv6
            else if (pd->Address.lpSockaddr->sa_family == AF_INET6)
            {
                sockaddr_in6 *si         = (sockaddr_in6 *)(pd->Address.lpSockaddr);
                char a[INET6_ADDRSTRLEN] = {};
                if (inet_ntop(AF_INET6, &(si->sin6_addr), a, sizeof(a)))
                    adapter.ipv6DnsAddrs.push_back(a);
            }
        }

        for (PIP_ADAPTER_GATEWAY_ADDRESS_LH pg = tp->FirstGatewayAddress; pg != NULL; pg = pg->Next)
        {
            // if ipv4
            if (pg->Address.lpSockaddr->sa_family == AF_INET)
            {
                sockaddr_in *si         = (sockaddr_in *)(pg->Address.lpSockaddr);
                char a[INET_ADDRSTRLEN] = {};
                if (inet_ntop(AF_INET, &(si->sin_addr), a, sizeof(a)))
                    adapter.ipv4GatewayAddrs.push_back(a);
            }
            // if ipv6
            else if (pg->Address.lpSockaddr->sa_family == AF_INET6)
            {
                sockaddr_in6 *si         = (sockaddr_in6 *)(pg->Address.lpSockaddr);
                char a[INET6_ADDRSTRLEN] = {};
                if (inet_ntop(AF_INET6, &(si->sin6_addr), a, sizeof(a)))
                    adapter.ipv6GatewayAddrs.push_back(a);
            }
        }
        if (tp->DnsSuffix)
            adapter.dnsSuffixes = str::w2utf8(tp->DnsSuffix);
        adapter.flags = tp->Flags;
        adapter.mtu   = tp->Mtu;
        if (tp->OperStatus == IfOperStatusUp)
            adapter.status = NetworkAdapter::NetworkStatus::Up;
        else
            adapter.status = NetworkAdapter::NetworkStatus::DOWN;

        if (tp->PhysicalAddressLength != 0)
        {
            // Get mac address hex string
            char mac_addr[19] = {0};
            sprintf_s(mac_addr, 18, "%02X:%02X:%02X:%02X:%02X:%02X", tp->PhysicalAddress[0], tp->PhysicalAddress[1],
                      tp->PhysicalAddress[2], tp->PhysicalAddress[3], tp->PhysicalAddress[4], tp->PhysicalAddress[5]);
            adapter.macAddr = mac_addr;
        }
        else
            adapter.macAddr = "";
        adapters.push_back(adapter);
    }
    return adapters;
}
int NetworkHelper::GetOutIpAddress(std::string &ipAddr, const std::string &toIP)
{
    WSADATA wsaData;
    int result = 0;
    const char *destination_address;
    sockaddr_storage Addr;
    unsigned long addr;
    WORD wVersionRequested = MAKEWORD(2, 1);
    char *source_address;
    int sockHandle = 0;
    int AddrLen;
    struct timeval timeout;
    result = WSAStartup(wVersionRequested, &wsaData);
    if (0 != result)
    {
        result = -1;
        goto exit;
    }
    destination_address                            = toIP.c_str();
    Addr                                           = {0};
    addr                                           = inet_addr(destination_address);
    ((struct sockaddr_in *)&Addr)->sin_addr.s_addr = addr;
    ((struct sockaddr_in *)&Addr)->sin_family      = AF_INET;
    ((struct sockaddr_in *)&Addr)->sin_port        = htons(9); // 9 is discard port
    sockHandle                                     = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (INVALID_SOCKET == sockHandle)
    {
        result = -1;
        goto exit;
    }
    AddrLen = sizeof(Addr);

    // Set timeout value

    timeout.tv_sec  = 2; // Timeout in seconds
    timeout.tv_usec = 0; // ... and microseconds

    if (setsockopt(sockHandle, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
    {
        result = -4;
        goto exit;
    }

    if (setsockopt(sockHandle, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
    {
        result = -4;
        goto exit;
    }
    result = connect(sockHandle, (sockaddr *)&Addr, AddrLen);
    if (0 != result)
    {
        result = -2;
        goto exit;
    }
    result = getsockname(sockHandle, (sockaddr *)&Addr, &AddrLen);
    if (0 != result)
    {
        result = -3;
        goto exit;
    }
    source_address = inet_ntoa(((struct sockaddr_in *)&Addr)->sin_addr);
    ipAddr         = source_address;
exit:
    if (sockHandle)
        closesocket(sockHandle);
    WSACleanup();
    return result;
}
