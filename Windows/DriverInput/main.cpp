#include <windows.h>
#include <Windows/util/Device/Interception/DeviceHelper.h>
#include <spdlog/spdlog.h>
using zzj::InputInterceptor;
using zzj::KeyScanCode;
int main()
{
    auto interceptor = InputInterceptor::CreateInstance();
    auto res = interceptor->Load();
    if (!res)
    {
        spdlog::info("Initialize fail!");
        return -1;
    }
    while (true)
    {
        interceptor->SendKey(KeyScanCode::T);
        Sleep(5000);
    }

    return 0;
}