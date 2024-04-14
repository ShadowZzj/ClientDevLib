
#include <Windows.h>
#include <General/util/StrUtil.h>
#include <json.hpp>
#include <spdlog/fmt/fmt.h>
#include <thread>
HANDLE hMutex;
//WindowProc
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));

        EndPaint(hwnd, &ps);
    }
    return 0;

    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void ThreadFunc1()
{
    while (true)
    {
        if (WaitForSingleObjectEx(hMutex, INFINITE,TRUE) == WAIT_OBJECT_0)
        {
            Sleep(5000);
			ReleaseMutex(hMutex);
		}
    }
}
void ThreadFunc2()
{
    while (true)
    {
        if (WaitForSingleObjectEx(hMutex, INFINITE, TRUE) == WAIT_OBJECT_0)
        {
            Sleep(5000);
            ReleaseMutex(hMutex);
        }
    }
}
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine,
                       _In_ int nShowCmd)
    {
    //Create a mutex
    
    hMutex = CreateMutex(NULL, FALSE, L"test");
    std::thread t1(ThreadFunc1);
    std::thread t2(ThreadFunc2);
    //Register the window class
    const wchar_t CLASS_NAME[] = L"test";

    WNDCLASS wc = { };

    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    //Create the window

    HWND hwnd = CreateWindowEx(
        0,                              // Optional window styles.
        CLASS_NAME,                     // Window class
        L"Learn to Program Windows",    // Window text
        WS_OVERLAPPEDWINDOW,            // Window style

        // Size and position
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,

        NULL,       // Parent window    
        NULL,       // Menu
        hInstance,  // Instance handle
        NULL        // Additional application data
    );

    if (hwnd == NULL)
    {
        return 0;
    }

    ShowWindow(hwnd, nShowCmd);

    // Run the message loop.

    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }


    return 0;
}