#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <detours.h>

#include <cstdio>
#include <ctime>
#include <string>
#include <unordered_map>

#pragma warning(push)
#pragma warning(disable: 4819)
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#pragma warning(pop)

// ============================================================================
// 全局变量
// ============================================================================
static HMODULE g_hModule = nullptr;
static std::shared_ptr<spdlog::logger> g_logger = nullptr;
static volatile bool g_shouldExit = false;

// ============================================================================
// DirectInput 扫描码到键名映射
// ============================================================================
static std::unordered_map<int, std::string> g_dikToName = {
    {DIK_ESCAPE, "ESC"}, {DIK_1, "1"}, {DIK_2, "2"}, {DIK_3, "3"}, {DIK_4, "4"},
    {DIK_5, "5"}, {DIK_6, "6"}, {DIK_7, "7"}, {DIK_8, "8"}, {DIK_9, "9"}, {DIK_0, "0"},
    {DIK_MINUS, "-"}, {DIK_EQUALS, "="}, {DIK_BACK, "BACKSPACE"}, {DIK_TAB, "TAB"},
    {DIK_Q, "Q"}, {DIK_W, "W"}, {DIK_E, "E"}, {DIK_R, "R"}, {DIK_T, "T"},
    {DIK_Y, "Y"}, {DIK_U, "U"}, {DIK_I, "I"}, {DIK_O, "O"}, {DIK_P, "P"},
    {DIK_LBRACKET, "["}, {DIK_RBRACKET, "]"}, {DIK_RETURN, "ENTER"},
    {DIK_LCONTROL, "LCTRL"}, {DIK_A, "A"}, {DIK_S, "S"}, {DIK_D, "D"},
    {DIK_F, "F"}, {DIK_G, "G"}, {DIK_H, "H"}, {DIK_J, "J"}, {DIK_K, "K"},
    {DIK_L, "L"}, {DIK_SEMICOLON, ";"}, {DIK_APOSTROPHE, "'"}, {DIK_GRAVE, "`"},
    {DIK_LSHIFT, "LSHIFT"}, {DIK_BACKSLASH, "\\"}, {DIK_Z, "Z"}, {DIK_X, "X"},
    {DIK_C, "C"}, {DIK_V, "V"}, {DIK_B, "B"}, {DIK_N, "N"}, {DIK_M, "M"},
    {DIK_COMMA, ","}, {DIK_PERIOD, "."}, {DIK_SLASH, "/"}, {DIK_RSHIFT, "RSHIFT"},
    {DIK_MULTIPLY, "NUM*"}, {DIK_LMENU, "LALT"}, {DIK_SPACE, "SPACE"},
    {DIK_CAPITAL, "CAPSLOCK"}, {DIK_F1, "F1"}, {DIK_F2, "F2"}, {DIK_F3, "F3"},
    {DIK_F4, "F4"}, {DIK_F5, "F5"}, {DIK_F6, "F6"}, {DIK_F7, "F7"}, {DIK_F8, "F8"},
    {DIK_F9, "F9"}, {DIK_F10, "F10"}, {DIK_NUMLOCK, "NUMLOCK"}, {DIK_SCROLL, "SCROLLLOCK"},
    {DIK_NUMPAD7, "NUM7"}, {DIK_NUMPAD8, "NUM8"}, {DIK_NUMPAD9, "NUM9"}, {DIK_SUBTRACT, "NUM-"},
    {DIK_NUMPAD4, "NUM4"}, {DIK_NUMPAD5, "NUM5"}, {DIK_NUMPAD6, "NUM6"}, {DIK_ADD, "NUM+"},
    {DIK_NUMPAD1, "NUM1"}, {DIK_NUMPAD2, "NUM2"}, {DIK_NUMPAD3, "NUM3"}, {DIK_NUMPAD0, "NUM0"},
    {DIK_DECIMAL, "NUM."}, {DIK_F11, "F11"}, {DIK_F12, "F12"},
    {DIK_NUMPADENTER, "NUMENTER"}, {DIK_RCONTROL, "RCTRL"}, {DIK_DIVIDE, "NUM/"},
    {DIK_SYSRQ, "SYSRQ"}, {DIK_RMENU, "RALT"}, {DIK_PAUSE, "PAUSE"},
    {DIK_HOME, "HOME"}, {DIK_UP, "UP"}, {DIK_PRIOR, "PGUP"}, {DIK_LEFT, "LEFT"},
    {DIK_RIGHT, "RIGHT"}, {DIK_END, "END"}, {DIK_DOWN, "DOWN"}, {DIK_NEXT, "PGDN"},
    {DIK_INSERT, "INSERT"}, {DIK_DELETE, "DELETE"}, {DIK_LWIN, "LWIN"}, {DIK_RWIN, "RWIN"}
};

// ============================================================================
// 日志输出（spdlog）
// ============================================================================
static void InitLogger()
{
    if (g_logger)
        return;

    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(path, L'\\');
    if (lastSlash)
        *(lastSlash + 1) = L'\0';
    wcscat_s(path, L"dinput_hook.log");

    // 转成窄字符
    char pathA[MAX_PATH]{};
    WideCharToMultiByte(CP_UTF8, 0, path, -1, pathA, MAX_PATH, nullptr, nullptr);

    try
    {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(pathA, false);
        auto msvc_sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
        
        std::vector<spdlog::sink_ptr> sinks{ file_sink, msvc_sink };
        g_logger = std::make_shared<spdlog::logger>("dinput_hook", sinks.begin(), sinks.end());
        g_logger->set_level(spdlog::level::info);
        g_logger->set_pattern("[%H:%M:%S.%e] [%n] %v");
        
        // 每条日志都立即 flush
        g_logger->flush_on(spdlog::level::info);
        
        g_logger->info("========== dinput_hook_dll loaded ==========");
    }
    catch (const std::exception& e)
    {
        OutputDebugStringA("Failed to init spdlog: ");
        OutputDebugStringA(e.what());
    }
}

static void CloseLogger()
{
    if (g_logger)
    {
        g_logger->info("========== dinput_hook_dll unloaded ==========");
        g_logger->flush();
        g_logger.reset();
    }
}

// ============================================================================
// 按键监控线程（Insert 键卸载）
// ============================================================================
static DWORD WINAPI KeyMonitorThread(LPVOID param)
{
    HMODULE hMod = (HMODULE)param;
    
    while (!g_shouldExit)
    {
        if (GetAsyncKeyState(VK_INSERT) & 0x8000)
        {
            if (g_logger)
                g_logger->info("Insert key detected! Unloading DLL...");
            
            while (GetAsyncKeyState(VK_INSERT) & 0x8000)
                Sleep(10);
            
            CloseLogger();
            FreeLibraryAndExitThread(hMod, 0);
            return 0;
        }
        
        Sleep(50);
    }
    
    return 0;
}

// ============================================================================
// IDirectInputDevice8::GetDeviceState hook（直接 hook 函数地址）
// ============================================================================
// GetDeviceState 的函数签名（STDMETHODCALLTYPE = __stdcall，但 COM 方法实际是 __stdcall）
using IDirectInputDevice8_GetDeviceState_t = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8W* pThis, DWORD cbData, LPVOID lpvData);

static IDirectInputDevice8_GetDeviceState_t Real_GetDeviceState = nullptr;

// ============================================================================
// Hook 函数：GetDeviceState
// ============================================================================
static HRESULT STDMETHODCALLTYPE Hook_GetDeviceState(IDirectInputDevice8W* pThis, DWORD cbData, LPVOID lpvData)
{
    // 调用原函数
    HRESULT hr = Real_GetDeviceState(pThis, cbData, lpvData);

    if (SUCCEEDED(hr) && lpvData && g_logger)
    {
        // 判断设备类型（根据数据大小粗略判断）
        if (cbData == sizeof(DIMOUSESTATE) || cbData == sizeof(DIMOUSESTATE2))
        {
            // 鼠标
            DIMOUSESTATE2* ms = (DIMOUSESTATE2*)lpvData;
            
            // 只在有移动或按键时才输出
            bool hasMovement = (ms->lX != 0 || ms->lY != 0 || ms->lZ != 0);
            bool hasButton = false;
            std::string buttonStr;
            
            if (ms->rgbButtons[0] & 0x80) { buttonStr += "LEFT "; hasButton = true; }
            if (ms->rgbButtons[1] & 0x80) { buttonStr += "RIGHT "; hasButton = true; }
            if (ms->rgbButtons[2] & 0x80) { buttonStr += "MIDDLE "; hasButton = true; }
            if (ms->rgbButtons[3] & 0x80) { buttonStr += "X1 "; hasButton = true; }
            if (ms->rgbButtons[4] & 0x80) { buttonStr += "X2 "; hasButton = true; }
            
            if (hasMovement && hasButton)
            {
                g_logger->info("Mouse: dx={:<4} dy={:<4} dz={:<3} | Buttons: {}", 
                              ms->lX, ms->lY, ms->lZ, buttonStr);
            }
            else if (hasMovement)
            {
                g_logger->info("Mouse: dx={:<4} dy={:<4} dz={:<3}", ms->lX, ms->lY, ms->lZ);
            }
            else if (hasButton)
            {
                g_logger->info("Mouse: Buttons: {}", buttonStr);
            }
        }
        else if (cbData == 256)
        {
            // 键盘（256 字节，每个键 1 字节）
            BYTE* keys = (BYTE*)lpvData;
            
            // 收集所有按下的键
            std::string pressedKeys;
            for (int i = 0; i < 256; i++)
            {
                if (keys[i] & 0x80)
                {
                    auto it = g_dikToName.find(i);
                    if (it != g_dikToName.end())
                    {
                        pressedKeys += it->second + " ";
                    }
                    else
                    {
                        pressedKeys += "DIK_" + std::to_string(i) + " ";
                    }
                }
            }
            
            if (!pressedKeys.empty())
            {
                g_logger->info("Key: {}", pressedKeys);
            }
        }
        else
        {
            // 其他设备（手柄等）
            g_logger->info("GetDeviceState: cbData={}", cbData);
        }
    }

    return hr;
}

// ============================================================================
// 获取 GetDeviceState 的函数地址（从 vtable）
// ============================================================================
static bool GetDeviceStateAddress()
{
    if (g_logger)
        g_logger->info("Creating temporary device to get GetDeviceState address...");
    
    // 临时创建 DirectInput 对象
    IDirectInput8W* pDI = nullptr;
    HRESULT hr = DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION,
                                   IID_IDirectInput8W, (VOID**)&pDI, nullptr);
    if (FAILED(hr) || !pDI)
    {
        if (g_logger)
            g_logger->error("Failed to create temporary DirectInput8.");
        return false;
    }

    // 临时创建键盘设备（用来读取 vtable）
    IDirectInputDevice8W* pDevice = nullptr;
    hr = pDI->CreateDevice(GUID_SysKeyboard, &pDevice, nullptr);
    if (FAILED(hr) || !pDevice)
    {
        pDI->Release();
        if (g_logger)
            g_logger->error("Failed to create temporary device.");
        return false;
    }

    // 读取 vtable：IDirectInputDevice8W 的 GetDeviceState 是第 9 个方法
    // vtable 布局：[0-2]=IUnknown, [3-8]=IDirectInputDevice8 前几个方法, [9]=GetDeviceState
    void** vtable = *(void***)pDevice;
    Real_GetDeviceState = (IDirectInputDevice8_GetDeviceState_t)vtable[9];

    if (g_logger)
        g_logger->info("GetDeviceState address: 0x{:X}", (uintptr_t)Real_GetDeviceState);

    // 清理临时对象
    pDevice->Release();
    pDI->Release();

    return Real_GetDeviceState != nullptr;
}

// ============================================================================
// DllMain
// ============================================================================
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (DetourIsHelperProcess())
        return TRUE;

    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        {
            DisableThreadLibraryCalls(hinstDLL);
            g_hModule = hinstDLL;
            
            InitLogger();
            if (g_logger)
                g_logger->info("DLL_PROCESS_ATTACH: hooking IDirectInputDevice8::GetDeviceState...");

            // 获取 GetDeviceState 函数地址
            if (GetDeviceStateAddress())
            {
                DetourRestoreAfterWith();
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());
                DetourAttach(reinterpret_cast<PVOID*>(&Real_GetDeviceState),
                             reinterpret_cast<PVOID>(Hook_GetDeviceState));
                
                if (DetourTransactionCommit() == NO_ERROR)
                {
                    if (g_logger)
                        g_logger->info("GetDeviceState hook installed successfully.");
                    
                    CreateThread(nullptr, 0, KeyMonitorThread, g_hModule, 0, nullptr);
                    
                    wchar_t msg[256];
                    DWORD pid = GetCurrentProcessId();
                    wchar_t procName[MAX_PATH]{};
                    GetModuleFileNameW(nullptr, procName, MAX_PATH);
                    wchar_t* baseName = wcsrchr(procName, L'\\');
                    if (baseName) baseName++; else baseName = procName;
                    
                    swprintf_s(msg, L"DirectInput Hook Injected!\n\nProcess: %s (PID: %lu)\n\nPress INSERT to unload", baseName, pid);
                    MessageBoxW(nullptr, msg, L"dinput_hook_dll", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
                }
                else
                {
                    if (g_logger)
                        g_logger->error("GetDeviceState hook FAILED.");
                    MessageBoxW(nullptr, L"GetDeviceState hook FAILED!", L"dinput_hook_dll", MB_OK | MB_ICONERROR | MB_TOPMOST);
                }
            }
            else
            {
                if (g_logger)
                    g_logger->error("Failed to get GetDeviceState address.");
                MessageBoxW(nullptr, L"Failed to get GetDeviceState address!", L"dinput_hook_dll", MB_OK | MB_ICONERROR | MB_TOPMOST);
            }
        }
        break;

    case DLL_PROCESS_DETACH:
        g_shouldExit = true;
        
        if (g_logger)
            g_logger->info("DLL_PROCESS_DETACH: removing GetDeviceState hook...");
        
        if (Real_GetDeviceState)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourDetach(reinterpret_cast<PVOID*>(&Real_GetDeviceState),
                         reinterpret_cast<PVOID>(Hook_GetDeviceState));
            DetourTransactionCommit();
        }

        if (g_logger)
            g_logger->info("GetDeviceState hook removed.");
        CloseLogger();
        break;
    }

    return TRUE;
}

