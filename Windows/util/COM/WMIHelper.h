#pragma once
#pragma once
#pragma once
#include "COMHelper.h"
#include <vector>
class Test;
namespace zzj
{
// Not support multi-thread, caller must synchronize.
class WMIWrapper
{
  public:
    friend class Test;

    // WMI
    HRESULT WMIConnectServer(const wchar_t *serverName);
    HRESULT WMIExecQuery(const char *sentenceType, const char *sentence);
    HRESULT WMIGetNextObject();
    HRESULT WMIReleaseThisObject();
    HRESULT WMIGetProperty(const wchar_t *property, VARIANT &var);
    HRESULT WMIReleaseProperty(VARIANT &var);


    WMIWrapper();
    ~WMIWrapper();
    WMIWrapper(const WMIWrapper &) = delete;
    WMIWrapper operator=(const WMIWrapper &) = delete;

  private:
    HRESULT Initialize();
    int UnInitialize();
    HRESULT WMIEnumUnInitialize();

    bool isIWbemLocatorReleaseNeeded         = false;
    bool isIWbemServicesReleaseNeeded        = false;
    bool isIEnumWbemClassObjectReleaseNeeded = false;
    IWbemLocator *pLoc                       = nullptr;
    IWbemServices *pSvc                      = nullptr;
    IEnumWbemClassObject *pEnumerator        = nullptr;
    IWbemClassObject *pclsObj                = nullptr;
    COMHelper::COMWrapper comWrapper;
};

} // namespace zzj

