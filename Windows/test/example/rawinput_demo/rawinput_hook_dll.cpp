#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <detours.h>

#include <cstdio>
#include <ctime>
#include <string>

// ============================================================================
// Hook target: GetRawInputData
// ============================================================================
using GetRawInputData_t = UINT(WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
static GetRawInputData_t Real_GetRawInputData = ::GetRawInputData;

// 用于自我卸载
static HMODULE g_hModule = nullptr;

// ============================================================================
// 日志输出（OutputDebugString + 文件）
// ============================================================================
static FILE* g_logFile = nullptr;

static void OpenLogFile()
{
    if (g_logFile)
        return;
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    // 拿目标进程 exe 所在目录
    wchar_t* lastSlash = wcsrchr(path, L'\\');
    if (lastSlash)
        *(lastSlash + 1) = L'\0';
    wcscat_s(path, L"rawinput_hook.log");

    _wfopen_s(&g_logFile, path, L"a, ccs=UTF-8");
    if (g_logFile)
    {
        time_t now = time(nullptr);
        struct tm t;
        localtime_s(&t, &now);
        fwprintf(g_logFile, L"\n========== rawinput_hook_dll loaded @ %04d-%02d-%02d %02d:%02d:%02d ==========\n",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
        fflush(g_logFile);
    }
}

static void CloseLogFile()
{
    if (g_logFile)
    {
        fwprintf(g_logFile, L"========== rawinput_hook_dll unloaded ==========\n");
        fflush(g_logFile);
        fclose(g_logFile);
        g_logFile = nullptr;
    }
}

static void LogOutput(const wchar_t* msg)
{
    OutputDebugStringW(msg);
    if (g_logFile)
    {
        fwprintf(g_logFile, L"%s", msg);
        fflush(g_logFile);
    }
}

// ============================================================================
// 解析 RAWINPUT
// ============================================================================
static std::wstring VKeyToName(UINT vkey)
{
    UINT scan = MapVirtualKeyW(vkey, MAPVK_VK_TO_VSC);
    if (vkey == VK_LEFT || vkey == VK_UP || vkey == VK_RIGHT || vkey == VK_DOWN ||
        vkey == VK_PRIOR || vkey == VK_NEXT || vkey == VK_END || vkey == VK_HOME ||
        vkey == VK_INSERT || vkey == VK_DELETE)
    {
        scan |= 0xE000;
    }

    wchar_t name[128]{};
    LONG lParam = (LONG)(scan << 16);
    int n = GetKeyNameTextW(lParam, name, 128);
    if (n > 0)
        return std::wstring(name, name + n);

    wchar_t fallback[32]{};
    swprintf_s(fallback, L"VK_%u", vkey);
    return fallback;
}

static void PrintRawInput(const RAWINPUT* ri, UINT size)
{
    wchar_t buf[512]{};

    if (ri->header.dwType == RIM_TYPEMOUSE)
    {
        const RAWMOUSE& m = ri->data.mouse;
        LONG dx = m.lLastX;
        LONG dy = m.lLastY;
        USHORT btn = m.usButtonFlags;

        if (dx != 0 || dy != 0)
        {
            swprintf_s(buf, L"[HOOK] mouse: dx=%ld dy=%ld\n", dx, dy);
            LogOutput(buf);
        }
        if (btn)
        {
            swprintf_s(buf, L"[HOOK] mouse: btn=0x%04X wheelDelta=%d\n", btn, (short)m.usButtonData);
            LogOutput(buf);
        }
    }
    else if (ri->header.dwType == RIM_TYPEKEYBOARD)
    {
        const RAWKEYBOARD& k = ri->data.keyboard;
        bool down = (k.Flags & RI_KEY_BREAK) == 0;
        std::wstring name = VKeyToName(k.VKey);
        swprintf_s(buf, L"[HOOK] key: %s vkey=%u scan=0x%02X flags=0x%04X (%s)\n",
                   down ? L"DOWN" : L"UP  ",
                   (unsigned)k.VKey,
                   (unsigned)k.MakeCode,
                   (unsigned)k.Flags,
                   name.c_str());
        LogOutput(buf);
    }
}

// ============================================================================
// 按键检测线程（用 GetAsyncKeyState 轮询 Insert 键）
// ============================================================================
static volatile bool g_shouldExit = false;

static DWORD WINAPI KeyMonitorThread(LPVOID param)
{
    HMODULE hMod = (HMODULE)param;
    
    while (!g_shouldExit)
    {
        // 检测 Insert 键是否被按下
        if (GetAsyncKeyState(VK_INSERT) & 0x8000)
        {
            LogOutput(L"[HOOK] Insert key detected! Unloading DLL...\n");
            
            // 等待按键释放，避免重复触发
            while (GetAsyncKeyState(VK_INSERT) & 0x8000)
                Sleep(10);
            
            CloseLogFile();
            FreeLibraryAndExitThread(hMod, 0);
            return 0;
        }
        
        Sleep(50); // 每 50ms 检测一次
    }
    
    return 0;
}

// ============================================================================
// Hook 函数
// ============================================================================
static UINT WINAPI Hook_GetRawInputData(HRAWINPUT hRawInput,
                                        UINT uiCommand,
                                        LPVOID pData,
                                        PUINT pcbSize,
                                        UINT cbSizeHeader)
{
    UINT ret = Real_GetRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);

    // 只在 RID_INPUT 且成功拿到数据时解析
    if (uiCommand == RID_INPUT && pData != nullptr && ret != (UINT)-1 && ret > 0)
    {
        auto* ri = reinterpret_cast<const RAWINPUT*>(pData);
        PrintRawInput(ri, ret);
    }

    return ret;
}

// ============================================================================
// DllMain: 安装/卸载 Detour
// ============================================================================
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (DetourIsHelperProcess())
        return TRUE;

    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hinstDLL);
        g_hModule = hinstDLL; // 保存模块句柄，用于自我卸载
        
        OpenLogFile();
        LogOutput(L"[HOOK] DLL_PROCESS_ATTACH: installing GetRawInputData hook...\n");

        DetourRestoreAfterWith();
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(reinterpret_cast<PVOID*>(&Real_GetRawInputData),
                     reinterpret_cast<PVOID>(Hook_GetRawInputData));
        if (DetourTransactionCommit() == NO_ERROR)
        {
            LogOutput(L"[HOOK] GetRawInputData hook installed successfully.\n");
            
            // 启动按键监控线程（用 GetAsyncKeyState 检测 Insert）
            CreateThread(nullptr, 0, KeyMonitorThread, g_hModule, 0, nullptr);
            LogOutput(L"[HOOK] Insert key monitor thread started.\n");
            
            // 弹窗提示注入成功
            wchar_t msg[256]{};
            DWORD pid = GetCurrentProcessId();
            wchar_t procName[MAX_PATH]{};
            GetModuleFileNameW(nullptr, procName, MAX_PATH);
            wchar_t* baseName = wcsrchr(procName, L'\\');
            if (baseName)
                baseName++;
            else
                baseName = procName;
            
            swprintf_s(msg, L"Raw Input Hook Injected!\n\nProcess: %s (PID: %lu)\n\nPress INSERT to unload DLL", baseName, pid);
            MessageBoxW(nullptr, msg, L"rawinput_hook_dll", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
        }
        else
        {
            LogOutput(L"[HOOK] GetRawInputData hook FAILED.\n");
            MessageBoxW(nullptr, L"Hook installation FAILED!", L"rawinput_hook_dll", MB_OK | MB_ICONERROR | MB_TOPMOST);
        }
        break;

    case DLL_PROCESS_DETACH:
        g_shouldExit = true; // 通知监控线程退出
        
        LogOutput(L"[HOOK] DLL_PROCESS_DETACH: removing GetRawInputData hook...\n");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(reinterpret_cast<PVOID*>(&Real_GetRawInputData),
                     reinterpret_cast<PVOID>(Hook_GetRawInputData));
        DetourTransactionCommit();

        LogOutput(L"[HOOK] GetRawInputData hook removed.\n");
        CloseLogFile();
        break;
    }

    return TRUE;
}

