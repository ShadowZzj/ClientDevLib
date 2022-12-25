#include "pch.h"
#include "ProcessUtil_win32.h"

#include <Psapi.h>
#include <UserEnv.h>
#include <Tlhelp32.h>
#include "sddl.h"
#pragma comment(lib, "Psapi.lib")
#pragma comment(lib,"Userenv.lib")

#include "ProcessKernelStruct_win32.h"

inline uint64_t FileTimeToTime_t(FILETIME& ft)
{
	ULARGE_INTEGER ui;
	ui.LowPart = ft.dwLowDateTime;
	ui.HighPart = ft.dwHighDateTime;
	return ((uint64_t)(ui.QuadPart - 116444736000000000) / 10000);
}

__declspec(selectany) std::mutex ProcessUtils_Win32::m_instanceLck;

inline ProcessUtils_Win32::ProcessUtils_Win32()
{
	m_inited = false;
	m_NtQuerySystemInformation = nullptr;
	m_NtQueryInformationProcess = nullptr;
	m_NtWow64QueryInformationProcess64 = nullptr;

	m_pfn_IsWow64Process = nullptr;
	m_NtReadVirtualMemory = nullptr;
	m_RtlNtStatusToDosError = nullptr;
	m_NtWow64ReadVirtualMemory64 = nullptr;
}

inline bool ProcessUtils_Win32::_init()
{
	if (m_inited)
		return true;

	HMODULE hNtdll = GetModuleHandle(L"ntdll.dll");
	if (!hNtdll)
		return false;

	m_NtReadVirtualMemory = (PFN_NtReadVirtualMemory)GetProcAddress(hNtdll, "NtReadVirtualMemory");
	m_RtlNtStatusToDosError = (PFN_RtlNtStatusToDosError)GetProcAddress(hNtdll, "RtlNtStatusToDosError");
	m_NtQuerySystemInformation = (PFN_NtQuerySystemInformation)GetProcAddress(hNtdll, "NtQuerySystemInformation");
	m_NtQueryInformationProcess = (PFN_NtQueryInformationProcess)GetProcAddress(hNtdll, "NtQueryInformationProcess");

	if (!m_NtQueryInformationProcess || !m_NtQuerySystemInformation || !m_NtReadVirtualMemory
		|| !m_RtlNtStatusToDosError)
		return false;

	m_pfn_IsWow64Process = (PFN_IsWow64Process)(GetProcAddress(GetModuleHandle(L"kernel32.dll"), "IsWow64Process"));
	m_bCurrent64BitSystem = (m_pfn_IsWow64Process != NULL);
	if (m_bCurrent64BitSystem)
	{
		// 没有导出该函数，肯定不是32位系统；导出了则查看当前系统位数
		SYSTEM_INFO si = {};
		GetNativeSystemInfo(&si);
		if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 || si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_IA64)
		{
			// 是64位系统的话，需要我们获取这些导出函数，以进行命令行信息的获取
			m_NtWow64ReadVirtualMemory64 = (PFN_NtWow64ReadVirtualMemory64)GetProcAddress(hNtdll, "NtWow64ReadVirtualMemory64");
			m_NtWow64QueryInformationProcess64 = (PFN_NtWow64QueryInformationProcess64)GetProcAddress(hNtdll, "NtWow64QueryInformationProcess64");
		}
		else
			m_bCurrent64BitSystem = FALSE;
	}

	m_inited = true;
	return m_inited;
}

inline bool ProcessUtils_Win32::EnablePrivilege(const wchar_t* privilegeName, bool bEnable)
{
	bool result = false;
	HANDLE hToken = NULL;
	do
	{
		int nRetCode = ::OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken);
		if (!nRetCode)
			break;

		TOKEN_PRIVILEGES tkp = { 0 };
		nRetCode = ::LookupPrivilegeValue(NULL, privilegeName, &tkp.Privileges[0].Luid);
		if (!nRetCode)
			break;

		tkp.PrivilegeCount = 1;
		tkp.Privileges[0].Attributes = bEnable ? SE_PRIVILEGE_ENABLED : 0;
		nRetCode = ::AdjustTokenPrivileges(hToken, FALSE, &tkp, sizeof(tkp), NULL, NULL);
		if (!nRetCode)
			break;

		result = true;

	} while (0);

	if (hToken != NULL)	CloseHandle(hToken);
	return result;
}

inline bool ProcessUtils_Win32::getProcessHandleByPid(int64_t processId, HANDLE& processHandle)
{
	if (!_init())
		return false;

	EnablePrivilege(SE_DEBUG_NAME, TRUE);

	DWORD dwAccessArray[] = { PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, PROCESS_QUERY_INFORMATION, PROCESS_QUERY_LIMITED_INFORMATION |
		PROCESS_VM_READ, PROCESS_QUERY_LIMITED_INFORMATION };

	for (uint32_t i = 0; i < sizeof(dwAccessArray) / sizeof(dwAccessArray[0]); i++)
	{
		DWORD dwAccessItem = dwAccessArray[i];
		HANDLE hProcess = OpenProcess(dwAccessItem, FALSE, (DWORD)processId);
		if (hProcess)
		{
			processHandle = hProcess;
			return true;
		}
	}
	return false;
}

inline bool ProcessUtils_Win32::getProcessTimesByHandle(HANDLE hProcess, int64_t& creationDate, int64_t& terminationDate,
	int64_t& kernelModeTime, int64_t& userModeTime)
{
	if (!_init())
		return false;

	FILETIME creationFileTime, exitFileTime, kernelFileTime, userFileTime;
	if (!GetProcessTimes(hProcess, &creationFileTime, &exitFileTime, &kernelFileTime, &userFileTime))
		return false;

	creationDate = FileTimeToTime_t(creationFileTime);
	terminationDate = FileTimeToTime_t(exitFileTime);
	kernelModeTime = FileTimeToTime_t(kernelFileTime);
	userModeTime = FileTimeToTime_t(userFileTime);
	return true;
}

inline bool ProcessUtils_Win32::getProcessThreadCount(uint32_t dwPID, uint32_t& threadCount)
{
	if (!_init())
		return false;

	EnablePrivilege(SE_DEBUG_NAME, TRUE);

	HANDLE hSnapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnapshot == (HANDLE)-1)
		return false;

	PROCESSENTRY32W pe = { 0 };
	pe.dwSize = sizeof(pe);

	BOOL bMore = ::Process32First(hSnapshot, &pe);
	while (bMore)
	{
		if (pe.th32ProcessID == dwPID)
		{
			threadCount = pe.cntThreads;
			break;
		}
		bMore = ::Process32Next(hSnapshot, &pe);
	}

	::CloseHandle(hSnapshot);
	return true;
}

inline bool ProcessUtils_Win32::getProcessCmdLineByHandle(HANDLE hProcess, std::wstring& strCmdLine, uint32_t* errCode)
{
	if (!_init())
		return false;

	PVOID buffer = NULL;
	NTSTATUS status = _queryProcessVariableSize(hProcess, _SELF_::ProcessCommandLineInformation, &buffer);

	if (NT_SUCCESS(status) && buffer)
	{
		PUNICODE_STRING pUnicodeString = (PUNICODE_STRING)buffer;
		strCmdLine = std::wstring(pUnicodeString->Buffer, pUnicodeString->Length / sizeof(wchar_t));
		free(buffer);
		return true;
	}
	else
	{
		BOOL bIsWow64 = FALSE;
		if (m_bCurrent64BitSystem)
			m_pfn_IsWow64Process(hProcess, &bIsWow64);

		try
		{
			// 如果当前不是64 位系统，或者目标进程不是32位进程，不操作
			if (!m_bCurrent64BitSystem || bIsWow64)
				return _getProcessCmdLineByHandleInMemory(hProcess, strCmdLine, errCode);
			else
				return _getProcessCmdLineByHandleInMemory_x64(hProcess, strCmdLine, errCode);
		}
		catch (...)
		{
		}
		return false;
	}
}

inline bool ProcessUtils_Win32::getProcessCurDirByHandle(HANDLE hProcess, std::wstring& strCurDir, uint32_t* errCode)
{
	if (!_init())
		return false;

// 	PVOID buffer = NULL;
// 	NTSTATUS status = _queryProcessVariableSize(hProcess, _SELF_::ProcessCommandLineInformation, &buffer);
// 
// 	if (NT_SUCCESS(status) && buffer)
// 	{
// 		PUNICODE_STRING pUnicodeString = (PUNICODE_STRING)buffer;
// 		strCurDir = std::wstring(pUnicodeString->Buffer, pUnicodeString->Length / sizeof(wchar_t));
// 		free(buffer);
// 		return true;
// 	}
// 	else
	{
		BOOL bIsWow64 = FALSE;
		if (m_bCurrent64BitSystem)
			m_pfn_IsWow64Process(hProcess, &bIsWow64);

		try
		{
			// 如果当前不是64 位系统，或者目标进程不是32位进程，不操作
			if (!m_bCurrent64BitSystem || bIsWow64)
				return _getProcessCurDirByHandleInMemory(hProcess, strCurDir, errCode);
			else
				return _getProcessCurDirByHandleInMemory_x64(hProcess, strCurDir, errCode);
		}
		catch (...)
		{
		}
		return false;
	}
}

inline bool ProcessUtils_Win32::getProcessCmdLineByPid(uint32_t pid, std::wstring& strCmdLine, uint32_t* errCode)
{
	if (!_init())
		return false;

	//system进程的命令行直接写死
	if (pid == SYSTEM_PROCESS_PID)
	{
		strCmdLine = SYSTEM_PROCESS_CMDLINE; 
		return true;
	}

	EnablePrivilege(SE_DEBUG_NAME, TRUE);

	DWORD dwAccessArray[] = { PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, PROCESS_QUERY_INFORMATION, PROCESS_QUERY_LIMITED_INFORMATION |
		PROCESS_VM_READ, PROCESS_QUERY_LIMITED_INFORMATION };

	DWORD dwRet = ERROR_FUNCTION_FAILED;
	for (uint32_t i = 0; i < sizeof(dwAccessArray) / sizeof(dwAccessArray[0]); i++)
	{
		DWORD dwAccessItem = dwAccessArray[i];
		HANDLE hProcess = OpenProcess(dwAccessItem, FALSE, pid);
		if (hProcess)
		{
			bool result = getProcessCmdLineByHandle(hProcess, strCmdLine, errCode);
			CloseHandle(hProcess);
			return result;
		}
	}
	return dwRet;
}

inline bool ProcessUtils_Win32::getProcessCurDirByPid(uint32_t pid, std::wstring& strCurDir, uint32_t* errCode)
{
	if (!_init())
		return false;

	EnablePrivilege(SE_DEBUG_NAME, TRUE);

	DWORD dwAccessArray[] = { PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, PROCESS_QUERY_INFORMATION, PROCESS_QUERY_LIMITED_INFORMATION |
		PROCESS_VM_READ, PROCESS_QUERY_LIMITED_INFORMATION };

	DWORD dwRet = ERROR_FUNCTION_FAILED;
	for (uint32_t i = 0; i < sizeof(dwAccessArray) / sizeof(dwAccessArray[0]); i++)
	{
		DWORD dwAccessItem = dwAccessArray[i];
		HANDLE hProcess = OpenProcess(dwAccessItem, FALSE, pid);
		if (hProcess)
		{
			bool result = getProcessCurDirByHandle(hProcess, strCurDir, errCode);
			CloseHandle(hProcess);
			return result;
		}
	}
	return dwRet;
}

inline NTSTATUS ProcessUtils_Win32::_queryProcessVariableSize(
	_In_ HANDLE ProcessHandle,
	_In_ _SELF_::PROCESSINFOCLASS_SELF ProcessInformationClass,
	_Out_ PVOID* Buffer)
{
	ULONG returnLength = 0;

	NTSTATUS status = m_NtQueryInformationProcess(ProcessHandle, ProcessInformationClass, NULL, 0, &returnLength);
	if (status != STATUS_BUFFER_OVERFLOW && status != STATUS_BUFFER_TOO_SMALL && status != STATUS_INFO_LENGTH_MISMATCH)
		return status;

	PVOID buffer = malloc(returnLength);
	if (!buffer)
		return ERROR_OUTOFMEMORY;

	status = m_NtQueryInformationProcess(ProcessHandle, ProcessInformationClass, buffer, returnLength, &returnLength);
	if (NT_SUCCESS(status))
	{
		*Buffer = buffer;
		return status;
	}
	else
	{
		free(buffer);
		return status;
	}
}

inline bool ProcessUtils_Win32::_getProcessCmdLineByHandleInMemory(HANDLE hProcess, std::wstring& strCmdLine, uint32_t* errCode)
{
	// 读入目标进程的进程环境块
	PROCESS_BASIC_INFORMATION pbi = {};
	NTSTATUS status = m_NtQueryInformationProcess(hProcess, ProcessBasicInformation, (PVOID)&pbi, sizeof(pbi), NULL);
	if (!NT_SUCCESS(status))
	{
		if (errCode) *errCode = status;
		return false;
	}

	try
	{
		// ProcessParameters
		SIZE_T dwReturn = 0;
		PRTL_USER_PROCESS_PARAMETERS pParam = NULL;
		if (ReadProcessMemory(hProcess, (LPVOID)((ULONG_PTR)pbi.PebBaseAddress + OFFSET_OF(PEB, ProcessParameters)), (LPVOID)&pParam, 
			sizeof(pParam), &dwReturn) && nullptr != pParam)
		{
			// ProcessParameters.CommandLine
			UNICODE_STRING unicode_string = {};
			if (ReadProcessMemory(hProcess, (PVOID)((ULONG_PTR)pParam + OFFSET_OF(RTL_USER_PROCESS_PARAMETERS, CommandLine)), 
				(LPVOID)&unicode_string, sizeof(unicode_string), &dwReturn))
			{
				USHORT usLength = unicode_string.Length;
				if (usLength > 0)
				{
					std::unique_ptr<unsigned char> buffer(new unsigned char[usLength]);
					if (ReadProcessMemory(hProcess, unicode_string.Buffer, (PVOID)buffer.get(), usLength, &dwReturn))
					{
						strCmdLine = std::wstring((wchar_t*)buffer.get(), usLength/sizeof(wchar_t));
						return true;
					}
				}
			}

			//ProcessCurrentDirectory

		}
	}
	catch (...)
	{
	}
	return false;
}

inline bool ProcessUtils_Win32::_getProcessCmdLineByHandleInMemory_x64(HANDLE hProcess, std::wstring& strCmdLine, uint32_t* errCode)
{
	if (!m_NtWow64QueryInformationProcess64) 
		return false;

	PROCESS_BASIC_INFORMATION64 pbi = {};
	NTSTATUS status = m_NtWow64QueryInformationProcess64(hProcess, ProcessBasicInformation, &pbi, sizeof(pbi), NULL);
	if (!NT_SUCCESS(status))
	{
		if (errCode) *errCode = status;
		return false;
	}

	try
	{
		ULONGLONG dwReturn = 0;
		PRTL_USER_PROCESS_PARAMETERS64 pParam = NULL;
		if (NT_SUCCESS(m_NtWow64ReadVirtualMemory64(hProcess, pbi.PebBaseAddress + OFFSET_OF(PEB64, ProcessParameters), &pParam, 
			sizeof(pParam), &dwReturn) && pParam))
		{
			UNICODE_STRING64 unicode_string64 = {};
			if (NT_SUCCESS(m_NtWow64ReadVirtualMemory64(hProcess, (ULONGLONG)pParam + +OFFSET_OF(RTL_USER_PROCESS_PARAMETERS64, CommandLine), 
				&unicode_string64, sizeof(unicode_string64), &dwReturn)))
			{
				USHORT usLength = unicode_string64.Length;
				if (usLength > 0)
				{
					std::unique_ptr<unsigned char> buffer(new unsigned char[usLength]);
					if (NT_SUCCESS(m_NtWow64ReadVirtualMemory64(hProcess, unicode_string64.Buffer, (PVOID)buffer.get(), usLength, &dwReturn)))
					{
						strCmdLine = std::wstring((wchar_t*)buffer.get(), usLength / sizeof(wchar_t));
						return true;
					}
				}
			}
		}
	}
	catch (...)
	{
	}
	return false;
}

inline bool ProcessUtils_Win32::_getProcessCurDirByHandleInMemory(HANDLE hProcess, std::wstring& strCurDir, uint32_t* errCode /* = nullptr */)
{
	// 读入目标进程的进程环境块
	PROCESS_BASIC_INFORMATION pbi = {};
	NTSTATUS status = m_NtQueryInformationProcess(hProcess, ProcessBasicInformation, (PVOID)&pbi, sizeof(pbi), NULL);
	if (!NT_SUCCESS(status))
	{
		if (errCode) *errCode = status;
		return false;
	}

	try
	{
		// ProcessParameters
		SIZE_T dwReturn = 0;
		PRTL_USER_PROCESS_PARAMETERS pParam = NULL;
		if (ReadProcessMemory(hProcess, (LPVOID)((ULONG_PTR)pbi.PebBaseAddress + OFFSET_OF(PEB, ProcessParameters)), &pParam,
			sizeof(pParam), &dwReturn) && pParam)
		{
			// _RTL_USER_PROCESS_PARAMETERS.DosPath
			UNICODE_STRING unicode_string = {};
			if (ReadProcessMemory(hProcess, (PVOID)((ULONG_PTR)pParam + OFFSET_OF(__RTL_USER_PROCESS_PARAMETERS, DosPath)),
				&unicode_string, sizeof(unicode_string), &dwReturn))
			{
				USHORT usLength = unicode_string.Length;
				if (usLength > 0)
				{
					std::unique_ptr<unsigned char> buffer(new unsigned char[usLength]);
					if (ReadProcessMemory(hProcess, unicode_string.Buffer, (PVOID)buffer.get(), usLength, &dwReturn))
					{
						strCurDir = std::wstring((wchar_t*)buffer.get(), usLength / sizeof(wchar_t));
						return true;
					}
				}
			}
		}
	}
	catch (...)
	{
	}
	return false;
}

inline bool ProcessUtils_Win32::_getProcessCurDirByHandleInMemory_x64(HANDLE hProcess, std::wstring& strCurDir, uint32_t* errCode /* = nullptr */)
{
	if (!m_NtWow64QueryInformationProcess64)
		return false;

	PROCESS_BASIC_INFORMATION64 pbi = {};
	NTSTATUS status = m_NtWow64QueryInformationProcess64(hProcess, ProcessBasicInformation, &pbi, sizeof(pbi), NULL);
	if (!NT_SUCCESS(status))
	{
		if (errCode) *errCode = status;
		return false;
	}

	try
	{
		ULONGLONG dwReturn = 0;
		PRTL_USER_PROCESS_PARAMETERS64 pParam = NULL;
		if (NT_SUCCESS(m_NtWow64ReadVirtualMemory64(hProcess, pbi.PebBaseAddress + OFFSET_OF(PEB64, ProcessParameters), &pParam,
			sizeof(pParam), &dwReturn) && pParam))
		{
			UNICODE_STRING64 unicode_string64 = {};
			if (NT_SUCCESS(m_NtWow64ReadVirtualMemory64(hProcess, (ULONGLONG)pParam + +OFFSET_OF(__RTL_USER_PROCESS_PARAMETERS64, DosPath),
				&unicode_string64, sizeof(unicode_string64), &dwReturn)))
			{
				USHORT usLength = unicode_string64.Length;
				if (usLength > 0)
				{
					std::unique_ptr<unsigned char> buffer(new unsigned char[usLength]);
					if (NT_SUCCESS(m_NtWow64ReadVirtualMemory64(hProcess, unicode_string64.Buffer, (PVOID)buffer.get(), usLength, &dwReturn)))
					{
						strCurDir = std::wstring((wchar_t*)buffer.get(), usLength / sizeof(wchar_t));
						return true;
					}
				}
			}
		}
	}
	catch (...)
	{
	}
	return false;
}

inline void _FreeTokenInfo(PVOID pTokenInfo)
{
	free(pTokenInfo);
}

inline bool _IsSidEqual(WELL_KNOWN_SID_TYPE Type, PSID pSid)
{
	bool bEqual = false;

	DWORD SidSize = SECURITY_MAX_SID_SIZE;
	PSID pSidNew = LocalAlloc(LMEM_FIXED, SidSize);
	if (NULL == pSidNew)
		goto Exit0;

	if (!CreateWellKnownSid(Type, NULL, pSidNew, &SidSize))
		goto Exit0;

	if (!::EqualSid(pSid, pSidNew))
		goto Exit0;

	bEqual = true;

Exit0:
	if (pSidNew)
		LocalFree(pSidNew);

	return bEqual;
}

inline BOOL _GetTokenInfo(HANDLE hToken, TOKEN_INFORMATION_CLASS Tic, PVOID* pTokenInfoOut, DWORD* pdwOutSize)
{
	DWORD dwSize = 0;
	PVOID pv = NULL;
	BOOL  bRetCode = TRUE;

	if (NULL == pTokenInfoOut)
		bRetCode = FALSE;
	else if (!GetTokenInformation(hToken, Tic, 0, 0, &dwSize) && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
		bRetCode = FALSE;
	else if (NULL == (pv = malloc(dwSize)))
		bRetCode = FALSE;
	else if (!GetTokenInformation(hToken, Tic, pv, dwSize, &dwSize))
	{
		free(pv);
		*pTokenInfoOut = 0;
		bRetCode = FALSE;
	}
	else
	{
		bRetCode = TRUE;
		*pTokenInfoOut = pv;

		if (pdwOutSize)
			*pdwOutSize = dwSize;
	}
	return bRetCode;
}

inline int _GetProcessMandatoryLevel(HANDLE hToken, std::string& integrityLevel)
{
	int Level = 0;
	PTOKEN_MANDATORY_LABEL pTML = NULL;
	DWORD dwSize = 0;
	if (!_GetTokenInfo(hToken, TokenIntegrityLevel, (void**)&pTML, &dwSize))
	{
		Level = WinHighLabelSid;
		goto Exit0;
	}

	typedef struct _MANDATORY_LEVEL_DATA
	{
		WELL_KNOWN_SID_TYPE	Type;
		int	Level;
		const char* levelString;
	} MANDATORY_LEVEL_DATA;

	static const MANDATORY_LEVEL_DATA MandatoryLevel[] =
	{
		{WinHighLabelSid, WinHighLabelSid, "High"},
		{WinSystemLabelSid, WinSystemLabelSid, "System"},
		{WinMediumLabelSid, WinMediumLabelSid, "Medium"},
		{WinLowLabelSid, WinLowLabelSid, "Low"}
	};

	for (int i = 0; i < sizeof(MandatoryLevel) / sizeof(MANDATORY_LEVEL_DATA); i++)
	{
		if (!_IsSidEqual(MandatoryLevel[i].Type, pTML->Label.Sid))
			continue;

		Level = MandatoryLevel[i].Level;
		integrityLevel = MandatoryLevel[i].levelString;

		break;
	}

Exit0:
	if (pTML)
		_FreeTokenInfo(pTML);

	return Level;
}

inline bool _GetProcessUserName(HANDLE hToken, std::wstring& userName, std::wstring& userDomainName, std::wstring& logonSid)
{
	bool bResult = false;
	PTOKEN_USER pTokenUser = nullptr;
	LPWSTR StringSid = NULL;
	do
	{
		DWORD dwSize = 0;
		if (!_GetTokenInfo(hToken, TokenUser, (void**)&pTokenUser, &dwSize))
			break;

		DWORD dwNameBufferSize = 256;
		DWORD dwDomainBufferSize = 256;

		wchar_t szUserName[256] = { 0 };
		wchar_t szDomainName[256] = { 0 };

		if(!ConvertSidToStringSid(pTokenUser->User.Sid, &StringSid))
			break;

		logonSid = StringSid;

		SID_NAME_USE snu;
		if (!LookupAccountSid(NULL, pTokenUser->User.Sid, szUserName, &dwNameBufferSize, szDomainName, &dwDomainBufferSize, &snu) != 0)
			break;

		userName = szUserName;
		userDomainName = szDomainName;

		bResult = true;

	} while (false);

	if (pTokenUser)
		_FreeTokenInfo(pTokenUser);
	if (StringSid)
		LocalFree(StringSid);
	
	return bResult;
}

inline bool _GetProcessSession(HANDLE hToken, uint32_t& processSessionId)
{
	DWORD dwReturnLen = 0;
	DWORD dwTokenSessionId = 0;
	if (!::GetTokenInformation(hToken, TokenSessionId, &dwTokenSessionId, sizeof(DWORD), &dwReturnLen))
		return false;

	processSessionId = dwTokenSessionId;
	return true;
}

inline bool ProcessUtils_Win32::getProcessIntegrityLevel(HANDLE hProcess, std::string& integrityLevel)
{
	if (!_init())
		return false;

	bool result = false;
	HANDLE hProcessToken = NULL;
	do
	{
		if (!OpenProcessToken(hProcess, TOKEN_QUERY, &hProcessToken))
			break;

		_GetProcessMandatoryLevel(hProcessToken, integrityLevel);
		result = true;

	} while (false);

	if (hProcessToken)
		CloseHandle(hProcessToken);

	return result;
}

inline bool ProcessUtils_Win32::_DosPathToNtPath(wchar_t* pszDosPath, wchar_t* pszNtPath)
{
	if (!pszDosPath || !pszNtPath)
		return false;

	wchar_t szDriveStr[500] = { 0 };
	//获取本地磁盘字符串
	if (GetLogicalDriveStrings(sizeof(szDriveStr), szDriveStr))
	{
		for (int32_t i = 0; szDriveStr[i]; i += 4)
		{
			if (!lstrcmpi(&(szDriveStr[i]), L"A:\\") || !lstrcmpi(&(szDriveStr[i]), L"B:\\"))
				continue;

			wchar_t	szDrive[3] = { 0 };
			szDrive[0] = szDriveStr[i];
			szDrive[1] = szDriveStr[i + 1];
			szDrive[2] = '\0';

			wchar_t	szDevName[100] = { 0 };
			//查询 Dos 设备名
			if (!QueryDosDevice(szDrive, szDevName, 100))
				return false;

			int32_t cchDevName = lstrlen(szDevName);
			if (_wcsnicmp(pszDosPath, szDevName, cchDevName) == 0)
			{
				//复制驱动器和路径
				lstrcpy(pszNtPath, szDrive);
				lstrcat(pszNtPath, pszDosPath + cchDevName);
				return true;
			}
		}
	}
	lstrcpy(pszNtPath, pszDosPath);
	return false;
}

inline bool ProcessUtils_Win32::getProcessFullPathByPid(uint32_t dwPID, std::wstring& processFullPath)
{
	if (!_init())
		return false;

	HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, 0, dwPID);
	if (!hProcess)
		return false;

	bool result = getProcessFullPathByHandle(hProcess, processFullPath);
	CloseHandle(hProcess);
	return result;
}

inline bool ProcessUtils_Win32::getProcessFullPathByHandle(HANDLE hProcess, std::wstring& processFullPath)
{
	if (!_init())
		return false;

	wchar_t szImagePath[MAX_PATH] = { 0 };
	if (!GetProcessImageFileName(hProcess, szImagePath, MAX_PATH))
		return false;

	wchar_t szFullPath[MAX_PATH * 2] = { 0 };
	if (!_DosPathToNtPath(szImagePath, szFullPath))
		return false;

	processFullPath = szFullPath;
	return true;
}


inline bool ProcessUtils_Win32::getProcessUserAndSessionInfosByHandle(HANDLE hProcess, std::wstring& userName,
	std::wstring& userDomainName, uint32_t& processSessionId, std::wstring& logonSid)
{
	if (NULL == hProcess || INVALID_HANDLE_VALUE == hProcess)
		return false;

	if (!_init())
		return false;

	bool result = false;
	HANDLE hProcessToken = NULL;
	do
	{
		if (!OpenProcessToken(hProcess, TOKEN_QUERY, &hProcessToken))
			break;

		_GetProcessUserName(hProcessToken, userName, userDomainName, logonSid);
		_GetProcessSession(hProcessToken, processSessionId);
		result = true;

	} while (false);

	if (hProcessToken)
		CloseHandle(hProcessToken);

	return result;
}
inline bool ProcessUtils_Win32::getProcessParentPidByPid( int64_t processId , int64_t& parentpid )
{
    if ( !_init( ) )
        return false;

    EnablePrivilege( SE_DEBUG_NAME , TRUE );

    HANDLE hSnapshot = ::CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS , 0 );
    if ( hSnapshot == ( HANDLE ) -1 )
        return false;

    PROCESSENTRY32W pe = { 0 };
    pe.dwSize = sizeof( pe );

    BOOL bMore = ::Process32First( hSnapshot , &pe );
    while ( bMore )
    {
        if ( pe.th32ProcessID == processId )
        {
			parentpid = pe.th32ParentProcessID;
            break;
        }
        bMore = ::Process32Next( hSnapshot , &pe );
    }

    ::CloseHandle( hSnapshot );
    return true;
}
