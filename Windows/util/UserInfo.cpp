#include "UserInfo.h"
#include <General/util/BaseUtil.hpp>
#include <General/util/StrUtil.h>
#include <Windows.h>
#include <sddl.h>
#include <spdlog/spdlog.h>
#include <wtsapi32.h>
#pragma comment(lib, "Wtsapi32.lib")

zzj::UserInfo zzj::UserInfo::GetActiveUserInfo()
{
    LPSTR szUserName   = NULL;
    LPSTR szDomainName = NULL;
    DEFER
    {
        if (szUserName)
            WTSFreeMemory(szUserName);
        if (szDomainName)
            WTSFreeMemory(szDomainName);
    };

    DWORD dwLen = 0;
    DWORD dwSessionId;
    UserInfo ret;
    dwSessionId = WTSGetActiveConsoleSessionId();

    if (0xffffffff == dwSessionId || 0 == dwSessionId)
        return {};

    BOOL bStatus =
        WTSQuerySessionInformationA(WTS_CURRENT_SERVER_HANDLE, dwSessionId, WTSDomainName, &szDomainName, &dwLen);
    if (bStatus)
    {
        ret.domainName = str::StrToUTF8(szDomainName);
        bStatus = WTSQuerySessionInformationA(WTS_CURRENT_SERVER_HANDLE, dwSessionId, WTSUserName, &szUserName, &dwLen);
        if (bStatus)
            ret.userName = str::StrToUTF8(szUserName);
    }
    std::string accName = std::string(szDomainName) + "\\" + szUserName;
    LPSTR retDomainName = (LPSTR)GlobalAlloc(GPTR, sizeof(CHAR) * 1024);
    DEFER
    {
        GlobalFree(retDomainName);
    };
    DWORD cchDomainName = 1024;
    SID_NAME_USE eSidType;
    LPSTR sidString = nullptr;
    DEFER
    {
        if (sidString)
            LocalFree(sidString);
    };

    char sid_buffer[1024];
    DWORD cbSid = 1024;
    SID *sid    = (SID *)sid_buffer;

    if (!LookupAccountNameA(NULL, accName.c_str(), sid_buffer, &cbSid, retDomainName, &cchDomainName, &eSidType))
    {
        spdlog::info("LookupAccountNameA error with {}", GetLastError());
        return {};
    }

    if (!ConvertSidToStringSidA(sid, &sidString))
    {
        spdlog::info("ConvertSidToStringSidA error with {}", GetLastError());
        return {};
    }
    ret.sid = str::StrToUTF8(sidString);
    return ret;
}