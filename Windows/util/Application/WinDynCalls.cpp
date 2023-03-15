#include <stdio.h>
#include "WinDynCalls.h"
KERNEL32_API_LIST(API_DLR)

#pragma comment(lib, "Pathcch.lib")
//Only load dll located in SystemDirectory e.g C:\Windows\System32
HMODULE SafeLoadLibrary(const WCHAR* dllName) {
	WCHAR dllAbsPath[MAX_PATH]{};
    UINT res = GetSystemDirectoryW(dllAbsPath, MAX_PATH);
    if (!res || res >= MAX_PATH)
		return nullptr;
    res = PathCchAppend(dllAbsPath, MAX_PATH, dllName);
	if (res != S_OK)
		return nullptr;
	return LoadLibraryW(dllAbsPath);
}
void InitDynCalls() {
	HMODULE h = SafeLoadLibrary(L"kernel32.dll");
	if (h == NULL)
		zzj::CrashMe();
	KERNEL32_API_LIST(API_LOAD);

}
