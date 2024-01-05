#include "D3D9Hook.h"
#include <Detours/build/include/detours.h>
#include <imgui/backends/imgui_impl_dx9.h>
#include <imgui/backends/imgui_impl_win32.h>
#include <imgui/imgui.h>
#include <intrin.h>
#include <stdexcept>
#include <spdlog/spdlog.h>
#pragma comment(lib, "d3d9.lib")
using namespace zzj::D3D;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK D3D9Hook::WindowProcess(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    // toogle menu
    if (GetAsyncKeyState(D3D9Hook::setting->GetToggleMenuKey()) & 1)
    {
        D3D9Hook::open = !D3D9Hook::open;
        spdlog::info("D3D9Hook::open {}", D3D9Hook::open);
    }

    // pass message to imgui
    if (D3D9Hook::open && ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam))
        return true;
    return CallWindowProc(D3D9Hook::originalWindowProcess, window, message, wParam, lParam);
}
bool D3D9Hook::SetupWindowClass(const char *windowClassName) noexcept
{
    windowClass               = {sizeof(WNDCLASSEX)};
    windowClass.style         = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc   = DefWindowProcA;
    windowClass.hInstance     = GetModuleHandle(nullptr);
    windowClass.lpszClassName = windowClassName;
    return RegisterClassExA(&windowClass);
}
void D3D9Hook::DestroyWindowClass() noexcept
{
    UnregisterClassA(windowClass.lpszClassName, windowClass.hInstance);
}

bool D3D9Hook::SetupWindow(const char *windowName) noexcept
{
    // create temp window
    window = CreateWindowA(windowClass.lpszClassName, windowName, WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr,
                           windowClass.hInstance, nullptr);
    if (!window)
        return false;

    return true;
}
void D3D9Hook::DestroyWindow() noexcept
{
    if (window)
        ::DestroyWindow(window);
}

bool D3D9Hook::SetupDirectX() noexcept
{
    const auto handle = GetModuleHandleA("d3d9.dll");
    if (!handle)
        return false;

    using CreateFn    = LPDIRECT3D9(WINAPI *)(UINT);
    const auto create = reinterpret_cast<CreateFn>(GetProcAddress(handle, "Direct3DCreate9"));
    if (!create)
        return false;

    d3d9 = create(D3D_SDK_VERSION);

    if (!d3d9)
        return false;

    D3DPRESENT_PARAMETERS params  = {0};
    params.BackBufferFormat       = D3DFMT_UNKNOWN;
    params.MultiSampleType        = D3DMULTISAMPLE_NONE;
    params.SwapEffect             = D3DSWAPEFFECT_DISCARD;
    params.hDeviceWindow          = window;
    params.Windowed               = TRUE;
    params.AutoDepthStencilFormat = D3DFMT_UNKNOWN;

    if (FAILED(d3d9->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF, window,
                                  D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_DISABLE_DRIVER_MANAGEMENT, &params,
                                  &device)))
        return false;

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
    D3D9Hook::setting = setting;
    if (!SetupWindowClass("zzj::D3D9::D3D9Hook"))
        throw std::runtime_error("Failed to setup window class");

    if (!SetupWindow("zzj::D3D9::D3D9Hook"))
        throw std::runtime_error("Failed to setup window");

    if (!SetupDirectX())
        throw std::runtime_error("Failed to setup directx");

    DestroyWindow();
    DestroyWindowClass();
    SetupHook();
}

void D3D9Hook::SetupMenu(LPDIRECT3DDEVICE9 device) noexcept
{
    spdlog::info("SetupMenu called");
    auto params = D3DDEVICE_CREATION_PARAMETERS{};
    device->GetCreationParameters(&params);

    window = params.hFocusWindow;

    originalWindowProcess = reinterpret_cast<WNDPROC>(SetWindowLongPtr(window, GWLP_WNDPROC, LONG_PTR(WindowProcess)));
    ImGui::CreateContext();
    D3D9Hook::setting->InitImguiConfig();
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX9_Init(device);

    setup = true;
}
void D3D9Hook::Destroy() noexcept
{
    DestroyHook();
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    D3D9Hook::setting->UninitImguiConfig();
    ImGui::DestroyContext();

    if (originalWindowProcess)
        SetWindowLongPtr(window, GWLP_WNDPROC, LONG_PTR(originalWindowProcess));

    DestroyDirectX();
}

void D3D9Hook::Render() noexcept
{
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();


    D3D9Hook::setting->Render(open);

    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

void D3D9Hook::SetupHook()
{
    originalEndScene = reinterpret_cast<EndSceneFn>(VirtualFunction(device, 42));
    originalReset    = reinterpret_cast<ResetFn>(VirtualFunction(device, 16));
    if (DetourTransactionBegin() != NO_ERROR)
        throw std::runtime_error("Failed to start transaction");
    if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR)
        throw std::runtime_error("Failed to update thread");
    if (DetourAttach(&(PVOID &)originalEndScene, &EndScene) != NO_ERROR)
        throw std::runtime_error("Failed to attach EndScene");
    if (DetourAttach(&(PVOID &)originalReset, &Reset) != NO_ERROR)
        throw std::runtime_error("Failed to attach Reset");
    if (DetourTransactionCommit() != NO_ERROR)
        throw std::runtime_error("Failed to commit transaction");


    DestroyDirectX();
}
void D3D9Hook::DestroyHook()
{
    if (DetourTransactionBegin() != NO_ERROR)
        throw std::runtime_error("Failed to start transaction");
    if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR)
        throw std::runtime_error("Failed to update thread");
    if (DetourDetach(&(PVOID &)originalEndScene, &EndScene) != NO_ERROR)
        throw std::runtime_error("Failed to detach EndScene");
    if (DetourDetach(&(PVOID &)originalReset, &Reset) != NO_ERROR)
        throw std::runtime_error("Failed to detach Reset");
    if (DetourTransactionCommit() != NO_ERROR)
        throw std::runtime_error("Failed to commit transaction"); 
}

HRESULT __stdcall D3D9Hook::EndScene(LPDIRECT3DDEVICE9 device)
{
    static const auto returnAddress = _ReturnAddress();
    auto result =  D3D9Hook::originalEndScene(device);

    if (!D3D9Hook::setup)
    {
        D3D9Hook::SetupMenu(device);
    }
    
    if(D3D9Hook::open)
    {
        D3D9Hook::Render();
    }

    return result;

}

HRESULT __stdcall D3D9Hook::Reset(LPDIRECT3DDEVICE9 device, D3DPRESENT_PARAMETERS *params)
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    auto result = D3D9Hook::originalReset(device,params);
    ImGui_ImplDX9_CreateDeviceObjects();
    return result;
}