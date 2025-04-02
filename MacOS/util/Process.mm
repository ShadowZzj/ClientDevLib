#include "Process.h"
#include "StringUtil.h"
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#include <General/util/File/File.h>
#include <errno.h>
#include <mach-o/dyld.h>
#include <spdlog/spdlog.h>
#include <sys/file.h>
#include <sys/sysctl.h>

auto CLOSEFILE = [](FILE *fp) {
    if (fp)
        fclose(fp);
};
using FILEPTR = std::unique_ptr<FILE, decltype(CLOSEFILE)>;

zzj::Process::TaskRetInfo zzj::Process::CreateProcess(const char *fullPath, std::vector<std::string> args,
                                                      bool waitFinish)
{
    @autoreleasepool
    {
        TaskRetInfo ret = {0};
        int result      = 0;
        NSTask *task    = [[NSTask alloc] init];

        NSString *exePath       = [NSString stringWithUTF8String:fullPath];
        NSURL *exeUrl           = [NSURL fileURLWithPath:exePath];
        NSError *err            = nil;
        NSMutableArray *argsArr = [[NSMutableArray alloc] init];

        for (std::string arg : args)
            [argsArr addObject:[NSString stringWithUTF8String:arg.c_str()]];
        [task setExecutableURL:exeUrl];
        [task setArguments:argsArr];

        [task launchAndReturnError:&err];
        if (nil != err)
        {
            ret.error = err.code;
            return ret;
        }
        if (waitFinish)
        {
            [task waitUntilExit];
            ret.returnVal = [task terminationStatus];
            ret.pid       = [task processIdentifier];
        }
        else
        {
            sleep(2);
            if (![task isRunning])
                ret.returnVal = [task terminationStatus];
            else
                ret.returnVal = -11111;
            ret.pid = [task processIdentifier];
        }
    exit:
        return ret;
    }
}

std::pair<bool, std::string> zzj::Process::CreateProcess(const std::string &cmd)
{
    std::string strOutPut;
    char buf[1000]{0};
    FILE *fp = NULL;

    fp = popen(cmd.c_str(), "r+");
    if (fp == NULL)
    {
        return {false, "Failed to run cmd!"};
    }

    while (NULL != fgets(buf, sizeof(buf), fp))
    {
        strOutPut.append(buf);
        if (feof(fp) || ferror(fp))
            break;
    }

    if (strOutPut.empty())
    {
        return {false, "Failed,return is null"};
    }

    if (fp)
        pclose(fp);
    return {true, strOutPut};
}

int zzj::Process::CreateProcess(const char *fullPath, std::vector<std::string> args, std::wstring &output,
                                bool waitForExit)
{
    @autoreleasepool
    {
        int result   = 0;
        NSTask *task = [[NSTask alloc] init];

        NSString *exePath       = [NSString stringWithUTF8String:fullPath];
        NSURL *exeUrl           = [NSURL fileURLWithPath:exePath];
        NSError *err            = nil;
        NSMutableArray *argsArr = [[NSMutableArray alloc] init];
        NSPipe *pout            = [NSPipe pipe];
        NSFileHandle *read      = nil;
        NSData *dataRead        = nil;
        NSString *stringRead    = nil;

        for (std::string arg : args)
            [argsArr addObject:[NSString stringWithUTF8String:arg.c_str()]];
        [task setStandardOutput:pout];
        [task setExecutableURL:exeUrl];
        [task setArguments:argsArr];

        [task launchAndReturnError:&err];
        if (nil != err)
        {
            result = err.code;
            NSLog(@"%@", err);
            goto exit;
        }

        if (waitForExit)
            [task waitUntilExit];

        read       = [pout fileHandleForReading];
        dataRead   = [read availableData];
        stringRead = [[NSString alloc] initWithData:dataRead encoding:NSUTF8StringEncoding];
        output     = NSStringToStringW(stringRead);

    exit:
        return result;
    }
}

// output一定有输出调用这个，获取不到进程返回值
int zzj::Process::CreateUserProcess(const char *fullPath, const char *userName, std::vector<std::string> args,
                                    std::wstring &outPut)
{
    int result = 0;
    std::string strOutPut;
    char buf[1000]{0};
    FILE *fp        = NULL;
    std::string cmd = "sudo -u ";
    cmd += userName;
    cmd += " ";
    cmd += "'";
    cmd += fullPath;
    cmd += "'";
    for (auto tmp : args)
    {
        if (tmp == "|")
            cmd += tmp;
        else
        {
            cmd += " ";
            cmd += "'";
            cmd += tmp;
            cmd += "'";
        }
    }

    fp = popen(cmd.c_str(), "r");
    if (NULL == fp)
    {
        spdlog::error("Failed to run cmd!\n");
        result = -1;
        goto exit;
    }

    while (NULL != fgets(buf, sizeof(buf), fp))
    {
        strOutPut.append(buf);
        if (feof(fp) || ferror(fp))
            break;
    }

    if (strOutPut.empty())
    {
        spdlog::error("Execute cmd :{} error\n", cmd.c_str());
        result = -2;
        goto exit;
    }
    outPut = Str2Wstr(strOutPut);
exit:
    if (fp)
        pclose(fp);

    return result;
}

// 获取进程返回值
int zzj::Process::CreateUserProcess(const char *fullPath, const char *userName, std::vector<std::string> args,
                                    bool waitForExit)
{
    int result      = 0;
    std::string cmd = "sudo -u ";
    cmd += userName;
    cmd += " ";
    cmd += "'";
    cmd += fullPath;
    cmd += "'";
    for (auto tmp : args)
    {
        cmd += " ";
        cmd += "'";
        cmd += tmp;
        cmd += "'";
    }
    if (!waitForExit)
        cmd += " &";
    result = system(cmd.c_str());
    return result;
}
std::string zzj::Process::GetProcessExePath()
{
    char exePath[PATH_MAX];
    uint32_t len = sizeof(exePath);
    std::string ret;
    if (_NSGetExecutablePath(exePath, &len) != 0)
    {
        exePath[0] = '\0'; // buffer too small (!)
        ret        = exePath;
    }
    else
    {
        // resolve symlinks, ., .. if possible
        char *canonicalPath = realpath(exePath, NULL);
        if (canonicalPath != NULL)
        {
            strncpy(exePath, canonicalPath, len);
            free(canonicalPath);
        }
    }
    return ret;
}

int zzj::Process::GetCurrentProcessId() {
    return getpid();
}

bool zzj::Process::ProcessIsState(const char *processName)
{
    FILE *fp;
    int count;
    int BUFSZ = 150;
    char buf[BUFSZ];
    char command[1024] = {0};
    sprintf(command, "pgrep -l \"%s\"| wc -l", processName);

    if ((fp = popen(command, "r")) == NULL)
        return false;

    if ((fgets(buf, BUFSZ, fp)) != NULL)
    {
        count = atoi(buf);
        if (count <= 0)
        {
            pclose(fp);
            return false;
        }
        else
        {
            pclose(fp);
            return true;
        }
    }
    pclose(fp);
    return true;
}

void zzj::Process::TerminateProcess(const char *processName, const char *arg)
{
    char command[1024] = {0};

    sprintf(command, "sudo killall %s \"%s\"", arg, processName);
    system(command);
}

void zzj::Process::TerminateProcessById(int pid)
{
    char command[1024] = {0};

    sprintf(command, "sudo kill %d ", pid);
    system(command);
}

#include <err.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int lockfile(int ff)
{
    int result = flock(ff, LOCK_EX);
    printf("result = %d\n", result);
    return result;
}

int zzj::Thread::WaitForMutex(const char *mutexFileName)
{
    int fd = open(mutexFileName, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd <= 0)
    {
        printf("fd == %d\n", fd);
        return fd;
    }
    flock(fd, LOCK_UN);
    int result = flock(fd, LOCK_EX);
    if (0 != result)
    {
        printf("WaitForMutex result = %d", result);
        return -1;
    }
    return fd;
}

int zzj::Thread::ReleaseMutex(int fileId)
{
    flock(fileId, LOCK_UN);
    close(fileId);
    return 0;
}

bool zzj::Process::IsCurrentProcessBeingDebugged()
{
    int junk;
    int mib[4];
    struct kinfo_proc info;
    size_t size;

    // Initialize the flags so that, if sysctl fails for some bizarre
    // reason, we get a predictable result.

    info.kp_proc.p_flag = 0;

    // Initialize mib, which tells sysctl the info we want, in this case
    // we're looking for information about a specific process ID.

    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_PID;
    mib[3] = getpid();

    // Call sysctl.

    size = sizeof(info);
    junk = sysctl(mib, sizeof(mib) / sizeof(*mib), &info, &size, NULL, 0);
    assert(junk == 0);

    // We're being debugged if the P_TRACED flag is set.

    return ((info.kp_proc.p_flag & P_TRACED) != 0);
}

int zzj::Process::CloseApplication(std::string applicationBundleName)
{
    AppleEvent event;
    AEAddressDesc target;
    OSErr err =
        AECreateDesc(typeApplicationBundleID, applicationBundleName.c_str(), applicationBundleName.length(), &target);
    if (0 != err)
        return err;
    err = AECreateAppleEvent(kCoreEventClass, kAEQuitApplication, &target, kAutoGenerateReturnID, kAnyTransactionID,
                             &event);
    if (0 != err)
    {
        AEDisposeDesc(&target);
        return err;
    }
    err = AESendMessage(&event, NULL, kAENoReply | kAENeverInteract, kAEDefaultTimeout);

    AEDisposeDesc(&target);
    AEDisposeDesc(&event);
    return err;
}

int zzj::Process::ActivateApplication(const std::string &applicationBundleName)
{
    @autoreleasepool
    {
        NSArray *specificApps = [NSRunningApplication
            runningApplicationsWithBundleIdentifier:[NSString stringWithUTF8String:applicationBundleName.c_str()]];

        for (int i = 0; i < [specificApps count]; i++)
        {
            NSRunningApplication *app = [specificApps objectAtIndex:i];
            [app activateWithOptions:NSApplicationActivateIgnoringOtherApps];
        }
        return 0;
    }
}
bool zzj::Process::IsCurrentProcessRunningTranslated()
{
    int ret     = 0;
    size_t size = sizeof(ret);
    errno       = 0;
    if (sysctlbyname("sysctl.proc_translated", &ret, &size, NULL, 0) == -1)
    {
        if (errno == ENOENT)
            return false;

        fprintf(stderr,
                "WARN: Could not determine if this process was running in a translation environment like Rosetta 2.\n"
                "Assuming that we're not.\n"
                "errno: %i, %s\n",
                errno, strerror(errno));

        return false;
    }
    return ret;
}
