#include "Bindable.h"
using namespace zzj;
ID3D11DeviceContext* Bindable::GetContext( Graphics& gfx ) noexcept
{
	return gfx.pContext.Get();
}

ID3D11Device* Bindable::GetDevice( Graphics& gfx ) noexcept
{
	return gfx.pDevice.Get();
}
