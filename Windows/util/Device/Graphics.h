#pragma once
#include <Windows.h>
#include <d3d11.h>
class Graphics
{
  public:
    Graphics(HWND hWnd);
    ~Graphics();
    Graphics(const Graphics &) = delete;
    Graphics &operator=(const Graphics &) = delete;
    void EndFrame();
    void ClearBuffer(float read, float green, float blue) noexcept;
  private:
    ID3D11Device *pDevice         = nullptr;
    ID3D11DeviceContext *pContext = nullptr;
    IDXGISwapChain *pSwapChain    = nullptr;
    ID3D11RenderTargetView *pTarget = nullptr;
};