#ifndef _G_FILE_H_
#define _G_FILE_H_

#include <string>
bool IsDirExist(const char *dirPath);
bool IsFileExist(const char* filePath);
long GetFileSize(std::string filename);
std::string GetExecutablePath();
std::string GetDynamicLibPath(void *anyAddressInDyLib = nullptr);

// KepmSpecific
std::string GetInstallFolder();
std::string GetLogFolder();

#endif
