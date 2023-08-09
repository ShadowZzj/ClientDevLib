#include <General/util/System/System.h>
#include <General/util/User/User.h>
#import <Foundation/Foundation.h>
#import <SystemConfiguration/SystemConfiguration.h>
namespace zzj
{
std::optional<Session> Session::GetActiveSessionInfo()
{
    @autoreleasepool
    {
        std::optional<Session> ret;
        std::optional<std::string> sessionId = GetActiveSessionId();
        if (sessionId.has_value())
        {
            ret = Session();
            ret->sessionId = *sessionId;
            ret->userInfo  = UserInfo::GetUserInfoBySessionId(*sessionId);
        }
        return ret;
    }
}
std::optional<std::string> Session::GetActiveSessionId()
{
    @autoreleasepool
    {

        SCDynamicStoreRef store;
        CFStringRef name;
        uid_t uid;
        std::optional<std::string> ret;

        store = SCDynamicStoreCreate(NULL, CFSTR("GetConsoleUser"), NULL, NULL);
        name  = SCDynamicStoreCopyConsoleUser(store, &uid, NULL);
        CFRelease(store);
        if (name != NULL)
        {
            CFRelease(name);
            ret = std::to_string(uid);
            if (*ret == "0")
                ret = {};
        }
        else
            return {};

        return ret;
    }
}
} // namespace zzj
