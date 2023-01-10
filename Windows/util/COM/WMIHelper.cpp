#include "WMIHelper.h"
#include <General/util/BaseUtil.hpp>
#include <General/util/StrUtil.h>
#include <iostream>
#pragma comment(lib, "wbemuuid.lib")
namespace zzj
{
using namespace std;

HRESULT WMIWrapper::WMIConnectServer(const wchar_t *serverName)
{
    HRESULT hres;
    hres = pLoc->ConnectServer(_bstr_t(serverName),            // WMI namespace
                               NULL,                           // User name
                               NULL,                           // User password
                               0,                              // Locale
                               WBEM_FLAG_CONNECT_USE_MAX_WAIT, // Security flags
                               0,                              // Authority
                               0,                              // Context object
                               &pSvc                           // IWbemServices proxy
    );

    if (FAILED(hres))
    {
        wcout << L"Could not connect. Error code = 0x" << hex << hres << endl;

        pLoc->Release();
        isIWbemLocatorReleaseNeeded = false;
        pLoc                        = nullptr;
        pSvc                        = nullptr;

        return hres;
    }
   // wcout << L"Connected to " << serverName << L" WMI namespace";

    isIWbemServicesReleaseNeeded = true;

    // Set the IWbemServices proxy so that impersonation
    // of the user (client) occurs.
    hres = CoSetProxyBlanket(

        pSvc,                        // the proxy to set
        RPC_C_AUTHN_WINNT,           // authentication service
        RPC_C_AUTHZ_NONE,            // authorization service
        NULL,                        // Server principal name
        RPC_C_AUTHN_LEVEL_CALL,      // authentication level
        RPC_C_IMP_LEVEL_IMPERSONATE, // impersonation level
        NULL,                        // client identity
        EOAC_NONE                    // proxy capabilities
    );

    if (FAILED(hres))
    {
        cout << "Could not set proxy blanket. Error code = 0x" << hex << hres << endl;
        pSvc->Release();
        isIWbemServicesReleaseNeeded = false;
        pSvc                         = nullptr;

        pLoc->Release();
        isIWbemLocatorReleaseNeeded = false;
        pLoc                        = nullptr;

        return hres; // Program has failed.
    }

    return hres;
}
HRESULT WMIWrapper::WMIExecQuery(const char *sentenceType, const char *sentence)
{
    if (!pSvc || !sentenceType || !sentence)
        return -1;

    HRESULT hres;
    hres = pSvc->ExecQuery(bstr_t(sentenceType), bstr_t(sentence),
                           WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);
    if (FAILED(hres))
    {
        cout << "Query failed. "
             << "Error code = 0x" << hex << hres << endl;
        pSvc->Release();
        isIWbemServicesReleaseNeeded = false;
        pSvc                         = nullptr;

        pLoc->Release();
        isIWbemLocatorReleaseNeeded = false;
        pLoc                        = nullptr;

        return hres; // Program has failed.
    }

    isIEnumWbemClassObjectReleaseNeeded = true;
    return hres;
}
HRESULT WMIWrapper::WMIGetNextObject()
{
    if (!pEnumerator)
        return -1;

    HRESULT hres;
    DWORD uReturn = 0;
    
    hres = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
    if (0 == uReturn)
        return -1;

    return S_OK;
}

HRESULT WMIWrapper::WMIReleaseThisObject()
{
    if (pclsObj)
        pclsObj->Release();
    pclsObj = nullptr;
    return S_OK;
}

HRESULT WMIWrapper::WMIGetProperty(const wchar_t *property, VARIANT &var)
{
    return pclsObj->Get(property, 0, &var, 0, 0);
}

HRESULT WMIWrapper::WMIReleaseProperty(VARIANT &var)
{
    VariantClear(&var);
    return S_OK;
}

HRESULT WMIWrapper::WMIEnumUnInitialize()
{
    if (isIEnumWbemClassObjectReleaseNeeded)
    {
        pEnumerator->Release();
        pEnumerator = nullptr;
    }
    return S_OK;
}
WMIWrapper::WMIWrapper()
{
    comWrapper = COMHelper::COMWrapper::GetInstance();
    Initialize();
}
WMIWrapper::~WMIWrapper()
{
    UnInitialize();
}
HRESULT WMIWrapper::Initialize()
{
    HRESULT hres;
    hres =
        comWrapper->CreateComInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID **)&pLoc);
    if (FAILED(hres))
    {
        std::cout << "CreateComInstance error, crash program";
        CrashMe();
    }
    isIWbemLocatorReleaseNeeded = true;
    return hres;
}
int WMIWrapper::UnInitialize()
{
    WMIEnumUnInitialize();
    if (isIWbemLocatorReleaseNeeded)
    {
        pLoc->Release();
        pLoc = nullptr;
    }
    if (isIWbemServicesReleaseNeeded)
    {
        pSvc->Release();
        pSvc = nullptr;
    }
    return 0;
}
} // namespace zzj