#pragma once
#include "Graphics.h"

namespace zzj
{
class Camera
{
  public:
    Camera() noexcept;
    DirectX::XMMATRIX GetMatrix() const noexcept;
    void SpawnControlWindow() noexcept;
    void Reset() noexcept;
    void Rotate(float dx, float dy) noexcept;
    void Translate(DirectX::XMFLOAT3 translation) noexcept;

  private:
    DirectX::XMFLOAT3 pos;
    float pitch;
    float yaw;
    static constexpr float travelSpeed   = 12.0f;
    static constexpr float rotationSpeed = 0.004f;
};
}; // namespace zzj