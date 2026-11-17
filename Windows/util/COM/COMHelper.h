#pragma once
#pragma once
#include <atlbase.h>
#include <comdef.h>
#include <wbemidl.h>
namespace zzj
{
// Not support multi-thread, caller must synchronize.
class COMWrapper
{
  public:
    HRESULT CreateComInstance(const IID &rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, const IID &riid,
                              LPVOID **ppv);
    COMWrapper();
    ~COMWrapper();
    COMWrapper(const COMWrapper &)           = delete;
    COMWrapper operator=(const COMWrapper &) = delete;
    bool IsInitialized()
    {
        return isCoUninitializeNeeded;
    }

  private:
    HRESULT Initialize();
    int UnInitialize();

    bool isCoUninitializeNeeded = false;
};

} // namespace zzj
