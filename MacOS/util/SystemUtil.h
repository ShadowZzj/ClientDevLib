#pragma once
#include <string>
#include <vector>

namespace zzj
{
class Computer
{
  public:
    static std::string GetIdentifier();
    static std::string GetUUID();
    static std::string GetCPUBrandString();
    static std::string GetCurrentTimeStamp();
};
class HardDrive
{
  public:
    static std::vector<std::string> GetRootHardDriveUUID();
};
} // namespace zzj