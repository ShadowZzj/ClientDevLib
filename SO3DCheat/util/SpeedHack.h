#pragma once
#include <Windows.h>

namespace SO3D::SpeedHack
{

void Setup();
void Detach();
void Shutdown();
void SetSpeed(double relSpeed);

double GetSpeed();
bool IsActive();

} // namespace SO3D::SpeedHack
