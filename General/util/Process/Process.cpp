#include <boost/process.hpp>
#include "Process.h"
#ifdef _WIN32
#include <Windows/util/Debug/CRTdbgHeader.h>
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
    hMutex = CreateMutexA(NULL, FALSE, id);
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
    int rc = 0;
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

namespace zzj
{
CommandHelper::CommandResult CommandHelper::ExecuteCommand(const std::string &command)
{
    namespace bp = boost::process;
    bp::ipstream pipe_out;  
    bp::ipstream pipe_err;  

#ifdef _WIN32
    bp::child c("cmd.exe", "/c", command, bp::std_out > pipe_out, bp::std_err > pipe_err);
#else
    bp::child c("/bin/bash", "-c", command, bp::std_out > pipe_out, bp::std_err > pipe_err);
#endif
    std::string line;
    std::string output;
    std::string error;

    while (std::getline(pipe_out, line))
    {
        output += line + "\n";
    }

    while (std::getline(pipe_err, line))
    {
        error += line + "\n";
    }

    c.wait();
    int exitCode = c.exit_code();
    return {exitCode, output, error};
}
}  // namespace zzj
