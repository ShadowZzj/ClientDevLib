#include "InputLayout.h"
#include <General/util/Exception/Exception.h>
using namespace zzj;
InputLayout::InputLayout( Graphics& gfx,
	const std::vector<D3D11_INPUT_ELEMENT_DESC>& layout,
	ID3DBlob* pVertexShaderBytecode )
{

	auto hr = GetDevice( gfx )->CreateInputLayout(
		layout.data(),(UINT)layout.size(),
		pVertexShaderBytecode->GetBufferPointer(),
		pVertexShaderBytecode->GetBufferSize(),
		&pInputLayout
	);
	if( FAILED( hr ) )
	{
		throw ZZJ_DX_EXCEPTION( hr );
	}
}

void InputLayout::Bind( Graphics& gfx ) noexcept
{
	GetContext( gfx )->IASetInputLayout( pInputLayout.Get() );
}
