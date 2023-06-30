#include "SkinnedBox.h"
#include "../Bindable/BindableBase.h"
#include "../Bindable/Sampler.h"
#include "../Bindable/Texture.h"
#include "../Primitives/Cube.h"
#include "../Surface.h"
#include <General/util/Exception/Exception.h>
#include <General/util/File/File.h>
#include <boost/filesystem.hpp>

using namespace zzj;
SkinnedBox::SkinnedBox(Graphics &gfx, std::mt19937 &rng, std::uniform_real_distribution<float> &adist,
                       std::uniform_real_distribution<float> &ddist, std::uniform_real_distribution<float> &odist,
                       std::uniform_real_distribution<float> &rdist)
    : r(rdist(rng)), droll(ddist(rng)), dpitch(ddist(rng)), dyaw(ddist(rng)), dphi(odist(rng)), dtheta(odist(rng)),
      dchi(odist(rng)), chi(adist(rng)), theta(adist(rng)), phi(adist(rng))
{
    namespace dx                           = DirectX;
    boost::filesystem::path currentExePath = zzj::GetExecutablePath();
    if (!IsStaticInitialized())
    {
        struct Vertex
        {
            dx::XMFLOAT3 pos;
            struct
            {
                float u;
                float v;
            } tex;
        };
        const auto model = Cube::MakeSkinned<Vertex>();

        AddStaticBind(std::make_unique<VertexBuffer>(gfx, model.vertices));

        AddStaticBind(std::make_unique<Texture>(gfx, Surface::FromFile((currentExePath / "cube.png").string())));
        AddStaticBind(std::make_unique<Sampler>(gfx));
		
        auto pvs   = std::make_unique<VertexShader>(gfx, (currentExePath / "TextureVS.cso").wstring().c_str());
        auto pvsbc = pvs->GetBytecode();
        AddStaticBind(std::move(pvs));

        AddStaticBind(std::make_unique<PixelShader>(gfx, (currentExePath / "TexturePS.cso").wstring().c_str()));

        AddStaticIndexBuffer(std::make_unique<IndexBuffer>(gfx, model.indices));

        const std::vector<D3D11_INPUT_ELEMENT_DESC> ied = {
            {"Position", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TexCoord", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        AddStaticBind(std::make_unique<InputLayout>(gfx, ied, pvsbc));

        AddStaticBind(std::make_unique<Topology>(gfx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST));
    }
    else
    {
        SetIndexFromStatic();
    }

    AddBind(std::make_unique<TransformCbuf>(gfx, *this));
}

void SkinnedBox::Update(float dt) noexcept
{
    roll += droll * dt;
    pitch += dpitch * dt;
    yaw += dyaw * dt;
    theta += dtheta * dt;
    phi += dphi * dt;
    chi += dchi * dt;
}

DirectX::XMMATRIX SkinnedBox::GetTransformXM() const noexcept
{
    namespace dx = DirectX;
    return dx::XMMatrixRotationRollPitchYaw(pitch, yaw, roll) * dx::XMMatrixTranslation(r, 0.0f, 0.0f) *
           dx::XMMatrixRotationRollPitchYaw(theta, phi, chi) * dx::XMMatrixTranslation(0.0f, 0.0f, 20.0f);
}
