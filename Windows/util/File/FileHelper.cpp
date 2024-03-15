#include "FileHelper.h"
#include <General/util/BaseUtil.hpp>
#include <General/util/Process/Process.h>
#include <Shlobj.h>
#include <Windows/util/HandleHelper.h>
#include <Windows/util/Process/ProcessHelper.h>
#include <Windows/util/Process/ThreadHelper.h>
#include <algorithm>
#include <direct.h>
#include <io.h>
#include <string>
#include <sys/stat.h>
#include <tchar.h>
#include <windows.h>
#include <wtsapi32.h>

using namespace zzj;

bool FileHelper::ReadFileAtOffset(std::string fileName, void *buffer, unsigned long numToRead, unsigned long fileOffset)
{

    if (fileName.empty() || !buffer || fileOffset < 0)
        return false;

    ScopeKernelHandle fileHandle =
        CreateFileA(fileName.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fileHandle == INVALID_HANDLE_VALUE)
        return false;

    DWORD bytesRead;
    bool bRet;

    DWORD dwRet = SetFilePointer(fileHandle, fileOffset, NULL, FILE_BEGIN);
    if (dwRet == INVALID_SET_FILE_POINTER)
        return false;

    bRet = ReadFile(fileHandle, buffer, numToRead, &bytesRead, NULL);
    if (!bRet)
        return false;
    if (numToRead != bytesRead)
        return false;

    return true;
}

#pragma warning(disable : 4996)
bool zzj::FileHelper::IsFileExist(std::string fileName)
{
    if (FILE *file = fopen(fileName.c_str(), "r"))
    {
        fclose(file);
        return true;
    }
    else
        return false;
}

int zzj::FileHelper::RemoveDirectoryRecursive(std::string path)
{
    std::string str(path);
    if (!str.empty())
    {
        while (*str.rbegin() == '\\' || *str.rbegin() == '/')
        {
            str.erase(str.size() - 1);
        }
    }
    replace(str.begin(), str.end(), '/', '\\');

    struct stat sb;
    if (stat((char *)str.c_str(), &sb) != 0)
    {
        return 0;
    }

    if (S_IFDIR && sb.st_mode)
    {
        HANDLE hFind;
        WIN32_FIND_DATAA FindFileData;

        char DirPath[MAX_PATH];
        char FileName[MAX_PATH];

        strcpy(DirPath, (char *)str.c_str());
        strcat(DirPath, "\\*");
        strcpy(FileName, (char *)str.c_str());
        strcat(FileName, "\\");

        hFind = FindFirstFileA(DirPath, &FindFileData);
        if (hFind == INVALID_HANDLE_VALUE)
            return -1;
        strcpy(DirPath, FileName);

        bool bSearch = true;
        while (bSearch)
        {
            if (FindNextFileA(hFind, &FindFileData))
            {
                if (!(strcmp(FindFileData.cFileName, ".") && strcmp(FindFileData.cFileName, "..")))
                    continue;
                strcat(FileName, FindFileData.cFileName);
                if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                {
                    if (0 != RemoveDirectoryRecursive(FileName))
                    {
                        FindClose(hFind);
                        return -2;
                    }
                    RemoveDirectoryA(FileName);
                    strcpy(FileName, DirPath);
                }
                else
                {
                    // if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                    //     _chmod(FileName, _S_IWRITE);

                    if (!DeleteFileA(FileName))
                    {
                        FindClose(hFind);
                        return -3;
                    }
                    strcpy(FileName, DirPath);
                }
            }
            else
            {
                if (GetLastError() == ERROR_NO_MORE_FILES)
                    bSearch = false;
                else
                {
                    FindClose(hFind);
                    return -4;
                }
            }
        }
        FindClose(hFind);

        return (int)(RemoveDirectoryA((char *)str.c_str()) == FALSE);
    }
    else
    {
        return -5;
    }
}

std::string zzj::FileHelper::GetExecutablePath()
{
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
}

std::string zzj::FileHelper::GetDllPath(void *dllAnyFunctionAddress)
{
    char path[MAX_PATH];
    HMODULE hm      = NULL;
    std::string ret = "";
    int separator_pos;

    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)dllAnyFunctionAddress, &hm) == 0)
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
exit:
    return ret;
}
std::string zzj::FileHelper::GetProgramPath()
{
    char path[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PROGRAM_FILES, NULL, 0, path) == S_OK)
    {
        return std::string(path);
    }
    else
        return "";
}

std::string zzj::FileHelper::GetCurrentUserProgramDataFolder()
{

    zzj::Process currentProcess;

    if (auto [result, res] = currentProcess.IsServiceProcess(); result == 0)
    {
        HANDLE handle = NULL;
        DEFER
        {
            if (handle != NULL)
                zzj::Thread::RevertToCurrentUser(handle);
        };
        if (res)
        {
            handle = zzj::Thread::ImpersonateCurrentUser();
            if (NULL == handle)
                return "";
        }
        char szAppDataPath[MAX_PATH];

        // 获取AppData\Roaming目录的路径
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, szAppDataPath)))
        {
            return std::string(szAppDataPath);
        }
        else
        {
            return "";
        }
    }

    return "";
}
std::string zzj::FileHelper::GetProgramDataPath()
{
    std::string ret;
    HRESULT hResult      = S_OK;
    int iRet             = 0;
    DWORD dwNameLen      = MAX_PATH;
    char sPath[MAX_PATH] = {0};
    hResult              = SHGetFolderPathA(NULL, CSIDL_COMMON_APPDATA, NULL, 0, sPath);
    if (S_OK != hResult)
    {
        return "";
    }

    iRet = _access(sPath, 0);
    if (iRet == -1)
    {
        _mkdir(sPath);
    }

    iRet = _access(sPath, 0);
    if (iRet == -1)
    {
        SECURITY_ATTRIBUTES SecAttr;
        SECURITY_DESCRIPTOR SecDesc;

        SecAttr.nLength              = sizeof(SecAttr);
        SecAttr.bInheritHandle       = FALSE;
        SecAttr.lpSecurityDescriptor = &SecDesc;

        InitializeSecurityDescriptor(&SecDesc, SECURITY_DESCRIPTOR_REVISION);
        SetSecurityDescriptorDacl(&SecDesc, TRUE, 0, FALSE);
        CreateFileA(sPath, 0, 0, &SecAttr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, 0);
    }
    ret = sPath;
    return ret;
}

std::string zzj::FileHelper::GetProgramDataPath(std::string appDir)
{
    std::string programDataFolder = GetProgramDataPath();
    char sPath[MAX_PATH]          = {0};
    strcpy_s(sPath, programDataFolder.c_str());
    strcat_s(sPath, "\\");
    strcat_s(sPath, appDir.c_str());

    int iRet = _access(sPath, 0);
    if (iRet == -1)
    {
        _mkdir(sPath);
    }

    iRet = _access(sPath, 0);
    if (iRet == -1)
    {
        SECURITY_ATTRIBUTES SecAttr;
        SECURITY_DESCRIPTOR SecDesc;

        SecAttr.nLength              = sizeof(SecAttr);
        SecAttr.bInheritHandle       = FALSE;
        SecAttr.lpSecurityDescriptor = &SecDesc;

        InitializeSecurityDescriptor(&SecDesc, SECURITY_DESCRIPTOR_REVISION);
        SetSecurityDescriptorDacl(&SecDesc, TRUE, 0, FALSE);
        CreateFileA(sPath, 0, 0, &SecAttr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, 0);
    }
    return sPath;
}

zzj::File zzj::FileHelper::GetFileInstance(const std::string &imagePath)
{
    return zzj::File(imagePath);
}

bool zzj::FileHelper::DeleteFileReboot(const std::string &filename)
{
    char szTemp[MAX_PATH] = "\\\\?\\";
    ::lstrcatA(szTemp, filename.c_str());
    bool bRet = ::MoveFileExA(szTemp, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);

    return bRet;
}

bool zzj::FileHelper::DeleteDirPossible(const std::string &filePathName)
{
    bool bRet = false;
    if (filePathName.empty())
    {
        return bRet;
    }
    if (filePathName.length() > MAX_PATH)
    {
        return bRet;
    }

    //遍历文件夹
    std::string strDir = filePathName;
    if (strDir[strDir.length() - 1] != '\\')
    {
        strDir += "\\";
    }
    strDir += "*.*";
    WIN32_FIND_DATAA wfd;
    HANDLE hFind = ::FindFirstFileA(strDir.c_str(), &wfd);
    if (INVALID_HANDLE_VALUE == hFind)
    {
        return bRet;
    }
    do
    {
        if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (strcmp(wfd.cFileName, ".") == 0 || strcmp(wfd.cFileName, "..") == 0)
            {
                continue;
            }
            std::string strSubDir = filePathName;
            strSubDir += "\\";
            strSubDir += wfd.cFileName;
            bRet = DeleteDirPossible(strSubDir);
        }
        else
        {
            std::string strFilePathName = filePathName;
            strFilePathName += "\\";
            strFilePathName += wfd.cFileName;

            int removeRet = std::remove(strFilePathName.c_str());
            if (removeRet != 0)
            {
                // rename file
                std::string strTemp = strFilePathName;
                // add time
                char szTime[32] = {0};
                SYSTEMTIME st;
                ::GetLocalTime(&st);
                ::wsprintfA(szTime, "%04d%02d%02d%02d%02d%02d%03d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                            st.wSecond, st.wMilliseconds);
                strTemp += szTime;
                strTemp += ".tmp";
                std::rename(strFilePathName.c_str(), strTemp.c_str());
                DeleteFileReboot(strTemp);
            }
        }
    } while (::FindNextFileA(hFind, &wfd));
    return bRet;
}
std::string zzj::FileHelper::GetProgramFilesFolder()
{
    char path[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PROGRAM_FILES, NULL, 0, path) == S_OK)
    {
        return std::string(path);
    }
    else
        return "";
}
