#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace SO3D
{

struct PatternEntry
{
    std::string name;
    std::string pattern;
    uintptr_t fallbackRVA = 0;
    int matchIndex         = 0;
    int offset             = 0;
    bool derefOperand      = false; // read 4 bytes at (match+offset) to get the actual address
    bool resolveRelCall    = false; // resolve E8/E9 relative call/jmp: target = (match+offset) + 4 + *(int32_t*)(match+offset)
    uintptr_t resolved     = 0;
};

class PatternResolver
{
  public:
    static void Init(HMODULE hCheatDll);
    static uintptr_t Get(const std::string &name);
    static uintptr_t GetModuleBase();
    static uintptr_t GetModuleSize();

  private:
    static void RegisterAll();
    static void ScanAll();
    static bool LoadCache();
    static void SaveCache();
    static void Register(const std::string &name, const std::string &pattern,
                         uintptr_t fallbackRVA, int matchIndex = 0, int offset = 0,
                         bool derefOperand = false, bool resolveRelCall = false);

    static std::string cachePath_;
    static std::string exeMd5_;
    static uintptr_t moduleBase_;
    static uintptr_t moduleSize_;
    static std::vector<PatternEntry> entries_;
    static std::unordered_map<std::string, size_t> nameIndex_;
    static bool initialized_;
};

} // namespace SO3D
