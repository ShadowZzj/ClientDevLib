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
    : TestObject(gfx, rng, adist, ddist, odist, rdist)
{
    namespace dx                           = DirectX;
    boost::filesystem::path currentExePath = zzj::GetExecutablePath();
    if (!IsStaticInitialized())
    {
        struct Vertex
        {
            dx::XMFLOAT3 pos;
            dx::XMFLOAT3 n;
            dx::XMFLOAT2 tc;
        };
        auto model = Cube::MakeIndependentTextured<Vertex>();
        model.SetNormalsIndependentFlat();

        AddStaticBind(std::make_unique<VertexBuffer>(gfx, model.vertices));

        AddStaticBind(std::make_unique<Texture>(gfx, Surface::FromFile((currentExePath / "kappa50.png").string())));
        AddStaticBind(std::make_unique<Sampler>(gfx));

        auto pvs   = std::make_unique<VertexShader>(gfx, (currentExePath / "TexturedPhongVS.cso").wstring().c_str());
        auto pvsbc = pvs->GetBytecode();
        AddStaticBind(std::move(pvs));

        AddStaticBind(std::make_unique<PixelShader>(gfx, (currentExePath / "TexturedPhongPS.cso").wstring().c_str()));

        AddStaticIndexBuffer(std::make_unique<IndexBuffer>(gfx, model.indices));

        const std::vector<D3D11_INPUT_ELEMENT_DESC> ied = {
            {"Position", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"Normal", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TexCoord", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        AddStaticBind(std::make_unique<InputLayout>(gfx, ied, pvsbc));

        AddStaticBind(std::make_unique<Topology>(gfx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST));
        struct PSMaterialConstant
        {
            float specularIntensity = 0.6f;
            float specularPower     = 30.0f;
            float padding[2];
        } colorConst;
        AddStaticBind(std::make_unique<PixelConstantBuffer<PSMaterialConstant>>(gfx, colorConst, 1u));
    }
    else
    {
        SetIndexFromStatic();
    }

    AddBind(std::make_unique<TransformCbuf>(gfx, *this));
}
