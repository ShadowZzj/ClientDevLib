#pragma once
#include <Windows.h>
#include <iostream>
namespace zzj
{
    class Console
    {
        public:
        Console()
        {
            AllocConsole();
            freopen_s(&f, "CONOUT$", "w+t", stdout);
            system("chcp 65001");
        }
        ~Console()
        {
            fclose(f);
            FreeConsole();
        }
        
        private:
        FILE *f;
    };
};