#pragma once
#ifdef DEBUG
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#include <stdlib.h>
#endif // DEBUG

#include <Windows/util/File/FileHelper.h>
#include <DbgHelp.h>
#include <stdio.h>
#include <string>
#include <time.h>
#include <windows.h>

#pragma comment(lib, "DbgHelp.lib")
#pragma warning(disable : 4996)
static std::string GetExecutablePath()
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

inline LONG WINAPI WriteDumpHandler(struct _EXCEPTION_POINTERS *pExceptionInfo)
{
    // ����EXCEPTION_CONTINUE_SEARCH���ó���ֹͣ����
    LONG ret = EXCEPTION_EXECUTE_HANDLER;

    time_t nowtime;
    time(&nowtime);
    struct tm *pTime                 = localtime(&nowtime);
    char params[128]                 = {0};
    char current_proc_path[MAX_PATH] = {0};
    ::GetModuleFileNameA(NULL, current_proc_path, MAX_PATH);

    std::string fileName = current_proc_path;
    // ����core�ļ�����Ŀ¼���ļ���

    fileName += ".dmp";
    if (zzj::FileHelper::IsFileExist(fileName))
        remove(fileName.c_str());
    HANDLE hFile = ::CreateFileA(fileName.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION ExInfo;

        ExInfo.ThreadId          = ::GetCurrentThreadId();
        ExInfo.ExceptionPointers = pExceptionInfo;
        ExInfo.ClientPointers    = NULL;

        // write the dump
        BOOL bOK =
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpNormal, &ExInfo, NULL, NULL);
        ret = EXCEPTION_EXECUTE_HANDLER;
        ::CloseHandle(hFile);
    }

    return ret;
}

inline BOOL PreventSetUnhandledExceptionFilter()
{
    HMODULE hKernel32 = LoadLibraryA("kernel32.dll");

    if (hKernel32 == NULL)
        return FALSE;

    void *pOrgEntry = GetProcAddress(hKernel32, "SetUnhandledExceptionFilter");

    if (pOrgEntry == NULL)
        return FALSE;

    unsigned char newJump[100];

    DWORD dwOrgEntryAddr = (DWORD)pOrgEntry;

    dwOrgEntryAddr += 5; // add 5 for 5 op-codes for jmp far

    void *pNewFunc = &WriteDumpHandler;

    DWORD dwNewEntryAddr = (DWORD)pNewFunc;
    DWORD dwRelativeAddr = dwNewEntryAddr - dwOrgEntryAddr;
    newJump[0]           = 0xE9; // JMP absolute
    memcpy(&newJump[1], &dwRelativeAddr, sizeof(pNewFunc));
    SIZE_T bytesWritten;
    BOOL bRet = WriteProcessMemory(GetCurrentProcess(), pOrgEntry, newJump, sizeof(pNewFunc) + 1, &bytesWritten);
    return bRet;
}