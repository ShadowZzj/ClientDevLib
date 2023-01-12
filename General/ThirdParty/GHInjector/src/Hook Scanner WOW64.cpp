#include "pch.h"

#ifdef _WIN64

#include "Hook Scanner.h"

bool ScanForHook_WOW64(HookInfo & Info, HANDLE hTargetProc, HANDLE hRefProcess)
{
	DWORD Address = 0;
	if(!GetProcAddressEx_WOW64(hRefProcess, Info.hModuleBase, Info.FunctionName, Address))
	{
		Info.ErrorCode = HOOK_SCAN_ERR_GETPROCADDRESS_FAILED;

		LOG(1, "GetProcAddressEx_WOW64 failed on %s\n", Info.FunctionName);

		return false;
	}

	Info.pFunc = MPTR(Address);

	if (!ReadProcessMemory(hRefProcess, Info.pFunc, Info.OriginalBytes, sizeof(Info.OriginalBytes), nullptr))
	{
		Info.ErrorCode = HOOK_SCAN_ERR_READ_PROCESS_MEMORY_FAILED;

		LOG(1, "ReadProcessMemory (reference process) failed on %s with error %08X\n", Info.FunctionName, GetLastError());

		return false;
	}

	BYTE Buffer[HOOK_SCAN_BYTE_COUNT]{ 0 };
	if (!ReadProcessMemory(hTargetProc, Info.pFunc, Buffer, sizeof(Buffer), nullptr))
	{
		Info.ErrorCode = HOOK_SCAN_ERR_READ_PROCESS_MEMORY_FAILED;

		LOG(1, "ReadProcessMemory (target process) failed on %s with error %08X\n", Info.FunctionName, GetLastError());

		return false;
	}
	
	for (int i = 0; i != HOOK_SCAN_BYTE_COUNT; ++i)
	{
		if (Info.OriginalBytes[i] != Buffer[i])
		{
			++Info.ChangeCount;
		}
	}

	return (Info.ChangeCount != 0);
}

#endif