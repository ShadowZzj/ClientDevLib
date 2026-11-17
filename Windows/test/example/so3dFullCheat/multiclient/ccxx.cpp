#include <Windows.h>
#include <Detours/build/include/detours.h>
#include <General/util/File/File.h>
#include <General/util/Process/Process.h>
#include <boost/filesystem.hpp>
#include <iostream>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <stdio.h>
#include <stdlib.h>
#define _CRT_SECURE_NO_DEPRECATE
#pragma warning(disable : 4996)

using SetWindowsHookA_t = HHOOK(WINAPI *)(int idHook, HOOKPROC lpfn, HINSTANCE hMod, DWORD dwThreadId);
using CreateWindowExA_t = HWND(WINAPI *)(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X,
                                         int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu,
                                         HINSTANCE hInstance, LPVOID lpParam);
SetWindowsHookA_t originalSetWindowsHook = nullptr;
CreateWindowExA_t originalCreateWindow   = nullptr;
#include <spdlog/sinks/basic_file_sink.h>
void InitLogs(const std::string &name)
{
    static bool isInit = false;
    if (isInit)
        return;
    if (name.empty())
    {
        return;
    }
    boost::filesystem::path logPath = zzj::GetDynamicLibPath(InitLogs);
    logPath /= name + ".log";
    auto file_logger = spdlog::basic_logger_mt("file_logger", logPath.string());
    spdlog::set_default_logger(file_logger);
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::info);
    spdlog::info("InitLog: {}", logPath.string());
    isInit = true;
}
extern "C" __declspec(dllexport) void test()
{
    spdlog::info("test called");
    return;
}
HHOOK WINAPI setWindowsHook(int idHook, HOOKPROC lpfn, HINSTANCE hMod, DWORD dwThreadId)
{
    spdlog::info("setWindowsHook called with idHook: {}", idHook);
    return NULL;
}
HWND WINAPI createWindow(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y,
                         int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    static bool isInit = false;
    if (!isInit)
    {
        std::vector<zzj::ProcessV2::Module> modules;
        zzj::ProcessV2 pro;
        pro.GetModules(modules);
        uintptr_t npmsgBase = 0;
        for (auto &module : modules)
        {
            if (module.name.find("NPmsg.dll") != std::string::npos)
            {
                npmsgBase = module.base;
                break;
            }
        }
        if (npmsgBase == 0)
            return originalCreateWindow(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight,
                                        hWndParent, hMenu, hInstance, lpParam);
        spdlog::info("npmsgBase: {0:x}", npmsgBase);
        uintptr_t hookAddress = npmsgBase + 0x26b20;
        char buffer[1024]{0};
        ReadProcessMemory(GetCurrentProcess(), (LPVOID)hookAddress, buffer, 0x1, NULL);
        BYTE writeContent = 0xC3;
        SIZE_T retSize;

        if (buffer[0] == 0x55)
        {
            WriteProcessMemory(GetCurrentProcess(), (LPVOID)hookAddress, &writeContent, 1, &retSize);
            spdlog::info("hooked");
        }
        isInit = true;
    }
    return originalCreateWindow(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu,
                                hInstance, lpParam);
}

DWORD WINAPI DoMagic(LPVOID lpParam)
{
    InitLogs("starterdll");
    auto user32                   = GetModuleHandleA("user32.dll");
    auto kernelbase               = GetModuleHandleA("kernelbase.dll");
    originalSetWindowsHook        = (SetWindowsHookA_t)GetProcAddress(user32, "SetWindowsHookExA");
    auto getsystemTimeAsFileTiime = (uintptr_t)GetProcAddress(kernelbase, "GetSystemTimeAsFileTime");

    spdlog::info("originalSetWindowsHook: {:x}", (uintptr_t)originalSetWindowsHook);
    spdlog::info("getsystemTimeAsFileTiime: {:x}", (uintptr_t)getsystemTimeAsFileTiime);
    auto createWindowPageStartAddress   = (uintptr_t)getsystemTimeAsFileTiime & 0xFFFFF000;
    auto setWindowsHookPageStartAddress = (uintptr_t)originalSetWindowsHook & 0xFFFFF000;
    uintptr_t npmsgBase                 = 0;
    while (1)
    {
        std::vector<zzj::ProcessV2::Module> modules;
        zzj::ProcessV2 pro;
        pro.GetModules(modules);
        for (auto &module : modules)
        {
            if (module.name.find("NPmsg.dll") != std::string::npos)
            {
                npmsgBase = module.base;
                break;
            }
        }
        if (npmsgBase != 0)
            break;
    }
    auto npMsgHookAddress = npmsgBase + 0x26b20;
    spdlog::info("npMsgHookAddress: {0:x}", npMsgHookAddress);

    DWORD oldProtect;
    if (!VirtualProtect((LPVOID)createWindowPageStartAddress, 0x1000, PAGE_EXECUTE_READWRITE, &oldProtect))
        spdlog::info("VirtualProtect failed: {}", GetLastError());
    if (!VirtualProtect((LPVOID)(npmsgBase + 0x26000), 0x1000, PAGE_EXECUTE_READWRITE, &oldProtect))
        spdlog::info("VirtualProtect failed: {}", GetLastError());
    char jmpF7[] = {0xEB, 0xF7};
    memcpy((void *)getsystemTimeAsFileTiime, jmpF7, sizeof(jmpF7));
    char hookNPMsg[]          = {0xb8, 0x20, 0x6b, 0x02, 0x10, 0xc6, 0x00, 0xc3, 0xeb, 0xc4};
    *(DWORD *)(hookNPMsg + 1) = npMsgHookAddress;
    char doubleJmp[]          = {0xEB, 0x39};
    memcpy((void *)((uintptr_t)getsystemTimeAsFileTiime - 7), doubleJmp, sizeof(doubleJmp));
    memcpy((void *)((uintptr_t)getsystemTimeAsFileTiime + 0x34), hookNPMsg, sizeof(hookNPMsg));
    spdlog::info("getsystemTimeAsFileTiime hooked");

    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    std::string path = buf;
    path             = path.substr(0, path.find_last_of("\\"));
    path             = path.substr(0, path.find_last_of("\\"));
    path += "\\test\\ClientDevLib_testdll.dll";
    spdlog::info("path: {}", path);

    HMODULE hModule = LoadLibraryA(path.c_str());
    // if (!VirtualProtect((LPVOID)setWindowsHookPageStartAddress, 0x1000, PAGE_EXECUTE_READWRITE, &oldProtect))
    //     spdlog::info("VirtualProtect failed: {}", GetLastError());
    // char jmpF5[] = { 0xEB, 0xF5 };
    // memcpy((void*)originalSetWindowsHook, jmpF5, sizeof(jmpF5));
    // char ret16[] = { 0xC2, 0x10, 0x00,0x90,0x90,0x90,0x90,0xeb,0x2};
    // memcpy((void*)((uintptr_t)originalSetWindowsHook - 9), ret16, sizeof(ret16));
    return 0;
}
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    HANDLE threadHandle;

    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        CloseHandle(CreateThread(NULL, 0, DoMagic, NULL, 0, NULL));
    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
