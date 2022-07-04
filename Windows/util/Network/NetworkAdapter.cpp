#include <General/util/Network/NetworkAdapter.h>
#include <General/util/BaseUtil.hpp>
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
    PIP_ADAPTER_INFO AdapterInfo = NULL;
    DEFER{
        if (AdapterInfo)
            free(AdapterInfo);
    };

    DWORD dwBufLen = sizeof(IP_ADAPTER_INFO);
    char* mac_addr = (char*)malloc(18);
    DEFER{
        free(mac_addr);
    };
    std::vector<NetworkAdapter> ret;

    AdapterInfo = (IP_ADAPTER_INFO*)malloc(sizeof(IP_ADAPTER_INFO));
    if (AdapterInfo == NULL) {
        free(mac_addr);
        return {};
    }

    // Make an initial call to GetAdaptersInfo to get the necessary size into the dwBufLen variable
    if (GetAdaptersInfo(AdapterInfo, &dwBufLen) == ERROR_BUFFER_OVERFLOW) {
        free(AdapterInfo);
        AdapterInfo = (IP_ADAPTER_INFO*)malloc(dwBufLen);
        if (AdapterInfo == NULL) {
            free(mac_addr);
            return {};
        }
    }

    if (GetAdaptersInfo(AdapterInfo, &dwBufLen) == NO_ERROR) {
        // Contains pointer to current adapter info
        PIP_ADAPTER_INFO pAdapterInfo = AdapterInfo;
        do {
            NetworkAdapter adapter;
            // technically should look at pAdapterInfo->AddressLength
            //   and not assume it is 6.
            sprintf_s(mac_addr, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
                pAdapterInfo->Address[0], pAdapterInfo->Address[1],
                pAdapterInfo->Address[2], pAdapterInfo->Address[3],
                pAdapterInfo->Address[4], pAdapterInfo->Address[5]);
            adapter.macAddr = mac_addr;

            PIP_ADDR_STRING ipInfo = &pAdapterInfo->IpAddressList;
            while (ipInfo != nullptr)
            {
                adapter.ipAddrs.push_back(ipInfo->IpAddress.String);
                ipInfo = ipInfo->Next;
            }

            adapter.name = pAdapterInfo->AdapterName;
            adapter.description = pAdapterInfo->Description;
            ret.push_back(adapter);
            pAdapterInfo = pAdapterInfo->Next;
        } while (pAdapterInfo);
    }

    return ret; // caller must free.
}
int NetworkHelper::GetOutIpAddress(std::string &ipAddr,const std::string& toIP)
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
    AddrLen                                    = sizeof(Addr);
    result                                            = connect(sockHandle, (sockaddr *)&Addr, AddrLen);
    if (0!=result)
    {
        result = -2;
        goto exit;
    }
    result                                           = getsockname(sockHandle, (sockaddr *)&Addr, &AddrLen);
    if (0 != result)
    {
        result = -3;
        goto exit;
    }
    source_address                           = inet_ntoa(((struct sockaddr_in *)&Addr)->sin_addr);
    ipAddr = source_address;
exit:
    if (sockHandle)
    closesocket(sockHandle);
    WSACleanup();
    return result;
}
