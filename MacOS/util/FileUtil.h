#ifndef _MAC_FILEUTIL_H_
#define _MAC_FILEUTIL_H_

#include <boost/filesystem.hpp>
#include <iostream>
#include <string>

namespace zzj
{
class File
{
  public:
    enum class Type : char
    {
        File,
        Directory
    };
    
    class Permission
    {
    public:
        bool canRead;
        bool canWrite;
        bool canExecute;
    };
    enum class PermissionRole : char
    {
        Owner,
        GroupMember,
        Others
    };
    static int ReadFileAtOffset(const char *fileName, char *buf, size_t size, int pos);
    static std::string GetTempDir();
    static int IsFileExist(const char *filePath);
    static unsigned long GetFileSize(const char *filePath);
    static int IsDirExist(const char *dirPath);
    static int RemoveDirRecursive(const char *dir);
    static int CopyFilesFromTo(const char *srcPath, const char *dstPath);
    static std::string GetCurrentModulePath();
    static std::string GetExecutablePath();
    static int MkdirRecursive(std::string dirPath, int mode);
    static int ChmodIfExe(std::string fileFullPath, mode_t mode);
    static std::string GetSystemAppDataFolder();
    static std::string GetCurrentUserAppDataFolder();
    static File CreateInstance(const std::string fileFullPath);
    
    mode_t GetPosixFilePermission();
    bool IsUserCanRead(uid_t _uid);
    bool IsUserCanWrite(uid_t _uid);
    bool IsUserCanExecute(uid_t _uid);
    bool IsGroupCanRead(gid_t _gid);
    bool IsGroupCanWrite(gid_t _gid);
    bool IsGroupCanExecute(gid_t _gid);
    
    bool IsUserCanChangePermission(uid_t _uid);
    int ChangeOwner(uid_t _uid);
    int ChangeGroup(gid_t _gid);
    int SetPermission(mode_t _permission);
    
    std::string fileFullPath;
    uid_t uid;
    gid_t gid;
    std::string ownerName;
    std::string groupName;
    zzj::File::Type type;
    uint64_t fileSize;
    std::map<zzj::File::PermissionRole,zzj::File::Permission> permissions;
    
private:
    File()
    {}
    mode_t _filePermission;
    
};
} // namespace zzj

template <typename UnaryFunc> int ForEachFileInDir(std::string dirPath, UnaryFunc func)
{
    boost::filesystem::path source = dirPath;
    namespace fs                   = boost::filesystem;
    try
    {
        // Check whether the function call is valid
        if (!fs::exists(source) || !fs::is_directory(source))
        {
            std::cerr << "Source directory " << source.string() << " does not exist or is not a directory." << '\n';
            return -1;
        }
    }
    catch (fs::filesystem_error const &e)
    {
        std::cerr << e.what() << '\n';
        return -2;
    }
    // Iterate through the source directory
    for (fs::directory_iterator file(source); file != fs::directory_iterator(); ++file)
    {
        try
        {
            fs::path current(file->path());
            if (fs::is_directory(current))
            {
                // Found directory: Recursion
                if (0 != ForEachFileInDir(current.c_str(), func))
                    return -3;
            }
            else
            {
                if (0 != func(current.generic_string()))
                    return -4;
            }
        }
        catch (fs::filesystem_error const &e)
        {
            std::cerr << e.what() << '\n';
            return -5;
        }
    }
    return 0;
}
#endif
