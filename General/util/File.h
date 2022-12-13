#ifndef _G_FILE_H_
#define _G_FILE_H_

#include <boost/filesystem.hpp>
#include <string>

bool IsDirExist(const char *dirPath);
bool IsFileExist(const char *filePath);
long GetFileSize(std::string filename);
std::string GetExecutablePath();
std::string GetDynamicLibPath(void *anyAddressInDyLib = nullptr);

namespace zzj
{
class FileV2
{
  public:
    static int RenameCopy(boost::filesystem::path src, boost::filesystem::path dst,std::string suffix);
};

}; // namespace zzj
#endif
