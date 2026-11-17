#pragma once
#include "../Vertex.h"
#include "Bindable.h"
#include <General/util/Exception/Exception.h>

namespace zzj
{
class VertexBuffer : public Bindable
{
  public:
    template <class V> VertexBuffer(Graphics &gfx, const std::vector<V> &vertices) : stride(sizeof(V))
    {

        D3D11_BUFFER_DESC bd      = {};
        bd.BindFlags              = D3D11_BIND_VERTEX_BUFFER;
        bd.Usage                  = D3D11_USAGE_DEFAULT;
        bd.CPUAccessFlags         = 0u;
        bd.MiscFlags              = 0u;
        bd.ByteWidth              = UINT(sizeof(V) * vertices.size());
        bd.StructureByteStride    = sizeof(V);
        D3D11_SUBRESOURCE_DATA sd = {};
        sd.pSysMem                = vertices.data();
        auto hr                   = GetDevice(gfx)->CreateBuffer(&bd, &sd, &pVertexBuffer);
        if (FAILED(hr))
        {
            throw ZZJ_DX_EXCEPTION(hr);
        }
    }
    VertexBuffer(Graphics &gfx, const DeviceVertex::VertexBuffer &vbuf);
    void Bind(Graphics &gfx) noexcept override;

  protected:
    UINT stride;
    Microsoft::WRL::ComPtr<ID3D11Buffer> pVertexBuffer;
};
}; // namespace zzj
