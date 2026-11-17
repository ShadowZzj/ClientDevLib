#pragma once
#include <General/util/User/User.h>
#include <iostream>
#include <memory>
#include <optional>
#include <string.h>
namespace zzj
{
class Session
{
  public:
    enum class SessionMessage : int
    {
        None,
        SessionLock,
        SessionUnlock,
        SessionLogoff,
        SessionLogon,
        SessionRemoteConnect,
        SessionRemoteDisconnect,
        SessionConsoleConnect,
        SessionConsoleDisconnect,
        SessionRemoteControl,
        SessionCreate,
        SessionTerminate
    };
    std::string sessionId;
    std::optional<UserInfo> userInfo;
    static std::optional<Session> GetActiveSessionInfo();
    static std::optional<std::string> GetActiveSessionId();
};
} // namespace zzj
