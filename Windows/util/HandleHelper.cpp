#include "HandleHelper.h"
using namespace zzj;

bool ScopeKernelHandle::SetInherited(bool isInherited){
	if (handle == INVALID_HANDLE_VALUE)
		return false;
	return SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
}

bool ScopeKernelHandle::SetCanClose(bool canClose)
{
	this->canClose = canClose;
	if(canClose)
		return SetHandleInformation(handle, HANDLE_FLAG_PROTECT_FROM_CLOSE, 0);
	else
		return SetHandleInformation(handle, HANDLE_FLAG_PROTECT_FROM_CLOSE, HANDLE_FLAG_PROTECT_FROM_CLOSE);
}
