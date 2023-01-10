#pragma once
#include <Windows.h>
#include <PathCch.h>

#include <General/util/BaseUtil.hpp>

#define SIG_DLR(ApiName) \
typedef decltype(ApiName)* Sig_##ApiName;

#define API_DLR(ApiName) \
Sig_##ApiName Dyn##ApiName=nullptr;

#define API_LOAD(ApiName) \
Dyn##ApiName =(Sig_##ApiName)GetProcAddress(h,#ApiName);\
if(Dyn##ApiName==NULL) \
	{zzj::CrashMe();}

#define KERNEL32_API_LIST(V) \
V(IsWow64Process) \
V(RtlCaptureContext) \
V(SetProcessDEPPolicy) \
V(SetDllDirectory) \
V(SetDefaultDllDirectories) \
V(SetProcessMitigationPolicy)

SIG_DLR(IsWow64Process)
SIG_DLR(RtlCaptureContext)
SIG_DLR(SetProcessDEPPolicy)
SIG_DLR(SetDllDirectory)
SIG_DLR(SetDefaultDllDirectories)
SIG_DLR(SetProcessMitigationPolicy)



void InitDynCalls();