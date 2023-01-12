#include "InputDevice.h"
#include <Windows.h>
bool zzj::Keyboard::IsKeyDown(int key)
{
    if (GetAsyncKeyState(key) & 0x8000)
        return true;
    return false;
}

bool zzj::Keyboard::IsKeyUp(int key)
{
    if (GetAsyncKeyState(key) & 0x1)
        return true;
    return false;
}
