#include <Windows/util/Device/Interception/DeviceHelper.h>
#include <spdlog/spdlog.h>
#include <windows.h>

using zzj::InputInterceptor;
using zzj::KeyScanCode;
int main()
{
    InterceptionContext context = interception_create_context();
    while (true)
    {
        Sleep(5000);

        InterceptionKeyStroke keyStroke[2];

        ZeroMemory(keyStroke, sizeof(keyStroke));
        /**/
        keyStroke[0].code = MapVirtualKey('T', MAPVK_VK_TO_VSC);

        keyStroke[0].state = INTERCEPTION_KEY_DOWN;

        keyStroke[1].code = keyStroke[0].code;

        keyStroke[1].state = INTERCEPTION_KEY_UP;

        interception_send(context, INTERCEPTION_KEYBOARD(0), (InterceptionStroke *)keyStroke, _countof(keyStroke));
    }
}