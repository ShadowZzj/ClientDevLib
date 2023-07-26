#include <General/util/StrUtil.h>
#include "util/App.h"
#include <json.hpp>
#include <spdlog/fmt/fmt.h>


int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
    try
    {
        return zzj::App(1280, 720, "hello").Go();
    }
    catch (const zzj::Exception &e)
    {
        nlohmann::json j = nlohmann::json::parse(e.what());
        std::string exceptionString;
        for (auto &i : j.items())
        {
            std::string valueStr;
            if (i.value().is_string())
            {
                valueStr = i.value().get<std::string>(); // get the string directly
            }
            else
            {
                valueStr = i.value().dump(); // for non-string types, dump as before
            }
            exceptionString += fmt::format("[{}]: {}\n", i.key(), valueStr);
        }
        auto wstr = zzj::str::utf82w(exceptionString);
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