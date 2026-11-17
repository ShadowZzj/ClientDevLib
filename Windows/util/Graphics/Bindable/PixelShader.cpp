#include "PixelShader.h"
#include <General/util/Exception/Exception.h>
using namespace zzj;
PixelShader::PixelShader(Graphics &gfx, const std::wstring &path)
{

    Microsoft::WRL::ComPtr<ID3DBlob> pBlob;
    auto hr = D3DReadFileToBlob(path.c_str(), &pBlob);
    if (FAILED(hr))
    {
        throw ZZJ_DX_EXCEPTION(hr);
    }
    hr = GetDevice(gfx)->CreatePixelShader(pBlob->GetBufferPointer(), pBlob->GetBufferSize(), nullptr, &pPixelShader);
    if (FAILED(hr))
    {
        throw ZZJ_DX_EXCEPTION(hr);
    }
}

void PixelShader::Bind(Graphics &gfx) noexcept
{
    GetContext(gfx)->PSSetShader(pPixelShader.Get(), nullptr, 0u);
}
