#pragma once
#include <Windows.h>
namespace zzj {
	class ScopeKernelHandle
	{
#define SKH ScopeKernelHandle
	public:
		SKH(HANDLE handle = INVALID_HANDLE_VALUE) :handle(handle) {}
		~SKH() {
			Release();
		}
		operator HANDLE() {
			return handle;
		}
		SKH(ScopeKernelHandle& other) {
			other.StealDataTo(*this);
		}
		SKH& operator=(SKH& other) {
			other.StealDataTo(*this);
			return *this;
		}
		SKH& operator=(HANDLE handle) {
			Release();
			this->handle = handle;
			canClose = true;
			return *this;
		}

		bool SetInherited(bool isInherited);
		bool SetCanClose(bool canClose);
	private:
		bool canClose = true;
		HANDLE handle = INVALID_HANDLE_VALUE;

		void Release() {
			if (canClose && handle != INVALID_HANDLE_VALUE)
				CloseHandle(handle);
		}
		void StealDataTo(SKH& other) {
			other.Release();
			other.handle = handle;
			other.canClose = canClose;
			handle = INVALID_HANDLE_VALUE;
			canClose = TRUE;
		}
	};
}
