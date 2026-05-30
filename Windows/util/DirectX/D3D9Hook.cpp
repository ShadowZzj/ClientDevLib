#include "D3D9Hook.h"
#include <Detours/build/include/detours.h>
#include <imgui/backends/imgui_impl_dx9.h>
#include <imgui/backends/imgui_impl_win32.h>
#include <imgui/imgui.h>
#include <intrin.h>
#include <spdlog/spdlog.h>
#include <atomic>
#include <stdexcept>
#include <utility>

#pragma comment(lib, "d3d9.lib")

using namespace zzj::D3D;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
constexpr DWORD kMenuToggleVk = VK_OEM_4; // '['
constexpr UINT  kToggleMenuMsg = WM_APP + 0x420;
constexpr UINT  kQuitMenuMsg   = WM_APP + 0x421;

HANDLE g_uiThread = nullptr;
DWORD  g_uiThreadId = 0;
HHOOK  g_menuKeyboardHook = nullptr;
DWORD  g_menuKeyboardThreadId = 0;
std::atomic<bool> g_stopUi{false};
D3DPRESENT_PARAMETERS g_d3dpp{};

struct UiThreadStart
{
    HANDLE ready = nullptr;
    bool   ok = false;
};

bool HostWindowHasFocus(HWND ourWindow)
{
    HWND fg = ::GetForegroundWindow();
    if (!fg)
        return false;
    if (ourWindow && fg == ourWindow)
        return true;

    DWORD fgPid = 0;
    ::GetWindowThreadProcessId(fg, &fgPid);
    return fgPid == ::GetCurrentProcessId();
}

BOOL CALLBACK FindHostWindowProc(HWND hwnd, LPARAM lparam)
{
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ::GetCurrentProcessId())
        return TRUE;
    if (hwnd == D3D9Hook::window)
        return TRUE;
    if (!::IsWindowVisible(hwnd))
        return TRUE;

    *reinterpret_cast<HWND *>(lparam) = hwnd;
    return FALSE;
}

HWND FindHostWindow()
{
    HWND hwnd = nullptr;
    ::EnumWindows(FindHostWindowProc, reinterpret_cast<LPARAM>(&hwnd));
    return hwnd;
}

void SignalStart(UiThreadStart *start, bool ok)
{
    if (!start)
        return;
    start->ok = ok;
    if (start->ready)
        ::SetEvent(start->ready);
}

void ResetExternalDevice()
{
    if (!D3D9Hook::device)
        return;

    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = D3D9Hook::device->Reset(&g_d3dpp);
    if (FAILED(hr))
        spdlog::warn("D3D9Hook: external device Reset failed, hr=0x{:08X}", static_cast<unsigned>(hr));
    ImGui_ImplDX9_CreateDeviceObjects();
}

void ApplyMenuVisibility()
{
    if (!D3D9Hook::window)
        return;

    const bool visible = ::IsWindowVisible(D3D9Hook::window) != FALSE;
    if (D3D9Hook::open && !visible)
    {
        ::ShowWindow(D3D9Hook::window, SW_SHOW);
        ::SetForegroundWindow(D3D9Hook::window);
    }
    else if (!D3D9Hook::open && visible)
    {
        ::ShowWindow(D3D9Hook::window, SW_HIDE);
    }
}

LRESULT CALLBACK MenuKeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && wParam == kMenuToggleVk)
    {
        const bool keyUp = (lParam & (1u << 31)) != 0;
        const bool wasDown = (lParam & (1u << 30)) != 0;
        if (!keyUp && !wasDown && HostWindowHasFocus(D3D9Hook::window) && g_uiThreadId)
            ::PostThreadMessageA(g_uiThreadId, kToggleMenuMsg, 0, 0);
    }

    return ::CallNextHookEx(g_menuKeyboardHook, code, wParam, lParam);
}

bool InstallMenuKeyboardHook()
{
    if (g_menuKeyboardHook)
        return true;

    HWND hostWindow = FindHostWindow();
    if (!hostWindow)
        return false;

    DWORD tid = ::GetWindowThreadProcessId(hostWindow, nullptr);
    if (!tid)
        return false;

    HMODULE self = nullptr;
    ::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(&MenuKeyboardProc),
                         &self);

    g_menuKeyboardHook = ::SetWindowsHookExA(WH_KEYBOARD, MenuKeyboardProc, self, tid);
    if (!g_menuKeyboardHook)
    {
        spdlog::warn("D3D9Hook: SetWindowsHookExA(WH_KEYBOARD) failed, tid={}, err={}",
                     tid, ::GetLastError());
        return false;
    }

    g_menuKeyboardThreadId = tid;
    spdlog::info("D3D9Hook: menu keyboard hook installed, tid={}, vk=0x{:02X}",
                 tid, static_cast<unsigned>(kMenuToggleVk));
    return true;
}

void UninstallMenuKeyboardHook()
{
    if (!g_menuKeyboardHook)
        return;

    ::UnhookWindowsHookEx(g_menuKeyboardHook);
    spdlog::info("D3D9Hook: menu keyboard hook removed, tid={}", g_menuKeyboardThreadId);
    g_menuKeyboardHook = nullptr;
    g_menuKeyboardThreadId = 0;
}

DWORD WINAPI MenuThreadProc(LPVOID param)
{
    auto *start = reinterpret_cast<UiThreadStart *>(param);

    try
    {
        if (!D3D9Hook::SetupWindowClass("zzj::D3D9::ExternalMenu"))
        {
            SignalStart(start, false);
            return 1;
        }

        if (!D3D9Hook::SetupWindow("GGThreadBlock"))
        {
            D3D9Hook::DestroyWindowClass();
            SignalStart(start, false);
            return 1;
        }

        if (!D3D9Hook::SetupDirectX())
        {
            D3D9Hook::DestroyWindow();
            D3D9Hook::DestroyWindowClass();
            SignalStart(start, false);
            return 1;
        }

        D3D9Hook::SetupMenu(D3D9Hook::device);
        SignalStart(start, true);
        start = nullptr;

        DWORD lastHookRetry = 0;
        MSG msg{};
        while (!g_stopUi.load(std::memory_order_acquire))
        {
            while (::PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == kToggleMenuMsg)
                {
                    D3D9Hook::open = !D3D9Hook::open;
                    spdlog::info("D3D9Hook::open {}", D3D9Hook::open);
                    continue;
                }
                if (msg.message == kQuitMenuMsg)
                {
                    g_stopUi.store(true, std::memory_order_release);
                    continue;
                }

                ::TranslateMessage(&msg);
                ::DispatchMessageA(&msg);
            }

            DWORD now = ::GetTickCount();
            if (!g_menuKeyboardHook && now - lastHookRetry >= 1000)
            {
                lastHookRetry = now;
                InstallMenuKeyboardHook();
            }

            ApplyMenuVisibility();

            if (D3D9Hook::open)
            {
                D3D9Hook::Render();
                ::Sleep(16);
            }
            else
            {
                ::Sleep(50);
            }
        }

        UninstallMenuKeyboardHook();

        if (D3D9Hook::setup)
        {
            ImGui_ImplDX9_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            D3D9Hook::setup = false;
        }

        D3D9Hook::DestroyDirectX();
        D3D9Hook::DestroyWindow();
        D3D9Hook::DestroyWindowClass();
    }
    catch (const std::exception &e)
    {
        spdlog::error("D3D9Hook menu thread exception: {}", e.what());
        SignalStart(start, false);
        return 1;
    }

    return 0;
}
} // namespace

LRESULT CALLBACK D3D9Hook::WindowProcess(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    if (message == WM_KEYDOWN && wParam == kMenuToggleVk)
    {
        D3D9Hook::open = !D3D9Hook::open;
        spdlog::info("D3D9Hook::open {}", D3D9Hook::open);
        ApplyMenuVisibility();
        return 0;
    }

    if (D3D9Hook::setup && ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam))
        return true;

    switch (message)
    {
    case WM_SIZE:
        if (D3D9Hook::device && wParam != SIZE_MINIMIZED)
        {
            g_d3dpp.BackBufferWidth = LOWORD(lParam);
            g_d3dpp.BackBufferHeight = HIWORD(lParam);
            ResetExternalDevice();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_CLOSE:
        D3D9Hook::open = false;
        ApplyMenuVisibility();
        return 0;
    case WM_DESTROY:
        return 0;
    default:
        break;
    }

    return ::DefWindowProcA(window, message, wParam, lParam);
}

bool D3D9Hook::SetupWindowClass(const char *windowClassName) noexcept
{
    windowClass = {sizeof(WNDCLASSEXA)};
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = D3D9Hook::WindowProcess;
    windowClass.hInstance = GetModuleHandle(nullptr);
    windowClass.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = windowClassName;
    return RegisterClassExA(&windowClass) != 0;
}

void D3D9Hook::DestroyWindowClass() noexcept
{
    if (windowClass.lpszClassName)
        UnregisterClassA(windowClass.lpszClassName, windowClass.hInstance);
}

bool D3D9Hook::SetupWindow(const char *windowName) noexcept
{
    window = CreateWindowExA(WS_EX_TOOLWINDOW, windowClass.lpszClassName, windowName,
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                             CW_USEDEFAULT, CW_USEDEFAULT, 460, 430, nullptr, nullptr,
                             windowClass.hInstance, nullptr);
    if (!window)
        return false;

    ShowWindow(window, SW_HIDE);
    UpdateWindow(window);
    return true;
}

void D3D9Hook::DestroyWindow() noexcept
{
    if (window)
    {
        ::DestroyWindow(window);
        window = nullptr;
    }
}

bool D3D9Hook::SetupDirectX() noexcept
{
    d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d9)
        return false;

    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    g_d3dpp.EnableAutoDepthStencil = FALSE;
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    g_d3dpp.hDeviceWindow = window;

    HRESULT hr = d3d9->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                    D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                    &g_d3dpp, &device);
    if (FAILED(hr))
    {
        hr = d3d9->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                                &g_d3dpp, &device);
    }

    if (FAILED(hr))
    {
        spdlog::warn("D3D9Hook: external CreateDevice failed, hr=0x{:08X}", static_cast<unsigned>(hr));
        DestroyDirectX();
        return false;
    }

    return true;
}

void D3D9Hook::DestroyDirectX() noexcept
{
    if (device)
    {
        device->Release();
        device = nullptr;
    }
    if (d3d9)
    {
        d3d9->Release();
        d3d9 = nullptr;
    }
}

void D3D9Hook::Setup(std::shared_ptr<Setting> setting)
{
    D3D9Hook::setting = std::move(setting);
    D3D9Hook::open = false;
    D3D9Hook::setup = false;
    g_stopUi.store(false, std::memory_order_release);

    UiThreadStart start{};
    start.ready = ::CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (!start.ready)
        throw std::runtime_error("Failed to create D3D9Hook startup event");

    g_uiThread = ::CreateThread(nullptr, 0, MenuThreadProc, &start, 0, &g_uiThreadId);
    if (!g_uiThread)
    {
        ::CloseHandle(start.ready);
        throw std::runtime_error("Failed to create D3D9Hook menu thread");
    }

    DWORD wait = ::WaitForSingleObject(start.ready, INFINITE);
    ::CloseHandle(start.ready);

    if (wait != WAIT_OBJECT_0 || !start.ok)
        throw std::runtime_error("Failed to setup external ImGui menu");
}

void D3D9Hook::SetupMenu(LPDIRECT3DDEVICE9) noexcept
{
    spdlog::info("SetupMenu called (external)");
    ImGui::CreateContext();
    D3D9Hook::setting->Init();
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX9_Init(device);
    setup = true;
}

void D3D9Hook::Destroy() noexcept
{
    g_stopUi.store(true, std::memory_order_release);
    if (g_uiThreadId)
        ::PostThreadMessageA(g_uiThreadId, kQuitMenuMsg, 0, 0);

    if (g_uiThread)
    {
        ::WaitForSingleObject(g_uiThread, INFINITE);
        ::CloseHandle(g_uiThread);
        g_uiThread = nullptr;
    }

    g_uiThreadId = 0;
    UninstallMenuKeyboardHook();
    D3D9Hook::open = false;
}

void D3D9Hook::Render() noexcept
{
    if (!device)
        return;

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    D3D9Hook::setting->Render(open);

    ImGui::EndFrame();

    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_RGBA(12, 12, 16, 255), 1.0f, 0);

    if (device->BeginScene() >= 0)
    {
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        device->EndScene();
    }

    HRESULT result = device->Present(nullptr, nullptr, nullptr, nullptr);
    if (result == D3DERR_DEVICELOST && device->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)
        ResetExternalDevice();
}

void D3D9Hook::SetupHook()
{
    // The GGThreadBlock menu is rendered in an external tool window now.
}

void D3D9Hook::DestroyHook()
{
}

HRESULT __stdcall D3D9Hook::EndScene(LPDIRECT3DDEVICE9 device)
{
    return D3D9Hook::originalEndScene ? D3D9Hook::originalEndScene(device) : D3D_OK;
}

HRESULT __stdcall D3D9Hook::Reset(LPDIRECT3DDEVICE9 device, D3DPRESENT_PARAMETERS *params)
{
    return D3D9Hook::originalReset ? D3D9Hook::originalReset(device, params) : D3D_OK;
}
