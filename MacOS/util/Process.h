#ifndef _MAC_PROCESS_H_
#define _MAC_PROCESS_H_

#include <string>
#include <vector>
#include <tuple>
namespace zzj
{
class Process
{
  public:
    static int CreateProcess(const char *fullPath, std::vector<std::string> args, std::wstring &output,
                             bool waitForExit);
    struct TaskRetInfo{
        int error;
        int pid;
        int returnVal;
    };
    static TaskRetInfo CreateProcess(const char *fullPath, std::vector<std::string> args,bool waitFinish = false);
    static std::pair<bool,std::string> CreateProcess(const std::string & cmd);
    static int CreateUserProcess(const char *fullPath, const char *userName, std::vector<std::string> args,std::wstring &outPut);
    static int CreateUserProcess(const char *fullPath, const char *userName, std::vector<std::string> args,bool waitForExit);
    static std::string GetProcessExePath();
    static bool ProcessIsState(const char* processName);
    static void TerminateProcess(const char* processName, const char* arg);
    //osstatus
    static int CloseApplication(std::string applicationBundleName);
    static int ActivateApplication(const std::string& applicationBundleName);
    static bool IsCurrentProcessBeingDebugged();
};
class Thread
{
    public:
    static int  WaitForMutex(const char* mutexFileName);
    static int ReleaseMutex(int fileId);
};
}; // namespace zzj

#endif
