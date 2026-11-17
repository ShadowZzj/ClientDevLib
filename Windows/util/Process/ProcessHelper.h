#pragma once
#include <Windows.h>
#include <General/util/BaseUtil.hpp>
#include <General/util/Lua/LuaExport.hpp>
#include <General/util/zzjErrorEnum.h>
#include <Psapi.h>
#include <TlHelp32.h>
#include <Windows/util/HandleHelper.h>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <General/util/Process/Process.h>
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
    TypeName(const TypeName &);            \
    TypeName &operator=(const TypeName &);

namespace zzj
{
struct ProcessEntry
{
    DWORD ProcessId;
    DWORD ParentProcessId;
    DWORD ThreadCount;
    LONG ThreadPriorityBase;

    std::wstring ExeName;
    std::wstring ExeFullPath;
};

/*
TokenElevationTypeDefault
built-in admin:elevated
guest or account not in admin group : limited token and no linked token

TokenElevationTypeLimited,TokenElevationTypeFull
usually account in admin group
has linked token in same user

see https://stackoverflow.com/questions/50562419/determine-if-user-can-elevate-a-process-with-c
*/
struct ActiveExplorerInfo
{
    DWORD ProcessId;
    DWORD SessionId;

    bool IsElevated;
    bool IsElevated_LinkedToken;
    TOKEN_ELEVATION_TYPE ElevationType;
    TOKEN_ELEVATION_TYPE ElevationType_LinkedToken;
    std::wstring UserSid;
    std::wstring UserName;
    std::wstring DomainName;
};
class ProcessIterator
{
   public:
    typedef std::list<ProcessEntry> ProcessEntries;

    ProcessIterator();
    virtual ~ProcessIterator();

    bool SnapshotAll(ProcessEntries &entries);
    bool SnapshotFilterExeName(ProcessEntries &entries, const wchar_t *exename);

    static bool FindEntryByPid(const ProcessEntries &entries, DWORD pid, ProcessEntry **entry);

   private:
    bool GetFirstEntry(ProcessEntries &entries);
    void InitEntry(PROCESSENTRY32W *pe32);
    void ConvertPE32ToPE(PROCESSENTRY32W *pe32, ProcessEntry *pe);

    HANDLE snapshot_handle;

    DISALLOW_COPY_AND_ASSIGN(ProcessIterator)
};
class EnvHelper
{
   public:
    EnvHelper() { RefreshEnv(); }
    void RefreshEnv();
    DWORD GetEnvVariable(const char *key, char *value, size_t len);
    DWORD GetEnvVariable(const wchar_t *key, wchar_t *value, size_t len);
    std::map<std::string, std::string> GetEnvVariables() { return env; }
    DWORD ExpandEnvVariable(const char *key, char *value, size_t len);
    DWORD ExpandEnvVariable(const wchar_t *key, wchar_t *value, size_t len);
    // if exist, modify. If nonexist, add. If value==NULL, delete.
    BOOL AddEnvVariableNoRefresh(const char *key, const char *value);
    BOOL AddEnvVariableNoRefresh(const wchar_t *key, const wchar_t *value);
    BOOL AddEnvVariable(const char *key, const char *value);
    BOOL AddEnvVariable(const wchar_t *key, const wchar_t *value);

   private:
    std::map<std::string, std::string> env;
};
class Process
{
    friend class Memory;

   public:
    enum class ProcessType : int
    {
        User,
        Admin,
        Service
    };
    class ExitCode
    {
       public:
        DWORD exitCode;
    };
    class Token
    {
       public:
        Token(HANDLE _token) : token(_token) {}
        ~Token()
        {
            if (token != INVALID_HANDLE_VALUE) CloseHandle(token);
        }

        Token(const Token &) = delete;
        Token &operator=(const Token &) = delete;

        Token(Token &&other) noexcept
        {
            token = other.token;
            other.token = INVALID_HANDLE_VALUE;
        }

        Token &operator=(Token &&other) noexcept
        {
            if (this != &other)
            {
                token = other.token;
                other.token = INVALID_HANDLE_VALUE;
            }
            return *this;
        }
        operator HANDLE() { return token; }

       private:
        HANDLE token = INVALID_HANDLE_VALUE;
    };
    static const int INVALID_VAL = -1;
    DWORD GetSessionId();
    DWORD GetProcessId();
    HANDLE GetProcessHandle();
    DWORD GetProcessDirectory(size_t len, char *buf);
    DWORD GetProcessDirectory(size_t len, wchar_t *buf);

    bool SetProcessPriority(DWORD priority);
    bool SetProcessDirectory(const char *dir);
    bool SetProcessDirectory(const wchar_t *dir);
    bool BindProcess(HANDLE handle);
    bool BindProcess(DWORD processId, DWORD deriredAccess);
    bool IsAlive();
    Token GetToken(DWORD desiredAccess = TOKEN_QUERY);
    std::tuple<int, ProcessType> GetProcessType();
    std::tuple<int, bool> IsServiceProcess();
    std::tuple<int, bool> IsAdminProcess();

    uintptr_t GetModuleBaseAddress(const std::string &moduleName);

    static DWORD GetSessionId(DWORD processId);
    static HANDLE GetProcessHandle(DWORD processId, DWORD desiredAccess);
    static std::vector<DWORD> GetProcessId(std::wstring processName);
    static DWORD GetProcessId(HANDLE processHandle);
    static bool IsMutexExist(std::string mutex);
    static std::string GetProcessUserName();

    // If in a user process, this function only can get current process user's explorer info
    // eg. The caller process is admin, then you login to a user named zhuzhengjia and you spawn
    // a process elevated to admin. The function will return false.
    static bool GetActiveExplorerInfo(ActiveExplorerInfo *pinfo);
    // System process create user or admin process.
    static DWORD SystemCreateProcess(const std::wstring &commandLine, bool bElevated, bool bWait,
                                     DWORD dwWaitTime, bool show);
    // System process create with stdout and stderr capture
    static ExitCode SystemCreateProcess(const std::wstring &commandLine, bool bElevated,
                                     std::wstring &stdOutput, std::wstring &stdError);
    // UTF-8 version for convenience
    static ExitCode SystemCreateProcess(const std::string &commandLine, bool bElevated,
                                     std::string &stdOutput, std::string &stdError);
    // Same level as caller.
    static BOOL RegularCreateProcess(std::string path, bool show, std::string cmdLine = "",
                                     bool wait = false, DWORD *errCode = nullptr);
    // Require uac if user process.
    static BOOL AdminCreateProcess(const char *pszFileName, bool show, const char *param,
                                   bool wait = false);
    static BOOL IsProcessAdmin();
    static bool KillProcess(DWORD pid);
    static bool KillProcess(const char *name);
    static std::wstring GetModulePath(std::wstring moduleName = L"");
    static std::vector<Process> GetProcessByName(const std::string &processName);
    Process()
    {
        process = std::make_shared<ScopeKernelHandle>(::GetCurrentProcess());
        processId = ::GetCurrentProcessId();
    }
    Process(HANDLE processHandle) { InitWithHandle(processHandle); }
    Process(DWORD _processId, DWORD deriredAccess)
    {
        if (!InitWithId(_processId, deriredAccess)) CrashMe();
    }
    operator HANDLE() { return *process; }

    EnvHelper envHelper;

   private:
    bool InitWithHandle(HANDLE processHandle)
    {
        process = std::make_shared<ScopeKernelHandle>(processHandle);
        processId = GetProcessId(processHandle);
        return true;
    }
    bool InitWithId(DWORD _processId, DWORD desiredAccess)
    {
        HANDLE processHandle = OpenProcess(desiredAccess, FALSE, _processId);
        if (processHandle == INVALID_HANDLE_VALUE) return false;
        process = std::make_shared<ScopeKernelHandle>(processHandle);
        processId = _processId;
        return true;
    }
    bool IsValid() { return process != nullptr && processId != INVALID_VAL; }
    std::shared_ptr<ScopeKernelHandle> process = nullptr;
    DWORD processId = INVALID_VAL;

   protected:
    DECLARE_LUA_EXPORT(Process)
};

struct MemoryInfo
{
    uintptr_t address;                 // 匹配的地址
    MEMORY_BASIC_INFORMATION memInfo;  // 内存页面信息
};

class Memory
{
   public:
    Memory(const Process &_process) : process(_process) {}
    Memory(const ProcessV2 &_process) { process.InitWithId(_process.pid, PROCESS_ALL_ACCESS); }
    ~Memory() = default;
    bool Read(uintptr_t address, void *buffer, size_t size);
    std::vector<uint8_t> ReadUntilZero(uintptr_t address);
    bool Write(uintptr_t address, const void *buffer, size_t size);
    bool Write(uintptr_t address, std::vector<uint8_t> buf);
    uintptr_t Alloc(DWORD size, DWORD allocationType = MEM_COMMIT | MEM_RESERVE,
                    DWORD protect = PAGE_EXECUTE_READWRITE);
    uintptr_t DeAlloc(uintptr_t address);
    bool Nop(uintptr_t address, size_t size);
    uintptr_t FindMultiPleLevelAddress(uintptr_t baseAddress, std::vector<unsigned int> offsets);
    std::optional<MODULEENTRY32> GetModuleInfo(const std::string &moduleName);
    std::vector<MODULEENTRY32> GetModuleInfos();
    std::vector<MemoryInfo> PatternScan(uintptr_t startAddress, size_t searchSize,
                                        const std::string &pattern);

   private:
    Process process;
};
}  // namespace zzj