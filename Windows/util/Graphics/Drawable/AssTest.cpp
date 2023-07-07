#include "AssTest.h"
#include "../Bindable/BindableBase.h"
#include "../Vertex.h"
#include <General/util/File/File.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <windows/util/Graphics/GDIPlusManager.h>

using namespace zzj;
AssTest::AssTest(Graphics &gfx, std::mt19937 &rng, std::uniform_real_distribution<float> &adist,
                 std::uniform_real_distribution<float> &ddist, std::uniform_real_distribution<float> &odist,
                 std::uniform_real_distribution<float> &rdist, DirectX::XMFLOAT3 material, float scale)
    : TestObject(gfx, rng, adist, ddist, odist, rdist)
{
    namespace dx = DirectX;

    if (!IsStaticInitialized())
    {
        VertexBufferImp vbuf(std::move(VertexLayout{}.Append(VertexLayout::Position3D).Append(VertexLayout::Normal)));
        boost::filesystem::path executablePath = zzj::GetExecutablePath();
        auto filePath                          = executablePath / "suzanne.obj";

        Assimp::Importer imp;
        const auto pModel =
            imp.ReadFile(filePath.string().c_str(), aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);
        const auto pMesh = pModel->mMeshes[0];

        for (unsigned int i = 0; i < pMesh->mNumVertices; i++)
        {
            vbuf.EmplaceBack(dx::XMFLOAT3{pMesh->mVertices[i].x * scale, pMesh->mVertices[i].y * scale,
                                          pMesh->mVertices[i].z * scale},
                             *reinterpret_cast<dx::XMFLOAT3 *>(&pMesh->mNormals[i]));
        }

        std::vector<unsigned short> indices;
        indices.reserve(pMesh->mNumFaces * 3);
        for (unsigned int i = 0; i < pMesh->mNumFaces; i++)
        {
            const auto &face = pMesh->mFaces[i];
            assert(face.mNumIndices == 3);
            indices.push_back(face.mIndices[0]);
            indices.push_back(face.mIndices[1]);
            indices.push_back(face.mIndices[2]);
        }

        AddStaticBind(std::make_unique<VertexBuffer>(gfx, vbuf));

        AddStaticIndexBuffer(std::make_unique<IndexBuffer>(gfx, indices));

        auto pvs   = std::make_unique<VertexShader>(gfx, (executablePath / "PhongVS.cso").wstring().c_str());
        auto pvsbc = pvs->GetBytecode();
        AddStaticBind(std::move(pvs));

        AddStaticBind(std::make_unique<PixelShader>(gfx, (executablePath / "PhongPS.cso").wstring().c_str()));

        AddStaticBind(std::make_unique<InputLayout>(gfx, vbuf.GetLayout().GetD3DLayout(), pvsbc));

        AddStaticBind(std::make_unique<Topology>(gfx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST));

        struct PSMaterialConstant
        {
            DirectX::XMFLOAT3 color;
            float specularIntensity = 0.6f;
            float specularPower     = 30.0f;
            float padding[3];
        } pmc;
        pmc.color = material;
        AddStaticBind(std::make_unique<PixelConstantBuffer<PSMaterialConstant>>(gfx, pmc, 1u));
    }
    else
    {
        SetIndexFromStatic();
    }

    AddBind(std::make_unique<TransformCbuf>(gfx, *this));
}
