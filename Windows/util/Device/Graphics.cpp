#include "Graphics.h"
#include <d3d11.h>
#include <General/util/BaseUtil.hpp>
#include <Windows/util/Device/DXError/dxerr.h>
#include <General/util/Exception/Exception.h>
#pragma comment(lib, "d3d11.lib")

Graphics::Graphics(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd               = {};
    sd.BufferDesc.Width                   = 0;
    sd.BufferDesc.Height                  = 0;
    sd.BufferDesc.Format                  = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 0;
    sd.BufferDesc.RefreshRate.Denominator = 0;
    sd.BufferDesc.Scaling                 = DXGI_MODE_SCALING_UNSPECIFIED;
    sd.BufferDesc.ScanlineOrdering        = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    sd.SampleDesc.Count                   = 1;
    sd.SampleDesc.Quality                 = 0;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount                        = 1;
    sd.OutputWindow                       = hWnd;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags                              = 0;

    auto hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd,
                                  &pSwapChain, &pDevice, nullptr, &pContext);
    if (hr != S_OK)
        throw ZZJ_DX_EXCEPTION(hr);

    ID3D11Resource *pBackBuffer = nullptr;
    DEFER
    {
        pBackBuffer->Release();
    };
    hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Resource), reinterpret_cast<void **>(&pBackBuffer));
    if (hr != S_OK)
        throw ZZJ_DX_EXCEPTION(hr);
    hr = pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pTarget);
    if (hr != S_OK)
        throw ZZJ_DX_EXCEPTION(hr);
}

Graphics::~Graphics()
{
    if (pContext != nullptr)
        pContext->Release();
    if (pSwapChain != nullptr)
        pSwapChain->Release();
    if (pDevice != nullptr)
        pDevice->Release();
}
void Graphics::EndFrame()
{
    auto hr = pSwapChain->Present(1u, 0u);
    if (hr == S_OK)
        return;
    else if (hr == DXGI_ERROR_DEVICE_REMOVED)
        throw ZZJ_DX_EXCEPTION(pDevice->GetDeviceRemovedReason());
    else
        throw ZZJ_DX_EXCEPTION(hr);
    return;
}
void Graphics::ClearBuffer(float read, float green, float blue) noexcept
{
    const float color[] = {read, green, blue, 1.0f};
    pContext->ClearRenderTargetView(pTarget, color);
}