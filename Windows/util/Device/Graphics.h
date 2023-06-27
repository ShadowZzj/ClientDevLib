#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <wrl.h>
class Graphics
{
  public:
    Graphics(HWND hWnd);
    ~Graphics()                = default;
    Graphics(const Graphics &) = delete;
    Graphics &operator=(const Graphics &) = delete;
    void EndFrame();
    void ClearBuffer(float read, float green, float blue) noexcept;
    void DrawTestTriangle(float angle);
  private:
    Microsoft::WRL::ComPtr<ID3D11Device> pDevice;
    Microsoft::WRL::ComPtr<IDXGISwapChain> pSwap;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> pContext;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> pTarget;
};