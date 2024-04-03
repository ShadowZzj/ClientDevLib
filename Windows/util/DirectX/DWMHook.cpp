#pragma once
#include "DWMHook.h"
#include <Detours/build/include/detours.h>
#include <Windows/util/Process/ProcessHelper.h>
#include <imgui/backends/imgui_impl_dx11.h>
#include <imgui/backends/imgui_impl_win32.h>
#include <General/ThirdParty/imgui/imgui.h>
#include <spdlog/spdlog.h>
using Fn_Present = __int64(__fastcall *)(void* thisptr, IDXGISwapChain* a2, __int64 a3, unsigned int a4, int, const void*, void*, unsigned int);
Fn_Present Original_Present = NULL;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK zzj::D3D::DWMHook::WindowProcess(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    // pass message to imgui
    if (ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam))
        return true;
    return CallWindowProc(originalWindowProcess, window, message, wParam, lParam);
}
BOOL zzj::D3D::DWMHook::ImguiInit()
{
    if (SUCCEEDED(g_pSwapChain->GetDevice(__uuidof(ID3D11Device), (void **)&g_pd3dDevice)))
    {
        g_pd3dDevice->GetImmediateContext(&g_pd3dDeviceContext);
    }
    ID3D11Texture2D *RenderTargetTexture = nullptr;
    if (SUCCEEDED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&RenderTargetTexture))))
    {
        g_pd3dDevice->CreateRenderTargetView(RenderTargetTexture, NULL, &g_mainRenderTargetView);
        if (!g_mainRenderTargetView)
            return FALSE;
        RenderTargetTexture->Release();
    }

    DXGI_SWAP_CHAIN_DESC desc;
    HWND windowHandle = FindWindow(L"Progman", L"Program Manager");
    originalWindowProcess = reinterpret_cast<WNDPROC>(SetWindowLongPtr(windowHandle, GWLP_WNDPROC, LONG_PTR(WindowProcess)));
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplWin32_Init(windowHandle);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // 设置风格
    ImGui::StyleColorsLight();

    // 字体设置
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 16.0F, NULL, io.Fonts->GetGlyphRangesChineseFull());
    return TRUE;
}

void zzj::D3D::DWMHook::Render(IDXGISwapChain *SwapChain)
{

    g_pSwapChain = SwapChain;
    std::call_once(IsInitialized, []
                   { ImguiInit(); });
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    bool open = true;
    setting->Render(open);
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

__int64 __fastcall zzj::D3D::DWMHook::DetourFN(void *thisptr, IDXGISwapChain *a2, __int64 a3, unsigned int a4, int a5, const void* a6 , void* a7 , unsigned int a8)
{
    zzj::D3D::DWMHook::Render(a2);
    return Original_Present(thisptr, a2, a3, a4, a5, a6, a7, a8);
}

ULONG64 zzj::D3D::DWMHook::GetPresentAddress()
{
    zzj::Process process;
    zzj::Memory memory(process);
    auto dwmcoreInfo = memory.GetModuleInfo("dwmcore.dll");
    if (!dwmcoreInfo) {
        spdlog::error("dwmcore.dll not found");
        return 0;
    }

    auto instructionAddress = memory.PatternScan((uintptr_t)dwmcoreInfo->modBaseAddr, dwmcoreInfo->modBaseSize, "E8 ?? ?? ?? ?? 8B D8 85 C0 78 ?? 41 8A D6 49 8B CF E8 ?? ?? ?? ?? EB ??");
    if (instructionAddress.size() == 0)
        return 0;

    auto instruction = instructionAddress[0].address;
    auto offsets = *(LONG *)(instruction + 1);
    auto result = (ULONG64)instruction + 5 + offsets;
    return result;
}

void zzj::D3D::DWMHook::Initialize()
{
    ULONG64 PresentAddress = GetPresentAddress();
    if (!PresentAddress)
        return;

    spdlog::info("Present Address: {0:x}", PresentAddress);
    Original_Present = (Fn_Present)PresentAddress;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID &)Original_Present, &DetourFN);
    DetourTransactionCommit();
}
BOOL zzj::D3D::DWMHook::Setup(std::shared_ptr<Setting> _setting)
{
    setting = _setting;
    Initialize();
    return TRUE;
}

void zzj::D3D::DWMHook::Destroy()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&(PVOID &)Original_Present, &DetourFN);
    DetourTransactionCommit();

    //ImGui_ImplDX11_Shutdown();
    //ImGui_ImplWin32_Shutdown();
    //setting->UninitImguiConfig();
    //ImGui::DestroyContext();
    //if (originalWindowProcess)
    //    SetWindowLongPtr(FindWindow(L"Progman", L"Program Manager"), GWLP_WNDPROC, LONG_PTR(originalWindowProcess));
    //
    //if (g_pd3dDevice)
    //    g_pd3dDevice->Release();
    //if (g_pd3dDeviceContext)
    //    g_pd3dDeviceContext->Release();
    //if (g_pSwapChain)
    //    g_pSwapChain->Release();
    //if (g_mainRenderTargetView)
    //    g_mainRenderTargetView->Release();

    return;
}