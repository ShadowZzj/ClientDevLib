#pragma once
#include <Windows.h>
namespace zzj
{
namespace AntiDebug
{
static volatile HANDLE tls_callback_thread_event = 0;
static volatile UINT32 tls_callback_thread_data  = 0;

VOID WINAPI tls_callback(PVOID hModule, DWORD dwReason, PVOID pContext);
BOOL TLSCallbackThread();
} // namespace AntiDebug
} // namespace zzj
