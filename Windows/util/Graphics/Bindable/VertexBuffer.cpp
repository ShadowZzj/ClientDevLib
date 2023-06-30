#include "VertexBuffer.h"
using namespace zzj;
void VertexBuffer::Bind(Graphics &gfx) noexcept
{
    const UINT offset = 0u;
    GetContext(gfx)->IASetVertexBuffers(0u, 1u, pVertexBuffer.GetAddressOf(), &stride, &offset);
}
