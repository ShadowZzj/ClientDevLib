#pragma once

#include "Injection Internal.h"
#include "Eject.h"
#include "Handle Hijacking.h"

//Cloaking options:
#define INJ_ERASE_HEADER				0x0001	//replaces the first 0x1000 bytes of the dll with 0's (takes priority over INJ_FAKE_HEADER if both are specified)
#define INJ_FAKE_HEADER					0x0002	//replaces the dlls header with the header of the ntdll.dll (superseded by INJ_ERASE_HEADER if both are specified)
#define INJ_UNLINK_FROM_PEB				0x0004	//unlinks the module from the process enviroment block (1)
#define INJ_THREAD_CREATE_CLOAKED		0x0008	//induces INJ_CTF_FAKE_START_ADDRESS | INJ_CTF_HIDE_FROM_DEBUGGER (2), see "Start Routine.h" for all options
#define INJ_SCRAMBLE_DLL_NAME			0x0010	//randomizes the dll name on disk before injecting it
#define INJ_LOAD_DLL_COPY				0x0020	//loads a copy of the dll from %temp% directory
#define INJ_HIJACK_HANDLE				0x0040	//tries to a hijack a handle from another process instead of using OpenProcess

//Notes:
///(1) ignored when manual mapping
///(2) launch method must be NtCreateThreadEx, ignored otherwise

//Manual mapping options:
#define INJ_MM_CLEAN_DATA_DIR			0x00010000	//removes data from the dlls PE header, ignored if INJ_MM_SET_PAGE_PROTECTIONS is set
#define INJ_MM_RESOLVE_IMPORTS			0x00020000	//resolves dll imports
#define INJ_MM_RESOLVE_DELAY_IMPORTS	0x00040000	//resolves delayed imports
#define INJ_MM_EXECUTE_TLS				0x00080000	//executes TLS callbacks and initializes static TLS data
#define INJ_MM_ENABLE_EXCEPTIONS		0x00100000	//enables exception handling
#define INJ_MM_SET_PAGE_PROTECTIONS		0x00200000	//sets page protections based on section characteristics, if set INJ_MM_CLEAN_DATA_DIR and INJ_MM_SHIFT_MODULE_BASE will be ignored
#define INJ_MM_INIT_SECURITY_COOKIE		0x00400000	//initializes security cookie for buffer overrun protection
#define INJ_MM_RUN_DLL_MAIN				0x00800000	//executes DllMain
													//this option induces INJ_MM_RESOLVE_IMPORTS
#define INJ_MM_RUN_UNDER_LDR_LOCK		0x01000000	//runs the DllMain under the loader lock
#define INJ_MM_SHIFT_MODULE_BASE		0x02000000	//shifts the module base by a random offset, ignored if INJ_MM_SET_PAGE_PROTECTIONS is set

#define MM_DEFAULT (INJ_MM_RESOLVE_IMPORTS | INJ_MM_RESOLVE_DELAY_IMPORTS | INJ_MM_INIT_SECURITY_COOKIE | INJ_MM_EXECUTE_TLS | INJ_MM_ENABLE_EXCEPTIONS | INJ_MM_RUN_DLL_MAIN | INJ_MM_SET_PAGE_PROTECTIONS)

//ansi version of the info structure:
struct INJECTIONDATAA
{
	char			szDllPath[MAX_PATH * 2];	//fullpath to the dll to inject
	DWORD			ProcessID;					//process identifier of the target process
	INJECTION_MODE	Mode;						//injection mode
	LAUNCH_METHOD	Method;						//method to execute the remote shellcode
	DWORD			Flags;						//combination of the flags defined above
	DWORD			Timeout;					//timeout for DllMain return in milliseconds
	DWORD			hHandleValue;				//optional value to identify a handle in a process
	HINSTANCE		hDllOut;					//returned image base of the injection
	bool			GenerateErrorLog;			//if true error data is generated and stored in GH_Inj_Log.txt
};

//unicode version of the info structure (documentation above).
//the additional member szTargetProcessExeFileName should be ignored since it's only used for error logging.
struct INJECTIONDATAW
{
	wchar_t			szDllPath[MAX_PATH * 2];
	wchar_t			szTargetProcessExeFileName[MAX_PATH];	//exe name of the target process, this value gets set automatically and should be initialized with 0s
	DWORD			ProcessID;
	INJECTION_MODE	Mode;
	LAUNCH_METHOD	Method;
	DWORD			Flags;
	DWORD			Timeout;
	DWORD			hHandleValue;
	HINSTANCE		hDllOut;
	bool			GenerateErrorLog;
};

DWORD __stdcall InjectA(INJECTIONDATAA * pData);
DWORD __stdcall InjectW(INJECTIONDATAW * pData);
//Main injection functions (ansi/unicode).
//
//Arguments:
//		pData (INJECTIONDATAA/INJECTIONDATAW):
///			Pointer to the information structure for the injection.
//
//Returnvalue (DWORD):
///		On success: INJ_ERR_SUCCESS.
///		On failure: One of the errorcodes defined in Error.h.

//Internal stuff
#define MAXPATH_IN_TCHAR	MAX_PATH
#define MAXPATH_IN_BYTE_A	MAX_PATH * sizeof(char)
#define MAXPATH_IN_BYTE_W	MAX_PATH * sizeof(wchar_t)
#define MAXPATH_IN_BYTE		MAX_PATH * sizeof(TCHAR)
#define INJ_HIJACK_TIMEOUT	250

//Returns the version string of the current instance
HRESULT __stdcall GetVersionA(char		* out, size_t cb_size);
HRESULT __stdcall GetVersionW(wchar_t	* out, size_t cb_size);

//Returns the state of the symbol download threads
//If finished SYMBOL_ERR_SUCCESS (0) is returned 
//If still in progress INJ_ERR_SYMBOL_INIT_NOT_DONE (0x1C) is returned.
//Other error codes are defined in Error.h.
DWORD __stdcall GetSymbolState();

//Returns the state of the import handler.
//If finished INJ_ERR_SUCCESS (0) is returned.
//If still in progress INJ_ERR_IMPORT_HANDLER_NOT_DONE (0x37) is returned.
//Other error codes are defined in Error.h.
DWORD __stdcall GetImportState();