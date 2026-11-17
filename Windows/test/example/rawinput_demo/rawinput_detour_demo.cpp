#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <detours.h>

#include <cstdio>
#include <string>
#include <vector>

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
    int n = GetKeyNameTextW(lParam, name, (int)(sizeof(name) / sizeof(name[0])));
    if (n > 0)
        return std::wstring(name, name + n);

    wchar_t fallback[32]{};
    swprintf_s(fallback, L"VK_%u", vkey);
    return fallback;
}

static bool RegisterRawKbMouse(HWND hwnd, bool background)
{
    RAWINPUTDEVICE rid[2]{};

    rid[0].usUsagePage = 0x01;
    rid[0].usUsage = 0x02; // Mouse
    rid[0].dwFlags = background ? RIDEV_INPUTSINK : 0;
    rid[0].hwndTarget = hwnd;

    rid[1].usUsagePage = 0x01;
    rid[1].usUsage = 0x06; // Keyboard
    rid[1].dwFlags = background ? RIDEV_INPUTSINK : 0;
    rid[1].hwndTarget = hwnd;

    return RegisterRawInputDevices(rid, 2, sizeof(rid[0])) == TRUE;
}

using GetRawInputData_t = UINT(WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
static GetRawInputData_t Real_GetRawInputData = ::GetRawInputData;

static void PrintRawInput(const RAWINPUT* ri, UINT size)
{
    if (!ri || size < sizeof(RAWINPUTHEADER))
        return;

    if (ri->header.dwType == RIM_TYPEMOUSE)
    {
        const RAWMOUSE& m = ri->data.mouse;
        LONG dx = m.lLastX;
        LONG dy = m.lLastY;
        USHORT btn = m.usButtonFlags;
        if (dx != 0 || dy != 0)
            std::printf("[hook][mouse] dx=%ld dy=%ld\n", dx, dy);
        if (btn)
            std::printf("[hook][mouse] buttonFlags=0x%04X wheelData=%d\n", btn, (int)m.usButtonData);
    }
    else if (ri->header.dwType == RIM_TYPEKEYBOARD)
    {
        const RAWKEYBOARD& k = ri->data.keyboard;
        bool down = (k.Flags & RI_KEY_BREAK) == 0;
        std::wstring name = VKeyToName(k.VKey);
        std::printf("[hook][key] %s  vkey=%u  make=0x%02X  flags=0x%04X  (%ls)\n",
                    down ? "DOWN" : "UP  ",
                    (unsigned)k.VKey,
                    (unsigned)k.MakeCode,
                    (unsigned)k.Flags,
                    name.c_str());
    }
}

static UINT WINAPI Hook_GetRawInputData(HRAWINPUT hRawInput,
                                       UINT uiCommand,
                                       LPVOID pData,
                                       PUINT pcbSize,
                                       UINT cbSizeHeader)
{
    // Call original (trampoline after DetourAttach).
    UINT ret = Real_GetRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);

    // Only decode the "copy into user buffer" call.
    if (uiCommand == RID_INPUT && pData != nullptr && ret != (UINT)-1)
    {
        auto* ri = reinterpret_cast<const RAWINPUT*>(pData);
        PrintRawInput(ri, ret);
    }

    return ret;
}

static bool InstallDetour()
{
    EnsureConsole();
    std::printf("[rawinput_detour_demo] installing detour for GetRawInputData...\n");

    DetourRestoreAfterWith();
    if (DetourTransactionBegin() != NO_ERROR)
        return false;
    if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR)
        return false;
    if (DetourAttach(reinterpret_cast<PVOID*>(&Real_GetRawInputData),
                     reinterpret_cast<PVOID>(Hook_GetRawInputData)) != NO_ERROR)
        return false;
    return DetourTransactionCommit() == NO_ERROR;
}

static void UninstallDetour()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(reinterpret_cast<PVOID*>(&Real_GetRawInputData),
                 reinterpret_cast<PVOID>(Hook_GetRawInputData));
    DetourTransactionCommit();
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        EnsureConsole();
        std::printf("[rawinput_detour_demo] started. move mouse / press keys.\n");
        if (!InstallDetour())
            std::printf("[rawinput_detour_demo] detour install failed.\n");

        if (!RegisterRawKbMouse(hwnd, /*background*/ true))
            std::printf("[rawinput_detour_demo] RegisterRawInputDevices failed, GetLastError=%lu\n", GetLastError());
        return 0;

    case WM_INPUT: {
        // Just trigger GetRawInputData; the detour prints the decoded input.
        UINT size = 0;
        ::GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
        if (size == 0)
            return 0;
        std::vector<BYTE> buf(size);
        ::GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buf.data(), &size, sizeof(RAWINPUTHEADER));
        return 0;
    }

    case WM_DESTROY:
        UninstallDetour();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    const wchar_t* kClass = L"rawinput_detour_demo_window";

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
        0, kClass, L"rawinput_detour_demo",
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
    return 0;
}


