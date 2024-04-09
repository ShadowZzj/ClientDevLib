#pragma once
#include <Windows.h>
#include <string>
#include <boost/filesystem.hpp>
#include <Windows/util/ApiLoader/WinStruct.h>
#include <tuple>
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
		static bool IsHandleValid(HANDLE handle) {
			return handle && handle != INVALID_HANDLE_VALUE;
		}
		static std::string GetHandleType(HANDLE handle);
		static boost::filesystem::path GetFileHandlePath(HANDLE handle);

		
		static std::vector<std::tuple<SYSTEM_HANDLE_TABLE_ENTRY_INFO, HANDLE>> FindIf(std::function<bool(const SYSTEM_HANDLE_TABLE_ENTRY_INFO&,const HANDLE&)> func);
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
