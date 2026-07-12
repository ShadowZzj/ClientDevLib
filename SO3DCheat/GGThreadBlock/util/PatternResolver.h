#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace GGTB
{

struct PatternEntry
{
    std::string name;
    std::string pattern;
    uintptr_t fallbackRVA = 0;
    int matchIndex       = 0;
    int offset           = 0;
    bool derefOperand    = false;
    bool resolveRelCall  = false;
    uintptr_t resolved   = 0;
};

// Skeleton pattern resolver mirroring SO3DCheat's, but with NO patterns
// pre-registered — base addresses & signatures are target-specific. Add
// Register(...) calls inside RegisterAll() when integrating with a real
// process.
class PatternResolver
{
  public:
    // hOwnerDll is used to locate the on-disk cache file.
    // targetModule defaults to the host process EXE (GetModuleHandleA(nullptr)).
    static void Init(HMODULE hOwnerDll, const std::string &targetModule = "");

    // Two-phase startup for packed hosts. Prepare only loads cache/fallback
    // addresses and never scans mutable .text. ResolvePatterns performs any
    // required scan after the unpack-complete signal.
    static void Prepare(HMODULE hOwnerDll, const std::string &targetModule = "");
    // allowScan=false finalizes cache/fallback values without touching packed
    // .text (used when the stage-2 sentinel could not be armed safely).
    static void ResolvePatterns(bool allowScan = true);

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
    static std::string targetModule_;
    static std::string moduleMd5_;
    static uintptr_t moduleBase_;
    static uintptr_t moduleSize_;
    static std::vector<PatternEntry> entries_;
    static std::unordered_map<std::string, size_t> nameIndex_;
    static bool prepared_;
    static bool needsScan_;
    static bool initialized_;
};

} // namespace GGTB
