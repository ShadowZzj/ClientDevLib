#include "pch.h"

#ifdef _WIN64

#include "Start Routine.h"

DWORD SR_KernelCallback_WOW64(HANDLE hTargetProc, f_Routine_WOW64 pRoutine, DWORD pArg, ULONG TargetSessionId, DWORD & Out, DWORD Timeout, ERROR_DATA & error_data)
{
	LOG(2, "Begin SR_KernelCallback_WOW64\n");

	std::wstring InfoPath = g_RootPathW;
	InfoPath += SM_INFO_FILENAME86;

	if (FileExists(InfoPath.c_str()))
	{
		DeleteFileW(InfoPath.c_str());
	}

	std::wofstream kc_info(InfoPath, std::ios_base::out | std::ios_base::app);
	if (!kc_info.good())
	{
		INIT_ERROR_DATA(error_data, INJ_ERR_ADVANCED_NOT_DEFINED);

		LOG(2, "Failed to create info file\n");

		return SR_KC_ERR_CANT_OPEN_INFO_TXT;
	}

	ProcessInfo PI;
	if (!PI.SetProcess(hTargetProc))
	{
		INIT_ERROR_DATA(error_data, INJ_ERR_ADVANCED_NOT_DEFINED);

		LOG(2, "Can't initialize ProcessInfo class\n");

		kc_info.close();
		DeleteFileW(InfoPath.c_str());

		return SR_KC_ERR_PROC_INFO_FAIL;
	}

	auto pPEB = PI.GetPEB_WOW64();

	if (!pPEB)
	{
		INIT_ERROR_DATA(error_data, INJ_ERR_ADVANCED_NOT_DEFINED);

		LOG(2, "Failed to get PEB pointer\n");

		kc_info.close();
		DeleteFileW(InfoPath.c_str());

		return SR_KC_ERR_CANT_GET_PEB;
	}

	PEB_32 peb{ 0 };
	if (!ReadProcessMemory(hTargetProc, pPEB, &peb, sizeof(PEB_32), nullptr))
	{
		INIT_ERROR_DATA(error_data, GetLastError());

		LOG(2, "ReadProcessMemory failed: %08X\n", error_data.AdvErrorCode);

		kc_info.close();
		DeleteFileW(InfoPath.c_str());

		return SR_KC_ERR_RPM_FAIL;
	}

	if (!peb.KernelCallbackTable)
	{
		INIT_ERROR_DATA(error_data, INJ_ERR_ADVANCED_NOT_DEFINED);

		LOG(2, "Kernel callback table not initialized\n");

		kc_info.close();
		DeleteFileW(InfoPath.c_str());

		return SR_KC_ERR_NO_INITIALIZED;
	}

	LOG(2, "Kernel callback table located at %08X\n", peb.KernelCallbackTable);

	DWORD kct[KERNEL_CALLBACK_TABLE_SIZE]{ 0 };
	SIZE_T size = KERNEL_CALLBACK_TABLE_SIZE;

	auto bRet = ReadProcessMemory(hTargetProc, MPTR(peb.KernelCallbackTable), kct, size * sizeof(DWORD), nullptr);
	if (!bRet)
	{
		if (GetLastError() == ERROR_PARTIAL_COPY) //guessed size may overlap with uninitalized page
		{
			SIZE_T base = (SIZE_T)peb.KernelCallbackTable;
			SIZE_T end = base + size * sizeof(DWORD);

			end &= 0xFFFFF000;

			size = (end - base) / sizeof(DWORD); //round down to next page boundary

			LOG(2, "Resized table to %08X bytes\n", size);

			bRet = ReadProcessMemory(hTargetProc, MPTR(peb.KernelCallbackTable), kct, size * sizeof(DWORD), nullptr);
		}

		if (!bRet)
		{
			INIT_ERROR_DATA(error_data, GetLastError());

			LOG(2, "ReadProcessMemory failed: %08X\n", error_data.AdvErrorCode);

			kc_info.close();
			DeleteFileW(InfoPath.c_str());

			return SR_KC_ERR_RPM_FAIL;
		}
	}

	LOG(2, "Copied kernel callback table\n");

	SIZE_T alloc_size = 0x100 + size * sizeof(DWORD);

	void * pMem = VirtualAllocEx(hTargetProc, nullptr, alloc_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!pMem)
	{
		INIT_ERROR_DATA(error_data, GetLastError());

		LOG(2, "VirtualAllocEx failed: %08X\n", error_data.AdvErrorCode);

		kc_info.close();
		DeleteFileW(InfoPath.c_str());

		return SR_KC_ERR_CANT_ALLOC_MEM;
	}

	BYTE Shellcode[] =
	{
		SR_REMOTE_DATA_BUFFER_86

		0x53,								// + 0x00			-> push	ebx						; push ebx on stack (non volatile)
		0xBB, 0x00, 0x00, 0x00, 0x00,		// + 0x01 (+ 0x02)	-> mov	ebx, 0x00000000			; move pData into ebx (update address manually on runtime)
		0x83, 0x3B, 0x00,					// + 0x06			-> cmp	dword ptr [ebx], 0		; test if SR_REMOTE_DATA::State is equal to SR_RS_ExecutionPending
		0x75, 0x1B,							// + 0x09			-> jne	0x26					; jump if not equal

		0xC6, 0x03, 0x01,					// + 0x0B			-> mov	byte ptr [ebx], 1		; set SR_REMOTE_DATA::State to SR_RS_Executing

		0xFF, 0x73, 0x0C,					// + 0x0E			-> push	[ebx + 0x0C]			; push pArg
		0xFF, 0x53, 0x10,					// + 0x11			-> call dword ptr [ebx + 0x10]	; call pRoutine
		0x89, 0x43, 0x04,					// + 0x14			-> mov	[ebx + 0x04], eax		; store returned value

		0x64, 0xA1, 0x18, 0x00, 0x00, 0x00,	// + 0x19			-> mov	eax, fs:[0x18]			; GetLastError
		0x8B, 0x40, 0x34,					// + 0x1D			-> mov	eax, [eax + 0x34]
		0x89, 0x43, 0x08,					// + 0x20			-> mov	[ebx + 0x08], eax		; store in SR_REMOTE_DATA::LastWin32Error

		0xC6, 0x03, 0x02,					// + 0x23			-> mov	byte ptr [ebx], 2		; set SR_REMOTE_DATA::State to SR_RS_ExecutionFinished

		0x5B,								// + 0x26			-> pop	ebx						; restore ebx
		0x31, 0xC0,							// + 0x27			-> xor	eax, eax				; set eax to 0 to prevent further handling of the message
		0xC2, 0x04, 0x00					// + 0x29			-> ret	0x04					; return
	}; // SIZE = 0x2A (+ sizeof(SR_REMOTE_DATA_WOW64))

	*ReCa<DWORD *>(Shellcode + 0x02 + sizeof(SR_REMOTE_DATA_WOW64)) = MDWD(pMem);

	DWORD pRemoteFunc = MDWD(pMem) + sizeof(SR_REMOTE_DATA_WOW64);

	auto * sr_data = ReCa<SR_REMOTE_DATA_WOW64 *>(Shellcode);
	sr_data->pArg		= pArg;
	sr_data->pRoutine	= pRoutine;

	if (!WriteProcessMemory(hTargetProc, pMem, Shellcode, sizeof(Shellcode), nullptr))
	{
		INIT_ERROR_DATA(error_data, GetLastError());

		LOG(2, "WriteProcessMemory failed: %08X\n", error_data.AdvErrorCode);

		VirtualFreeEx(hTargetProc, pMem, 0, MEM_RELEASE);

		kc_info.close();
		DeleteFileW(InfoPath.c_str());

		return SR_KC_ERR_WPM_FAIL;
	}

	LOG(2, "Hook will be called with:\n");
	LOG(3, "pRoutine = %08X\n", MDWD(pRemoteFunc));
	LOG(3, "pArg     = %08X\n", MDWD(pMem));

	kct[0] = pRemoteFunc;
	auto table_offset	= ALIGN_UP(sizeof(Shellcode), sizeof(DWORD));
	auto pTable			= ReCa<BYTE *>(pMem) + table_offset;

	if (!WriteProcessMemory(hTargetProc, pTable, kct, size * sizeof(DWORD), nullptr))
	{
		INIT_ERROR_DATA(error_data, GetLastError());

		LOG(2, "WriteProcessMemory failed: %08X\n", error_data.AdvErrorCode);

		VirtualFreeEx(hTargetProc, pMem, 0, MEM_RELEASE);

		kc_info.close();
		DeleteFileW(InfoPath.c_str());

		return SR_KC_ERR_WPM_FAIL;
	}

	LOG(2, "Copied kernel callback table into target process at %08X\n", MDWD(pTable));

	if (!WriteProcessMemory(hTargetProc, &pPEB->KernelCallbackTable, &pTable, sizeof(DWORD), nullptr))
	{
		INIT_ERROR_DATA(error_data, GetLastError());

		LOG(2, "WriteProcessMemory failed: %08X\n", error_data.AdvErrorCode);

		VirtualFreeEx(hTargetProc, pMem, 0, MEM_RELEASE);

		kc_info.close();
		DeleteFileW(InfoPath.c_str());

		return SR_KC_ERR_WPM_FAIL;
	}

	LOG(2, "Updated kernel callback table pointer\n");

	kc_info << std::dec << GetProcessId(hTargetProc) << std::endl;
	kc_info.close();

	std::wstring smPath = g_RootPathW;
	smPath += SM_EXE_FILENAME86;

	wchar_t cmdLine[] = L"\"" SM_EXE_FILENAME86 "\" " ID_KC;

	PROCESS_INFORMATION pi{ 0 };
	STARTUPINFOW		si{ 0 };
	si.cb			= sizeof(si);
	si.dwFlags		= STARTF_USESHOWWINDOW;
	si.wShowWindow	= SW_HIDE;

	LOG(2, "Data and command line prepared\n");

	if (TargetSessionId != -1)
	{
		LOG(2, "Target process is in a different session\n");

		HANDLE hUserToken = nullptr;
		if (!WTSQueryUserToken(TargetSessionId, &hUserToken))
		{
			INIT_ERROR_DATA(error_data, GetLastError());

			LOG(2, "WTSQueryUserToken failed: %08X\n", error_data.AdvErrorCode);

			WriteProcessMemory(hTargetProc, &pPEB->KernelCallbackTable, &peb.KernelCallbackTable, sizeof(DWORD), nullptr);
			VirtualFreeEx(hTargetProc, pMem, 0, MEM_RELEASE);
			DeleteFileW(InfoPath.c_str());

			return SR_KC_ERR_WTSQUERY_FAIL;
		}

		HANDLE hNewToken = nullptr;
		if (!DuplicateTokenEx(hUserToken, MAXIMUM_ALLOWED, nullptr, SecurityIdentification, TokenPrimary, &hNewToken))
		{
			INIT_ERROR_DATA(error_data, GetLastError());

			LOG(2, "DuplicateTokenEx failed: %08X\n", error_data.AdvErrorCode);

			WriteProcessMemory(hTargetProc, &pPEB->KernelCallbackTable, &peb.KernelCallbackTable, sizeof(DWORD), nullptr);
			CloseHandle(hUserToken);
			VirtualFreeEx(hTargetProc, pMem, 0, MEM_RELEASE);
			DeleteFileW(InfoPath.c_str());

			return SR_KC_ERR_DUP_TOKEN_FAIL;
		}

		DWORD SizeOut = 0;
		TOKEN_LINKED_TOKEN admin_token{ 0 };
		if (!GetTokenInformation(hNewToken, TokenLinkedToken, &admin_token, sizeof(admin_token), &SizeOut))
		{
			INIT_ERROR_DATA(error_data, GetLastError());

			LOG(2, "GetTokenInformation failed: %08X\n", error_data.AdvErrorCode);

			WriteProcessMemory(hTargetProc, &pPEB->KernelCallbackTable, &peb.KernelCallbackTable, sizeof(DWORD), nullptr);
			CloseHandle(hNewToken);
			CloseHandle(hUserToken);
			VirtualFreeEx(hTargetProc, pMem, 0, MEM_RELEASE);
			DeleteFileW(InfoPath.c_str());

			return SR_KC_ERR_GET_ADMIN_TOKEN_FAIL;
		}

		HANDLE hAdminToken = admin_token.LinkedToken;

		LOG(2, "Token prepared\n");

		LOG(2, "Launching %ls:\n       command line = %ls\n", SM_EXE_FILENAME86, cmdLine);

		if (!CreateProcessAsUserW(hAdminToken, smPath.c_str(), cmdLine, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
		{
			INIT_ERROR_DATA(error_data, GetLastError());

			LOG(2, "CreateProcessAsUserW failed: %08X\n", error_data.AdvErrorCode);

			WriteProcessMemory(hTargetProc, &pPEB->KernelCallbackTable, &peb.KernelCallbackTable, sizeof(DWORD), nullptr);
			CloseHandle(hAdminToken);
			CloseHandle(hNewToken);
			CloseHandle(hUserToken);
			VirtualFreeEx(hTargetProc, pMem, 0, MEM_RELEASE);
			DeleteFileW(InfoPath.c_str());

			return SR_KC_ERR_CANT_CREATE_PROCESS;
		}

		LOG(2, "%ls launched\n", SM_EXE_FILENAME86);

		CloseHandle(hAdminToken);
		CloseHandle(hNewToken);
		CloseHandle(hUserToken);
	}
	else
	{
		LOG(2, "Launching %ls:\n       command line = %ls\n", SM_EXE_FILENAME86, cmdLine);

		if (!CreateProcessW(smPath.c_str(), cmdLine, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
		{
			INIT_ERROR_DATA(error_data, GetLastError());

			LOG(2, "CreateProcessW failed: %08X\n", error_data.AdvErrorCode);

			WriteProcessMemory(hTargetProc, &pPEB->KernelCallbackTable, &peb.KernelCallbackTable, sizeof(DWORD), nullptr);
			VirtualFreeEx(hTargetProc, pMem, 0, MEM_RELEASE);
			DeleteFileW(InfoPath.c_str());

			return SR_KC_ERR_CANT_CREATE_PROCESS;
		}

		LOG(2, "%ls launched\n", SM_EXE_FILENAME86);
	}

	LOG(2, "Entering wait state\n");

	Sleep(SR_REMOTE_DELAY);

	auto Timer = GetTickCount64();

	DWORD dwWaitRet = WaitForSingleObject(pi.hProcess, Timeout);
	if (dwWaitRet != WAIT_OBJECT_0)
	{
		INIT_ERROR_DATA(error_data, GetLastError());

		LOG(2, "%ls timed out: %08X\n", SM_EXE_FILENAME86, error_data.AdvErrorCode);

		WriteProcessMemory(hTargetProc, &pPEB->KernelCallbackTable, &peb.KernelCallbackTable, sizeof(DWORD), nullptr);
		TerminateProcess(pi.hProcess, 0);
		DeleteFileW(InfoPath.c_str());

		return SR_KC_ERR_KC_TIMEOUT;
	}

	WriteProcessMemory(hTargetProc, &pPEB->KernelCallbackTable, &peb.KernelCallbackTable, sizeof(DWORD), nullptr);

	DWORD ExitCode = 0;
	GetExitCodeProcess(pi.hProcess, &ExitCode);

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	DeleteFileW(InfoPath.c_str());

	if (ExitCode != KC_ERR_SUCCESS)
	{
		INIT_ERROR_DATA(error_data, ExitCode);

		LOG(2, "%ls failed: %08X\n", SM_EXE_FILENAME, ExitCode);

		VirtualFreeEx(hTargetProc, pMem, 0, MEM_RELEASE);

		return ExitCode;
	}

	SR_REMOTE_DATA_WOW64 data{ };
	data.State			= (DWORD)SR_REMOTE_STATE::SR_RS_ExecutionPending;
	data.Ret			= ERROR_SUCCESS;
	data.LastWin32Error = ERROR_SUCCESS;

	while (GetTickCount64() - Timer < Timeout)
	{
		dwWaitRet = WaitForSingleObject(g_hInterruptEvent, 10);

		bRet = ReadProcessMemory(hTargetProc, pMem, &data, sizeof(data), nullptr);
		if (bRet && data.State == (DWORD)SR_REMOTE_STATE::SR_RS_ExecutionFinished)
		{
			LOG(2, "Shelldata retrieved\n");

			break;
		}
		else if (!bRet || dwWaitRet == WAIT_OBJECT_0)
		{
			INIT_ERROR_DATA(error_data, GetLastError());

			if (dwWaitRet == WAIT_OBJECT_0)
			{
				LOG(2, "Interrupt!\n");
			}
			else
			{
				LOG(2, "ReadProcessMemory failed: %08X\n", error_data.AdvErrorCode);
			}

			VirtualFreeEx(hTargetProc, pMem, 0, MEM_RELEASE);

			if (dwWaitRet == WAIT_OBJECT_0)
			{
				SetEvent(g_hInterruptedEvent);

				return SR_ERR_INTERRUPT;
			}

			return SR_KC_ERR_RPM_FAIL;
		}
	}

	VirtualFreeEx(hTargetProc, pMem, 0, MEM_RELEASE);

	if (data.State != (DWORD)SR_REMOTE_STATE::SR_RS_ExecutionFinished)
	{
		INIT_ERROR_DATA(error_data, INJ_ERR_ADVANCED_NOT_DEFINED);

		LOG(2, "Shell timed out\n");

		return SR_KC_ERR_REMOTE_TIMEOUT;
	}

	LOG(2, "pRoutine returned: %08X\n", data.Ret);

	Out = data.Ret;

	return SR_ERR_SUCCESS;
}

#endif