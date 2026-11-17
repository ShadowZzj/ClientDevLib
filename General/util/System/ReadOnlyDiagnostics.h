#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

namespace zzj
{
struct DiagnosticError : std::runtime_error
{
    std::string code;
    DiagnosticError(std::string code, const std::string &message) : std::runtime_error(message), code(std::move(code)) {}
};
// Optional checkpoint lets callers enforce their own deadline/cancellation between OS queries.
using DiagnosticCheck = std::function<void()>;
struct ProcessSummary
{
    uint32_t pid = 0, parentPid = 0, threads = 0;
    std::string name;
};
struct ProcessDetails
{
    ProcessSummary process;
    std::string executable;
    std::optional<uint64_t> residentBytes, cpuTimeMs, startedAtMs;
    bool partial = false; // Process exited or some OS queries were denied.
};
struct MachineInfo
{
    std::string platform, version, architecture, hostname;
    uint32_t logicalProcessors = 0;
    uint64_t uptimeMs = 0;
};
struct MemoryInfoSnapshot
{
    uint64_t totalBytes = 0, availableBytes = 0;
    bool availableIsEstimate = false;
};
struct CpuCounters
{
    uint64_t user = 0, system = 0, idle = 0, nice = 0;
    bool wraps32 = false;
    std::string scope = "system";
};
struct DiskInfoSnapshot
{
    std::string mount, filesystem;
    uint64_t totalBytes = 0, freeBytes = 0, availableBytes = 0;
};
struct ServiceInfoSnapshot
{
    std::string name, displayName, state, startType;
};
class ReadOnlyDiagnostics
{
  public:
    static std::vector<ProcessSummary> Processes(const DiagnosticCheck &check = {});
    static ProcessDetails Process(uint32_t pid, const DiagnosticCheck &check = {});
    static MachineInfo Machine();
    static MemoryInfoSnapshot Memory();
    static CpuCounters Cpu();
    // Local fixed disks / local mounts only. No caller-provided filesystem paths.
    static std::vector<DiskInfoSnapshot> Disks(const DiagnosticCheck &check = {});
    static std::vector<ServiceInfoSnapshot> Services(const DiagnosticCheck &check = {});
};
}
