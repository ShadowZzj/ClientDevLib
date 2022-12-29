#ifndef _Process_Kernel_Struct_win32_h
#define _Process_Kernel_Struct_win32_h

#include <winternl.h>

#define MAX_UNICODE_PATH 32767L
#define STATUS_BUFFER_OVERFLOW           ((NTSTATUS)0x80000005L)
#define STATUS_BUFFER_TOO_SMALL          ((NTSTATUS)0xC0000023L)
#define STATUS_INFO_LENGTH_MISMATCH      ((NTSTATUS)0xC0000004L)

#define OFFSET_OF(x,y) (ULONG_PTR)(&(((x*)0)->y)) - 0

typedef BOOL(WINAPI* PFN_IsWow64Process) (HANDLE, PBOOL);

typedef ULONG(WINAPI* PFN_RtlNtStatusToDosError)(NTSTATUS Status);

typedef int (WINAPI* PFN_NtReadVirtualMemory) (HANDLE, ULONG, PVOID, ULONG, PULONG);

typedef int (WINAPI* PFN_NtWow64ReadVirtualMemory64) (HANDLE, ULONGLONG, PVOID, ULONGLONG, PULONGLONG);

typedef LONG(WINAPI* PFN_NtQueryInformationProcess)(HANDLE, UINT, PVOID, ULONG, ULONG*);

typedef LONG(WINAPI* PFN_NtWow64QueryInformationProcess64)(HANDLE, UINT, PVOID, ULONG, PULONG);

typedef NTSTATUS(NTAPI* pfnNtQueryInformationProcess)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);

typedef ULONG PPS_POST_PROCESS_INIT_ROUTINE_TYPE;

// Used in PEB struct
typedef struct _smPEB_LDR_DATA_TYPE {
	BYTE Reserved1[8];
	PVOID Reserved2[3];
	LIST_ENTRY InMemoryOrderModuleList;
} PEB_LDR_DATA_TYPE, * PPEB_LDR_DATA_TYPE;

// Used in PEB struct
typedef struct _smRTL_USER_PROCESS_PARAMETERS_TYPE {
	BYTE Reserved1[16];
	PVOID Reserved2[10];
	UNICODE_STRING ImagePathName;
	UNICODE_STRING CommandLine;
} RTL_USER_PROCESS_PARAMETERS_TYPE, * PRTL_USER_PROCESS_PARAMETERS_TYPE;

typedef struct _smPEB {
	BYTE Reserved1[2];
	BYTE BeingDebugged;
	BYTE Reserved2[1];
	PVOID Reserved3[2];
	PPEB_LDR_DATA_TYPE Ldr;
	PRTL_USER_PROCESS_PARAMETERS_TYPE ProcessParameters;
	BYTE Reserved4[104];
	PVOID Reserved5[52];
	PPS_POST_PROCESS_INIT_ROUTINE_TYPE PostProcessInitRoutine;
	BYTE Reserved6[128];
	PVOID Reserved7[1];
	ULONG SessionId;
} smPEB, * smPPEB;

typedef struct _smPROCESS_BASIC_INFORMATION {
	LONG ExitStatus;
	PPEB PebBaseAddress;
	ULONG_PTR AffinityMask;
	LONG BasePriority;
	ULONG_PTR UniqueProcessId;
	ULONG_PTR InheritedFromUniqueProcessId;
} smPROCESS_BASIC_INFORMATION, * smPPROCESS_BASIC_INFORMATION;

typedef struct _UNICODE_STRING64 {
	USHORT Length;
	USHORT MaximumLength;
	DWORD  Reserved1;
	ULONG64  Buffer;
} UNICODE_STRING64;

typedef struct _PROCESS_BASIC_INFORMATION64 {
	ULONG64 Reserved1;
	ULONG64 PebBaseAddress;
	ULONG64 Reserved2[2];
	ULONG64 UniqueProcessId;
	ULONG64 Reserved3;
} PROCESS_BASIC_INFORMATION64;

// 读入目标进程的进程环境块
typedef struct _RTL_USER_PROCESS_PARAMETERS64 {
	BYTE				Reserved1[16];
	ULONGLONG			Reserved2[10];
	UNICODE_STRING64	ImagePathName;
	UNICODE_STRING64	CommandLine;
} RTL_USER_PROCESS_PARAMETERS64, * PRTL_USER_PROCESS_PARAMETERS64;

typedef struct _PEB64 {
	BYTE Reserved1[2];
	BYTE BeingDebugged;
	BYTE Reserved2[21];
	ULONGLONG LoaderData;
	RTL_USER_PROCESS_PARAMETERS64 ProcessParameters;
	// 下面的不重要
} PEB64;

#endif