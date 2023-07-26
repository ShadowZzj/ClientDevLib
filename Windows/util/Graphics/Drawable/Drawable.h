#pragma once
#include "../Graphics.h"
#include <DirectXMath.h>
namespace zzj
{
class Bindable;
class IndexBuffer;
class Drawable
{
    template <class T> friend class DrawableBase;

  public:
    Drawable()                                                = default;
    Drawable(const Drawable &)                                = delete;
    virtual DirectX::XMMATRIX GetTransformXM() const noexcept = 0;
    void Draw(Graphics &gfx) const;
    virtual void Update(float dt) noexcept
    {
    }
    virtual ~Drawable() = default;

  protected:
    void AddBind(std::unique_ptr<Bindable> bind);
    void AddIndexBuffer(std::unique_ptr<class IndexBuffer> ibuf) noexcept;
    template <class T> T *QueryBindable() noexcept
    {
        for (auto &pb : binds)
        {
            if (auto pt = dynamic_cast<T *>(pb.get()))
            {
                return pt;
            }
        }
        return nullptr;
    }

  private:
    virtual const std::vector<std::unique_ptr<Bindable>> &GetStaticBinds() const noexcept = 0;
    const class IndexBuffer *pIndexBuffer                                                 = nullptr;
    std::vector<std::unique_ptr<Bindable>> binds;
};
}; // namespace zzj