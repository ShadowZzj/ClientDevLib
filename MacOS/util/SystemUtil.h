#ifndef _MAC_COMPUTER_H_
#define _MAC_COMPUTER_H_

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
    static std::string GetActiveConsoleSessionId();
    static std::string GetActiveConsoleGroupId();
    static std::string GetCurrentTimeStamp();
    static std::string GetCurrentUserName();
    static std::string GetCurrentUserHomeDir();
};
class HardDrive
{
  public:
    static std::vector<std::string> GetRootHardDriveUUID();
};
} // namespace zzj

#endif