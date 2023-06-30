#pragma once
#include "Bindable.h"
#include <General/util/Exception/Exception.h>
namespace zzj
{
template <typename C> class ConstantBuffer : public Bindable
{
  public:
    void Update(Graphics &gfx, const C &consts)
    {

        D3D11_MAPPED_SUBRESOURCE msr;
        auto hr = GetContext(gfx)->Map(pConstantBuffer.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &msr);
        if (FAILED(hr))
        {
            ZZJ_DX_EXCEPTION(hr);
        }
        memcpy(msr.pData, &consts, sizeof(consts));
        GetContext(gfx)->Unmap(pConstantBuffer.Get(), 0u);
    }
    ConstantBuffer(Graphics &gfx, const C &consts)
    {

        D3D11_BUFFER_DESC cbd;
        cbd.BindFlags           = D3D11_BIND_CONSTANT_BUFFER;
        cbd.Usage               = D3D11_USAGE_DYNAMIC;
        cbd.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
        cbd.MiscFlags           = 0u;
        cbd.ByteWidth           = sizeof(consts);
        cbd.StructureByteStride = 0u;

        D3D11_SUBRESOURCE_DATA csd = {};
        csd.pSysMem                = &consts;
        auto hr = GetDevice(gfx)->CreateBuffer(&cbd, &csd, &pConstantBuffer);
        if (FAILED(hr))
        {
            ZZJ_DX_EXCEPTION(hr);
        }
    }
    ConstantBuffer(Graphics &gfx)
    {
        D3D11_BUFFER_DESC cbd;
        cbd.BindFlags           = D3D11_BIND_CONSTANT_BUFFER;
        cbd.Usage               = D3D11_USAGE_DYNAMIC;
        cbd.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
        cbd.MiscFlags           = 0u;
        cbd.ByteWidth           = sizeof(C);
        cbd.StructureByteStride = 0u;
        auto hr = GetDevice(gfx)->CreateBuffer(&cbd, nullptr, &pConstantBuffer);
        if (FAILED(hr))
        {
            ZZJ_DX_EXCEPTION(hr);
        }
    }

  protected:
    Microsoft::WRL::ComPtr<ID3D11Buffer> pConstantBuffer;
};

template <typename C> class VertexConstantBuffer : public ConstantBuffer<C>
{
    using ConstantBuffer<C>::pConstantBuffer;
    using Bindable::GetContext;

  public:
    using ConstantBuffer<C>::ConstantBuffer;
    void Bind(Graphics &gfx) noexcept override
    {
        GetContext(gfx)->VSSetConstantBuffers(0u, 1u, pConstantBuffer.GetAddressOf());
    }
};

template <typename C> class PixelConstantBuffer : public ConstantBuffer<C>
{
    using ConstantBuffer<C>::pConstantBuffer;
    using Bindable::GetContext;

  public:
    using ConstantBuffer<C>::ConstantBuffer;
    void Bind(Graphics &gfx) noexcept override
    {
        GetContext(gfx)->PSSetConstantBuffers(0u, 1u, pConstantBuffer.GetAddressOf());
    }
};
} // namespace zzj