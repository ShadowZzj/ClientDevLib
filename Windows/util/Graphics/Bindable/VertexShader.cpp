#include "VertexShader.h"
#include <General/util/Exception/Exception.h>

using namespace zzj;
VertexShader::VertexShader(Graphics &gfx, const std::wstring &path)
{

    auto hr = D3DReadFileToBlob(path.c_str(), &pBytecodeBlob);
	if (FAILED(hr))
	{
		throw ZZJ_DX_EXCEPTION(hr);
	}
    hr = GetDevice(gfx)->CreateVertexShader(pBytecodeBlob->GetBufferPointer(), pBytecodeBlob->GetBufferSize(), nullptr,
                                            &pVertexShader);
	if (FAILED(hr))
	{
		throw ZZJ_DX_EXCEPTION(hr);
	}
}

void VertexShader::Bind(Graphics &gfx) noexcept
{
    GetContext(gfx)->VSSetShader(pVertexShader.Get(), nullptr, 0u);
}

ID3DBlob *VertexShader::GetBytecode() const noexcept
{
    return pBytecodeBlob.Get();
}
