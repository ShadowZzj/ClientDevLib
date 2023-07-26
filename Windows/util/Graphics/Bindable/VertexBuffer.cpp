#include "VertexBuffer.h"
using namespace zzj;
VertexBuffer::VertexBuffer(Graphics &gfx, const DeviceVertex::VertexBuffer &vbuf)
    : stride((UINT)vbuf.GetLayout().Size())
{
    D3D11_BUFFER_DESC bd      = {};
    bd.BindFlags              = D3D11_BIND_VERTEX_BUFFER;
    bd.Usage                  = D3D11_USAGE_DEFAULT;
    bd.CPUAccessFlags         = 0u;
    bd.MiscFlags              = 0u;
    bd.ByteWidth              = UINT(vbuf.SizeBytes());
    bd.StructureByteStride    = stride;
    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem                = vbuf.GetData();
    auto res                  = GetDevice(gfx)->CreateBuffer(&bd, &sd, &pVertexBuffer);
    if (FAILED(res))
        throw ZZJ_DX_EXCEPTION(res);
}

void VertexBuffer::Bind(Graphics &gfx) noexcept
{
    const UINT offset = 0u;
    GetContext(gfx)->IASetVertexBuffers(0u, 1u, pVertexBuffer.GetAddressOf(), &stride, &offset);
}
