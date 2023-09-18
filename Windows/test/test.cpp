#include <Windows.h>
#include <iostream>
#include <vector>
#pragma comment(lib, "ntdll.lib")
extern "C" NTSTATUS NTAPI NtQuerySystemInformation(ULONG SystemInformationClass, PVOID SystemInformation,
                                                   ULONG SystemInformationLength, PULONG ReturnLength);

typedef struct _SYSTEM_MODULE
{
    ULONG Reserved1;
    ULONG Reserved2;
    PVOID ImageBaseAddress;
    ULONG ImageSize;
    ULONG Flags;
    WORD Id;
    WORD Rank;
    WORD w018;
    WORD NameOffset;
    BYTE Name[256];
} SYSTEM_MODULE, *PSYSTEM_MODULE;

typedef struct _SYSTEM_MODULE_INFORMATION
{
    ULONG ModulesCount;
    SYSTEM_MODULE Modules[1];
} SYSTEM_MODULE_INFORMATION, *PSYSTEM_MODULE_INFORMATION;

int main()
{
    std::cout << "asdasdasd";
    std::wcout << L"朱政嘉自行车哦带我去阿伯";
    return 0;
}