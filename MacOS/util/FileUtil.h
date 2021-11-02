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