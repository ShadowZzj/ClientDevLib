#pragma once
#include <DirectXMath.h>
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <memory>
#include <random>
#include <wrl.h>

namespace zzj
{
class Graphics
{
    friend class Bindable;

  public:
    Graphics(HWND hWnd);
    ~Graphics();
    Graphics(const Graphics &) = delete;
    Graphics &operator=(const Graphics &) = delete;
    void EndFrame();
    void BeginFrame(float read, float green, float blue) noexcept;
    void DrawIndexed(UINT count);
    void SetProjection(DirectX::FXMMATRIX proj) noexcept;
    DirectX::XMMATRIX GetProjection() const noexcept;
    void EnableImgui() noexcept;
    void DisableImgui() noexcept;
    bool IsImguiEnabled() const noexcept;
    void SetCamera(DirectX::FXMMATRIX cam) noexcept;
    DirectX::XMMATRIX GetCamera() const noexcept;

  private:
    DirectX::XMMATRIX projection;
    Microsoft::WRL::ComPtr<ID3D11Device> pDevice;
    Microsoft::WRL::ComPtr<IDXGISwapChain> pSwap;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> pContext;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> pTarget;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> pDSV;
    DirectX::XMMATRIX camera;
    bool imguiEnabled = true;
};
}; // namespace zzj