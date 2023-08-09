#include <General/util/System/System.h>
#include <wtsapi32.h>
#define SECURITY_WIN32
#include <security.h>
#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "Wtsapi32.lib")
#include <sddl.h>

namespace zzj
{
std::optional<std::string> Session::GetActiveSessionId()
{
    DWORD dwSessionId               = 0xffffffff;
    WTS_SESSION_INFOA *pSessionInfo = NULL;
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

    if (0xffffffff == dwSessionId)
        return {};

    return std::to_string(dwSessionId);
}

std::optional<Session> Session::GetActiveSessionInfo()
{
    auto sessionId = GetActiveSessionId();
    if (!sessionId)
        return {};
    Session ret;
    ret.sessionId = sessionId;
    ret.userInfo  = UserInfo::GetActiveUserInfo();
    return ret;
}

} // namespace zzj