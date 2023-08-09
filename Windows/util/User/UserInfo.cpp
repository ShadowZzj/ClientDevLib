#include <General/util/BaseUtil.hpp>
#include <General/util/StrUtil.h>
#include <General/util/System/System.h>
#include <General/util/User/User.h>
#include <Windows.h>
#include <Windows/util/Process/ThreadHelper.h>
#include <Windows/util/Registry/WinReg.hpp>
#include <sddl.h>
#include <spdlog/spdlog.h>
#include <wtsapi32.h>

#define SECURITY_WIN32
#include <Windows/util/System/SystemHelper.h>
#include <lm.h>
#include <security.h>
#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "Wtsapi32.lib")
#pragma comment(lib, "netapi32.lib")
std::optional<std::string> GetSidByName(const std::string &acctName)
{
    LPSTR sidString = nullptr;
    DEFER
    {
        if (sidString)
            LocalFree(sidString);
    };
    DWORD dwSize   = 0;
    DWORD dwErr    = 0;
    DWORD dwName   = 0;
    DWORD dwDomain = 0;
    SID_NAME_USE sidUse;
    BOOL bStatus = LookupAccountNameA(NULL, acctName.c_str(), NULL, &dwSize, NULL, &dwDomain, &sidUse);
    if (bStatus == FALSE)
    {
        dwErr = GetLastError();
        if (dwErr != ERROR_INSUFFICIENT_BUFFER)
        {
            spdlog::error("LookupAccountNameA failed, error code: {}", dwErr);
            return {};
        }
    }
    std::vector<char> buf(dwSize);
    std::vector<char> domainBuf(dwDomain);
    bStatus = LookupAccountNameA(NULL, acctName.c_str(), buf.data(), &dwSize, domainBuf.data(), &dwDomain, &sidUse);
    if (bStatus == FALSE)
    {
        dwErr = GetLastError();
        spdlog::error("LookupAccountNameA failed, error code: {}", dwErr);
        return {};
    }
    PSID pSid = reinterpret_cast<PSID>(buf.data());
    if (!ConvertSidToStringSidA(pSid, &sidString))
    {
        spdlog::error("ConvertSidToStringSidA failed, error code: {}", GetLastError());
        return {};
    }
    return zzj::str::ansi2utf8(sidString);
}
std::optional<std::map<std::string, std::string>> GetUserGourpNameAndSidByName(const std::string &userName)
{
    LOCALGROUP_USERS_INFO_0 *pBuf = NULL;
    DEFER
    {
        if (pBuf != NULL)
            NetApiBufferFree(pBuf);
    };
    DWORD dwLevel        = 0;
    DWORD dwPrefMaxLen   = MAX_PREFERRED_LENGTH;
    DWORD dwEntriesRead  = 0;
    DWORD dwTotalEntries = 0;
    NET_API_STATUS nStatus;

    std::wstring wUserName = zzj::str::utf82w(userName);
    nStatus = NetUserGetLocalGroups(NULL, wUserName.c_str(), dwLevel, NULL,(LPBYTE *)&pBuf, dwPrefMaxLen, &dwEntriesRead,
                               &dwTotalEntries);

    if (nStatus != NERR_Success)
        return {};

    LOCALGROUP_USERS_INFO_0* pTmpBuf;
    DWORD i;
    DWORD dwTotalCount = 0;

    std::map<std::string, std::string> ret;
    if ((pTmpBuf = pBuf) != NULL)
    {
        for (i = 0; i < dwEntriesRead; i++)
        {
            if (pTmpBuf == NULL)
            {
                break;
            }
            std::string groupName = zzj::str::w2utf8(pTmpBuf->lgrui0_name);
            auto sid              = GetSidByName(groupName);
            if (!sid)
                continue;

            ret[*sid] = groupName;
            pTmpBuf++;
        }
    }

    return ret;
}
std::optional<std::string> GetUserHomeDirectoryByName(const std::string &userName)
{
    try
    {
        std::wstring profileListKeyPath = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList";
        winreg::RegKey profileListKey(HKEY_LOCAL_MACHINE, profileListKeyPath.c_str(), KEY_READ);
        if (!profileListKey)
            return {};
        auto subKeys = profileListKey.EnumSubKeys();
        if (subKeys.empty())
            return {};

        auto sid = GetSidByName(userName);
        if (!sid)
            return {};

        for (auto &subKey : subKeys)
        {
            if (zzj::str::w2utf8(subKey) != *sid)
                continue;
            winreg::RegKey subKeyKey(profileListKey.Get(), subKey.c_str(), KEY_READ);
            if (!subKeyKey)
                continue;

            auto profileImagePath = subKeyKey.TryGetStringValue(L"ProfileImagePath");
            if (!profileImagePath)
                continue;
            auto homeDir = zzj::str::w2utf8(*profileImagePath);
            return homeDir;
        }

        return {};
    }
    catch (const std::exception &e)
    {
        return {};
    }
}
std::optional<zzj::UserInfo> zzj::UserInfo::GetUserInfoByUserName(const std::string &userName)
{
    auto computerName = zzj::SystemInfo::GetComputerNameStr();
    if (!computerName)
        return {};

    zzj::UserInfo ret;
    ret.userName = userName;

    std::string acctName = *computerName + "\\" + userName;
    auto sid             = GetSidByName(acctName);
    if (!sid)
        return {};
    ret.sid = *sid;

    auto homeDir = GetUserHomeDirectoryByName(userName);
    if (!homeDir)
        return {};
    ret.homeDirectory = *homeDir;
    auto groupInfo    = GetUserGourpNameAndSidByName(userName);
    if (!groupInfo)
        return {};
    ret.groupInfo = *groupInfo;
    return ret;
}

std::optional<zzj::UserInfo> zzj::UserInfo::GetUserInfoBySessionId(const std::string &sessionId)
{
    LPSTR szUserName = NULL;
    DWORD dwLen      = 0;
    DEFER
    {
        if (szUserName)
            WTSFreeMemory(szUserName);
    };
    DWORD dwSessionId = std::stoi(sessionId);

    BOOL bStatus =
        WTSQuerySessionInformationA(WTS_CURRENT_SERVER_HANDLE, dwSessionId, WTSUserName, &szUserName, &dwLen);
    if (!bStatus)
    {
        spdlog::info("WTSQuerySessionInformationA WTSUserName error with {}", GetLastError());
        return {};
    }

    return GetUserInfoByUserName(zzj::str::ansi2utf8(szUserName));
}

std::optional<zzj::UserInfo> zzj::UserInfo::GetActiveUserInfo()
{
    auto sessionId = zzj::Session::GetActiveSessionId();
    if (!sessionId)
        return {};
    auto ret = GetUserInfoBySessionId(*sessionId);
    if (!ret.has_value())
        return {};
    zzj::Process currentProcess;
    HANDLE impersonateHandle = NULL;
    DEFER
    {
        if (impersonateHandle)
            zzj::Thread::RevertToCurrentUser(impersonateHandle);
    };
    if (auto [result, res] = currentProcess.IsServiceProcess(); result == 0 && res)
    {
        impersonateHandle = zzj::Thread::ImpersonateCurrentUser();
        if (NULL == impersonateHandle)
        {
            return {};
        }
    }
    char guid[256 + 1] = {0};
    DWORD sizeExtended = ARRAYSIZE(guid);

    GetUserNameExA(NameUniqueId, guid, &sizeExtended);
    if (!std::string(guid).empty())
        ret->domainGUID = guid;
    return ret;
}

std::vector<zzj::UserInfo> zzj::UserInfo::GetComputerUserInfos()
{
    DWORD dwLevel        = 1;
    DWORD dwFilter       = FILTER_NORMAL_ACCOUNT; // 只获取普通用户，不包括系统账户等
    DWORD dwPrefMaxLen   = MAX_PREFERRED_LENGTH;
    DWORD dwEntriesRead  = 0;
    DWORD dwTotalEntries = 0;
    DWORD dwResumeHandle = 0;
    USER_INFO_1 *pBuf    = nullptr;
    USER_INFO_1 *pTmpBuf;
    DEFER
    {
        if (pBuf)
            NetApiBufferFree(pBuf);
    };

    NET_API_STATUS nStatus;

    std::vector<zzj::UserInfo> ret;
    do
    {
        nStatus = NetUserEnum(NULL, dwLevel, dwFilter, (LPBYTE *)&pBuf, dwPrefMaxLen, &dwEntriesRead, &dwTotalEntries,
                              &dwResumeHandle);
        DEFER
        {
            if (pBuf)
                NetApiBufferFree(pBuf);
            pBuf = NULL;
        };
        if (nStatus != NERR_Success && nStatus != ERROR_MORE_DATA)
            continue;

        if ((pTmpBuf = pBuf) == NULL)
            continue;

        for (DWORD i = 0; i < dwEntriesRead; i++)
        {
            if (pTmpBuf == NULL)
                break;

            std::string userName = str::w2utf8(pBuf[i].usri1_name);
            auto userInfo        = GetUserInfoByUserName(userName);
            if (userInfo)
                ret.push_back(*userInfo);
            pTmpBuf++;
        }
    } while (nStatus == ERROR_MORE_DATA); // 如果缓冲区不够大，继续获取

    return ret;
}
