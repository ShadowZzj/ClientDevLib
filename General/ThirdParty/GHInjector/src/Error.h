#pragma once

/// ###############	##########		##########		     #######	   ##########			###			###
/// ###############	############	############	  ####     ####    ############			###			###
/// ###				###        ###	###        ###	 ###         ###   ###        ###		###			###
/// ###				###        ###	###        ###	###           ###  ###        ###		###			###
/// ###				###       ###	###       ###	###           ###  ###       ###		###			###
/// ###############	###########		###########		###           ###  ###########			###############
/// ###############	###########		########### 	###			  ###  ###########			###############
/// ###				###      ###	###		###     ###			  ###  ###		###			###			###
/// ###				###		  ###	###		  ###	###           ###  ###		  ###		###			###
/// ###				###		   ###	###		   ###	 ###         ###   ###		   ###	 #	###			###
/// ###############	###		   ###	###		   ###	  ####     ####    ###		   ###  ###	###			###
/// ###############	###        ###	###        ###	     #######	   ###         ###   #	###			###

//Injection errors:
#define INJ_ERR_SUCCESS					0x00000000
#define INJ_ERR_ADVANCED_NOT_DEFINED	0x00000000
#define INJ_ERR_SHELLCODE_DUMPED		0x80000000
													
														//Source							: advanced error type	: error description

#define INJ_ERR_NO_DATA						0x00000001	//internal error					: -						: nullptr passed to InjectA/InjectW
#define INJ_ERR_INVALID_FILEPATH			0x00000002	//internal error					: -						: INJECTIONDATA/W::szDllPath is a nullptr
#define INJ_ERR_STR_CONVERSION_TO_W_FAILED	0x00000003	//mbstowcs_s						: errno_t				: conversion to unicode of an ansi string failed
#define INJ_ERR_STRINGC_XXX_FAIL			0x00000004	//StringCXXX failed					: HRESULT				: string operation failed
#define INJ_ERR_FILE_DOESNT_EXIST			0x00000005	//GetFileAttributesW				: win32 error			: INJECTIONDATAW::szDllPath doesn't exist
#define INJ_ERR_INVALID_PID					0x00000006	//internal error					: -						: provided process id is 0
#define INJ_ERR_CANT_OPEN_PROCESS			0x00000007	//OpenProcess						: win32 error			: opening the specified target process failed
#define INJ_ERR_INVALID_PROC_HANDLE			0x00000008	//GetHandleInformation				: win32 error			: the provided handle value is not a valid handle
#define INJ_ERR_CANT_GET_EXE_FILENAME		0x00000009	//QueryFullProcessImageNameW		: win32 error			: failed to resolve the file name of the target process
#define INJ_ERR_PLATFORM_MISMATCH			0x0000000A	//internal error					: file error			: the provided file can't be injected (file error 0x20000001 - 0x20000003)
#define INJ_ERR_CANT_GET_TEMP_DIR			0x0000000B	//GetTempPathW						: win32 error			: unable to retrieve the path to the current users temp directory
#define INJ_ERR_CANT_COPY_FILE				0x0000000C	//CopyFileW							: win32 error			: unable to create a copy of the specified dll file
#define INJ_ERR_CANT_RENAME_FILE			0x0000000D	//_wrename							: errno					: renaming the file failed
#define INJ_ERR_INVALID_INJ_METHOD			0x0000000E	//bruh moment						: bruh moment			: bruh moment
#define INJ_ERR_REMOTE_CODE_FAILED			0x0000000F	//internal error					: -						: the remote code wasn't able to load the specified module
#define INJ_ERR_WPM_FAIL					0x00000010	//WriteProcessMemory				: win32 error			: write operation failed
#define	INJ_ERR_RPM_FAIL					0x00000011	//ReadProcessMemory					: win32 error			: read operation failed
#define INJ_ERR_GET_MODULE_HANDLE_FAIL		0x00000012	//GetModuleHandle					: win32 error			: address of the specified module couldn't be resolved
#define INJ_ERR_CANT_FIND_MOD_PEB			0x00000013	//internal error					: -						: injected module isn't linked to the peb
#define INJ_ERR_UNLINKING_FAILED			0x00000014	//internal error					: PEB linker error		: unlinking the injected module from the PEB failed (PEB linker error 0x60000001 - 0x600000XX)
#define INJ_ERR_OUT_OF_MEMORY_EXT			0x00000015	//VirtualAllocEx					: win32 error			: memory allocation in the target process failed
#define INJ_ERR_OUT_OF_MEMORY_INT			0x00000016	//VirtualAlloc						: win32 error			: internal memory allocation failed
#define INJ_ERR_OUT_OF_MEMORY_NEW			0x00000017	//operator new						: -						: internal memory allocation on heap failed
#define INJ_ERR_IMAGE_CANT_RELOC			0x00000018	//internal error					: -						: image has to be relocated but base reloc directory is emtpy
#define INJ_ERR_GET_SYMBOL_ADDRESS_FAILED	0x00000019	//internal error					: -						: can't resolve the address of a required symbol
#define INJ_ERR_GET_PROC_ADDRESS_FAIL		0x0000001A	//GetProcAddress					: -						: resolving the address of a required function failed
#define INJ_ERR_VERIFY_RESULT_FAIL			0x0000001B	//ReadProcessMemory					: win32 error			: reading the result data of the injection failed
#define INJ_ERR_SYMBOL_INIT_NOT_DONE		0x0000001C	//SYMBOL_LOADER::Initialize			: -						: initializations process of the symbol loader isn't finished
#define INJ_ERR_SYMBOL_LOAD_FAIL			0x0000001D	//SYMBOL_LOADER::Initialize			: symbol error			: initialization failed (symbol error 0x40000001 - ...)
#define INJ_ERR_SYMBOL_GET_FAIL				0x0000001E	//SYMBOL_PARSER::GetSymbolAddress	: symbol error			: couldn't get address of required symbol (symbol error 0x40000001 - ...)
#define INJ_ERR_CANT_GET_MODULE_PATH		0x0000001F	//internal error					: -						: can't resolve the path of this instance of the injection library
#define INJ_ERR_FAILED_TO_LOAD_DLL			0x00000020	//internal error					: -						: the injection failed for unknown reasons
#define INJ_ERR_HIJACK_NO_HANDLES			0x00000021	//internal error					: -						: can't find a process handle to the target process
#define INJ_ERR_HIJACK_NO_NATIVE_HANDLE		0x00000022	//internal error					: -						: can't find a hijackable handle to the target process
#define INJ_ERR_HIJACK_INJ_FAILED			0x00000023	//internal error					: GH Inj error code		: injecting injection module into handle owner process failed
#define INJ_ERR_HIJACK_OUT_OF_MEMORY_EXT	0x00000024	//VirtualAllocEx					: win32 error			: memory allocation in the hijack process failed
#define INJ_ERR_HIJACK_WPM_FAIL				0x00000025	//WriteProcessMemory				: win32 error			: writing injection data to hijack process failed
#define INJ_ERR_HIJACK_INJECTW_MISSING		0x00000026	//internal error					: -						: can't locate remote injection function in hijack process
#define INJ_ERR_HIJACK_REMOTE_INJ_FAIL		0x00000027	//internal error					: GH Inj error code		: injection executed in the hijack process failed, additional error log was generated
#define INJ_ERR_LLEXW_FAILED				0x00000028	//LoadLibraryExW					: win32 error			: LoadLibraryExW failed loading the dll
#define INJ_ERR_LDRLDLL_FAILED				0x00000029	//LdrLoadDll						: NTSTATUS				: LdrLoadDll failed loading the dll
#define INJ_ERR_LDRPLDLL_FAILED				0x0000002A	//LdrpLoadDll						: NTSTATUS				: LdrpLoadDll failed loading the dll
#define INJ_ERR_LDRPLDLLINTERNAL_FAILED		0x0000002B	//LdrpLoadDllInternal				: NTSTATUS				: LdrpLoadDllInternal failed loading the dll
#define INJ_ERR_CANT_GET_PEB				0x0000002C	//__readgsqword or __readfsdword	: -						: reading the linear address of the PEB failed
#define INJ_ERR_INVALID_PEB_DATA			0x0000002D	//internal error					: -						: peb data required to erase/fake header or unlike the module from the peb wasn't findable
#define INJ_ERR_UPDATE_PROTECTION_FAILED	0x0000002E	//NtProtectVirtualMemory			: NTSTATUS				: updating the page protection of the pe header failed
#define INJ_ERR_WOW64_NTDLL_MISSING			0x0000002F	//internal error					: -						: can't resolve address of the wow64 ntdll.dll
#define INJ_ERR_INVALID_PATH_SEPERATOR		0x00000030	//internal error					: -						: can't find '\' in a path. '/' as seperators aren't supported
#define INJ_ERR_LDRP_PREPROCESS_FAILED		0x00000031	//LdrpPreprocessDllName				: NTSTATUS				: preprocessing the dll name for LdrpLoadDll(Internal) failed
#define INJ_ERR_INVALID_POINTER				0x00000032	//internal error					: -						: an invalid funtion pointer was passed to SetRawPrintCallback
#define INJ_ERR_NOT_IMPLEMENTED				0x00000033	//internal error					: -						: the module was compiled without DEBUG_INFO being defined, check pch.h for more information if you want to redirect debug output
#define INJ_ERR_KERNEL32_MISSING			0x00000034	//internal error					: -						: failed to resolve address of kernel32.dll (native)
#define INJ_ERR_WOW64_KERNEL32_MISSING		0x00000035	//internal error					: -						: can't resolve address of the wow64 kernel32.dll
#define INJ_ERR_OPEN_WOW64_PROCESS			0x00000036	//OpenProcess						: win32 error			: failed to attach to wow64 process to resolve addresses
#define INJ_ERR_IMPORT_HANDLER_NOT_DONE		0x00000037	//internal error					: -						: import handler isn't finished resolving all required functions or is waiting for symbol parser thread(s) to finish
#define INJ_ERR_WCSRCHR_FAILED				0x00000038	//wcsrchr							: -						: wcsrchr failed to find a character in a string (usually '\\' in a path)
#define INJ_ERR_TARGET_EXE_NAME_IS_NULL		0x00000039	//internal error					: -						: the length of the name of the specified process is 0
#define INJ_ERR_LDR_ENTRY_IS_NULL			0x0000003A	//internal error					: -						: LdrpLoadDll(Internal) didn't return a valid LDR_DATA_TABLE_ENTRY pointer
#define INJ_ERR_NOT_SUPPORTED				0x0000003B	//internal error					: -						: the requested operation is not supported on the current operating system
#define INJ_ERR_CREATE_EVENT_FAILED			0x0000003C	//CreateEventEx						: win32 error			: failed to create an event for wow64 process
#define INJ_ERR_CREATE_PROCESS_FAILED		0x0000003D	//CreateProcessW					: win32 error			: failed to create process for wow64 module addresses
#define INJ_ERR_WAIT_FAILED					0x0000003E	//WaitForSingleObject				: win32 error			: failed to wait for an event to trigger
#define INJ_ERR_WAIT_TIMEOUT				0x0000003F	//WaitForSingleObject				: -						: event timed out
#define INJ_ERR_WINDOWS_VERSION				0x00000040	//internal error					: -						: failed to resolve the version number of the operating system
#define INJ_ERR_WINDOWS_TOO_OLD				0x00000041	//internal error					: -						: the injection library only runs on Windows 7 or higher
#define INJ_ERR_ALREADY_RUNNING				0x00000042	//internal error					: -						: there's already an injection running
#define INJ_ERR_IMPORT_INTERRUPT			0x00000043	//internal error					: -						: the import handler was interrupted with InterruptDownload()
#define INJ_ERR_INVALID_SYMBOL_INDEX		0x00000044	//internal error					: -						: an invalid index was passed to the symbol loader
#define INJ_ERR_INTERRUPT					0x00000045	//internal error					: -						: the injection was interrupted because the interrupt event was set
#define INJ_ERR_SYMBOL_PARSE_FAIL			0x00000046	//SYMBOL_PARSER::Initialize			: symbol error			: initialization failed (symbol error 0x40000001 - ...)
#define	INJ_ERR_SM86_EXE_MISSING			0x00000047	//internal errro					: -						: "GH Injector SM - x86.exe" is missing, this file is required for import resolving

///////////////////
///ManualMap
															//Source							: advanced error type	: error description

#define INJ_MM_ERR_NO_DATA						0x00400001	//internal error					: -						: pData is NULL
#define INJ_MM_ERR_NT_OPEN_FILE					0x00400002	//NtOpenFile						: NTSTATUS				: NtOpenFile failed
#define INJ_MM_ERR_HEAP_ALLOC					0x00400003	//NtAllocateHeap					: -						: memory allocation failed
#define INJ_MM_ERR_NT_READ_FILE					0x00400004	//NtReadFile						: NTSTATUS				: reading the file failed
#define INJ_MM_ERR_SET_FILE_POSITION			0x00400005	//NtSetInformationFile				: NTSTATUS				: failed to reset the file pointer to  the beginning of the file 
#define INJ_MM_ERR_UPDATE_PAGE_PROTECTION		0x00400006	//NtProtectVirtualMemory			: NTSTATUS				: setting the page protection of a section failed
#define INJ_MM_ERR_CANT_GET_FILE_SIZE			0x00400007	//NtQueryInformationFile			: NTSTATUS				: querying the file size failed
#define INJ_MM_ERR_MEMORY_ALLOCATION_FAILED		0x00400008	//NtAllocateVirtualMemory			: NTSTATUS				: couldn't allocate memory
#define INJ_MM_ERR_IMAGE_CANT_BE_RELOCATED		0x00400009	//internal error					: -						: the image has to be relocated but the reloc directory of the image is empty
#define INJ_MM_ERR_IMPORT_FAIL					0x0040000A	//internal error					: NTSTATUS				: one module couldn't be loaded or an import couldn't be resolved, if ntRet is STATUS_HEAP_CORRUPTION, memory allocation failed
#define INJ_MM_ERR_DELAY_IMPORT_FAIL			0x0040000B	//internal error					: NTSTATUS				: one module couldn't be loaded or an import couldn't be resolved, if ntRet is STATUS_HEAP_CORRUPTION, memory allocation failed
#define INJ_MM_ERR_ENABLING_SEH_FAILED			0x0040000C	//RtlInsertInvertedFunctionTable	: NTSTATUS				: enabling exception handling by calling RtlInsertInvertedFunctionTable failed
#define INJ_MM_ERR_NOT_IN_LDRP_SEH_TABLE		0x0040000D	//internal error					: -						: RtlInsertInvertedFunctionTable didn't insert data into LdrpInvertedFunctionTable, manual insertion currently not supported
#define INJ_MM_ERR_INVALID_HEAP_HANDLE			0x0040000E	//internal error					: -						: the provided pointer to the LdrpHeap is invalid
#define INJ_MM_ERR_CANT_GET_PEB					0x0040000F	//__readgsqword or __readfsdword	: -						: reading the linear address of the PEB failed
#define INJ_MM_ERR_INVALID_PEB_DATA				0x00400010	//internal error					: -						: peb data required to fake header wasn't findable



/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//Start Routine errors:
#define SR_ERR_SUCCESS					0x00000000
													
													//Source					: advanced error type	: error description
 
#define SR_ERR_CANT_QUERY_SESSION_ID	0x10000001	//NtQueryInformationProcess	: NTSTATUS				: querying the session id of the target process failed
#define SR_ERR_INVALID_LAUNCH_METHOD	0x10000002	//bruh moment				: bruh moment			: bruh moment
#define SR_ERR_NOT_LOCAL_SYSTEM			0x10000003	//internal error			: -						: SetWindowsHookEx with handle hijacking only works within the same session or from session 0 (LocalSystem account) because of the WtsAPIs
#define SR_ERR_INTERRUPT				0x10000004	//internal error			: -						: execution of the injection was interrupted by InterruptInjection()

///////////////////
///NtCreateThreadEx
														//Source					: advanced error type	: error description

#define SR_NTCTE_ERR_PROC_INFO_FAIL			0x10100001	//internal error			: -						: can't grab process information
#define SR_NTCTE_ERR_GET_ENTRYPOINT			0x10100002	//internal error			: -						: failed to resolve the entrypoint of the target process
#define SR_NTCTE_ERR_CANT_ALLOC_MEM			0x10100003	//VirtualAllocEx			: win32 error			: memory allocation for the shellcode failed
#define SR_NTCTE_ERR_WPM_FAIL				0x10100004	//WriteProcessMemory		: win32 error			: writing the shellcode into the target process' memory failed
#define SR_NTCTE_ERR_NTCTE_FAIL				0x10100005	//NtCreateThreadEx			: NTSTATUS				: thread creation using NtCreateThreadEx failed
#define SR_NTCTE_ERR_GET_CONTEXT_FAIL		0x10100006	//(Wow64)GetThreadContext	: win32 error			: can't get thread context
#define SR_NTCTE_ERR_SET_CONTEXT_FAIL		0x10100007	//(Wow64)SetThreadContext	: win32 error			: can't set thread context
#define SR_NTCTE_ERR_RESUME_FAIL			0x10100008	//ResumeThread				: win32 error			: resuming the thread failed
#define SR_NTCTE_ERR_REMOTE_TIMEOUT			0x10100009	//WaitForSingleObject		: win32 error			: execution time of the shellcode exceeded SR_REMOTE_TIMEOUT
#define SR_NTCTE_ERR_GECT_FAIL				0x1010000A	//GetExitCodeThread			: win32 error			: can't retrieve the exit code of the thread
#define SR_NTCTE_ERR_SHELLCODE_SETUP_FAIL	0x1010000B	//shellcode					: - 					: argument passed to the shellcode is 0
#define SR_NTCTE_ERR_RPM_FAIL				0x1010000C	//ReadProcessMemory			: win32 error			: reading the results of the shellcode failed
#define SR_NTCTE_ERR_CANT_FIND_THREAD		0x1010000D	//internal error			: -						: ProcessInfo class failed to resolve information about the new thread
#define SR_NTCTE_ERR_NTQIT_FAIL				0x1010000E	//NtQueryInformationThread	: NTSTATUS				: failed to get THREAD_BASIC_INFORMATION


///////////////
///HijackThread
														//Source					: advanced error type	: error description

#define SR_HT_ERR_PROC_INFO_FAIL			0x10200001	//internal error			: -						: can't grab process information
#define SR_HT_ERR_NO_THREADS				0x10200002	//internal error			: -						: no threads to hijack
#define SR_HT_ERR_OPEN_THREAD_FAIL			0x10200003	//OpenThread				: win32 error			: can't open handle to the target thread
#define SR_HT_ERR_SUSPEND_FAIL				0x10200004	//SuspendThread				: win32 error			: suspending the target thread failed
#define SR_HT_ERR_GET_CONTEXT_FAIL			0x10200005	//(Wow64)GetThreadContext	: win32 error			: can't get thread context
#define SR_HT_ERR_CANT_ALLOC_MEM			0x10200006	//VirtualAllocEx			: win32 error			: memory allocation for the shellcode failed
#define SR_HT_ERR_WPM_FAIL					0x10200007	//WriteProcessMemory		: win32 error			: writing the shellcode into the target process' memory failed
#define SR_HT_ERR_SET_CONTEXT_FAIL			0x10200008	//(Wow64)SetThreadContext	: win32 error			: can't update the thread context
#define SR_HT_ERR_RESUME_FAIL				0x10200009	//ResumeThread				: win32 error			: resuming the thread failed
#define SR_HT_ERR_REMOTE_TIMEOUT			0x1020000A	//internal error			: -						: execution time exceeded SR_REMOTE_TIMEOUT (can't be deallocated safely)
#define SR_HT_ERR_REMOTE_PENDING_TIMEOUT	0x1020000B	//internal error			: -						: execution time exceeded SR_REMOTE_TIMEOUT while pending (can be deallocated safely)
#define SR_HT_ERR_RPM_FAIL					0x1020000C	//ReadProcessMemory			: win32 error			: reading the results of the shellcode failed

////////////////////
///SetWindowsHookEx
														//Source				: advanced error type	: error description

#define SR_SWHEX_ERR_CANT_OPEN_INFO_TXT		0x10300001	//internal error		: -						: can't open swhex info file
#define SR_SWHEX_ERR_CANT_ALLOC_MEM			0x10300002	//VirtualAllocEx		: win32 error			: memory allocation for the shellcode failed
#define SR_SWHEX_ERR_WPM_FAIL				0x10300003	//WriteProcessMemory	: win32 error			: writing the shellcode into the target process' memory failed
#define SR_SWHEX_ERR_WTSQUERY_FAIL			0x10300004	//WTSQueryUserToken		: win32 error			: failed to query the token for the target process user session
#define SR_SWHEX_ERR_DUP_TOKEN_FAIL			0x10300005	//DuplicateTokenEx		: win32 error			: failed to duplicate the token for the target process user session
#define SR_SWHEX_ERR_GET_ADMIN_TOKEN_FAIL	0x10300006	//GetTokenInformation	: win32 error			: failed to retrieve information from the token handle
#define SR_SWHEX_ERR_CANT_CREATE_PROCESS	0x10300007	//CreateProcessAsUserW	: win32 error			: failed to launch SM_EXE_FILENAME.exe to execute shellcode
														//CreateProcessW		: win32 error			: failed to launch SM_EXE_FILENAME.exe to execute shellcode
#define SR_SWHEX_ERR_SWHEX_TIMEOUT			0x10300008	//WaitForSingleObject	: win32 error			: SM_EXE_FILENAME.exe execution time exceeded
#define SR_SWHEX_ERR_REMOTE_TIMEOUT			0x10300009	//internal error		: -						: execution time exceeded SR_REMOTE_TIMEOUT
#define SR_SWHEX_ERR_RPM_FAIL				0x1030000A	//ReadProcessMemory		: win32 error			: reading the results of the shellcode failed

#define SR_SWHEX_ERR_SWHEX_EXT_ERROR		0x1030000B	//SM_EXE_FILENAME.exe	: "GH Injector SM - XX.exe" error code, 0x30100001 - 0x30100006 (see below) or win32 exception

///////////////
///QueueUserAPC
														//Source					: advanced error type	: error description

#define SR_QUAPC_ERR_CANT_ALLOC_MEM			0x10400002	//VirtualAllocEx			: win32 error			: memory allocation for the shellcode failed
#define SR_QUAPC_ERR_WPM_FAIL				0x10400003	//WriteProcessMemory		: win32 error			: writing the shellcode into the target process' memory failed
#define SR_QUAPC_ERR_PROC_INFO_FAIL			0x10400004	//internal error			: -						: can't grab process information
#define SR_QUAPC_ERR_NO_THREADS				0x10400005	//internal error			: -						: no threads to queue an apc to
#define SR_QUAPC_ERR_REMOTE_TIMEOUT			0x10400006	//internal error			: -						: execution time exceeded SR_REMOTE_TIMEOUT
#define SR_QUAPC_ERR_RPM_FAIL				0x10400007	//WriteProcessMemory		: win32 error			: reading the results of the shellcode failed

///////////////
///KernelCallback
													//Source				: advanced error type	: error description

#define SR_KC_ERR_CANT_OPEN_INFO_TXT	0x10500001	//internal error		: -						: can't open kc info file
#define SR_KC_ERR_PROC_INFO_FAIL		0x10500002	//internal error		: -						: can't grab process information
#define SR_KC_ERR_CANT_GET_PEB			0x10500003	//internal error		: -						: failed to retrieve pointer to the (wow64) peb
#define SR_KC_ERR_RPM_FAIL				0x10500004	//ReadProcessMemory		: win32 error			: failed to read memory from the target process
#define SR_KC_ERR_NO_INITIALIZED		0x10500005	//internal error		: -						: the kernel callback table is not initialized
#define SR_KC_ERR_CANT_ALLOC_MEM		0x10500006	//VirtualAllocEx		: win32 error			: memory allocation for the shellcode/table failed
#define SR_KC_ERR_WPM_FAIL				0x10500007	//WriteProcessMemory	: win32 error			: writing the shellcode/table into the target process' memory failed
#define SR_KC_ERR_WTSQUERY_FAIL			0x10500008	//WTSQueryUserToken		: win32 error			: failed to query the token for the target process user session
#define SR_KC_ERR_DUP_TOKEN_FAIL		0x10500009	//DuplicateTokenEx		: win32 error			: failed to duplicate the token for the target process user session
#define SR_KC_ERR_GET_ADMIN_TOKEN_FAIL	0x1050000A	//GetTokenInformation	: win32 error			: failed to retrieve information from the token handle
#define SR_KC_ERR_CANT_CREATE_PROCESS	0x1050000B	//CreateProcessAsUserW	: win32 error			: failed to launch SM_EXE_FILENAME.exe to execute shellcode
													//CreateProcessW		: win32 error			: failed to launch SM_EXE_FILENAME.exe to execute shellcode
#define SR_KC_ERR_KC_TIMEOUT			0x1050000C	//WaitForSingleObject	: win32 error			: SM_EXE_FILENAME.exe execution time exceeded
#define SR_KC_ERR_REMOTE_TIMEOUT		0x1050000D	//internal error		: -						: execution time exceeded SR_REMOTE_TIMEOUT

#define SR_KC_ERR_KC_EXT_ERROR			0x1050000E	//SM_EXE_FILENAME.exe	: "GH Injector SM - XX.exe" error code, 0x50100001 - 0x50100006 (see below) or win32 exception

///////////////
///FakeVEH
												//Source				: advanced error type	: error description

#define SR_VEH_ERR_CANT_ALLOC_MEM	0x10600001	//VirtualAllocEx			: win32 error			: memory allocation for the shellcode failed
#define SR_VEH_ERR_WPM_FAIL			0x10600002	//WriteProcessMemory		: win32 error			: writing the shellcode into the target process' memory failed
#define SR_VEH_ERR_RPM_FAIL			0x10600003	//ReadProcessMemory			: win32 error			: failed to read memory from the target process
#define SR_VEH_ERR_PROCESS_COOKIE	0x10600004	//internal error			: -						: failed to get the process cookie
#define SR_VEH_ERR_PROC_INFO_FAIL	0x10600005	//internal error			: -						: can't grab process information
#define SR_VEH_ERR_CANT_GET_PEB		0x10600006	//internal error			: -						: failed to retrieve pointer to the (wow64) peb
#define SR_VEH_ERR_PROTECT_FAIL		0x10600007	//VirtualProtectEx			: win32 error			: failed to update page protection
#define SR_VEH_ERR_REMOTE_TIMEOUT	0x10600008	//internal error			: -						: execution time exceeded SR_REMOTE_TIMEOUT



/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//File errors:
#define FILE_ERR_SUCCESS					0x00000000

														//Source				: error description
#define FILE_ERR_CANT_OPEN_FILE				0x20000001	//std::ifstream::good	: opening the file failed
#define FILE_ERR_INVALID_FILE_SIZE			0x20000002	//internal error		: file isn't a valid PE
#define FILE_ERR_INVALID_FILE				0x20000003	//internal error		: PE isn't compatible with the injection settings
#define FILE_ERR_MEMORY_ALLOCATION_FAILED	0x20000004	//operator new			: failed to allocate data buffer



/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//GH Injector SM - XX.exe errors:

												//Source	:	error description

#define SM_ERR_INVALID_ARGC	0x30000001			//main		:	GH Injector SM - XX.exe was called with the wrong amount of arguments
#define SM_ERR_INVALID_ARGV	0x30000002			//main		:	GH Injector SM - XX.exe was called with invalid arguments

////////////////////////////////////////////////////////////
//GH Injector SM - XX.exe specific errors:

///SetWindowHookEx:
#define SWHEX_ERR_SUCCESS 0x00000000
												//Source				: error description

#define SWHEX_ERR_INVALID_PATH		0x30100001	//StringCchLengthW		: path exceeds MAX_PATH * 2 chars
#define SWHEX_ERR_CANT_OPEN_FILE	0x30100002	//std::ifstream::good	: openening the SMXX.txt failed
#define SWHEX_ERR_EMPTY_FILE		0x30100003	//internal error		: SMXX.txt is empty
#define SWHEX_ERR_INVALID_INFO		0x30100004	//internal error		: provided info is wrong / invalid
#define SWHEX_ERR_ENUM_WINDOWS_FAIL 0x30100005	//EnumWindows			: API fail
#define SWHEX_ERR_NO_WINDOWS		0x30100006	//internal error		: no compatible window found

///KernelCallbackTable
#define KC_ERR_SUCCESS 0x00000000
												//Source				: error description

#define KC_ERR_INVALID_PATH			0x50100001	//StringCchLengthW		: path exceeds MAX_PATH * 2 chars
#define KC_ERR_CANT_OPEN_FILE		0x50100002	//std::ifstream::good	: opening the SMXX.txt failed
#define KC_ERR_EMPTY_FILE			0x50100003	//internal error		: SMXX.txt is empty
#define KC_ERR_INVALID_INFO			0x50100004	//internal error		: provided info is wrong / invalid
#define KC_ERR_ENUM_WINDOWS_FAIL	0x50100005	//EnumWindows			: API fail
#define KC_ERR_NO_WINDOWS			0x50100006	//internal error		: no compatible window found



/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//Symbol errors:
#define SYMBOL_ERR_SUCCESS						0x00000000

															//Source					: error description
#define SYMBOL_ERR_CANT_OPEN_MODULE				0x40000001	//std::ifstream::good		: can't open the specified module
#define SYMBOL_ERR_FILE_SIZE_IS_NULL			0x40000002	//std::ifstream::tellg		: file size of the specified module is 0
#define SYMBOL_ERR_CANT_ALLOC_MEMORY_NEW		0x40000003	//operator new				: can't allocate memory
#define SYMBOL_ERR_INVALID_FILE_ARCHITECTURE	0x40000004	//internal error			: the architecture of the specified file doesn't match AMD64 or I386
#define SYMBOL_ERR_CANT_ALLOC_MEMORY			0x40000005	//VirtualAlloc				: can't allocate memory
#define SYMBOL_ERR_NO_PDB_DEBUG_DATA			0x40000006	//internal error			: debug directory is empty or wrong type
#define SYMBOL_ERR_PATH_DOESNT_EXIST			0x40000007	//CreateDirectoryA			: path doesn't exit and can't be created
#define SYMBOL_ERR_CANT_CREATE_DIRECTORY		0x40000008	//CreateDirectoryA			: path doesn't exit and can't be created (x86/x64 subdirectory)
#define SYMBOL_ERR_CANT_CONVERT_PDB_GUID		0x40000008	//StringFromGUID2			: conversion of the GUID to string failed
#define SYMBOL_ERR_GUID_TO_ANSI_FAILED			0x40000009	//wcstombs_s				: conversion of GUID to ANSI string failed
#define SYMBOL_ERR_DOWNLOAD_FAILED				0x4000000A	//URLDownloadToCacheFileA	: downloading the pdb file failed
#define SYMBOL_ERR_CANT_ACCESS_PDB_FILE			0x4000000B	//GetFileAttributesExA		: can't access the pdb file
#define SYMBOL_ERR_CANT_OPEN_PDB_FILE			0x4000000C	//CreateFileA				: can't open the pdb file
#define SYMBOL_ERR_CANT_OPEN_PROCESS			0x4000000D	//OpenProcess				: can't open handle to current process
#define SYMBOL_ERR_SYM_INIT_FAIL				0x4000000E	//SymInitialize				: couldn't initialize pdb symbol stuff
#define SYMBOL_ERR_SYM_LOAD_TABLE				0x4000000F	//SymLoadModule64			: couldn't load symbol table
#define SYMBOL_ERR_ALREADY_INITIALIZED			0x40000010	//internal error			: this instance of the SYMBOL_PARSER has already been initialized
#define SYMBOL_ERR_NOT_INITIALIZED				0x40000011	//internal error			: this isntance of the SYMBOL_PARSER hasn't benen initialized
#define SYMBOL_ERR_IVNALID_SYMBOL_NAME			0x40000012	//internal error			: szSymbolName is NULL
#define SYMBOL_ERR_SYMBOL_SEARCH_FAILED			0x40000013	//SymFromName				: couldn't find szSymbolName in the specified pdb
#define SYMBOL_CANT_OPEN_PROCESS				0x40000014	//OpenProcess				: can't get PROCESS_QUERY_LIMITED_INFORMATION handle to current process
#define SYMBOL_ERR_COPYFILE_FAILED				0x40000015	//CopyFileA					: copying the file from the cache directory failed
#define SYMBOL_ERR_INTERRUPT					0x40000016	//internal error			: download has been interrupted
#define SYMBOL_ERR_CANNOT_CONNECT				0x40000017	//InternetCheckConnectionW	: GetLastError returned ERROR_INTERNET_CANNOT_CONNECT which might be caused by a firewall rule
#define SYMBOL_ERR_OBJECT_IS_NULL				0x40000018	//internal error			: the provided SYMBOL_LOADER object pointer is a nullptr
#define SYMBOL_ERR_OBJECT_NOT_READY				0x40000019	//internal error			: the provided SYMBOL_LOADER object is not in the ready state


/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//Symbol errors:
#define HOOK_SCAN_ERR_SUCCESS						0x00000000

																//Source				: error description
#define HOOK_SCAN_ERR_INVALID_PROCESS_ID			0x50000001	//internal error		: target process identifier is 0
#define HOOK_SCAN_ERR_CANT_OPEN_PROCESS				0x50000002	//OpenProcess			: target process identifier is 0
#define HOOK_SCAN_ERR_PLATFORM_MISMATCH				0x50000003	//internal error		: wow64 injector can't scan x64 process
#define HOOK_SCAN_ERR_GETPROCADDRESS_FAILED			0x50000004	//GetProcAddress		: GetProcAddress failed internally
#define HOOK_SCAN_ERR_READ_PROCESS_MEMORY_FAILED	0x50000005	//ReadProcessMemory		: ReadProcessMemory failed while reading the bytes of the target function
#define HOOK_SCAN_ERR_CANT_GET_OWN_MODULE_PATH		0x50000006	//GetOwnModulePath		: unable to obtain path to the GH Injector directory
#define HOOK_SCAN_ERR_CREATE_EVENT_FAILED			0x50000007	//CreateEventEx			: win32 error
#define HOOK_SCAN_ERR_CREATE_PROCESS_FAILED			0x50000008	//CreateProcessW		: win32 error
#define HOOK_SCAN_ERR_WAIT_FAILED					0x50000009	//WaitForSingleObject	: win32 error
#define HOOK_SCAN_ERR_WAIT_TIMEOUT					0x5000000A	//WaitForSingleObject	: waiting timed out
#define HOOK_SCAN_ERR_BUFFER_TOO_SMALL				0x5000000B	//internal error		: the buffer passed to ValidateInjectionFunctions is too small



/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct ERROR_DATA
{
	DWORD		AdvErrorCode;
	wchar_t		szFileName[MAX_PATH];
	wchar_t		szFunctionName[MAX_PATH];
	int			Line;
};

#define INIT_ERROR_DATA(data, error) \
data.AdvErrorCode = error;															\
data.Line = __LINE__;																\
memset(data.szFileName, 0, sizeof(data.szFileName));								\
memset(data.szFunctionName, 0, sizeof(data.szFunctionName));						\
memcpy(data.szFileName, __FILENAMEW__,  ((size_t)lstrlenW(__FILENAMEW__)) * 2);		\
memcpy(data.szFunctionName, __FUNCTIONW__, ((size_t)lstrlenW(__FUNCTIONW__)) * 2);