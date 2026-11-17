#include "SolidSphere.h"
#include "../Bindable/BindableCommon.h"
#include "../Primitives/Sphere.h"
#include <General/util/File/File.h>
#include <boost/filesystem.hpp>
using namespace zzj;
SolidSphere::SolidSphere(Graphics &gfx, float radius)
{
    namespace dx                           = DirectX;
    boost::filesystem::path currentExePath = zzj::GetExecutablePath();
    if (!IsStaticInitialized())
    {
        struct Vertex
        {
            dx::XMFLOAT3 pos;
        };
        auto model = Sphere::Make<Vertex>();
        model.Transform(dx::XMMatrixScaling(radius, radius, radius));
        AddBind(std::make_unique<VertexBuffer>(gfx, model.vertices));
        AddIndexBuffer(std::make_unique<IndexBuffer>(gfx, model.indices));

        auto pvs   = std::make_unique<VertexShader>(gfx, (currentExePath / "SolidVS.cso").wstring().c_str());
        auto pvsbc = pvs->GetBytecode();
        AddStaticBind(std::move(pvs));

        AddStaticBind(std::make_unique<PixelShader>(gfx, (currentExePath / "SolidPS.cso").wstring().c_str()));

        struct PSColorConstant
        {
            dx::XMFLOAT3 color = {1.0f, 1.0f, 1.0f};
            float padding;
        } colorConst;
        AddStaticBind(std::make_unique<PixelConstantBuffer<PSColorConstant>>(gfx, colorConst));

        const std::vector<D3D11_INPUT_ELEMENT_DESC> ied = {
            {"Position", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
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

void SolidSphere::Update(float dt) noexcept
{
}

void SolidSphere::SetPos(DirectX::XMFLOAT3 pos) noexcept
{
    this->pos = pos;
}

DirectX::XMMATRIX SolidSphere::GetTransformXM() const noexcept
{
    return DirectX::XMMatrixTranslation(pos.x, pos.y, pos.z);
}
