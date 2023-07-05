#pragma once
#include "TestObject.h"
#include <random>
namespace zzj
{
class Cylinder : public TestObject<Cylinder>
{
  public:
    Cylinder(Graphics &gfx, std::mt19937 &rng, std::uniform_real_distribution<float> &adist,
             std::uniform_real_distribution<float> &ddist, std::uniform_real_distribution<float> &odist,
             std::uniform_real_distribution<float> &rdist, std::uniform_real_distribution<float> &bdist,
             std::uniform_int_distribution<int> &tdist);
};
}; // namespace zzj