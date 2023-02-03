#include <iostream>

#include <windows.h>


bool HandleKeys(int key)
{
    switch (key)
    {
    case VK_ESCAPE:
        return true;
    default:
        return false;
    }
}
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_KEYDOWN:
        if(HandleKeys(wParam))
            return 0;
        else
            break;
    case WM_CHAR:
        OutputDebugStringA("WM_CHAR");
        OutputDebugStringA((char*)&wParam);
        OutputDebugStringA("\n");
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
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
    //register the window class
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = nullptr;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"WindowClass";


    if (!RegisterClassEx(&wc))
        return -1;

    //create the window and use the result as the handle
    HWND hwnd = CreateWindowEx(
        0,                              //optional window styles
        L"WindowClass",                 //window class
        L"Learn to Program Windows",    //window title
        WS_OVERLAPPEDWINDOW,            //window style
        //size and position
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL,       //parent window
        NULL,       //menu
        hInstance,  //instance handle
        NULL        //additional application data
    );

    if (hwnd == NULL)
        return -1;
+
    //display the window on the screen
    ShowWindow(hwnd, nShowCmd);

    //run the message loop
    //this is the code that pumpts messages from the queue
    MSG msg = { 0 };
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    UnregisterClassW(L"WindowClass", hInstance);

    return 0;
}