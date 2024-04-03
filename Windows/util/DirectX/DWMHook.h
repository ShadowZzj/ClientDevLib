#pragma once
#include "Setting.hpp"
#include <d3d11.h>
#include <memory>
#include <mutex>

#pragma comment(lib, "D3D11.lib")
namespace zzj
{
    namespace D3D
    {
        class DWMHook
        {
        public:
            static BOOL Setup(std::shared_ptr<Setting> _setting);
            static void Destroy();

        private:
            static LRESULT CALLBACK WindowProcess(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
            static BOOL ImguiInit();
            static uintptr_t GetPresentAddress();
            static void Initialize();
            static void Render(IDXGISwapChain *SwapChain);
            static __int64 __fastcall DetourFN(void* thisptr, IDXGISwapChain* a2, __int64 a3, unsigned int a4, int a5, const void* a6, void* a7, unsigned int a8);
            inline static std::shared_ptr<Setting> setting;
            inline static ID3D11Device *g_pd3dDevice = NULL;
            inline static ID3D11DeviceContext *g_pd3dDeviceContext = NULL;
            inline static IDXGISwapChain *g_pSwapChain = NULL;
            inline static ID3D11RenderTargetView *g_mainRenderTargetView = NULL;
            inline static WNDPROC originalWindowProcess = nullptr;
            inline static std::once_flag IsInitialized;
        };
    };
};