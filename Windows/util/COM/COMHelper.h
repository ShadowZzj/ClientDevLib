#pragma once
#pragma once
#include <atlbase.h>
#include <wbemidl.h>
#include <comdef.h>
namespace COMHelper {
	//Not support multi-thread, caller must synchronize.
	class COMWrapper {
	public:

		//COM
		HRESULT CreateComInstance(const IID& rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, const IID& riid, LPVOID** ppv);

		static COMWrapper* GetInstance();
		static void DestroyInstance();
	private:
		COMWrapper();
		~COMWrapper();
		COMWrapper(const COMWrapper&) = delete;
		COMWrapper operator=(const COMWrapper&) = delete;

		HRESULT Initialize();
		int UnInitialize();

		bool isCoUninitializeNeeded = false;

		static COMWrapper* comWrapper;
	};

}
