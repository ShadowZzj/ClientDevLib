#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <string>

static void ListExports(const wchar_t* dllPath)
{
    HMODULE hMod = LoadLibraryW(dllPath);
    if (!hMod)
    {
        wprintf(L"Failed to load %s\n", dllPath);
        return;
    }

    wprintf(L"DLL loaded at: 0x%p\n\n", hMod);

    // 读取 PE 头
    BYTE* base = (BYTE*)hMod;
    IMAGE_DOS_HEADER* dosHdr = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* ntHdr = (IMAGE_NT_HEADERS*)(base + dosHdr->e_lfanew);
    IMAGE_EXPORT_DIRECTORY* expDir = nullptr;

    DWORD expRva = ntHdr->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    if (expRva)
        expDir = (IMAGE_EXPORT_DIRECTORY*)(base + expRva);

    if (!expDir)
    {
        wprintf(L"No export directory found.\n");
        FreeLibrary(hMod);
        return;
    }

    DWORD* names = (DWORD*)(base + expDir->AddressOfNames);
    DWORD* funcs = (DWORD*)(base + expDir->AddressOfFunctions);
    WORD* ords = (WORD*)(base + expDir->AddressOfNameOrdinals);

    wprintf(L"Total exports: %lu\n\n", expDir->NumberOfNames);
    wprintf(L"Filtering for input-related functions...\n\n");

    // 只打印输入相关的
    for (DWORD i = 0; i < expDir->NumberOfNames; i++)
    {
        const char* name = (const char*)(base + names[i]);
        std::string nameStr(name);
        
        // 过滤：只显示输入相关的函数
        if (nameStr.find("Event") != std::string::npos ||
            nameStr.find("Keyboard") != std::string::npos ||
            nameStr.find("Mouse") != std::string::npos ||
            nameStr.find("Input") != std::string::npos ||
            nameStr.find("Poll") != std::string::npos ||
            nameStr.find("Wait") != std::string::npos ||
            nameStr.find("Peep") != std::string::npos ||
            nameStr.find("Key") != std::string::npos ||
            nameStr.find("Button") != std::string::npos)
        {
            void* addr = (void*)(base + funcs[ords[i]]);
            printf("  %-50s  0x%p\n", name, addr);
        }
    }

    wprintf(L"\nPress Enter to exit...\n");
    getchar();
    
    FreeLibrary(hMod);
}

int wmain(int argc, wchar_t* argv[])
{
    if (argc < 2)
    {
        wprintf(L"Usage: list_sdl_exports.exe <path_to_SDL3.dll>\n");
        wprintf(L"Example: list_sdl_exports.exe \"C:\\Games\\Dota2\\game\\bin\\win64\\SDL3.dll\"\n");
        return 1;
    }

    ListExports(argv[1]);
    return 0;
}

