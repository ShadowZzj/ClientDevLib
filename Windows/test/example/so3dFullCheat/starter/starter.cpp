#include <Windows.h>
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
#include <chrono>
#include <Windows/util/Process/ProcessHelper.h>
#include <General/util/Sync/ProcessSync.hpp>
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
    wi.vecHWnds = &vecHWnds;

    // 枚举所有顶级窗口
    EnumWindows(lpEnumFunc, // 回调函数指针
                (LPARAM)&wi // 传递给回调函数的值
    );
}

BOOL CALLBACK GetAllWindowHandleEnum(HWND hwnd, LPARAM lParam)
{
    std::vector<HWND> *pArg = (std::vector<HWND> *)lParam;
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
    if (argc != 2)
    {
        spdlog::error("Usage: {} <username>", argv[0]);
        return 1;
    }
    zzj::ProcessSync processSync("mutex_sealstarter");
    if (!processSync.try_lock())
    {
        spdlog::info("Process is already running, exit");
        return 0;
    }
    auto allWindow = GetAllWindowHandle();
    std::string username = argv[1];
    std::string targetWindowTitle = "SO3D Plus|";
    targetWindowTitle += username;
    for (auto window : allWindow)
    {
        CHAR szBuf_title[MAX_PATH]{0};
        auto res = GetWindowTextA(window,      // 窗口句柄
                                  szBuf_title, // 接收窗口标题的缓冲区指针
                                  MAX_PATH     // 缓冲区字节大小
        );
        if (res == 0)
            continue;
        std::string title = szBuf_title;
        if (title == targetWindowTitle)
        {
            spdlog::info("Username already login {}", username);
            return 0;
        }
    }
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    DEBUG_EVENT debugEvent;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    boost::filesystem::path executablePath = zzj::GetExecutablePath();
    executablePath /= "..\\Game\\SO3DPlus.exe";
    // absolute path
    executablePath = boost::filesystem::absolute(executablePath);
    spdlog::info("Executable path: {}", executablePath.string());
    std::wstring cmdLine = zzj::str::ansi2w(executablePath.string()) + L" \"^xhlrmxgkrhtlvdjdyTTdirmsgkrltlfjdy^\"";

    // std::wstring cmdLine =L"notepad.exe";
    //  �������̲���ʼ����
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
    std::vector<HWND> vecHWnds; // 进程下的窗体句柄数组
    static std::chrono::time_point<std::chrono::system_clock> start = std::chrono::system_clock::now();
    while (true)
    {
        std::chrono::duration<double> elapsed_seconds = std::chrono::system_clock::now() - start;
        if (elapsed_seconds.count() > 30)
        {
            spdlog::error("Timeout");
            zzj::Process::KillProcess(pi.dwProcessId);
            return 0;
        }
        GetHWndsByProcessID(pi.dwProcessId, vecHWnds);
        // 获取 HWND 窗口标题、窗口类名
        CHAR szBuf_title[MAX_PATH]{0};
        CHAR szBuf_class[MAX_PATH];
        for (const HWND &h : vecHWnds)
        {
            GetWindowTextA(h, szBuf_title, MAX_PATH);
            GetClassNameA(h, szBuf_class, MAX_PATH);
            std::string title = szBuf_title;
            std::string className = szBuf_class;
            if (title == "SO3D Plus")
            {
                SetWindowTextA(h, targetWindowTitle.c_str());
                boost::filesystem::path currentPath = zzj::GetExecutablePath();
                auto loginConfigPath = currentPath / "loginUserName.json";
                while (true)
                {
                    zzj::Process process(pi.dwProcessId,PROCESS_ALL_ACCESS);
                    if (!process.IsAlive())
                    {
                        spdlog::info("Process exit! exit the loader");
                        return 0;
                    }
                    if (boost::filesystem::exists(loginConfigPath))
                    {
                        std::ifstream ifs(loginConfigPath.string());
                        nlohmann::json j;
                        ifs >> j;
                        if(j.find(username) != j.end())
                        {
                            DWORD pid = j[username];
                            if(pid == pi.dwProcessId)
                            {
                                spdlog::info("Username  {} login success", username);
                                return 0;
                            }
                        }
                    }
                    Sleep(1000);
                }
                
            }
        }
    }
    return 0;
}