#include "Domain.h"
#include <windows.h>
#include <DSRole.h>
#include <spdlog/fmt/fmt.h>
#include <iostream>
#include <General/util/StrUtil.h>
#pragma comment(lib, "netapi32.lib")
bool zzj::Domain::IsDomainJoined()
{

    PDSROLE_PRIMARY_DOMAIN_INFO_BASIC pdsrib = NULL;
    DWORD dwErr = DsRoleGetPrimaryDomainInformation(NULL, DsRolePrimaryDomainInfoBasic, (PBYTE*)&pdsrib);
    if (dwErr != ERROR_SUCCESS)
    {
        throw std::runtime_error(fmt::format("DsRoleGetPrimaryDomainInformation failed with error code {}", dwErr));
    }
    if (pdsrib->MachineRole == DsRole_RoleMemberWorkstation)
    {
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
        DWORD dwErr = DsRoleGetPrimaryDomainInformation(NULL, DsRolePrimaryDomainInfoBasic, (PBYTE*)&pdsrib);
        if (dwErr != ERROR_SUCCESS)
        {
            throw std::runtime_error(fmt::format("DsRoleGetPrimaryDomainInformation failed with error code {}", dwErr));
        }
        Domain domain;
        domain.domainName = zzj::str::w2utf8(pdsrib->DomainNameFlat);
        domain.dnsDomainName = zzj::str::w2utf8(pdsrib->DomainNameDns);
        DsRoleFreeMemory(pdsrib);
        return domain;
    }
    return Domain();
}
