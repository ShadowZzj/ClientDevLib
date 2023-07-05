#include "Graphics.h"
#include "DXError/DxgiInfoManager.h"
#include "DXError/dxerr.h"
#include <DirectXMath.h>
#include <General/util/BaseUtil.hpp>
#include <General/util/Exception/Exception.h>
#include <General/util/File/File.h>
#include <boost/filesystem.hpp>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <imgui/backends/imgui_impl_dx11.h>
#include <imgui/backends/imgui_impl_win32.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "D3DCompiler.lib")

namespace wrl = Microsoft::WRL;
namespace dx  = DirectX;
using namespace zzj;
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

    auto debugFlag = D3D11_CREATE_DEVICE_DEBUG;
    auto hr        = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, debugFlag, nullptr, 0,
                                            D3D11_SDK_VERSION, &sd, &pSwap, &pDevice, nullptr, &pContext);
    if (hr != S_OK)
        throw ZZJ_DX_EXCEPTION(hr);

    Microsoft::WRL::ComPtr<ID3D11Resource> pBackBuffer = nullptr;
    hr                                                 = pSwap->GetBuffer(0, __uuidof(ID3D11Resource), &pBackBuffer);
    if (hr != S_OK)
        throw ZZJ_DX_EXCEPTION(hr);
    hr = pDevice->CreateRenderTargetView(pBackBuffer.Get(), nullptr, &pTarget);
    if (hr != S_OK)
        throw ZZJ_DX_EXCEPTION(hr);
    // create depth stensil state
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable              = TRUE;
    dsDesc.DepthWriteMask           = D3D11_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc                = D3D11_COMPARISON_LESS;
    wrl::ComPtr<ID3D11DepthStencilState> pDSState;
    hr = pDevice->CreateDepthStencilState(&dsDesc, &pDSState);
    if (FAILED(hr))
    {
        throw ZZJ_DX_EXCEPTION(hr);
    }

    // bind depth state
    pContext->OMSetDepthStencilState(pDSState.Get(), 1u);

    // create depth stensil texture
    wrl::ComPtr<ID3D11Texture2D> pDepthStencil;
    D3D11_TEXTURE2D_DESC descDepth = {};
    descDepth.Width                = 800u;
    descDepth.Height               = 600u;
    descDepth.MipLevels            = 1u;
    descDepth.ArraySize            = 1u;
    descDepth.Format               = DXGI_FORMAT_D32_FLOAT;
    descDepth.SampleDesc.Count     = 1u;
    descDepth.SampleDesc.Quality   = 0u;
    descDepth.Usage                = D3D11_USAGE_DEFAULT;
    descDepth.BindFlags            = D3D11_BIND_DEPTH_STENCIL;
    hr                             = pDevice->CreateTexture2D(&descDepth, nullptr, &pDepthStencil);
    if (FAILED(hr))
    {
        throw ZZJ_DX_EXCEPTION(hr);
    }
    // create view of depth stensil texture
    D3D11_DEPTH_STENCIL_VIEW_DESC descDSV = {};
    descDSV.Format                        = DXGI_FORMAT_D32_FLOAT;
    descDSV.ViewDimension                 = D3D11_DSV_DIMENSION_TEXTURE2D;
    descDSV.Texture2D.MipSlice            = 0u;
    hr                                    = pDevice->CreateDepthStencilView(pDepthStencil.Get(), &descDSV, &pDSV);
    if (FAILED(hr))
    {
        throw ZZJ_DX_EXCEPTION(hr);
    }

    // bind depth stensil view to OM
    pContext->OMSetRenderTargets(1u, pTarget.GetAddressOf(), pDSV.Get());

    // configure viewport
    D3D11_VIEWPORT vp;
    vp.Width    = 800.0f;
    vp.Height   = 600.0f;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    pContext->RSSetViewports(1u, &vp);

    // init imgui d3d impl
    ImGui_ImplDX11_Init(pDevice.Get(), pContext.Get());
}

void Graphics::EndFrame()
{
    // imgui frame end
    if (imguiEnabled)
    {
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    auto hr = pSwap->Present(1u, 0u);
    if (hr == S_OK)
        return;
    else if (hr == DXGI_ERROR_DEVICE_REMOVED)
        throw ZZJ_DX_EXCEPTION(pDevice->GetDeviceRemovedReason());
    else
        throw ZZJ_DX_EXCEPTION(hr);
    return;
}
void Graphics::BeginFrame(float read, float green, float blue) noexcept
{
    // imgui begin frame
    if (imguiEnabled)
    {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
    }

    const float color[] = {read, green, blue, 1.0f};
    pContext->ClearRenderTargetView(pTarget.Get(), color);
    pContext->ClearDepthStencilView(pDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0u);
}

void Graphics::DrawIndexed(UINT count)
{
    zzj::DxgiInfoManager::GetInstance().Begin();
    pContext->DrawIndexed(count, 0u, 0u);
    zzj::DxgiInfoManager::GetInstance().End();
}

void Graphics::SetProjection(DirectX::FXMMATRIX proj) noexcept
{
    projection = proj;
}

DirectX::XMMATRIX Graphics::GetProjection() const noexcept
{
    return projection;
}

void Graphics::EnableImgui() noexcept
{
    imguiEnabled = true;
}

void Graphics::DisableImgui() noexcept
{
    imguiEnabled = false;
}

bool Graphics::IsImguiEnabled() const noexcept
{
    return imguiEnabled;
}

void Graphics::SetCamera(DirectX::FXMMATRIX cam) noexcept
{
    camera = cam;
}

DirectX::XMMATRIX Graphics::GetCamera() const noexcept
{
    return camera;
}
Graphics::~Graphics()
{
    ImGui_ImplDX11_Shutdown();
}
