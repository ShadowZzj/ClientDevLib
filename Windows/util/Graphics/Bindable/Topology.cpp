#include "Topology.h"
using namespace zzj;
Topology::Topology(Graphics &gfx, D3D11_PRIMITIVE_TOPOLOGY type) : type(type)
{
}

void Topology::Bind(Graphics &gfx) noexcept
{
    GetContext(gfx)->IASetPrimitiveTopology(type);
}
