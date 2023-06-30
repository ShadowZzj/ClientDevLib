#pragma once
#include "ConstantBuffers.h"
#include "../Drawable/Drawable.h"
#include <DirectXMath.h>

namespace zzj
{
class TransformCbuf : public Bindable
{
  public:
    TransformCbuf(Graphics &gfx, const Drawable &parent);
    void Bind(Graphics &gfx) noexcept override;

  private:
    static std::unique_ptr<VertexConstantBuffer<DirectX::XMMATRIX>> pVcbuf;
    const Drawable &parent;
};
}; // namespace zzj