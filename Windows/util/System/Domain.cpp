#include "Domain.h"
#include <windows.h>
#include <DSRole.h>
#include <DsGetDC.h>
#include <General/util/StrUtil.h>
#include <iostream>
#include <LM.h>
#include <spdlog/fmt/fmt.h>


#pragma comment(lib, "netapi32.lib")
using namespace zzj;

std::string GUIDToString(GUID guid)
{
    std::string guidStr;
    guidStr = fmt::format("{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}", guid.Data1,
                          guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
                          guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return guidStr;
}
bool zzj::Domain::IsDomainJoined()
{

    PDSROLE_PRIMARY_DOMAIN_INFO_BASIC pdsrib = NULL;
    DWORD dwErr = DsRoleGetPrimaryDomainInformation(NULL, DsRolePrimaryDomainInfoBasic, (PBYTE *)&pdsrib);
    if (dwErr != ERROR_SUCCESS)
    {
        throw std::runtime_error(fmt::format("DsRoleGetPrimaryDomainInformation failed with error code {}", dwErr));
    }
    if (pdsrib->MachineRole == DsRole_RoleMemberWorkstation)
    {
        DsRoleFreeMemory(pdsrib);
        return true;
    }
    DsRoleFreeMemory(pdsrib);
    return false;
}

zzj::Domain zzj::Domain::GetDomain()
{
    if (IsDomainJoined())
    {
        PDSROLE_PRIMARY_DOMAIN_INFO_BASIC pdsrib = NULL;
        DEFER
        {
            if (pdsrib)
            {
                DsRoleFreeMemory(pdsrib);
            }
        };
        DWORD dwErr = DsRoleGetPrimaryDomainInformation(NULL, DsRolePrimaryDomainInfoBasic, (PBYTE *)&pdsrib);
        if (dwErr != ERROR_SUCCESS)
        {
            throw std::runtime_error(fmt::format("DsRoleGetPrimaryDomainInformation failed with error code {}", dwErr));
        }
        Domain domain;
        domain.domainName    = str::w2utf8(pdsrib->DomainNameFlat);
        domain.dnsDomainName = str::w2utf8(pdsrib->DomainNameDns);
        domain.domainGUID    = GUIDToString(pdsrib->DomainGuid);

        PDOMAIN_CONTROLLER_INFOA pdcInfo = NULL;
        DEFER
        {
            if (pdcInfo)
            {
                NetApiBufferFree(pdcInfo);
            }
        };
        dwErr = DsGetDcNameA(NULL, NULL, NULL, NULL, 0, &pdcInfo);
        if (dwErr != ERROR_SUCCESS)
        {
            throw std::runtime_error(fmt::format("DsGetDcNameA error with {}", dwErr));
        }
        std::string domainControllerName = zzj::str::ansi2utf8(pdcInfo->DomainControllerName);
        std::string strippedDCName       = domainControllerName.substr(2, domainControllerName.find_first_of('.') - 2);
        domain.domainControllerName      = strippedDCName;

        return domain;
    }
    return Domain();
}
