#include "Service.h"

int zzj::ServiceInterface::Protect(ServiceInterface *otherService)
{
    bool isInstalled         = false;
    bool isRunning           = false;
    int result               = 0;
    if (otherService != nullptr)
        return otherService->Protect();
    result = IsServiceInstalled(isInstalled);
    if (0 != result)
    {
        result = -1;
        goto exit;
    }
    if (!isInstalled)
    {
        result = Install();
        if (0 != result)
        {
            result = -2;
            goto exit;
        }
    }
    
    result = IsServiceRunning(isRunning);
    if (0 != result)
    {
        result = -3;
        goto exit;
    }
    if (!isRunning)
    {
        result = Start();
        if (0 != result)
        {
            result = -4;
            goto exit;
        }
    }
exit:
    return result;
}
