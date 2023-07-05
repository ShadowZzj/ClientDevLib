#include "Pyramid.h"
#include "../Bindable/BindableBase.h"
#include "../Primitives/Cone.h"
#include <General/util/File/File.h>
#include <array>
#include <boost/filesystem.hpp>

using namespace zzj;
Pyramid::Pyramid(Graphics &gfx, std::mt19937 &rng, std::uniform_real_distribution<float> &adist,
                 std::uniform_real_distribution<float> &ddist, std::uniform_real_distribution<float> &odist,
                 std::uniform_real_distribution<float> &rdist, std::uniform_int_distribution<int> &tdist)
    : TestObject(gfx, rng, adist, ddist, odist, rdist)
{
    namespace dx                           = DirectX;
    boost::filesystem::path currentExePath = zzj::GetExecutablePath();
    if (!IsStaticInitialized())
    {

        auto pvs   = std::make_unique<VertexShader>(gfx, (currentExePath / "BlendedPhongVS.cso").wstring().c_str());
        auto pvsbc = pvs->GetBytecode();
        AddStaticBind(std::move(pvs));

        AddStaticBind(std::make_unique<PixelShader>(gfx, (currentExePath / "BlendedPhongPS.cso").wstring().c_str()));

        const std::vector<D3D11_INPUT_ELEMENT_DESC> ied = {
            {"Position", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"Normal", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"Color", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
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

    AddBind(std::make_unique<TransformCbuf>(gfx, *this));
    struct Vertex
    {
        dx::XMFLOAT3 pos;
        dx::XMFLOAT3 n;
        std::array<char, 4> color;
        char padding;
    };
    const auto tesselation = tdist(rng);
    auto model             = Cone::MakeTesselatedIndependentFaces<Vertex>(tesselation);
    // set vertex colors for mesh (tip red blending to blue base)
    for (auto &v : model.vertices)
    {
        v.color = {(char)10, (char)10, (char)255};
    }
    for (int i = 0; i < tesselation; i++)
    {
        model.vertices[i * 3].color = {(char)255, (char)10, (char)10};
    }
    // squash mesh a bit in the z direction
    model.Transform(dx::XMMatrixScaling(1.0f, 1.0f, 0.7f));
    // add normals
    model.SetNormalsIndependentFlat();

    AddBind(std::make_unique<VertexBuffer>(gfx, model.vertices));
    AddIndexBuffer(std::make_unique<IndexBuffer>(gfx, model.indices));

    AddBind(std::make_unique<TransformCbuf>(gfx, *this));
}