#pragma once
#include "../Bindable/BindableCommon.h"
#include "../Vertex.h"
#include "DrawableBase.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <optional>
namespace zzj
{
class ModuleWindow;
class Mesh : public DrawableBase<Mesh>
{
  public:
    Mesh(Graphics &gfx, std::vector<std::unique_ptr<Bindable>> bindPtrs);
    void Draw(Graphics &gfx, DirectX::FXMMATRIX accumulatedTransform) const;
    DirectX::XMMATRIX GetTransformXM() const noexcept override;

  private:
    mutable DirectX::XMFLOAT4X4 transform;
};

class Node
{
    friend class Model;

  public:
    Node(int id, const std::string &name, std::vector<Mesh *> meshPtrs, const DirectX::XMMATRIX &transform) noexcept;
    void Draw(Graphics &gfx, DirectX::FXMMATRIX accumulatedTransform) const;
    void SetAppliedTransform(DirectX::FXMMATRIX transform) noexcept;
    int GetId() const noexcept;
    void ShowTree(Node *&pSelectedNode) const noexcept;

  private:
    void AddChild(std::unique_ptr<Node> pChild);

  private:
    int id;
    std::string name;
    std::vector<std::unique_ptr<Node>> childPtrs;
    std::vector<Mesh *> meshPtrs;
    DirectX::XMFLOAT4X4 transform;
    DirectX::XMFLOAT4X4 appliedTransform;
};

class Model
{
  public:
    Model(Graphics &gfx, const std::string fileName);
    void Draw(Graphics &gfx) const;
    void ShowWindow(const char *windowName = nullptr) noexcept;
    ~Model() noexcept;

  private:
    static std::unique_ptr<Mesh> ParseMesh(Graphics &gfx, const aiMesh &mesh);
    std::unique_ptr<Node> ParseNode(int &nextId, const aiNode &node) noexcept;

  private:
    std::unique_ptr<Node> pRoot;
    std::vector<std::unique_ptr<Mesh>> meshPtrs;
    std::unique_ptr<class ModelWindow> pWindow;
};
}; // namespace zzj