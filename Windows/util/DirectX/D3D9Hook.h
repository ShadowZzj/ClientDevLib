#pragma once
#include <d3d9.h>
#include <Windows/util/DirectX/Setting.hpp>
#include <memory>
#include <string>
namespace zzj
{
    namespace D3D
    {
        class D3D9Hook
        {
        public:
            struct SetupOptions
            {
                std::string windowClassName;
                std::string windowName;
                DWORD exStyle = WS_EX_TOOLWINDOW;
                DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
                int x = CW_USEDEFAULT;
                int y = CW_USEDEFAULT;
                int width = 460;
                int height = 430;
                HWND parent = nullptr;
                HMENU menu = nullptr;
                HINSTANCE instance = nullptr;
                bool showInitially = false;
            };

            D3D9Hook() = default;
            ~D3D9Hook() = default;
            static bool SetupWindowClass(const SetupOptions& options) noexcept;
            static void DestroyWindowClass() noexcept;
 
            static bool SetupWindow(const SetupOptions& options) noexcept;
            static void DestroyWindow() noexcept;
 
            static bool SetupDirectX() noexcept;
            static void DestroyDirectX() noexcept;
 
            static void Setup(std::shared_ptr<Setting> setting, const SetupOptions& options);

            static void SetupMenu(LPDIRECT3DDEVICE9 device) noexcept;
            static void Destroy() noexcept;

            static void Render() noexcept;
            static void SetOpen(bool value) noexcept;
            static void ToggleOpen() noexcept;

            static void SetupHook();
            static void DestroyHook();
            static  void* VirtualFunction(void* instance, size_t index) noexcept
            {
                auto vtable = *reinterpret_cast<void***>(instance);
                return vtable[index]; 
            }
            
            inline static bool open = false;
            inline static bool setup = false;

            inline static HWND window = nullptr;
            inline static WNDCLASSEXA windowClass = { 0 };
            inline static WNDPROC originalWindowProcess = nullptr;
            
            inline static LPDIRECT3D9 d3d9 = nullptr;
            inline static LPDIRECT3DDEVICE9 device = nullptr;
        private:
            
            using EndSceneFn = HRESULT (__stdcall* ) (LPDIRECT3DDEVICE9);
            inline static EndSceneFn originalEndScene = nullptr;
            static HRESULT __stdcall EndScene(LPDIRECT3DDEVICE9 device);

            using ResetFn = HRESULT (__stdcall* ) (LPDIRECT3DDEVICE9, D3DPRESENT_PARAMETERS*);
            inline static ResetFn originalReset = nullptr;
            static HRESULT __stdcall Reset(LPDIRECT3DDEVICE9 device, D3DPRESENT_PARAMETERS* params);
            static LRESULT CALLBACK WindowProcess(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
            inline static std::shared_ptr<Setting> setting = nullptr;
        };
    };
};
