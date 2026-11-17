#include <Windows.h>
#include <iostream>
#include <string>
#include <UserEnv.h>
#include <General/util/File/File.h>
#include <boost/filesystem.hpp>
#include <TlHelp32.h>
#include <General/util/StrUtil.h>
#include <GHInjector/Injection.h>
#include <boost/filesystem.hpp>
#include <spdlog/spdlog.h>
#pragma comment(lib, "Userenv.lib")

void InjectDll(boost::filesystem::path dllName, DWORD TargetProcessId)
{
    std::string libPath =
        R"(D:\repos\red-pass\redpass-daemon\ClientDevLib\Windows\build\x86\Release\bin\GH Injector - x86.dll)";
    HINSTANCE hInjectionMod = LoadLibraryA(libPath.c_str());

    auto InjectA               = (f_InjectA)GetProcAddress(hInjectionMod, "InjectA");
    auto GetSymbolState        = (f_GetSymbolState)GetProcAddress(hInjectionMod, "GetSymbolState");
    auto GetImportState        = (f_GetSymbolState)GetProcAddress(hInjectionMod, "GetImportState");
    auto StartDownload         = (f_StartDownload)GetProcAddress(hInjectionMod, "StartDownload");
    auto GetDownloadProgressEx = (f_GetDownloadProgressEx)GetProcAddress(hInjectionMod, "GetDownloadProgressEx");

    // due to a minor bug in the current version you have to wait a bit before starting the download
    // will be fixed in version 4.7
    Sleep(500);

    StartDownload();

    // since GetSymbolState and GetImportState only return after the downloads are finished
    // checking the download progress is not necessary
    while (GetDownloadProgressEx(PDB_DOWNLOAD_INDEX_NTDLL, false) != 1.0f)
    {
        Sleep(10);
    }

#ifdef _WIN64
    while (GetDownloadProgressEx(PDB_DOWNLOAD_INDEX_NTDLL, true) != 1.0f)
    {
        Sleep(10);
    }
#endif

    while (GetSymbolState() != 0)
    {
        Sleep(10);
    }

    while (GetImportState() != 0)
    {
        Sleep(10);
    }


    INJECTIONDATAA data = {"",
                           TargetProcessId,
                           INJECTION_MODE::IM_LoadLibraryExW,
                           LAUNCH_METHOD::LM_NtCreateThreadEx,
                           NULL,
                           5000,
                           NULL,
                           NULL,
                           true};

    strcpy(data.szDllPath, dllName.string().c_str());

    spdlog::info("Before Inject {}",data.szDllPath);
    spdlog::info("pid {}",data.ProcessID);
    auto ret = InjectA(&data);
    spdlog::info("Inject ret :{}", ret);
}
DWORD FindProcessId(const std::wstring &processName)
    {
    DWORD processId  = 0;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(PROCESSENTRY32);
        if (Process32First(hSnapshot, &pe))
        {
            do
            {
                if (std::wstring(pe.szExeFile) == processName)
                {
                    processId = pe.th32ProcessID;
                    break;
                }
            } while (Process32Next(hSnapshot, &pe));
        }
        CloseHandle(hSnapshot);
    }
    return processId;
}
void EnviromentChanger()
{
    // ��ȡ��ǰ���̵Ļ�����
    LPTCH lpvEnv = GetEnvironmentStrings();

    // ��� 'test' ���������Ƿ����
    LPTSTR lpszVariable;
    bool testExists = false;
    for (lpszVariable = (LPTSTR)lpvEnv; *lpszVariable; lpszVariable++)
    {
        std::wcout << lpszVariable << std::endl;
        if (wcsncmp(lpszVariable, L"_NO_DEBUG_HEAP=1", sizeof(L"_NO_DEBUG_HEAP=")) == 0)
        {
            testExists = true;
            break;
        }
        while (*lpszVariable)
        {
            lpszVariable++;
        }
    }

    // ��� 'test' �������������ڣ�������
    if (!testExists)
    {
        if(!SetEnvironmentVariableW(L"_NO_DEBUG_HEAP", L"1"))
        {
            std::cout << "error!@!@#@!#!@#" << std::endl;
        }
        else
        {
            std::cout << "success!@!@#@!#!@#" << std::endl;
        }
    }
}
int main()
{
    SetCurrentDirectoryA("C:\\");
    EnviromentChanger();
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    DEBUG_EVENT debugEvent;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    boost::filesystem::path executablePath = zzj::GetExecutablePath();
    executablePath = "D:\\777\\SO3DPlus.exe";
    std::wstring cmdLine = zzj::str::ansi2w(executablePath.string()) + L" \"^xhlrmxgkrhtlvdjdyTTdirmsgkrltlfjdy^\"";

    // std::wstring cmdLine =L"notepad.exe";
    //  �������̲���ʼ����
    if (!CreateProcessW(NULL,                    // ��ִ���ļ�·��
                        (LPWSTR)cmdLine.c_str(), // �����в���
                        NULL,                    // ���̰�ȫ����
                        NULL,                    // �̰߳�ȫ����
                        FALSE,                   // ����̳�ѡ��
                        DEBUG_ONLY_THIS_PROCESS | CREATE_UNICODE_ENVIRONMENT, // ������־��ֻ�����������
                        NULL,                    // ʹ�ø����̵Ļ�����
                        NULL,                    // ʹ�ø����̵���ʼĿ¼
                        &si,                     // STARTUPINFO ָ��
                        &pi                      // PROCESS_INFORMATION ָ��
                        ))
    {
        std::cerr << "CreateProcess failed (" << GetLastError() << ")\n";
        return 1;
    }

    HANDLE hProcess = pi.hProcess;
    char buffer[1024]{0};
    SIZE_T retSize;

    bool entryPointReached = false;
    LPVOID entryPoint      = nullptr;
    BYTE originalByte;



    bool taskDone = false;
    while (WaitForDebugEvent(&debugEvent, INFINITE))
    {
        DWORD continueStatus = DBG_CONTINUE;
        // ����¼�����
        switch (debugEvent.dwDebugEventCode)
        {
        case CREATE_PROCESS_DEBUG_EVENT: {
            //// ��ȡ��ڵ��ַ
            entryPoint = debugEvent.u.CreateProcessInfo.lpStartAddress;
            //// ����ԭʼ�ֽ�
            ReadProcessMemory(pi.hProcess, entryPoint, &originalByte, 1, NULL);
            // д��INT3�ϵ�
            BYTE int3 = 0xCC;
            WriteProcessMemory(pi.hProcess, entryPoint, &int3, 1, NULL);
            break;
        }
        case EXCEPTION_DEBUG_EVENT: {
            if (debugEvent.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT)
            {
                PVOID exceptionAddress = (PVOID)debugEvent.u.Exception.ExceptionRecord.ExceptionAddress;
                if (exceptionAddress == entryPoint)
                {
                    if (!entryPointReached)
                    {
                        entryPointReached = true;
                        WriteProcessMemory(pi.hProcess, entryPoint, &originalByte, 1, NULL);

                        ReadProcessMemory(hProcess, (LPVOID)0x10026b20, buffer, 0x1, NULL);
                        BYTE writeContent = 0xC3;
                        if (buffer[0] == 0x55)
                            WriteProcessMemory(hProcess, (LPVOID)0x10026b20, &writeContent, 1, &retSize);

                        writeContent = 0x11;
                        WriteProcessMemory(hProcess, (LPVOID)0x10060ed5, &writeContent, 1, &retSize);
                        HANDLE threadHandle =
                            OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, debugEvent.dwThreadId);
                        CONTEXT context;
                        context.ContextFlags = CONTEXT_CONTROL;
                        bool res             = GetThreadContext(threadHandle, &context);
                        context.Eip--; // ����EIP��ָ��ϵ�λ��
                        res = SetThreadContext(threadHandle, &context);
                        CloseHandle(threadHandle);


                        ContinueDebugEvent(debugEvent.dwProcessId, debugEvent.dwThreadId, continueStatus);
                        DebugActiveProcessStop(pi.dwProcessId);
                        taskDone = true;
                        break;
                    }
                }
            }
            break;
        }
        default:
            continueStatus = DBG_EXCEPTION_NOT_HANDLED;
            break;
        }
        if (taskDone)
            break;
        // �������Խ���
        ContinueDebugEvent(debugEvent.dwProcessId, debugEvent.dwThreadId, continueStatus);
    }
    DWORD pid = pi.dwProcessId;
    CloseHandle(pi.hProcess);
    std::string libPath =
        R"(D:\repos\red-pass\redpass-daemon\ClientDevLib\Windows\build\x86\Release\bin\ClientDevLib_testdll.dll)";
    boost::filesystem::path dllPath = libPath;
    InjectDll(dllPath, pid);

    return 0;
}