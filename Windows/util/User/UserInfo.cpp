#include "UserInfo.h"
#include <General/util/BaseUtil.hpp>
#include <General/util/StrUtil.h>
#include <Windows.h>
#include <sddl.h>
#include <spdlog/spdlog.h>
#include <wtsapi32.h>
#include <Windows/util/Process/ThreadHelper.h>
#define SECURITY_WIN32
#include <security.h>
#pragma comment(lib, "secur32.lib")
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
    DWORD dwSessionId = 0xffffffff;
    UserInfo ret;
    WTS_SESSION_INFOA * pSessionInfo = NULL;
    DWORD dwCount = 0;
    
    if (WTSEnumerateSessionsA(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pSessionInfo, &dwCount) == FALSE)
    {
        return {};
    }
    for (DWORD i = 0; i < dwCount; i++)
    {
        WTS_SESSION_INFOA sessionInfo = pSessionInfo[i];
        if (sessionInfo.State == WTSActive)
            dwSessionId = sessionInfo.SessionId;
    }
    WTSFreeMemory(pSessionInfo);

    if (0xffffffff == dwSessionId || 0 == dwSessionId)
        return {};

    BOOL bStatus =
        WTSQuerySessionInformationA(WTS_CURRENT_SERVER_HANDLE, dwSessionId, WTSDomainName, &szDomainName, &dwLen);
    if (bStatus)
    {
        ret.domainName = str::ansi2utf8(szDomainName);
        bStatus = WTSQuerySessionInformationA(WTS_CURRENT_SERVER_HANDLE, dwSessionId, WTSUserName, &szUserName, &dwLen);
        if (bStatus)
            ret.userName = str::ansi2utf8(szUserName);
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
    ret.sid = str::ansi2utf8(sidString);


    zzj::Process currentProcess;
    HANDLE impersonateHandle = NULL;
    DEFER
    {
        if (impersonateHandle)
            zzj::Thread::RevertToCurrentUser(impersonateHandle);
    };
    if (auto [result, res] = currentProcess.IsServiceProcess();result == 0 && res)
    {
        impersonateHandle = zzj::Thread::ImpersonateCurrentUser();
        if (NULL == impersonateHandle)
        {
            spdlog::info("ImpersonateCurrentUser error with {}", GetLastError());
            return {};
        }
    }
    char guid[256 + 1]      = {};
    DWORD sizeExtended              = ARRAYSIZE(guid);

    GetUserNameExA(NameUniqueId, guid, &sizeExtended);
    ret.guid = guid;

    return ret;
}