#include "Texture.h"
#include "../Surface.h"
#include <General/util/Exception/Exception.h>
namespace wrl = Microsoft::WRL;
using namespace zzj;
Texture::Texture(Graphics &gfx, const Surface &s, unsigned int slot) : slot(slot)
{
    // create texture resource
    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width                = s.GetWidth();
    textureDesc.Height               = s.GetHeight();
    textureDesc.MipLevels            = 1;
    textureDesc.ArraySize            = 1;
    textureDesc.Format               = DXGI_FORMAT_B8G8R8A8_UNORM;
    textureDesc.SampleDesc.Count     = 1;
    textureDesc.SampleDesc.Quality   = 0;
    textureDesc.Usage                = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags            = D3D11_BIND_SHADER_RESOURCE;
    textureDesc.CPUAccessFlags       = 0;
    textureDesc.MiscFlags            = 0;

    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem                = s.GetBufferPtr();
    sd.SysMemPitch            = s.GetWidth() * sizeof(Surface::Color);

    wrl::ComPtr<ID3D11Texture2D> pTexture;
    auto hr = GetDevice(gfx)->CreateTexture2D(&textureDesc, &sd, &pTexture);
    if (FAILED(hr))
        throw ZZJ_DX_EXCEPTION(hr);

    // create the resource view on the texture
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                          = textureDesc.Format;
    srvDesc.ViewDimension                   = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip       = 0;
    srvDesc.Texture2D.MipLevels             = 1;
    hr = GetDevice(gfx)->CreateShaderResourceView(pTexture.Get(), &srvDesc, &pTextureView);
    if (FAILED(hr))
        throw ZZJ_DX_EXCEPTION(hr);
}

void Texture::Bind(Graphics &gfx) noexcept
{
    GetContext(gfx)->PSSetShaderResources(slot, 1u, pTextureView.GetAddressOf());
}
