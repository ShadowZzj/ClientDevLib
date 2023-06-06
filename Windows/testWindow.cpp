#include <Windows/util/Application/App.h>
#include <General/util/StrUtil.h>
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
    try
    {
        return zzj::App(800, 600, "hello").Go();
    }
    catch (const zzj::Exception &e)
    {
        auto wstr = zzj::str::utf82w(e.what());
        MessageBoxW(nullptr, wstr.c_str(), NULL, MB_OK | MB_ICONEXCLAMATION);
    }
    catch (const std::exception &e)
    {
        MessageBoxA(nullptr, e.what(), "Standard Exception", MB_OK | MB_ICONEXCLAMATION);
    }
    catch (...)
    {
        MessageBoxA(nullptr, "No details available", "Unknown Exception", MB_OK | MB_ICONEXCLAMATION);
    }
    return -1;
}