#include <General/util/Network/DNSHelper.h>
#include <Winsock2.h>
#include <Windns.h>

#pragma comment(lib, "Dnsapi.lib")
#pragma comment(lib, "Ws2_32.lib")

std::tuple<zzj::DNSRelolver::Error, std::string> zzj::DNSRelolver::ResolveARecord(const std::string &domain)
{
    DNS_RECORD *pDnsRecord;
    PDNS_RECORD pRecord;
    std::string retString;
    zzj::DNSRelolver::Error retError = zzj::DNSRelolver::Error::Success;
    // Query for A records
    DNS_STATUS status = DnsQuery_A(domain.c_str(), DNS_TYPE_A, DNS_QUERY_STANDARD, NULL, &pDnsRecord, NULL);

    if (status == ERROR_SUCCESS)
    {
        for (pRecord = pDnsRecord; pRecord != NULL; pRecord = pRecord->pNext)
        {
            if (pRecord->wType == DNS_TYPE_A)
            {
                struct in_addr addr;
                addr.S_un.S_addr = (pRecord->Data.A.IpAddress);
                retString = inet_ntoa(addr);
                retError = zzj::DNSRelolver::Error::Success;
                break;
            }
        }
        DnsRecordListFree(pDnsRecord, DnsFreeRecordList);

        if(retString.empty())
        {
            retError = zzj::DNSRelolver::Error::NoHost;
        }

        return std::make_tuple(retError, retString);
    }
    else
    {
        switch (status)
        {
        case DNS_INFO_NO_RECORDS:
            retError = zzj::DNSRelolver::Error::NoHost;
            break;
        case ERROR_TIMEOUT:
            retError = zzj::DNSRelolver::Error::Fail;
            break;
        default:
            retError = zzj::DNSRelolver::Error::Fail;
            break; 
        }
        return std::make_tuple(retError, retString);
    }
}
