#pragma once
#include <string>
#include <vector>
namespace zzj
{
class CPU
{
  public:
    static std::wstring GetCPUBrandString();
};

class HardDrive
{
  public:
    static std::vector<std::wstring> GetHardDriveSerialNumber();
};

class Bios
{
  public:
    static std::wstring GetBiosSerialNumber();
};
}