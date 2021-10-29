#include "File.h"
#include <boost/predef/os.h>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>

#if (BOOST_OS_WINDOWS)
#include <Windows/WinUtilLib/WindowsUtilLib/StrUtil.h>
#include <Windows.h>
#include <shlobj.h >
#include <stdlib.h>
#elif (BOOST_OS_SOLARIS)
#include <limits.h>
#include <stdlib.h>
#elif (BOOST_OS_LINUX)
#include <limits.h>
#include <unistd.h>
#elif (BOOST_OS_MACOS)
#import <MacOS/util/FileUtil.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#elif (BOOST_OS_BSD_FREE)
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

long GetFileSize(std::string filename)
{
    struct stat stat_buf;
    int rc = stat(filename.c_str(), &stat_buf);
    return rc == 0 ? stat_buf.st_size : -1;
}

bool IsDirExist(const char *dirPath)
{
    struct stat info;
    if (stat(dirPath, &info) != 0)
        return false;
    else if (info.st_mode & S_IFDIR)
        return true;
    else
        return false;
}
bool IsFileExist(const char *filePath)
{
    struct stat buffer;
    return (stat(filePath, &buffer) == 0);
}
/*
 * Returns the full path to the currently running executable,
 * or an empty string in case of failure.
 */
std::string GetExecutablePath()
{
#if (BOOST_OS_WINDOWS)
    char current_proc_path[MAX_PATH] = {0};
    ::GetModuleFileNameA(NULL, current_proc_path, MAX_PATH);

    std::string exePath;
    exePath.append(current_proc_path);
    int separator_pos = exePath.rfind('\\');

    if (std::string::npos == separator_pos)
    {
        exePath = "";
    }
    else
    {
        exePath = exePath.substr(0, separator_pos);
    }
    return exePath;
#elif (BOOST_OS_SOLARIS)
    char exePath[PATH_MAX];
    if (realpath(getexecname(), exePath) == NULL)
        exePath[0] = '\0';
#elif (BOOST_OS_LINUX)
    char exePath[PATH_MAX];
    ssize_t len = ::readlink("/proc/self/exe", exePath, sizeof(exePath));
    if (len == -1 || len == sizeof(exePath))
        len = 0;
    exePath[len] = '\0';
#elif (BOOST_OS_MACOS)
    char exePath[PATH_MAX];
    uint32_t len = sizeof(exePath);
    if (_NSGetExecutablePath(exePath, &len) != 0)
    {
        exePath[0] = '\0'; // buffer too small (!)
    }
    else
    {
        *(strrchr(exePath, '/')) = '\0';
        // resolve symlinks, ., .. if possible
        char *canonicalPath = realpath(exePath, NULL);
        if (canonicalPath != NULL)
        {
            strncpy(exePath, canonicalPath, len);
            free(canonicalPath);
        }
    }
#elif (BOOST_OS_BSD_FREE)
    char exePath[2048];
    int mib[4];
    mib[0]     = CTL_KERN;
    mib[1]     = KERN_PROC;
    mib[2]     = KERN_PROC_PATHNAME;
    mib[3]     = -1;
    size_t len = sizeof(exePath);
    if (sysctl(mib, 4, exePath, &len, NULL, 0) != 0)
        exePath[0] = '\0';
#endif
    return std::string(exePath);
}

std::string GetDynamicLibPath(void *anyAddressInDyLib)
{
    static std::string DynamicLibPath = "";

    std::string ret = "";
    if (nullptr == anyAddressInDyLib)
    {
        ret = DynamicLibPath;
        goto exit;
    }
#if (BOOST_OS_WINDOWS)
    char path[MAX_PATH];
    HMODULE hm = NULL;
    int separator_pos;

    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)anyAddressInDyLib, &hm) == 0)
    {
        ret = "";
        goto exit;
    }
    if (GetModuleFileNameA(hm, path, sizeof(path)) == 0)
    {
        ret = "";
        goto exit;
    }

    ret.append(path);
    separator_pos = ret.rfind('\\');
    if (std::string::npos == separator_pos)
    {
        ret = "";
    }
    else
    {
        ret = ret.substr(0, separator_pos);
    }

#elif (BOOST_OS_MACOS)
    Dl_info info;
    if (!dladdr(anyAddressInDyLib, &info))
    {
        ret = "";
        goto exit;
    }
    ret = info.dli_fname;
    ret = ret.substr(0, ret.rfind("/"));
#endif
exit:
    DynamicLibPath = ret;
    return ret;
}
