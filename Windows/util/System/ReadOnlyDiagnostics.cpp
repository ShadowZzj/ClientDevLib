#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <General/util/System/ReadOnlyDiagnostics.h>
#include <Windows/util/Process/ProcessHelper.h>
#include <Windows/util/StartUp/StartUp.h>
#include <Windows/util/System/SystemHelper.h>
#include <Psapi.h>
#include <algorithm>
#include <array>

namespace zzj
{
namespace
{
void Check(const DiagnosticCheck &check) { if (check) check(); }
[[noreturn]] void Fail(const char *operation)
{
    throw DiagnosticError("UNAVAILABLE", std::string(operation) + " failed (Win32 " + std::to_string(GetLastError()) + ")");
}
std::string Utf8(const std::wstring &value)
{
    if (value.empty()) return {};
    const auto count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(count, 0);
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}
uint64_t Ticks(FILETIME value) { return (uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime; }
}
std::vector<ProcessSummary> ReadOnlyDiagnostics::Processes(const DiagnosticCheck &check)
{
    Check(check);
    ProcessIterator iterator;
    ProcessIterator::ProcessEntries entries;
    if (!iterator.SnapshotAll(entries)) Fail("Process snapshot");
    std::vector<ProcessSummary> result;
    for (const auto &entry : entries)
    {
        Check(check);
        result.push_back({entry.ProcessId, entry.ParentProcessId, entry.ThreadCount, Utf8(entry.ExeName)});
    }
    return result;
}
ProcessDetails ReadOnlyDiagnostics::Process(uint32_t pid, const DiagnosticCheck &check)
{
    const auto processes = Processes(check);
    const auto found = std::find_if(processes.begin(), processes.end(), [pid](const auto &p) { return p.pid == pid; });
    if (found == processes.end()) throw DiagnosticError("NOT_FOUND", "Process is no longer present");
    ProcessDetails result; result.process = *found;
    const auto handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!handle) { result.partial = true; return result; }
    struct Close { HANDLE value; ~Close() { CloseHandle(value); } } close{handle};
    Check(check);
    std::array<wchar_t, 32768> path{}; DWORD size = static_cast<DWORD>(path.size());
    if (QueryFullProcessImageNameW(handle, 0, path.data(), &size)) result.executable = Utf8(std::wstring(path.data(), size));
    else result.partial = true;
    FILETIME created{}, ended{}, kernel{}, user{};
    if (GetProcessTimes(handle, &created, &ended, &kernel, &user))
    {
        result.cpuTimeMs = (Ticks(kernel) + Ticks(user)) / 10000;
        result.startedAtMs = (Ticks(created) - 116444736000000000ULL) / 10000;
    }
    else result.partial = true;
    PROCESS_MEMORY_COUNTERS memory{};
    if (GetProcessMemoryInfo(handle, &memory, sizeof(memory))) result.residentBytes = memory.WorkingSetSize;
    else result.partial = true;
    return result;
}
MachineInfo ReadOnlyDiagnostics::Machine()
{
    MachineInfo result; result.platform = "windows";
    const auto version = SystemInfo::GetWindowsVersion();
    if (!version) throw DiagnosticError("UNAVAILABLE", "Windows version unavailable");
    result.version = std::to_string(version->major) + "." + std::to_string(version->minor) + "." +
                     std::to_string(version->buildnumber & 0xffff);
    std::array<wchar_t, 256> name{}; DWORD length = static_cast<DWORD>(name.size());
    if (!GetComputerNameW(name.data(), &length)) Fail("Computer name");
    result.hostname = Utf8(std::wstring(name.data(), length));
    SYSTEM_INFO system{}; GetNativeSystemInfo(&system);
    result.architecture = system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x86_64" :
        system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? "arm64" :
        system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL ? "x86" : "unknown";
    result.logicalProcessors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    result.uptimeMs = GetTickCount64();
    return result;
}
MemoryInfoSnapshot ReadOnlyDiagnostics::Memory()
{
    MEMORYSTATUSEX state{}; state.dwLength = sizeof(state);
    if (!GlobalMemoryStatusEx(&state)) Fail("Memory status");
    return {state.ullTotalPhys, state.ullAvailPhys, false};
}
CpuCounters ReadOnlyDiagnostics::Cpu()
{
    FILETIME idle{}, kernel{}, user{};
    if (!GetSystemTimes(&idle, &kernel, &user)) Fail("CPU times");
    return {Ticks(user), Ticks(kernel) - Ticks(idle), Ticks(idle), 0, false,
        GetActiveProcessorGroupCount() > 1 ? "primary_processor_group" : "system"};
}
std::vector<DiskInfoSnapshot> ReadOnlyDiagnostics::Disks(const DiagnosticCheck &check)
{
    const auto drives = GetLogicalDrives();
    if (!drives) Fail("Logical drives");
    std::vector<DiskInfoSnapshot> result;
    for (int i = 0; i < 26; ++i)
    {
        Check(check);
        if (!(drives & (1u << i))) continue;
        const wchar_t path[] = {static_cast<wchar_t>(L'A' + i), L':', L'\\', 0};
        if (GetDriveTypeW(path) != DRIVE_FIXED) continue;
        ULARGE_INTEGER total{}, free{}, available{};
        if (!GetDiskFreeSpaceExW(path, &available, &total, &free)) Fail("Disk capacity");
        std::array<wchar_t, 64> filesystem{};
        GetVolumeInformationW(path, nullptr, 0, nullptr, nullptr, nullptr, filesystem.data(), static_cast<DWORD>(filesystem.size()));
        result.push_back({Utf8(path), Utf8(filesystem.data()), total.QuadPart, free.QuadPart, available.QuadPart});
    }
    return result;
}
std::vector<ServiceInfoSnapshot> ReadOnlyDiagnostics::Services(const DiagnosticCheck &check)
{
    Check(check);
    auto [error, services] = StartUp().GetServiceStartUp();
    if (error) throw DiagnosticError("UNAVAILABLE", "Service enumeration unavailable");
    std::vector<ServiceInfoSnapshot> result;
    for (const auto &service : services)
    {
        Check(check);
        // Deliberately omit binaryPath (can contain credentials), account and launch arguments.
        result.push_back({service.name, service.displayName, service.status, service.startType});
    }
    return result;
}
}
