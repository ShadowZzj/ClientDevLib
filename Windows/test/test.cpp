#include <iostream>
#include <Windows/util/Process/ProcessHelper.h>
#include <General/util/Process/Process.h>
#include <spdlog/spdlog.h>
#include <General/ThirdParty/GHInjector/Injection.h>
#include <filesystem>
#include <Detours/build/include/detours.h>

int main()
{
    while (true)
    {
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;

        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));

        auto id = GetProcessId(GetCurrentProcess());
        CreateProcessA("ipconfig /all", NULL, NULL, NULL, false, NULL, NULL, NULL, &si, &pi);
        printf("nihao %d", id);
        Sleep(1000);
    }
}