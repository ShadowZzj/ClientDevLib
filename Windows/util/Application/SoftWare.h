#pragma once
#include <General/util/Application/Software.h>
namespace zzj
{
class SoftInfoManager : SoftInfoManagerInterface
{
  public:
    virtual std::tuple < int, std::vector<SoftInfo>> GetInstalledSoftware() override;
    ~SoftInfoManager()
    {
    }
    DECLARE_LUA_EXPORT(SoftInfoManager)
};
}; // namespace zzj