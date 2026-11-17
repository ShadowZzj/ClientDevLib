#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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
    // Minimal handling for extended keys (e.g. arrows)
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

    // Mouse
    rid[0].usUsagePage = 0x01; // Generic Desktop Controls
    rid[0].usUsage = 0x02;     // Mouse
    rid[0].dwFlags = background ? RIDEV_INPUTSINK : 0;
    rid[0].hwndTarget = hwnd;

    // Keyboard
    rid[1].usUsagePage = 0x01; // Generic Desktop Controls
    rid[1].usUsage = 0x06;     // Keyboard
    rid[1].dwFlags = background ? RIDEV_INPUTSINK : 0;
    rid[1].hwndTarget = hwnd;

    return RegisterRawInputDevices(rid, 2, sizeof(rid[0])) == TRUE;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        EnsureConsole();
        std::printf("[rawinput_demo] started. focus the window and move mouse / press keys.\n");
        std::printf("[rawinput_demo] tip: RIDEV_INPUTSINK is enabled; WM_INPUT can be received even when not focused.\n");
        if (!RegisterRawKbMouse(hwnd, /*background*/ true))
        {
            std::printf("[rawinput_demo] RegisterRawInputDevices failed, GetLastError=%lu\n", GetLastError());
        }
        return 0;

    case WM_INPUT: {
        UINT size = 0;
        if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 || size == 0)
            return 0;

        std::vector<BYTE> buf(size);
        if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buf.data(), &size, sizeof(RAWINPUTHEADER)) != size)
            return 0;

        const RAWINPUT* ri = reinterpret_cast<const RAWINPUT*>(buf.data());
        if (ri->header.dwType == RIM_TYPEMOUSE)
        {
            const RAWMOUSE& m = ri->data.mouse;
            LONG dx = m.lLastX;
            LONG dy = m.lLastY;
            USHORT btn = m.usButtonFlags;
            if (dx != 0 || dy != 0)
                std::printf("[mouse] dx=%ld dy=%ld\n", dx, dy);
            if (btn)
                std::printf("[mouse] buttonFlags=0x%04X wheelData=%d\n", btn, (int)m.usButtonData);

            wchar_t title[128]{};
            swprintf_s(title, L"rawinput_demo  |  mouse dx=%ld dy=%ld", dx, dy);
            SetWindowTextW(hwnd, title);
        }
        else if (ri->header.dwType == RIM_TYPEKEYBOARD)
        {
            const RAWKEYBOARD& k = ri->data.keyboard;
            bool down = (k.Flags & RI_KEY_BREAK) == 0;
            std::wstring name = VKeyToName(k.VKey);
            std::printf("[key] %s  vkey=%u  make=0x%02X  flags=0x%04X  (%ls)\n",
                        down ? "DOWN" : "UP  ",
                        (unsigned)k.VKey,
                        (unsigned)k.MakeCode,
                        (unsigned)k.Flags,
                        name.c_str());
        }
        return 0;
    }

    // Also shows the classic message path (some UIs rely on it; Raw Input gameplay often doesn't).
    case WM_KEYDOWN:
        std::printf("[wm_keydown] vkey=%u\n", (unsigned)wParam);
        return 0;
    case WM_MOUSEMOVE:
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    const wchar_t* kClass = L"rawinput_demo_window";

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
        0, kClass, L"rawinput_demo",
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


