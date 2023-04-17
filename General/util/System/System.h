#ifndef _G_SYSTEM_H_
#define _G_SYSTEM_H_
#include <iostream>
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
};
} // namespace zzj
std::string GetActiveConsoleUserName();
std::string GetActiveConsoleSessionId();
std::string GetActiveUserSid();

#endif
