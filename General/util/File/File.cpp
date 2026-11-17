#include "File.h"
#include <boost/predef/os.h>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <General/util/StrUtil.h>
#if (BOOST_OS_WINDOWS)
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

long zzj::GetFileSize(std::string filename)
{
    struct stat stat_buf;
    int rc = stat(filename.c_str(), &stat_buf);
    return rc == 0 ? stat_buf.st_size : -1;
}

bool zzj::IsDirExist(const char *dirPath)
{
    struct stat info;
    if (stat(dirPath, &info) != 0)
        return false;
    else if (info.st_mode & S_IFDIR)
        return true;
    else
        return false;
}
bool zzj::IsFileExist(const char *filePath)
{
    struct stat buffer;
    return (stat(filePath, &buffer) == 0);
}
/*
 * Returns the full path to the currently running executable,
 * or an empty string in case of failure.
 */
std::string zzj::GetExecutablePath()
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

std::string zzj::GetDynamicLibPath(void *anyAddressInDyLib)
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

int zzj::FileV2::RenameCopy(boost::filesystem::path src, boost::filesystem::path dst,std::string suffix)
{
    int result = 0;
    try
    {
        std::map<std::string, std::string> oldToNew;

        for (auto &it : boost::filesystem::recursive_directory_iterator(src))
        {
            if (boost::filesystem::is_directory(it))
                continue;
            
            boost::filesystem::path oldFile = dst / it.path().filename();            
            boost::filesystem::path newFile = dst / (it.path().filename().string() +  "_" + suffix);
            try {
                if(!boost::filesystem::exists(oldFile))
                    continue;
                boost::filesystem::rename(oldFile, newFile);
                oldToNew.emplace(oldFile.string(), newFile.string());
            } catch (std::exception & ex) {
                spdlog::error("Filesystem rename exception {}",ex.what());
                //rollback
                for (auto [key, value] : oldToNew)
                    boost::filesystem::rename(value, key);
                return -2;
            }
        }
        
#ifdef _WIN32

        std::filesystem::copy(src.string(), dst.string(),
                              std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing);
#else
        result = zzj::File::CopyFilesFromTo(src.c_str(), dst.c_str());
        if (0!=result)
            throw std::runtime_error(std::string("copy file error with ")+std::to_string(result));
        
#endif
    }
    catch (const std::exception &exp)
    {
        spdlog::error("copy error: {}", exp.what());
        result = -1;
    }
    return result;
}
