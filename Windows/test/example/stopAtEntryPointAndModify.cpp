#include <Windows.h>
#include <iostream>
#include <string>
#include <UserEnv.h>
#include <General/util/File/File.h>
#include <boost/filesystem.hpp>
#include <TlHelp32.h>
#include <General/util/StrUtil.h>
#pragma comment(lib, "Userenv.lib")

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
    EnviromentChanger();
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    DEBUG_EVENT debugEvent;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    boost::filesystem::path executablePath = zzj::GetExecutablePath();
    executablePath /= "SO3DPlus1.exe";
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

                        ReadProcessMemory(hProcess, (LPVOID)0x10025CD0, buffer, 0x1, NULL);
                        BYTE writeContent = 0xC3;
                        if (buffer[0] == 0x55)
                            WriteProcessMemory(hProcess, (LPVOID)0x10025CD0, &writeContent, 1, &retSize);

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
                        std::cout << "ExitProcess" << std::endl;
                        ExitProcess(0);
                    }
                }
            }
            break;
        }
        default:
            continueStatus = DBG_EXCEPTION_NOT_HANDLED;
            break;
        }

        // �������Խ���
        ContinueDebugEvent(debugEvent.dwProcessId, debugEvent.dwThreadId, continueStatus);
    }
    return 0;
}