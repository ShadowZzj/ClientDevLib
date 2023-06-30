#pragma once
#include "Bindable.h"
#include <vector>
namespace zzj
{
class InputLayout : public Bindable
{
  public:
    InputLayout(Graphics &gfx, const std::vector<D3D11_INPUT_ELEMENT_DESC> &layout, ID3DBlob *pVertexShaderBytecode);
    void Bind(Graphics &gfx) noexcept override;

  protected:
    Microsoft::WRL::ComPtr<ID3D11InputLayout> pInputLayout;
};
}; // namespace zzj