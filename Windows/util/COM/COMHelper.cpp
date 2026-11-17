#include "COMHelper.h"
#include <iostream>
#pragma comment(lib, "wbemuuid.lib")
namespace zzj
{
using namespace std;

HRESULT COMWrapper::CreateComInstance(const IID &rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, const IID &riid,
                                      OUT LPVOID **ppv)
{
    HRESULT hres = CoCreateInstance(rclsid, pUnkOuter, dwClsContext, riid, (LPVOID *)ppv);
    if (FAILED(hres))
    {
        cout << "Failed to create IWbemLocator object. "
             << "Error code = 0x" << hex << hres << endl;
        return hres;
    }

    return hres;
}

COMWrapper::COMWrapper()
{
    Initialize();
}
COMWrapper::~COMWrapper()
{
    UnInitialize();
}
HRESULT COMWrapper::Initialize()
{
    HRESULT hres;

    // Initialize COM.
    hres = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (hres == RPC_E_CHANGED_MODE)
        isCoUninitializeNeeded = false;
    else if (FAILED(hres))
    {
        isCoUninitializeNeeded = false;
        cout << "Failed to initialize COM library. "
             << "Error code = 0x" << hex << hres << endl;
        return hres; // Program has failed.
    }
    else
        isCoUninitializeNeeded = true;

    // Initialize
    hres = CoInitializeSecurity(NULL,
                                -1,                          // COM negotiates service
                                NULL,                        // Authentication services
                                NULL,                        // Reserved
                                RPC_C_AUTHN_LEVEL_DEFAULT,   // authentication
                                RPC_C_IMP_LEVEL_IMPERSONATE, // Impersonation
                                NULL,                        // Authentication info
                                EOAC_NONE,                   // Additional capabilities
                                NULL                         // Reserved
    );
    if (FAILED(hres) && hres != RPC_E_TOO_LATE)
    {
        cout << "Failed to initialize security. "
             << "Error code = 0x" << hex << hres << endl;
        CoUninitialize();
        isCoUninitializeNeeded = false;
        return hres; // Program has failed.
    }
    return hres;
}
int COMWrapper::UnInitialize()
{
    if (isCoUninitializeNeeded)
        CoUninitialize();
    return 0;
}
} // namespace zzj