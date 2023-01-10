#ifndef ZZJFILE
#define ZZJFILE

#include <Windows.h>
#include <functional>
#include <map>
#include <string>
namespace zzj
{

class File
{

  public:
    class FileInfo
    {
      public:
        FileInfo(const std::string &imageName);
        ~FileInfo();

      public:
        std::int64_t GetFileSize();

      private:
        std::string m_imageName;
    };
    File(const std::string &imageName);
    ~File();
    FileInfo GetFileInfo();
  private:
    std::string m_imageName;
};

} // namespace zzj
#endif //
