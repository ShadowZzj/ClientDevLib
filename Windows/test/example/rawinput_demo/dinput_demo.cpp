#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include <cstdio>
#include <string>

// ============================================================================
// 全局变量
// ============================================================================
static IDirectInput8W* g_pDI = nullptr;
static IDirectInputDevice8W* g_pKeyboard = nullptr;
static IDirectInputDevice8W* g_pMouse = nullptr;

// ============================================================================
// 控制台输出
// ============================================================================
static void EnsureConsole()
{
    if (GetConsoleWindow())
        return;
    AllocConsole();
    FILE* fp = nullptr;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    freopen_s(&fp, "CONIN$", "r", stdin);
    SetConsoleOutputCP(CP_UTF8);
}

// ============================================================================
// DirectInput 初始化
// ============================================================================
static bool InitDirectInput(HWND hwnd)
{
    HRESULT hr;

    // 创建 DirectInput 对象
    hr = DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION, 
                           IID_IDirectInput8W, (VOID**)&g_pDI, nullptr);
    if (FAILED(hr))
    {
        printf("[DInputDemo] DirectInput8Create FAILED: 0x%08lX\n", hr);
        return false;
    }
    printf("[DInputDemo] DirectInput8Create succeeded.\n");

    // 创建键盘设备
    hr = g_pDI->CreateDevice(GUID_SysKeyboard, &g_pKeyboard, nullptr);
    if (FAILED(hr))
    {
        printf("[DInputDemo] CreateDevice(Keyboard) FAILED: 0x%08lX\n", hr);
        return false;
    }

    hr = g_pKeyboard->SetDataFormat(&c_dfDIKeyboard);
    if (FAILED(hr))
    {
        printf("[DInputDemo] SetDataFormat(Keyboard) FAILED: 0x%08lX\n", hr);
        return false;
    }

    hr = g_pKeyboard->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    if (FAILED(hr))
    {
        printf("[DInputDemo] SetCooperativeLevel(Keyboard) FAILED: 0x%08lX\n", hr);
        return false;
    }

    g_pKeyboard->Acquire();
    printf("[DInputDemo] Keyboard device created and acquired.\n");

    // 创建鼠标设备
    hr = g_pDI->CreateDevice(GUID_SysMouse, &g_pMouse, nullptr);
    if (FAILED(hr))
    {
        printf("[DInputDemo] CreateDevice(Mouse) FAILED: 0x%08lX\n", hr);
        return false;
    }

    hr = g_pMouse->SetDataFormat(&c_dfDIMouse2);
    if (FAILED(hr))
    {
        printf("[DInputDemo] SetDataFormat(Mouse) FAILED: 0x%08lX\n", hr);
        return false;
    }

    hr = g_pMouse->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    if (FAILED(hr))
    {
        printf("[DInputDemo] SetCooperativeLevel(Mouse) FAILED: 0x%08lX\n", hr);
        return false;
    }

    g_pMouse->Acquire();
    printf("[DInputDemo] Mouse device created and acquired.\n");

    return true;
}

// ============================================================================
// DirectInput 清理
// ============================================================================
static void CleanupDirectInput()
{
    if (g_pKeyboard)
    {
        g_pKeyboard->Unacquire();
        g_pKeyboard->Release();
        g_pKeyboard = nullptr;
    }

    if (g_pMouse)
    {
        g_pMouse->Unacquire();
        g_pMouse->Release();
        g_pMouse = nullptr;
    }

    if (g_pDI)
    {
        g_pDI->Release();
        g_pDI = nullptr;
    }

    printf("[DInputDemo] DirectInput cleaned up.\n");
}

// ============================================================================
// 读取输入（每帧调用）
// ============================================================================
static void UpdateInput(HWND hwnd)
{
    HRESULT hr;

    // 读取键盘
    BYTE keyState[256]{};
    hr = g_pKeyboard->GetDeviceState(sizeof(keyState), keyState);
    if (SUCCEEDED(hr))
    {
        // 检查常用键（只打印按下的）
        static const struct { int scanCode; const char* name; } keys[] = {
            {DIK_W, "W"}, {DIK_A, "A"}, {DIK_S, "S"}, {DIK_D, "D"},
            {DIK_SPACE, "SPACE"}, {DIK_ESCAPE, "ESC"},
            {DIK_RETURN, "ENTER"}, {DIK_LSHIFT, "LSHIFT"}
        };

        for (const auto& k : keys)
        {
            if (keyState[k.scanCode] & 0x80)
            {
                printf("[DInputDemo] Key pressed: %s\n", k.name);
            }
        }
    }
    else if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED)
    {
        g_pKeyboard->Acquire();
    }

    // 读取鼠标
    DIMOUSESTATE2 mouseState{};
    hr = g_pMouse->GetDeviceState(sizeof(mouseState), &mouseState);
    if (SUCCEEDED(hr))
    {
        if (mouseState.lX != 0 || mouseState.lY != 0 || mouseState.lZ != 0)
        {
            printf("[DInputDemo] Mouse: dx=%ld dy=%ld dz=%ld\n", 
                   mouseState.lX, mouseState.lY, mouseState.lZ);
        }

        if (mouseState.rgbButtons[0] & 0x80)
            printf("[DInputDemo] Mouse: LEFT button pressed\n");
        if (mouseState.rgbButtons[1] & 0x80)
            printf("[DInputDemo] Mouse: RIGHT button pressed\n");

        // 更新窗口标题
        wchar_t title[256];
        swprintf_s(title, L"DirectInput Demo | Mouse: dx=%ld dy=%ld | Press ESC to exit", 
                   mouseState.lX, mouseState.lY);
        SetWindowTextW(hwnd, title);
    }
    else if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED)
    {
        g_pMouse->Acquire();
    }
}

// ============================================================================
// 窗口过程
// ============================================================================
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        EnsureConsole();
        printf("[DInputDemo] Window created. Initializing DirectInput...\n");
        
        if (!InitDirectInput(hwnd))
        {
            printf("[DInputDemo] DirectInput initialization FAILED!\n");
            MessageBoxW(hwnd, L"DirectInput initialization failed!", L"Error", MB_OK | MB_ICONERROR);
            PostQuitMessage(1);
        }
        else
        {
            printf("[DInputDemo] DirectInput initialized successfully.\n");
            printf("[DInputDemo] Move mouse and press keys (W/A/S/D/SPACE/ESC) to test.\n");
            printf("[DInputDemo] You can now inject dinput_hook_dll.dll to test the hook.\n");
        }
        
        SetTimer(hwnd, 1, 16, nullptr); // 约 60 FPS
        return 0;

    case WM_TIMER:
        if (wParam == 1)
        {
            UpdateInput(hwnd);
        }
        return 0;

    case WM_ACTIVATE:
        // 窗口激活/失活时重新 Acquire
        if (LOWORD(wParam) != WA_INACTIVE)
        {
            if (g_pKeyboard) g_pKeyboard->Acquire();
            if (g_pMouse) g_pMouse->Acquire();
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        CleanupDirectInput();
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// ============================================================================
// WinMain
// ============================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    const wchar_t* kClass = L"dinput_demo_window";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&wc))
        return 1;

    HWND hwnd = CreateWindowExW(
        0, kClass, L"DirectInput Demo",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 600,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd)
        return 2;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}

