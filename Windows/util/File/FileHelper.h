#pragma once
#include "File.h"
#include <General/util/Lua/LuaExport.hpp>
#include <Windows.h>
#include <functional>
#include <map>
#include <string>
namespace zzj
{
class FileHelper
{
  public:
    static bool ReadFileAtOffset(std::string fileName, void *buffer, unsigned long numToRead, unsigned long fileOffset);
    static bool IsFileExist(std::string fileName);
    static bool DeleteFileReboot(const std::string &fileName);
    static bool DeleteDirPossible(const std::string &filePathName);
    static int RemoveDirectoryRecursive(std::string path);
    static std::string GetExecutablePath();
    static std::string GetDllPath(void *dllAnyFunctionAddress);
    static std::string GetProgramDataPath(std::string appDir);
    static std::string GetProgramDataPath();
    static std::string GetProgramPath();
    static std::string GetCurrentUserProgramDataFolder();
    static zzj::File GetFileInstance(const std::string &imagePath);

  protected:
    DECLARE_LUA_EXPORT(FileHelper)
};
} // namespace zzj