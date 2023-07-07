#pragma once
#include "../Bindable/ConstantBuffers.h"
#include "TestObject.h"

namespace zzj
{
class AssTest : public TestObject<AssTest>
{
  public:
    AssTest(Graphics &gfx, std::mt19937 &rng, std::uniform_real_distribution<float> &adist,
            std::uniform_real_distribution<float> &ddist, std::uniform_real_distribution<float> &odist,
            std::uniform_real_distribution<float> &rdist, DirectX::XMFLOAT3 material, float scale);
};
}; // namespace zzj