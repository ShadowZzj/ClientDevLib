#include <Windows.h>﻿
#include <GHInjector/Injection.h>
#include <General/util/File/File.h>
#include <General/util/StrUtil.h>
#include <TlHelp32.h>
#include <UserEnv.h>
#include <boost/filesystem.hpp>
#include <iostream>
#include <spdlog/spdlog.h>
#include <string>
#include <General/util/Process/Process.h>
#include <json.hpp>
#pragma comment(lib, "Userenv.lib")
typedef struct EnumHWndsArg
{
    std::vector<HWND> *vecHWnds;
    DWORD dwProcessId;
} EnumHWndsArg, *LPEnumHWndsArg;

// 判断窗口是否属于目标进程
BOOL CALLBACK lpEnumFunc(HWND hwnd, LPARAM lParam)
{
    EnumHWndsArg *pArg = (LPEnumHWndsArg)lParam;
    DWORD processId;

    // 检索窗口线程标识符
    GetWindowThreadProcessId(hwnd,      // 窗口句柄
                             &processId // 接收 PID 的指针
    );

    // 如果这个 HWND 属于这个 PID ，则加入到 vecHWnds 数组末尾
    if (processId == pArg->dwProcessId)
    {
        pArg->vecHWnds->push_back(hwnd);
    }

    return TRUE;
}

// 根据 PID 获取 HWND
void GetHWndsByProcessID(DWORD processID, std::vector<HWND> &vecHWnds)
{
    EnumHWndsArg wi;
    wi.dwProcessId = processID;
    wi.vecHWnds    = &vecHWnds;

    // 枚举所有顶级窗口
    EnumWindows(lpEnumFunc, // 回调函数指针
                (LPARAM)&wi // 传递给回调函数的值
    );
}

BOOL CALLBACK GetAllWindowHandleEnum(HWND hwnd, LPARAM lParam)
{
    std::vector<HWND> *pArg = (std::vector<HWND>*)lParam;
    pArg->push_back(hwnd);
    return TRUE;
}
std::vector<HWND> GetAllWindowHandle()
{
    std::vector<HWND> ret;
    EnumWindows(GetAllWindowHandleEnum, (LPARAM)&ret);
    return ret;
}
int main(int argc, char *argv[])
{
    
    boost::filesystem::path executablePath = zzj::GetExecutablePath();
    executablePath /= "ClientDevLib_test.exe";
    // absolute path
    executablePath = boost::filesystem::absolute(executablePath);
    spdlog::info("Executable path: {}", executablePath.string());

    //boost::filesystem::path testPath = zzj::GetExecutablePath();
    //testPath /= "ClientDevLib_testdll.dll";
    //LoadLibraryA(testPath.string().c_str());
    while (true)
    {
        try
        {
            
            nlohmann::json j;
            boost::filesystem::path configPath = zzj::GetExecutablePath();
            configPath /= "autologin.json";
            std::ifstream ifs(configPath.string());
            ifs >> j;
            ifs.close();

            std::vector<std::string> userNames = j["userNames"];
            for (auto& username:userNames)
            {
                STARTUPINFO si;
                PROCESS_INFORMATION pi;

                ZeroMemory(&si, sizeof(si));
                si.cb = sizeof(si);
                ZeroMemory(&pi, sizeof(pi));

                std::wstring cmdLine = zzj::str::ansi2w(executablePath.string()) + L" \"" + zzj::str::ansi2w(username) + L"\"";
                if (!CreateProcessW(NULL,                       // ��ִ���ļ�·��
                            (LPWSTR)cmdLine.c_str(),    // �����в���
                            NULL,                       // ���̰�ȫ����
                            NULL,                       // �̰߳�ȫ����
                            FALSE,                      // ����̳�ѡ��
                            CREATE_UNICODE_ENVIRONMENT, // ������־��ֻ�����������
                            NULL,                       // ʹ�ø����̵Ļ�����
                            NULL,                       // ʹ�ø����̵���ʼĿ¼
                            &si,                        // STARTUPINFO ָ��
                            &pi                         // PROCESS_INFORMATION ָ��
                            ))
                {
                    std::cerr << "CreateProcess failed (" << GetLastError() << ")\n";
                    return 1;
                }
                CloseHandle(pi.hProcess);
                Sleep(2000);
            }
            
        }
        catch(const std::exception& e)
        {
            Sleep(5000);
            continue;
        }
        Sleep(5000);
    }
    return 0;
}