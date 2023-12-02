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
    typedef struct KernelVersion
    {
        short int major;
        short int minor;
        short int patch;
    } KernelVersion;

    static void GetKernelVersion(KernelVersion *k);

    /* compare the given os version with the one installed returns:
    0 if equals the installed version
    positive value if less than the installed version
    negative value if more than the installed version
    */
    static int CompareKernelVersion(KernelVersion v);

    // lowerBound <= currentVersion < upperBound
    static bool KernelVersionIsBetween(KernelVersion lowerBound, KernelVersion upperBound);

    static double CalculateNanosecondsPerMachTick(void);
};
class HardDrive
{
  public:
    static std::vector<std::string> GetRootHardDriveUUID();
};
} // namespace zzj