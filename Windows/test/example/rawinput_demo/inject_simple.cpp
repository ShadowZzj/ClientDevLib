#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <TlHelp32.h>
#include <conio.h>

#include <cstdio>
#include <string>
#include <vector>

static std::string GetBaseName(const std::string& path)
{
    size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

static std::string GetExeDir()
{
    char buf[MAX_PATH]{};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    char* slash = strrchr(buf, '\\');
    if (slash) *slash = '\0';
    return buf;
}

static std::string MakeAbsoluteIfNeeded(const std::string& maybePath)
{
    if (maybePath.find(":\\") != std::string::npos || (!maybePath.empty() && maybePath[0] == '\\'))
        return maybePath;
    return GetExeDir() + "\\" + maybePath;
}

static DWORD FindProcessIdByName(const char* exeName)
{
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;

    int wlen = MultiByteToWideChar(CP_ACP, 0, exeName, -1, nullptr, 0);
    std::vector<wchar_t> wname(wlen);
    MultiByteToWideChar(CP_ACP, 0, exeName, -1, wname.data(), wlen);

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe))
    {
        do
        {
            if (_wcsicmp(pe.szExeFile, wname.data()) == 0)
            {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return pid;
}

static DWORD ParsePidOrProcessName(const char* arg)
{
    char* endptr = nullptr;
    unsigned long pid = strtoul(arg, &endptr, 10);
    if (endptr && *endptr == '\0' && pid > 0 && pid <= UINT32_MAX)
        return static_cast<DWORD>(pid);

    return FindProcessIdByName(arg);
}

static bool IsModuleLoaded(DWORD pid, const char* moduleBaseName)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE)
        return false;

    int wlen = MultiByteToWideChar(CP_ACP, 0, moduleBaseName, -1, nullptr, 0);
    std::vector<wchar_t> wname(wlen);
    MultiByteToWideChar(CP_ACP, 0, moduleBaseName, -1, wname.data(), wlen);

    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    bool found = false;
    if (Module32FirstW(snap, &me))
    {
        do
        {
            if (_wcsicmp(me.szModule, wname.data()) == 0)
            {
                found = true;
                break;
            }
        } while (Module32NextW(snap, &me));
    }

    CloseHandle(snap);
    return found;
}

static void PrintUsage(const char* exeName)
{
    printf("Usage: %s [--no-wait] <pid|process_name> <dll_path>\n", exeName);
    printf("\n");
    printf("Arguments:\n");
    printf("  --no-wait          Do not wait for ENTER before exit (non-interactive mode)\n");
    printf("  pid|process_name  Process ID (number) or process name (e.g., dota2.exe)\n");
    printf("  dll_path          Full path or relative path to the DLL to inject\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s 12345 C:\\path\\to\\mydll.dll\n", exeName);
    printf("  %s dota2.exe sdl3_hook_dll.dll\n", exeName);
    printf("  %s notepad.exe ..\\..\\mydll.dll\n", exeName);
    printf("  %s --no-wait dota2.exe sdl3_hook_dll.dll\n", exeName);
}

static void WaitBeforeExit()
{
    printf("\n[inject] Press ENTER to exit...\n");
    fflush(stdout);
    while (true)
    {
        int ch = _getch();
        if (ch == '\r' || ch == '\n')
            break;
    }
}

static bool InjectByCreateRemoteThread(DWORD pid, const std::string& dllPath)
{
    const DWORD access =
        PROCESS_CREATE_THREAD |
        PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE |
        PROCESS_VM_READ;

    HANDLE hProcess = OpenProcess(access, FALSE, pid);
    if (!hProcess)
    {
        printf("[inject] OpenProcess failed: %lu\n", GetLastError());
        return false;
    }

    const size_t bytes = dllPath.size() + 1;
    void* remoteBuf = VirtualAllocEx(hProcess, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteBuf)
    {
        printf("[inject] VirtualAllocEx failed: %lu\n", GetLastError());
        CloseHandle(hProcess);
        return false;
    }

    if (!WriteProcessMemory(hProcess, remoteBuf, dllPath.c_str(), bytes, nullptr))
    {
        printf("[inject] WriteProcessMemory failed: %lu\n", GetLastError());
        VirtualFreeEx(hProcess, remoteBuf, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HMODULE hKernel = GetModuleHandleA("kernel32.dll");
    FARPROC loadLibraryA = hKernel ? GetProcAddress(hKernel, "LoadLibraryA") : nullptr;
    if (!loadLibraryA)
    {
        printf("[inject] GetProcAddress(LoadLibraryA) failed: %lu\n", GetLastError());
        VirtualFreeEx(hProcess, remoteBuf, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HANDLE hThread = CreateRemoteThread(
        hProcess,
        nullptr,
        0,
        (LPTHREAD_START_ROUTINE)loadLibraryA,
        remoteBuf,
        0,
        nullptr);
    if (!hThread)
    {
        printf("[inject] CreateRemoteThread failed: %lu\n", GetLastError());
        VirtualFreeEx(hProcess, remoteBuf, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    WaitForSingleObject(hThread, INFINITE);
    DWORD exitCode = 0;
    if (!GetExitCodeThread(hThread, &exitCode) || exitCode == 0)
    {
        printf("[inject] LoadLibraryA failed in target process (exitCode=%lu)\n", exitCode);
        CloseHandle(hThread);
        VirtualFreeEx(hProcess, remoteBuf, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteBuf, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return true;
}

int main(int argc, char** argv)
{
    bool noWait = false;
    std::vector<const char*> args;
    args.reserve((size_t)argc);
    for (int i = 0; i < argc; i++)
        args.push_back(argv[i]);

    for (size_t i = 1; i < args.size();)
    {
        if (strcmp(args[i], "--no-wait") == 0 || strcmp(args[i], "/no-wait") == 0 || strcmp(args[i], "-no-wait") == 0)
        {
            noWait = true;
            args.erase(args.begin() + (ptrdiff_t)i);
            continue;
        }
        i++;
    }

    if ((int)args.size() < 3)
    {
        PrintUsage(argv[0]);
        if (!noWait)
            WaitBeforeExit();
        return 1;
    }

    const char* pidOrName = args[1];
    std::string dllPath = MakeAbsoluteIfNeeded(args[2]);

    DWORD pid = ParsePidOrProcessName(pidOrName);
    if (!pid)
    {
        printf("[inject] process not found: %s\n", pidOrName);
        if (!noWait)
            WaitBeforeExit();
        return 1;
    }

    if (GetFileAttributesA(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        printf("[inject] dll not found: %s\n", dllPath.c_str());
        if (!noWait)
            WaitBeforeExit();
        return 1;
    }

    printf("[inject] target: %s (pid=%lu)\n", pidOrName, pid);
    printf("[inject] injecting %s -> pid=%lu\n", dllPath.c_str(), pid);

    const std::string dllBase = GetBaseName(dllPath);
    if (IsModuleLoaded(pid, dllBase.c_str()))
    {
        printf("[inject] already injected: %s (pid=%lu)\n", dllBase.c_str(), pid);
        if (!noWait)
            WaitBeforeExit();
        return 0;
    }

    const bool ok = InjectByCreateRemoteThread(pid, dllPath);
    printf("[inject] result: %s\n", ok ? "success" : "failed");

    if (!noWait)
        WaitBeforeExit();
    return ok ? 0 : 2;
}


