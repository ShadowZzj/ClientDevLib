#ifndef _G_FILE_H_
#define _G_FILE_H_

#include <boost/filesystem.hpp>
#include <string>

namespace zzj
{
bool IsDirExist(const char *dirPath);
bool IsFileExist(const char *filePath);
long GetFileSize(std::string filename);
std::string GetExecutablePath();
std::string GetDynamicLibPath(void *anyAddressInDyLib = nullptr);
class FileV2
{
  public:
    static int RenameCopy(boost::filesystem::path src, boost::filesystem::path dst, std::string suffix);
};

}; // namespace zzj
#endif
