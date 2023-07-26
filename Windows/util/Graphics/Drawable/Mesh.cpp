#include "Mesh.h"
#include <General/util/File/File.h>
#include <imgui/imgui.h>
#include <unordered_map>
namespace dx = DirectX;

namespace zzj
{
// Mesh
Mesh::Mesh(Graphics &gfx, std::vector<std::unique_ptr<Bindable>> bindPtrs)
{
    if (!IsStaticInitialized())
    {
        AddStaticBind(std::make_unique<Topology>(gfx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST));
    }

    for (auto &pb : bindPtrs)
    {
        if (auto pi = dynamic_cast<IndexBuffer *>(pb.get()))
        {
            AddIndexBuffer(std::unique_ptr<IndexBuffer>{pi});
            pb.release();
        }
        else
        {
            AddBind(std::move(pb));
        }
    }

    AddBind(std::make_unique<TransformCbuf>(gfx, *this));
}
void Mesh::Draw(Graphics &gfx, DirectX::FXMMATRIX accumulatedTransform) const
{
    DirectX::XMStoreFloat4x4(&transform, accumulatedTransform);
    Drawable::Draw(gfx);
}
DirectX::XMMATRIX Mesh::GetTransformXM() const noexcept
{
    return DirectX::XMLoadFloat4x4(&transform);
}

// Node
Node::Node(int id, const std::string &name, std::vector<Mesh *> meshPtrs, const DirectX::XMMATRIX &transform_in) noexcept
    : id(id),
      meshPtrs(std::move(meshPtrs)),
      name(name)
{
    dx::XMStoreFloat4x4(&transform, transform_in);
    dx::XMStoreFloat4x4(&appliedTransform, dx::XMMatrixIdentity());
}

void Node::ShowTree(Node *&pSelectedNode) const noexcept
{
    // if there is no selected node, set selectedId to an impossible value
    const int selectedId = (pSelectedNode == nullptr) ? -1 : pSelectedNode->GetId();
    // build up flags for current node
    const auto node_flags = ImGuiTreeNodeFlags_OpenOnArrow |
                            ((GetId() == selectedId) ? ImGuiTreeNodeFlags_Selected : 0) |
                            ((childPtrs.size() == 0) ? ImGuiTreeNodeFlags_Leaf : 0);
    // render this node
    const auto expanded = ImGui::TreeNodeEx((void *)(intptr_t)GetId(), node_flags, name.c_str());
    // processing for selecting node
    if (ImGui::IsItemClicked())
    {
        pSelectedNode = const_cast<Node *>(this);
    }
    // recursive rendering of open node's children
    if (expanded)
    {
        for (const auto &pChild : childPtrs)
        {
            pChild->ShowTree(pSelectedNode);
        }
        ImGui::TreePop();
    }
}
int Node::GetId() const noexcept
{
    return id;
}

void Node::Draw(Graphics &gfx, DirectX::FXMMATRIX accumulatedTransform) const
{
    const auto built = dx::XMLoadFloat4x4(&appliedTransform) * dx::XMLoadFloat4x4(&transform) * accumulatedTransform;
    for (const auto pm : meshPtrs)
    {
        pm->Draw(gfx, built);
    }
    for (const auto &pc : childPtrs)
    {
        pc->Draw(gfx, built);
    }
}
void Node::AddChild(std::unique_ptr<Node> pChild)
{
    assert(pChild);
    childPtrs.push_back(std::move(pChild));
}
void Node::SetAppliedTransform(DirectX::FXMMATRIX transform) noexcept
{
    dx::XMStoreFloat4x4(&appliedTransform, transform);
}

// Model
class ModelWindow // pImpl idiom, only defined in this .cpp
{
  public:
    void Show(const char *windowName, const Node &root) noexcept
    {
        // window name defaults to "Model"
        windowName = windowName ? windowName : "Model";
        // need an ints to track node indices and selected node
        int nodeIndexTracker = 0;
        if (ImGui::Begin(windowName))
        {
            ImGui::Columns(2, nullptr, true);
            root.ShowTree(pSelectedNode);

            ImGui::NextColumn();
            if (pSelectedNode != nullptr)
            {
                auto &transform = transforms[pSelectedNode->GetId()];
                ImGui::Text("Orientation");
                ImGui::SliderAngle("Roll", &transform.roll, -180.0f, 180.0f);
                ImGui::SliderAngle("Pitch", &transform.pitch, -180.0f, 180.0f);
                ImGui::SliderAngle("Yaw", &transform.yaw, -180.0f, 180.0f);
                ImGui::Text("Position");
                ImGui::SliderFloat("X", &transform.x, -20.0f, 20.0f);
                ImGui::SliderFloat("Y", &transform.y, -20.0f, 20.0f);
                ImGui::SliderFloat("Z", &transform.z, -20.0f, 20.0f);
            }
        }
        ImGui::End();
    }
    dx::XMMATRIX GetTransform() const noexcept
    {
        assert(pSelectedNode != nullptr);
        const auto &transform = transforms.at(pSelectedNode->GetId());
        return dx::XMMatrixRotationRollPitchYaw(transform.roll, transform.pitch, transform.yaw) *
               dx::XMMatrixTranslation(transform.x, transform.y, transform.z);
    }
    Node *GetSelectedNode() const noexcept
    {
        return pSelectedNode;
    }

  private:
    Node *pSelectedNode;
    struct TransformParameters
    {
        float roll  = 0.0f;
        float pitch = 0.0f;
        float yaw   = 0.0f;
        float x     = 0.0f;
        float y     = 0.0f;
        float z     = 0.0f;
    };
    std::unordered_map<int, TransformParameters> transforms;
};
// Model
Model::Model(Graphics &gfx, const std::string fileName) : pWindow(std::make_unique<ModelWindow>())
{
    Assimp::Importer imp;
    const auto pScene = imp.ReadFile(fileName.c_str(), aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                                                           aiProcess_ConvertToLeftHanded | aiProcess_GenNormals);
    for (size_t i = 0; i < pScene->mNumMeshes; i++)
    {
        meshPtrs.push_back(ParseMesh(gfx, *pScene->mMeshes[i]));
    }

    int nextId = 0;
    pRoot      = ParseNode(nextId, *pScene->mRootNode);
}
void Model::Draw(Graphics &gfx) const
{
    if (auto node = pWindow->GetSelectedNode())
    {
        node->SetAppliedTransform(pWindow->GetTransform());
    }
    pRoot->Draw(gfx, dx::XMMatrixIdentity());
}
void Model::ShowWindow(const char *windowName) noexcept
{
    pWindow->Show(windowName, *pRoot);
}
Model::~Model() noexcept
{
}

std::unique_ptr<Mesh> Model::ParseMesh(Graphics &gfx, const aiMesh &mesh)
{

    DeviceVertex::VertexBuffer vbuf(std::move(DeviceVertex::VertexLayout{}
                                                  .Append(DeviceVertex::VertexLayout::Position3D)
                                                  .Append(DeviceVertex::VertexLayout::Normal)));

    for (unsigned int i = 0; i < mesh.mNumVertices; i++)
    {
        vbuf.EmplaceBack(*reinterpret_cast<dx::XMFLOAT3 *>(&mesh.mVertices[i]),
                         *reinterpret_cast<dx::XMFLOAT3 *>(&mesh.mNormals[i]));
    }

    std::vector<unsigned short> indices;
    indices.reserve(mesh.mNumFaces * 3);
    for (unsigned int i = 0; i < mesh.mNumFaces; i++)
    {
        const auto &face = mesh.mFaces[i];
        assert(face.mNumIndices == 3);
        indices.push_back(face.mIndices[0]);
        indices.push_back(face.mIndices[1]);
        indices.push_back(face.mIndices[2]);
    }

    std::vector<std::unique_ptr<Bindable>> bindablePtrs;

    bindablePtrs.push_back(std::make_unique<VertexBuffer>(gfx, vbuf));

    bindablePtrs.push_back(std::make_unique<IndexBuffer>(gfx, indices));

    boost::filesystem::path exePath = zzj::GetExecutablePath();
    auto vsShader                   = exePath / "PhongVS.cso";
    auto psShader                   = exePath / "PhongPS.cso";

    auto pvs   = std::make_unique<VertexShader>(gfx, vsShader.wstring());
    auto pvsbc = pvs->GetBytecode();
    bindablePtrs.push_back(std::move(pvs));

    bindablePtrs.push_back(std::make_unique<PixelShader>(gfx, psShader.wstring()));

    bindablePtrs.push_back(std::make_unique<InputLayout>(gfx, vbuf.GetLayout().GetD3DLayout(), pvsbc));

    struct PSMaterialConstant
    {
        DirectX::XMFLOAT3 color = {0.6f, 0.6f, 0.8f};
        float specularIntensity = 0.6f;
        float specularPower     = 30.0f;
        float padding[3];
    } pmc;
    bindablePtrs.push_back(std::make_unique<PixelConstantBuffer<PSMaterialConstant>>(gfx, pmc, 1u));

    return std::make_unique<Mesh>(gfx, std::move(bindablePtrs));
}
std::unique_ptr<Node> Model::ParseNode(int &nextId, const aiNode &node) noexcept
{
    namespace dx = DirectX;
    const auto transform =
        dx::XMMatrixTranspose(dx::XMLoadFloat4x4(reinterpret_cast<const dx::XMFLOAT4X4 *>(&node.mTransformation)));

    std::vector<Mesh *> curMeshPtrs;
    curMeshPtrs.reserve(node.mNumMeshes);
    for (size_t i = 0; i < node.mNumMeshes; i++)
    {
        const auto meshIdx = node.mMeshes[i];
        curMeshPtrs.push_back(meshPtrs.at(meshIdx).get());
    }

    auto pNode = std::make_unique<Node>(nextId++, node.mName.C_Str(), std::move(curMeshPtrs), transform);
    for (size_t i = 0; i < node.mNumChildren; i++)
    {
        pNode->AddChild(ParseNode(nextId, *node.mChildren[i]));
    }

    return pNode;
}
} // namespace zzj