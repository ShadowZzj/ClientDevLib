#pragma once
#include <General/util/Software.h>
namespace zzj
{
class SoftInfoManager : SoftInfoManagerInterface
{
  public:
    virtual int GetInstalledSoftware(std::vector<SoftInfo> &softInfos);
    SoftInfoManager()
    {
    }
    ~SoftInfoManager()
    {
    }
    DECLARE_LUA_EXPORT(SoftInfoManager)
};
}; // namespace zzj