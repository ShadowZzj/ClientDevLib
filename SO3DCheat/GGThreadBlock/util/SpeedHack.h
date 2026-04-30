#pragma once
#include <Windows.h>

namespace GGTB::SpeedHack
{

void Setup();
void Detach();
void Shutdown();
void SetSpeed(double relSpeed);

double GetSpeed();
bool   IsActive();

} // namespace GGTB::SpeedHack
