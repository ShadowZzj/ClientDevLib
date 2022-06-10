#ifdef _WIN32
#include "../../Windows/WinUtilLib/WindowsUtilLib/CRTdbgHeader.h"
#include <Windows.h>
#else
#include <errno.h>
#include <sys/file.h>

#endif
int IsProcessWithIdRunning(const char *id)
{
    int result = 0;
#ifdef _WIN32
    HANDLE hMutex = NULL;
    hMutex        = CreateMutexA(NULL, FALSE, id);
    if (hMutex != NULL)
    {
        if (ERROR_ALREADY_EXISTS == GetLastError())
        {
            ReleaseMutex(hMutex);
            return 0;
        }
    }
    return -1;
#else
    int rc       = 0;
    int pid_file = open(id, O_CREAT | O_RDWR, 0666);
    if (-1 == pid_file)
    {
        result = -1;
        goto exit;
    }
    rc = flock(pid_file, LOCK_EX | LOCK_NB);
    if (rc)
    {
        if (EWOULDBLOCK == errno)
        {
            result = 1;
            goto exit;
        }
        result = -2;
        goto exit;
    }
    else
    {
        result = 0;
        goto exit;
    }
#endif

exit:
    return result;
}

int AddCrashHandler()
{
#ifdef _WIN32
    ::SetUnhandledExceptionFilter(WriteDumpHandler);
#else
#endif
    return 0;
}