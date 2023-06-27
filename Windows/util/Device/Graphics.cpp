#include "Graphics.h"
#include "DXError/DxgiInfoManager.h"
#include <DirectXMath.h>
#include <General/util/BaseUtil.hpp>
#include <General/util/Exception/Exception.h>
#include <General/util/File/File.h>
#include <Windows/util/Device/DXError/dxerr.h>
#include <boost/filesystem.hpp>
#include <d3d11.h>
#include <d3dcompiler.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "D3DCompiler.lib")

namespace wrl = Microsoft::WRL;
namespace dx  = DirectX;
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
}

void Graphics::EndFrame()
{
    auto hr = pSwap->Present(1u, 0u);
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
    pContext->ClearRenderTargetView(pTarget.Get(), color);
}

void Graphics::DrawTestTriangle(float angle)
{
    struct Vertex
    {
        float x;
        float y;
        float r;
        float g;
        float b;
    };
    const Vertex vertices[] = {{0.0f, 0.5f, 1.0f, 0, 0}, {0.5f, -0.5f, 0, 1.0f, 0}, {-0.5f, -0.5f, 0, 0, 1.0f}};
    wrl::ComPtr<ID3D11Buffer> pVertexBuffer;
    D3D11_BUFFER_DESC bd      = {};
    bd.BindFlags              = D3D11_BIND_VERTEX_BUFFER;
    bd.Usage                  = D3D11_USAGE_DEFAULT;
    bd.CPUAccessFlags         = 0u;
    bd.MiscFlags              = 0u;
    bd.ByteWidth              = sizeof(vertices);
    bd.StructureByteStride    = sizeof(Vertex);
    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem                = vertices;
    auto hr                   = pDevice->CreateBuffer(&bd, &sd, &pVertexBuffer);
    if (hr != S_OK)
        throw ZZJ_DX_EXCEPTION(hr);

    const UINT stride = sizeof(Vertex);
    const UINT offset = 0u;
    pContext->IASetVertexBuffers(0u, 1u, pVertexBuffer.GetAddressOf(), &stride, &offset);

    boost::filesystem::path currentExecutablePath = GetExecutablePath();

    struct ContantBuffer
    {
        dx::XMMATRIX transform;
    };
    const ContantBuffer cb = {
        dx::XMMatrixTranspose(dx::XMMatrixRotationZ(angle) * dx::XMMatrixScaling(3 / 4, 3 / 4, 0.0f)) *
        dx::XMMatrixTranslation(0.5f, 0.5f, 0.0f)};
    wrl::ComPtr<ID3D11Buffer> pContantBuffer;
    D3D11_BUFFER_DESC cbd      = {};
    cbd.BindFlags              = D3D11_BIND_CONSTANT_BUFFER;
    cbd.Usage                  = D3D11_USAGE_DYNAMIC;
    cbd.CPUAccessFlags         = D3D11_CPU_ACCESS_WRITE;
    cbd.MiscFlags              = 0u;
    cbd.ByteWidth              = sizeof(cb);
    cbd.StructureByteStride    = 0u;
    D3D11_SUBRESOURCE_DATA scd = {};
    scd.pSysMem                = &cb;
    hr                         = pDevice->CreateBuffer(&cbd, &scd, &pContantBuffer);
    if (hr != S_OK)
        throw ZZJ_DX_EXCEPTION(hr);
    pContext->VSSetConstantBuffers(0u, 1u, pContantBuffer.GetAddressOf());

    wrl::ComPtr<ID3DBlob> pBlob;
    wrl::ComPtr<ID3D11PixelShader> pPixelShader;
    boost::filesystem::path pixelShader = currentExecutablePath / "PixelShader.cso";
    hr                                  = D3DReadFileToBlob(pixelShader.wstring().c_str(), &pBlob);
    if (hr != S_OK)
        throw ZZJ_DX_EXCEPTION(hr);
    hr = pDevice->CreatePixelShader(pBlob->GetBufferPointer(), pBlob->GetBufferSize(), nullptr, &pPixelShader);
    if (hr != S_OK)
        throw ZZJ_DX_EXCEPTION(hr);
    pContext->PSSetShader(pPixelShader.Get(), nullptr, 0u);

    wrl::ComPtr<ID3D11VertexShader> pVertexShader;
    auto vertexShader = currentExecutablePath / "VertexShader.cso";
    hr                = D3DReadFileToBlob(vertexShader.wstring().c_str(), &pBlob);
    if (hr != S_OK)
        throw ZZJ_DX_EXCEPTION(hr);
    hr = pDevice->CreateVertexShader(pBlob->GetBufferPointer(), pBlob->GetBufferSize(), nullptr, &pVertexShader);
    if (hr != S_OK)
        throw ZZJ_DX_EXCEPTION(hr);
    pContext->VSSetShader(pVertexShader.Get(), nullptr, 0u);

    wrl::ComPtr<ID3D11InputLayout> pInputLayout;
    const D3D11_INPUT_ELEMENT_DESC ied[] = {
        {"Position", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"Color", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 8u, D3D11_INPUT_PER_VERTEX_DATA, 0}};

    hr = pDevice->CreateInputLayout(ied, (UINT)std::size(ied), pBlob->GetBufferPointer(), pBlob->GetBufferSize(),
                                    &pInputLayout);
    if (hr != S_OK)
        throw ZZJ_DX_EXCEPTION(hr);
    pContext->IASetInputLayout(pInputLayout.Get());

    pContext->OMSetRenderTargets(1u, pTarget.GetAddressOf(), nullptr);

    pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY::D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D11_VIEWPORT vp = {};
    vp.Width          = 800;
    vp.Height         = 600;
    vp.MinDepth       = 0;
    vp.MaxDepth       = 1;
    vp.TopLeftX       = 0;
    vp.TopLeftY       = 0;
    pContext->RSSetViewports(1u, &vp);

    zzj::DxgiInfoManager::GetInstance().Begin();
    pContext->Draw(std::size(vertices), 0u);
    zzj::DxgiInfoManager::GetInstance().End();
}
