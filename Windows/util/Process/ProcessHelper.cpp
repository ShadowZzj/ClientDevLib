#include "ProcessHelper.h"
#include <General/util/BaseUtil.hpp>
#include <General/util/StrUtil.h>
#include <UserEnv.h>
#include <sddl.h>
#include <spdlog/spdlog.h>
#include <string>
#include <strsafe.h>
#include <vector>
#include <windows.h>
using namespace zzj;
#pragma comment(lib, "Userenv.lib")
#pragma comment(lib, "Wtsapi32.lib")
DWORD Process::GetSessionId()
{
    DWORD processId = GetProcessId();
    DWORD sessionId = GetSessionId(processId);
    return sessionId;
}

DWORD Process::GetProcessId() { return processId; }

HANDLE Process::GetProcessHandle() { return *process; }
DWORD Process::GetSessionId(DWORD processId)
{
    DWORD sessionId;
    if (ProcessIdToSessionId(processId, &sessionId))
        return sessionId;
    else
        return INVALID_VAL;
}

bool Process::IsMutexExist(std::string mutex)
{
    std::wstring userNameWide = str::utf82w(mutex.c_str());
    HANDLE hMutex = CreateMutexW(NULL, false, userNameWide.c_str());
    DWORD err = GetLastError();
    if (err == ERROR_ALREADY_EXISTS) return true;
    return false;
}

std::string zzj::Process::GetProcessUserName()
{
    char *buf = nullptr;
    DWORD bufSize = 0;
    std::string ret = "";
    if (!GetUserNameA(buf, &bufSize) && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
    {
        buf = (char *)malloc(bufSize);
        if (!GetUserNameA(buf, &bufSize))
            free(buf);
        else
        {
            ret = buf;
            free(buf);
        }
    }

    return ret;
}

HANDLE zzj::Process::GetProcessHandle(DWORD processId, DWORD desiredAccess)
{
    return OpenProcess(desiredAccess, FALSE, processId);
}

std::vector<DWORD> Process::GetProcessId(std::wstring processName)
{
    std::vector<DWORD> pids;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (INVALID_HANDLE_VALUE == hSnapshot) return std::vector<DWORD>();

    PROCESSENTRY32 pe = {sizeof(pe)};
    for (BOOL ret = Process32First(hSnapshot, &pe); ret; ret = Process32Next(hSnapshot, &pe))
    {
        if (std::wstring(pe.szExeFile) == processName.c_str()) pids.push_back(pe.th32ProcessID);
    }
    CloseHandle(hSnapshot);
    return pids;
}

DWORD Process::GetProcessId(HANDLE processHandle)
{
    DWORD ret = ::GetProcessId(processHandle);
    if (ret)
        return ret;
    else
        return INVALID_VAL;
}

DWORD Process::GetProcessDirectory(size_t len, char *buf)
{
    if (!IsValid())
        return GetCurrentDirectoryA(len, buf);
    else
    {
        // TODO
        return INVALID_VAL;
    }
}

DWORD Process::GetProcessDirectory(size_t len, wchar_t *buf)
{
    if (!IsValid())
        return GetCurrentDirectoryW(len, buf);
    else
    {
        // TODO
        return INVALID_VAL;
    }
}

bool zzj::Process::SetProcessPriority(DWORD priority)
{
    return SetPriorityClass(*process, priority);
}

bool Process::SetProcessDirectory(const char *dir)
{
    if (!IsValid())
        return SetCurrentDirectoryA(dir);
    else
    {
        // TODO
        return INVALID_VAL;
    }
}
bool Process::SetProcessDirectory(const wchar_t *dir)
{
    if (!IsValid())
        return SetCurrentDirectoryW(dir);
    else
    {
        // TODO
        return INVALID_VAL;
    }
}

bool zzj::Process::BindProcess(HANDLE handle) { return InitWithHandle(handle); }

bool zzj::Process::BindProcess(DWORD processId, DWORD deriredAccess)
{
    return InitWithId(processId, deriredAccess);
}
bool zzj::Process::IsAlive()
{
    DWORD exitCode;
    if (GetExitCodeProcess(*process, &exitCode)) return exitCode == STILL_ACTIVE;
    return false;
}
std::tuple<int, zzj::Process::ProcessType> zzj::Process::GetProcessType()
{
    if (auto [err, is_admin] = IsAdminProcess(); err == 0)
    {
        if (is_admin)
        {
            return {0, ProcessType::Admin};
        }

        if (auto [err2, is_service] = IsServiceProcess(); err2 == 0)
        {
            if (is_service)
            {
                return {0, ProcessType::Service};
            }
            return {0, ProcessType::User};
        }
        else
            return {-1, ProcessType::User};
    }
    else
        return {-1, ProcessType::User};
}
std::tuple<int, bool> zzj::Process::IsServiceProcess()
{
    bool is_service = false;
    HANDLE token = NULL;
    PTOKEN_USER token_user = NULL;
    DWORD return_length = 0;

    if (!OpenProcessToken(*process, TOKEN_QUERY, &token))
    {
        return {-1, is_service};
    }

    GetTokenInformation(token, TokenUser, NULL, 0, &return_length);
    token_user = (PTOKEN_USER)malloc(return_length);

    if (GetTokenInformation(token, TokenUser, token_user, return_length, &return_length))
    {
        LPWSTR string_sid = NULL;

        if (ConvertSidToStringSidW(token_user->User.Sid, &string_sid))
        {
            is_service = wcsstr(string_sid, L"S-1-5-18") != NULL;
            LocalFree(string_sid);
        }
    }

    if (token_user)
    {
        free(token_user);
    }

    if (token)
    {
        CloseHandle(token);
    }

    return {0, is_service};
}
std::tuple<int, bool> zzj::Process::IsAdminProcess()
{
    BOOL is_admin = FALSE;
    SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;
    PSID administrators_group = NULL;

    if (AllocateAndInitializeSid(&nt_authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &administrators_group))
    {
        bool isSuccess = CheckTokenMembership(*process, administrators_group, &is_admin);
        if (!isSuccess)
        {
            FreeSid(administrators_group);
            return {-1, false};
        }
    }

    return {0, is_admin};
}
uintptr_t zzj::Process::GetModuleBaseAddress(const std::string &moduleName)
{
    uintptr_t moduleBaseAddress = 0;
    std::wstring wmoduleName = zzj::str::utf82w(moduleName);
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, processId);
    if (INVALID_HANDLE_VALUE == hSnapshot) return 0;

    MODULEENTRY32 me = {sizeof(me)};
    for (BOOL ret = Module32First(hSnapshot, &me); ret; ret = Module32Next(hSnapshot, &me))
    {
        if (me.szModule == wmoduleName)
        {
            moduleBaseAddress = (uintptr_t)me.modBaseAddr;
            break;
        }
    }

    return moduleBaseAddress;
}
void EnvHelper::RefreshEnv()
{
    env.clear();
    wchar_t *envStr = GetEnvironmentStringsW();
    if (!envStr) return;

    wchar_t *str = envStr;
    long prePos = 0;
    while (*str)
    {
        /* key=value */
        std::wstring tmp(str);
        long i = tmp.find(L"=");
        if (i > 0)
        {
            std::wstring key = tmp.substr(0, i);
            std::wstring val = tmp.substr(i + 1, tmp.length());
            env[zzj::str::w2utf8(key)] = zzj::str::w2utf8(val);
        }

        size_t len = wcslen(str);
        str += len + 1;
    }
    FreeEnvironmentStringsW(envStr);

    return;
}

DWORD EnvHelper::GetEnvVariable(const char *key, char *value, size_t len)
{
    DWORD requireLen = GetEnvironmentVariableA(key, value, 0);
    if (len <= requireLen) return requireLen;

    GetEnvironmentVariableA(key, value, len);
    return 0;
}

DWORD EnvHelper::GetEnvVariable(const wchar_t *key, wchar_t *value, size_t len)
{
    DWORD requireLen = GetEnvironmentVariableW(key, value, 0);
    if (len <= requireLen) return requireLen;

    GetEnvironmentVariableW(key, value, len);
    return 0;
}

DWORD EnvHelper::ExpandEnvVariable(const char *key, char *value, size_t len)
{
    DWORD requireLen = ExpandEnvironmentStringsA(key, NULL, 0);
    if (len <= requireLen) return requireLen;

    ExpandEnvironmentStringsA(key, value, len);
    return 0;
}

DWORD EnvHelper::ExpandEnvVariable(const wchar_t *key, wchar_t *value, size_t len)
{
    DWORD requireLen = ExpandEnvironmentStringsW(key, NULL, 0);
    if (len <= requireLen) return requireLen;

    ExpandEnvironmentStringsW(key, value, len);
    return 0;
}

BOOL EnvHelper::AddEnvVariableNoRefresh(const char *key, const char *value)
{
    return SetEnvironmentVariableA(key, value);
}
BOOL EnvHelper::AddEnvVariableNoRefresh(const wchar_t *key, const wchar_t *value)
{
    return SetEnvironmentVariableW(key, value);
}
BOOL EnvHelper::AddEnvVariable(const char *key, const char *value)
{
    bool ret = AddEnvVariableNoRefresh(key, value);
    RefreshEnv();
    return ret;
}
BOOL EnvHelper::AddEnvVariable(const wchar_t *key, const wchar_t *value)
{
    bool ret = AddEnvVariableNoRefresh(key, value);
    RefreshEnv();
    return ret;
}

bool ProcessIterator::FindEntryByPid(const ProcessEntries &entries, DWORD pid, ProcessEntry **entry)
{
    for (auto &item : entries)
    {
        if (item.ProcessId == pid)
        {
            *entry = (ProcessEntry *)&item;
            return true;
        }
    }
    return false;
}

ProcessIterator::ProcessIterator() : snapshot_handle(INVALID_HANDLE_VALUE)
{
    snapshot_handle = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
}

ProcessIterator::~ProcessIterator()
{
    if (INVALID_HANDLE_VALUE != snapshot_handle) ::CloseHandle(snapshot_handle);
}

bool ProcessIterator::SnapshotAll(ProcessEntries &entries)
{
    if (INVALID_HANDLE_VALUE == snapshot_handle) return false;

    if (!GetFirstEntry(entries)) return false;

    PROCESSENTRY32W pe32w;
    InitEntry(&pe32w);
    while (::Process32NextW(snapshot_handle, &pe32w))
    {
        ProcessEntry pe;
        ConvertPE32ToPE(&pe32w, &pe);
        entries.push_back(pe);

        InitEntry(&pe32w);
    }

    return true;
}

bool ProcessIterator::SnapshotFilterExeName(ProcessEntries &entries, const wchar_t *exename)
{
    if (INVALID_HANDLE_VALUE == snapshot_handle) return false;

    if (!GetFirstEntry(entries)) return false;

    // first entry is not equal to exename
    if (0 != _wcsicmp(exename, entries.back().ExeName.c_str()))
    {
        entries.pop_back();
    }

    PROCESSENTRY32W pe32w;
    InitEntry(&pe32w);
    while (::Process32NextW(snapshot_handle, &pe32w))
    {
        // ensure null terminated
        pe32w.szExeFile[MAX_PATH - 1] = 0;
        if (0 == _wcsicmp(exename, pe32w.szExeFile))
        {
            ProcessEntry pe;
            ConvertPE32ToPE(&pe32w, &pe);
            entries.push_back(pe);
        }

        InitEntry(&pe32w);
    }

    return true;
}

bool ProcessIterator::GetFirstEntry(ProcessEntries &entries)
{
    PROCESSENTRY32W pe32w;
    InitEntry(&pe32w);

    if (!::Process32FirstW(snapshot_handle, &pe32w)) return false;

    ProcessEntry pe;
    // get first entry
    ConvertPE32ToPE(&pe32w, &pe);
    entries.push_back(pe);

    return true;
}

void ProcessIterator::InitEntry(PROCESSENTRY32W *pe32)
{
    memset(pe32, 0, sizeof(PROCESSENTRY32W));
    pe32->dwSize = sizeof(PROCESSENTRY32W);
}

void ProcessIterator::ConvertPE32ToPE(PROCESSENTRY32W *pe32, ProcessEntry *pe)
{
    if (nullptr == pe32 || nullptr == pe) return;

    pe->ProcessId = pe32->th32ProcessID;
    pe->ParentProcessId = pe32->th32ParentProcessID;
    pe->ThreadCount = pe32->cntThreads;
    pe->ThreadPriorityBase = pe32->pcPriClassBase;

    // ensure null terminated
    pe32->szExeFile[MAX_PATH - 1] = 0;

    pe->ExeName = pe32->szExeFile;

    HANDLE hProcess = ::OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pe32->th32ProcessID);
    if (NULL != hProcess)
    {
        wchar_t ProcessPath[1024] = {0};
        DWORD dwSize = 1023;
        if (::QueryFullProcessImageNameW(hProcess, 0, ProcessPath, &dwSize))
        {
            pe->ExeFullPath = ProcessPath;
        }

        ::CloseHandle(hProcess);
    }
}

bool getTokenInfo(HANDLE hToken, TOKEN_INFORMATION_CLASS infotype, std::vector<char> &tokeninfobuf)
{
    DWORD dwTokenInfoLen = 0;
    if (!::GetTokenInformation(hToken, infotype, NULL, 0, &dwTokenInfoLen))
    {
        if (dwTokenInfoLen > 0)
        {
            tokeninfobuf.resize(static_cast<size_t>(dwTokenInfoLen), 0);
            memset(&tokeninfobuf[0], 0, static_cast<size_t>(dwTokenInfoLen));
            if (::GetTokenInformation(hToken, infotype, reinterpret_cast<LPVOID>(&tokeninfobuf[0]),
                                      dwTokenInfoLen, &dwTokenInfoLen))
            {
                return true;
            }
        }
    }
    return false;
}

bool GetActiveExplorerTokenInfo(DWORD pid, ActiveExplorerInfo *pinfo)
{
    bool bRet = false;
    HANDLE hToken = NULL;
    HANDLE hProcess = NULL;

    do
    {
        hProcess = ::OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (NULL == hProcess)
        {
            break;
        }
        if (!::OpenProcessToken(hProcess, TOKEN_ALL_ACCESS, &hToken))
        {
            break;
        }

        // init token info
        pinfo->ElevationType = TokenElevationTypeDefault;
        pinfo->ElevationType_LinkedToken = TokenElevationTypeDefault;
        pinfo->IsElevated = false;
        pinfo->IsElevated_LinkedToken = false;

        // get token user info
        std::vector<char> TokenInfoBuf(5, 0);
        if (getTokenInfo(hToken, TokenUser, TokenInfoBuf))
        {
            PTOKEN_USER pTU = reinterpret_cast<PTOKEN_USER>(&TokenInfoBuf[0]);
            LPWSTR lpwSid = NULL;
            if (::ConvertSidToStringSidW(pTU->User.Sid, &lpwSid))
            {
                pinfo->UserSid = lpwSid;

                // get user name and domain name
                DWORD dwUserNameLen = 0;
                DWORD dwDomainNameLen = 0;
                SID_NAME_USE sidType = SidTypeUnknown;
                ::LookupAccountSidW(NULL, pTU->User.Sid, NULL, &dwUserNameLen, NULL,
                                    &dwDomainNameLen, &sidType);
                if (dwUserNameLen > 0)
                {
                    std::vector<wchar_t> username_buf(static_cast<size_t>(dwUserNameLen + 2), 0);
                    std::vector<wchar_t> domainname_buf(static_cast<size_t>(dwDomainNameLen + 2),
                                                        0);
                    if (::LookupAccountSidW(NULL, pTU->User.Sid, &username_buf[0], &dwUserNameLen,
                                            &domainname_buf[0], &dwDomainNameLen, &sidType))
                    {
                        pinfo->UserName = reinterpret_cast<const wchar_t *>(&username_buf[0]);
                        pinfo->DomainName = reinterpret_cast<const wchar_t *>(&domainname_buf[0]);
                    }
                }

                ::LocalFree(lpwSid);
            }
        }

        if (getTokenInfo(hToken, TokenElevationType, TokenInfoBuf))
        {
            PTOKEN_ELEVATION_TYPE pti = reinterpret_cast<PTOKEN_ELEVATION_TYPE>(&TokenInfoBuf[0]);
            pinfo->ElevationType = *pti;
        }
        if (getTokenInfo(hToken, TokenElevation, TokenInfoBuf))
        {
            PTOKEN_ELEVATION pti = reinterpret_cast<PTOKEN_ELEVATION>(&TokenInfoBuf[0]);
            if (0 != pti->TokenIsElevated)
            {
                pinfo->IsElevated = true;
            }
        }

        if (getTokenInfo(hToken, TokenLinkedToken, TokenInfoBuf))
        {
            PTOKEN_LINKED_TOKEN pti = reinterpret_cast<PTOKEN_LINKED_TOKEN>(&TokenInfoBuf[0]);

            std::vector<char> TokenInfoBuf_lt(5, 0);
            if (getTokenInfo(pti->LinkedToken, TokenElevationType, TokenInfoBuf_lt))
            {
                PTOKEN_ELEVATION_TYPE pti_lt =
                    reinterpret_cast<PTOKEN_ELEVATION_TYPE>(&TokenInfoBuf_lt[0]);
                pinfo->ElevationType_LinkedToken = *pti_lt;
            }
            if (getTokenInfo(pti->LinkedToken, TokenElevation, TokenInfoBuf_lt))
            {
                PTOKEN_ELEVATION pti_lt = reinterpret_cast<PTOKEN_ELEVATION>(&TokenInfoBuf_lt[0]);
                if (0 != pti_lt->TokenIsElevated)
                {
                    pinfo->IsElevated_LinkedToken = true;
                }
            }

            ::CloseHandle(pti->LinkedToken);
        }

        bRet = true;
    } while (false);

    if (NULL != hToken)
    {
        ::CloseHandle(hToken);
    }
    if (NULL != hProcess)
    {
        ::CloseHandle(hProcess);
    }

    return bRet;
}

bool GetActiveExplorerProcess(DWORD *pid, DWORD *sessionid)
{
    // check active session
    // sessionId = 0 is the system process and service session
    DWORD dwActiveConsoleSessionId = ::WTSGetActiveConsoleSessionId();
    if (0xffffffff == dwActiveConsoleSessionId || 0 == dwActiveConsoleSessionId)
    {
        return false;
    }

    // get active explorer.exe process
    bool bFind = false;
    DWORD dwActiveExplorerPid = 0;
    ProcessIterator pi;
    ProcessIterator::ProcessEntries pe;
    if (!pi.SnapshotFilterExeName(pe, L"explorer.exe"))
    {
        return false;
    }
    if (pe.size() == 0)
    {
        return false;
    }
    for (auto &peinfo : pe)
    {
        DWORD dwSessionId = 0;
        if (::ProcessIdToSessionId(peinfo.ProcessId, &dwSessionId))
        {
            if (dwSessionId == dwActiveConsoleSessionId)
            {
                bFind = true;
                dwActiveExplorerPid = peinfo.ProcessId;

                break;
            }
        }
    }
    if (!bFind)
    {
        return false;
    }

    *pid = dwActiveExplorerPid;
    *sessionid = dwActiveConsoleSessionId;
    return true;
}

bool Process::GetActiveExplorerInfo(ActiveExplorerInfo *pinfo)
{
    // LsaEnumerateLogonSessions,LsaGetLogonSessionData
    // we can use the functions above to get Logon info.

    DWORD explorer_pid = 0;
    DWORD explorer_sessionid = 0;
    if (!GetActiveExplorerProcess(&explorer_pid, &explorer_sessionid))
    {
        return false;
    }

    if (!GetActiveExplorerTokenInfo(explorer_pid, pinfo))
    {
        return false;
    }

    pinfo->ProcessId = explorer_pid;
    pinfo->SessionId = explorer_sessionid;

    return true;
}

bool AdjustPrivilege(LPCWSTR lpwPrivilegeName, bool bEnable)
{
    bool bRet = false;
    HANDLE hToken = NULL;
    if (::OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken))
    {
        TOKEN_PRIVILEGES tp = {0};
        tp.PrivilegeCount = 1;
        if (::LookupPrivilegeValueW(NULL, lpwPrivilegeName, &tp.Privileges[0].Luid))
        {
            tp.Privileges[0].Attributes = bEnable ? SE_PRIVILEGE_ENABLED : 0;
            if (::AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, NULL))
            {
                if (ERROR_SUCCESS == ::GetLastError())
                {
                    bRet = true;
                }
            }
        }

        ::CloseHandle(hToken);
    }

    return bRet;
}
DWORD Process::SystemCreateProcess(const std::wstring &commandLine, bool bElevated, bool bWait,
                                   DWORD dwWaitTime, bool show)
{
    DWORD dwPid = 0;
    HANDLE hActiveUserProcess = NULL;
    HANDLE hActiveUserProcessToken = NULL;
    HANDLE hCreateProcessToken = NULL;
    LPVOID lpEnvironment = NULL;

    if (!AdjustPrivilege(SE_INCREASE_QUOTA_NAME, true)) return dwPid;
    if (!AdjustPrivilege(SE_ASSIGNPRIMARYTOKEN_NAME, true)) return dwPid;

    size_t BuffCount = commandLine.size() + 1;
    WCHAR *szCommanline = new WCHAR[BuffCount];
    if (szCommanline == nullptr) return dwPid;
    memset(szCommanline, 0, BuffCount * sizeof(WCHAR));
    memcpy(szCommanline, commandLine.c_str(), (BuffCount - 1) * sizeof(WCHAR));

    do
    {
        ActiveExplorerInfo aei;
        if (!GetActiveExplorerInfo(&aei)) break;

        hActiveUserProcess = ::OpenProcess(PROCESS_ALL_ACCESS, false, aei.ProcessId);
        if (NULL == hActiveUserProcess) break;
        if (!::OpenProcessToken(hActiveUserProcess, TOKEN_ALL_ACCESS, &hActiveUserProcessToken))
            break;

        bool bTokenTempNeedClose = false;
        HANDLE hTokenTemp = NULL;
        if (bElevated)
        {
            if (TokenElevationTypeDefault == aei.ElevationType && aei.IsElevated)
            {
                hTokenTemp = hActiveUserProcessToken;
            }
            else if (TokenElevationTypeFull == aei.ElevationType && aei.IsElevated)
            {
                hTokenTemp = hActiveUserProcessToken;
            }
            else if (TokenElevationTypeFull == aei.ElevationType_LinkedToken &&
                     aei.IsElevated_LinkedToken)
            {
                std::vector<char> TokenInfoBuf(5, 0);
                if (getTokenInfo(hActiveUserProcessToken, TokenLinkedToken, TokenInfoBuf))
                {
                    PTOKEN_LINKED_TOKEN pti =
                        reinterpret_cast<PTOKEN_LINKED_TOKEN>(&TokenInfoBuf[0]);
                    hTokenTemp = pti->LinkedToken;
                    bTokenTempNeedClose = true;
                }
            }
        }
        else
        {
            hTokenTemp = hActiveUserProcessToken;
        }
        if (NULL == hTokenTemp)
        {
            break;
        }

        BOOL bDup = ::DuplicateTokenEx(hTokenTemp, TOKEN_ALL_ACCESS, NULL, SecurityIdentification,
                                       TokenPrimary, &hCreateProcessToken);
        if (bTokenTempNeedClose && NULL != hTokenTemp)
        {
            ::CloseHandle(hTokenTemp);
        }
        if (!bDup)
        {
            break;
        }

        if (!::CreateEnvironmentBlock(&lpEnvironment, hActiveUserProcessToken, FALSE)) break;

        DWORD dwCreationFlags =
            NORMAL_PRIORITY_CLASS | CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT;
        STARTUPINFOW siw = {0};
        siw.cb = sizeof(STARTUPINFOW);
        siw.lpDesktop = (LPWSTR)L"winsta0\\default";
        siw.dwFlags = STARTF_USESHOWWINDOW;
        if (show)
            siw.wShowWindow = SW_SHOW;
        else
            siw.wShowWindow = SW_HIDE;

        PROCESS_INFORMATION pi = {0};

        if (!::CreateProcessAsUserW(hCreateProcessToken, NULL, szCommanline, NULL, NULL, FALSE,
                                    dwCreationFlags, lpEnvironment, NULL, &siw, &pi))
        {
            break;
        }

        if (bWait)
        {
            ::WaitForSingleObject(pi.hProcess, dwWaitTime);
        }
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);

        dwPid = pi.dwProcessId;

    } while (false);

    delete[] szCommanline;

    AdjustPrivilege(SE_ASSIGNPRIMARYTOKEN_NAME, false);
    AdjustPrivilege(SE_INCREASE_QUOTA_NAME, false);

    if (NULL != hActiveUserProcess) ::CloseHandle(hActiveUserProcess);
    if (NULL != hActiveUserProcessToken) ::CloseHandle(hActiveUserProcessToken);
    if (NULL != hCreateProcessToken) ::CloseHandle(hCreateProcessToken);
    if (NULL != lpEnvironment) ::DestroyEnvironmentBlock(lpEnvironment);

    return dwPid;
}

Process::ExitCode Process::SystemCreateProcess(const std::wstring &commandLine, bool bElevated,
                                   std::wstring &stdOutput, std::wstring &stdError)
{
    DWORD dwPid = 0;
    HANDLE hActiveUserProcess = NULL;
    HANDLE hActiveUserProcessToken = NULL;
    HANDLE hCreateProcessToken = NULL;
    LPVOID lpEnvironment = NULL;

    HANDLE hStdOutRead = NULL;
    HANDLE hStdOutWrite = NULL;
    HANDLE hStdErrRead = NULL;
    HANDLE hStdErrWrite = NULL;

    SECURITY_ATTRIBUTES sa = {0};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    // 使用DEFER确保所有资源在函数结束时被释放
    DEFER
    {
        if (hStdOutRead) CloseHandle(hStdOutRead);
        if (hStdOutWrite) CloseHandle(hStdOutWrite);
        if (hStdErrRead) CloseHandle(hStdErrRead);
        if (hStdErrWrite) CloseHandle(hStdErrWrite);
        if (hActiveUserProcess) ::CloseHandle(hActiveUserProcess);
        if (hActiveUserProcessToken) ::CloseHandle(hActiveUserProcessToken);
        if (hCreateProcessToken) ::CloseHandle(hCreateProcessToken);
        if (lpEnvironment) ::DestroyEnvironmentBlock(lpEnvironment);
    };

    // 创建管道
    if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0)) throw std::runtime_error("CreatePipe failed");

    if (!CreatePipe(&hStdErrRead, &hStdErrWrite, &sa, 0)) throw std::runtime_error("CreatePipe failed");

    if (!SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0)) throw std::runtime_error("SetHandleInformation failed");

    if (!SetHandleInformation(hStdErrRead, HANDLE_FLAG_INHERIT, 0)) throw std::runtime_error("SetHandleInformation failed");

    // 获取活动的资源管理器信息
    ActiveExplorerInfo aei;
    if (!GetActiveExplorerInfo(&aei)) throw std::runtime_error("GetActiveExplorerInfo failed");

    hActiveUserProcess = ::OpenProcess(PROCESS_ALL_ACCESS, false, aei.ProcessId);
    if (NULL == hActiveUserProcess) throw std::runtime_error("OpenProcess failed");

    if (!::OpenProcessToken(hActiveUserProcess, TOKEN_ALL_ACCESS, &hActiveUserProcessToken))
        throw std::runtime_error("OpenProcessToken failed");

    // 处理Token
    bool bTokenTempNeedClose = false;
    HANDLE hTokenTemp = NULL;
    if (bElevated)
    {
        if (TokenElevationTypeDefault == aei.ElevationType && aei.IsElevated)
        {
            hTokenTemp = hActiveUserProcessToken;
        }
        else if (TokenElevationTypeFull == aei.ElevationType && aei.IsElevated)
        {
            hTokenTemp = hActiveUserProcessToken;
        }
        else if (TokenElevationTypeFull == aei.ElevationType_LinkedToken &&
                 aei.IsElevated_LinkedToken)
        {
            std::vector<char> TokenInfoBuf(5, 0);
            if (getTokenInfo(hActiveUserProcessToken, TokenLinkedToken, TokenInfoBuf))
            {
                PTOKEN_LINKED_TOKEN pti = reinterpret_cast<PTOKEN_LINKED_TOKEN>(&TokenInfoBuf[0]);
                hTokenTemp = pti->LinkedToken;
                bTokenTempNeedClose = true;
            }
        }
    }
    else
    {
        hTokenTemp = hActiveUserProcessToken;
    }

    if (NULL == hTokenTemp) throw std::runtime_error("DuplicateTokenEx failed");

    // 使用DEFER确保临时token被关闭
    DEFER
    {
        if (bTokenTempNeedClose && NULL != hTokenTemp)
        {
            ::CloseHandle(hTokenTemp);
        }
    };

    BOOL bDup = ::DuplicateTokenEx(hTokenTemp, TOKEN_ALL_ACCESS, NULL, SecurityIdentification,
                                   TokenPrimary, &hCreateProcessToken);
    if (!bDup) throw std::runtime_error("DuplicateTokenEx failed");

    DWORD dwCreationFlags = NORMAL_PRIORITY_CLASS |  CREATE_UNICODE_ENVIRONMENT;
    dwCreationFlags |= CREATE_NO_WINDOW;

    STARTUPINFOA siw = {0};
    siw.cb = sizeof(STARTUPINFOA);
    siw.dwFlags = STARTF_USESTDHANDLES;
    siw.hStdOutput = hStdOutWrite;
    siw.hStdError = hStdErrWrite;
    siw.lpDesktop = (LPSTR) "winsta0\\default";
    siw.wShowWindow = SW_HIDE;

    // 创建环境块
    if (!::CreateEnvironmentBlock(&lpEnvironment, hActiveUserProcessToken, FALSE)) throw std::runtime_error("CreateEnvironmentBlock failed");

    PROCESS_INFORMATION pi = {0};
    std::string cmd = zzj::str::w2ansi(commandLine);

    // 创建进程
    if (!::CreateProcessAsUserA(hCreateProcessToken, NULL, (LPSTR)cmd.c_str(), NULL, NULL, TRUE,
                                dwCreationFlags, lpEnvironment, NULL, &siw, &pi))
    {
        throw std::runtime_error("CreateProcessAsUserA failed");
    }

    DEFER
    {
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
    };

    CloseHandle(hStdOutWrite);
    hStdOutWrite = NULL;
    CloseHandle(hStdErrWrite);
    hStdErrWrite = NULL;

    std::string outResult;
    std::string errResult;

    ::WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = -1;
    if (!GetExitCodeProcess(pi.hProcess, &exitCode))
    {
        throw std::runtime_error("GetExitCodeProcess failed");
    }
    
    auto readFromPipe = [](HANDLE hPipe)
    {
        constexpr DWORD BUFFER_SIZE = 4096;
        char buffer[BUFFER_SIZE];
        DWORD bytesRead;
        std::string output;

        while (true)
        {
            if (!ReadFile(hPipe, buffer, BUFFER_SIZE, &bytesRead, NULL) || bytesRead == 0)
            {
                break;
            }
            output.append(buffer, bytesRead);
        }
        return output;
    };

    outResult = readFromPipe(hStdOutRead);
    errResult = readFromPipe(hStdErrRead);

    stdOutput = zzj::str::ansi2w(outResult);
    stdError = zzj::str::ansi2w(errResult);

    dwPid = pi.dwProcessId;

    return Process::ExitCode{exitCode};
}

Process::ExitCode Process::SystemCreateProcess(const std::string &commandLine, bool bElevated,
                                   std::string &stdOutput, std::string &stdError)
{
    std::wstring wStdOutput;
    std::wstring wStdError;

    auto exitCode =
        SystemCreateProcess(zzj::str::utf82w(commandLine), bElevated, wStdOutput, wStdError);

    stdOutput = zzj::str::w2utf8(wStdOutput);
    stdError = zzj::str::w2utf8(wStdError);

    return exitCode;
}

BOOL zzj::Process::RegularCreateProcess(std::string path, bool show, std::string cmdLine, bool wait,
                                        DWORD *errCode)
{
    STARTUPINFOA stinfo;
    ZeroMemory((void *)&stinfo, sizeof(STARTUPINFO));
    PROCESS_INFORMATION ProcessInfo;
    stinfo.cb = sizeof(STARTUPINFO);
    stinfo.dwFlags = STARTF_USESHOWWINDOW;
    if (!show)
        stinfo.wShowWindow = SW_HIDE;
    else
        stinfo.wShowWindow = SW_NORMAL;
    if (!cmdLine.empty()) path = path + " " + cmdLine;
    if (!CreateProcessA(NULL, (LPSTR)path.c_str(), NULL, NULL, false, 0, NULL, NULL, &stinfo,
                        &ProcessInfo))
        return false;
    else
    {
        if (wait) WaitForSingleObject(ProcessInfo.hProcess, INFINITE);

        if (errCode) GetExitCodeProcess(ProcessInfo.hProcess, errCode);

        // Close process and thread handles.
        CloseHandle(ProcessInfo.hProcess);
        CloseHandle(ProcessInfo.hThread);
        return true;
    }
}
zzj::Process::Token zzj::Process::GetToken(DWORD desiredAccess)
{
    HANDLE hToken = INVALID_HANDLE_VALUE;
    if (!OpenProcessToken(*process, desiredAccess, &hToken))
        throw std::runtime_error("OpenProcessToken failed");
    return Token(hToken);
}
BOOL zzj::Process::AdminCreateProcess(const char *pszFileName, bool show, const char *param,
                                      bool wait)
{
    SHELLEXECUTEINFOA ShExecInfo = {0};
    ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
    ShExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    ShExecInfo.hwnd = NULL;
    ShExecInfo.lpVerb = "runas";
    ShExecInfo.lpFile = pszFileName;
    ShExecInfo.lpParameters = param;
    ShExecInfo.lpDirectory = NULL;
    if (show)
        ShExecInfo.nShow = SW_SHOW;
    else
        ShExecInfo.nShow = SW_HIDE;
    ShExecInfo.hInstApp = NULL;
    ShellExecuteExA(&ShExecInfo);
    if (32 >= (DWORD)ShExecInfo.hInstApp)
    {
        return false;
    }
    if (wait) WaitForSingleObject(ShExecInfo.hProcess, INFINITE);
    CloseHandle(ShExecInfo.hProcess);

    return true;
}

BOOL zzj::Process::IsProcessAdmin()
{
    BOOL bElevated = FALSE;
    HANDLE hToken = NULL;

    // Get current process token
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) return FALSE;

    TOKEN_ELEVATION tokenEle;
    DWORD dwRetLen = 0;

    // Retrieve token elevation information
    if (GetTokenInformation(hToken, TokenElevation, &tokenEle, sizeof(tokenEle), &dwRetLen))
    {
        if (dwRetLen == sizeof(tokenEle))
        {
            bElevated = tokenEle.TokenIsElevated;
        }
    }

    CloseHandle(hToken);
    return bElevated;
}

bool Process::KillProcess(DWORD pid)
{
    bool bRet = false;
    HANDLE hProcess = ::OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (NULL != hProcess)
    {
        if (::TerminateProcess(hProcess, 0))
        {
            bRet = true;
        }
        ::CloseHandle(hProcess);
    }
    return bRet;
}
std::vector<Process> Process::GetProcessByName(const std::string &processName)
{
    std::vector<Process> ret;
    std::wstring wName = zzj::str::utf82w(processName);
    std::vector<DWORD> pids = GetProcessId(wName);
    for (auto pid : pids)
    {
        Process process(pid, PROCESS_ALL_ACCESS);
        if (process.IsValid()) ret.push_back(process);
    }
    return ret;
}

bool zzj::Process::KillProcess(const char *name)
{
    std::wstring wName = zzj::str::utf82w(name);

    std::vector<DWORD> pids = GetProcessId(wName);
    if (pids.size() == 0) return true;

    for (auto pid : pids) KillProcess(pid);
    return true;
}

std::wstring zzj::Process::GetModulePath(std::wstring moduleName)
{
    WCHAR szFilePath[MAX_PATH + 1] = {0};
    HMODULE mHandle;
    if (moduleName.empty())
        mHandle = NULL;
    else
        mHandle = GetModuleHandleW(moduleName.c_str());

    GetModuleFileNameW(mHandle, szFilePath, MAX_PATH);
    (wcsrchr(szFilePath, L'\\'))[1] = 0;  
    return szFilePath;
}

std::vector<uint8_t> zzj::Memory::ReadUntilZero(uintptr_t address)
{
    std::vector<uint8_t> ret;
    uint8_t buf;
    while (Read(address, &buf, sizeof(buf)))
    {
        if (buf == 0)
        {
            ret.push_back(buf);
            break;
        }
        ret.push_back(buf);
        address += sizeof(buf);
    }
    return ret;
}
bool zzj::Memory::Read(uintptr_t address, void *buffer, size_t size)
{
    SIZE_T numRead = 0;
    bool ret = true;

    DWORD oldProtect;
    bool isProtectChange = false;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQueryEx(process, (LPCVOID)address, &mbi, sizeof(mbi)))
    {
        spdlog::error("VirtualQueryEx failed:{}", GetLastError());
        return false;
    }
    DEFER
    {
        if (isProtectChange)
            VirtualProtectEx(*process.process, (LPVOID)address, size, oldProtect, &oldProtect);
    };

    if (mbi.Protect & PAGE_READONLY || mbi.Protect & PAGE_READWRITE ||
        mbi.Protect & PAGE_EXECUTE_READ || mbi.Protect & PAGE_EXECUTE_READWRITE)
    {
        isProtectChange = false;
    }
    else
    {
        if (!VirtualProtectEx(process, (LPVOID)address, size, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            spdlog::error("VirtualProtectEx1 failed:{}", GetLastError());
            spdlog::error("Failed Address:{:#x}", address);
            return false;
        }
        isProtectChange = true;
    }

    ret = ReadProcessMemory(*process.process, (LPCVOID)address, buffer, size, &numRead);

    if (numRead != size)
    {
        spdlog::error("ReadProcessMemory failed:{}", GetLastError());
        return false;
    }

    return ret && numRead == size;
}

bool zzj::Memory::Write(uintptr_t address, const void *buffer, size_t size)
{
    SIZE_T numWrite = 0;
    DWORD oldProtect;
    bool ret = true;
    VirtualProtectEx(process, (LPVOID)address, size, PAGE_EXECUTE_READWRITE, &oldProtect);
    if (process.processId != GetCurrentProcessId())
        ret = WriteProcessMemory(*process.process, (LPVOID)address, buffer, size, &numWrite);
    else
    {
        memcpy((void *)address, buffer, size);
        numWrite = size;
    }
    VirtualProtectEx(process, (LPVOID)address, size, oldProtect, &oldProtect);
    return ret && numWrite == size;
}

bool zzj::Memory::Write(uintptr_t address, std::vector<uint8_t> buf)
{
    return Write(address, buf.data(), buf.size());
}

uintptr_t zzj::Memory::Alloc(DWORD size, DWORD allocationType, DWORD protect)
{
    return (uintptr_t)VirtualAllocEx(process, NULL, size, allocationType, protect);
}

uintptr_t zzj::Memory::DeAlloc(uintptr_t address)
{
    return (uintptr_t)VirtualFreeEx(process, (LPVOID)address, 0, MEM_RELEASE);
}

bool zzj::Memory::Nop(uintptr_t address, size_t size)
{
    std::vector<uint8_t> nop(size, 0x90);
    return Write(address, nop.data(), size);
}

uintptr_t zzj::Memory::FindMultiPleLevelAddress(uintptr_t baseAddress,
                                                std::vector<unsigned int> offsets)
{
    uintptr_t address = baseAddress;
    if (!Read(address, &address, sizeof(address))) return uintptr_t();
    for (auto offset : offsets)
    {
        address += offset;
        if (!Read(address, &address, sizeof(address))) return uintptr_t();
    }
    return address;
}
std::vector<MODULEENTRY32> zzj::Memory::GetModuleInfos()
{
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, process.processId);
    if (hSnapshot == INVALID_HANDLE_VALUE)
    {
        spdlog::error("CreateToolhelp32Snapshot fails with error code {}", GetLastError());
        return {};
    }

    MODULEENTRY32 moduleEntry;
    moduleEntry.dwSize = sizeof(MODULEENTRY32);

    std::vector<MODULEENTRY32> moduleInfos;

    
    if (Module32First(hSnapshot, &moduleEntry))
    {
        do
        {
            moduleInfos.push_back(moduleEntry);
        } while (Module32Next(hSnapshot, &moduleEntry));
    }

    CloseHandle(hSnapshot);
    return moduleInfos;
}

std::optional<MODULEENTRY32> zzj::Memory::GetModuleInfo(const std::string &moduleName)
{
    auto modules = GetModuleInfos();
    for (auto &module : modules)
    {
        if (module.szModule == zzj::str::utf82w(moduleName)) return module;
    }
    return std::nullopt;
}
std::vector<MemoryInfo> zzj::Memory::PatternScan(uintptr_t startAddress, size_t searchSize,
                                                 const std::string &pattern)
{
    spdlog::info("PatternScan startAddress:{:#x} searchSize:{:#x} pattern:{}", startAddress,
                 searchSize, pattern);
    std::vector<MemoryInfo> matches;
    if (pattern.empty() || startAddress == 0 || searchSize == 0) return matches;

    try
    {
        
        std::istringstream iss(pattern);
        std::string byteString;
        std::vector<std::pair<uint8_t, bool>> bytePattern;
        while (iss >> byteString)
        {
            if (byteString == "??")
            {
                bytePattern.emplace_back(0, true);  // Wildcard
            }
            else
            {
                bytePattern.emplace_back(static_cast<uint8_t>(std::stoul(byteString, nullptr, 16)),
                                         false);  // Regular byte
            }
        }
        size_t patternSize = bytePattern.size();

        
        const size_t blockSize = 4096;
        std::vector<uint8_t> buffer(blockSize +
                                    patternSize);  

        size_t processedSize = 0;
        while (processedSize < searchSize)
        {
            size_t currentBlockSize =
                blockSize < (searchSize - processedSize) ? blockSize : (searchSize - processedSize);
            
            if (!Read(startAddress + processedSize, buffer.data(), currentBlockSize + patternSize))
            {
                processedSize += currentBlockSize;
                continue;
            }

            
            for (size_t i = 0; i < currentBlockSize; i++)
            {
                bool found = true;
                for (size_t j = 0; j < patternSize; j++)
                {
                    if (!bytePattern[j].second && buffer[i + j] != bytePattern[j].first)
                    {
                        found = false;
                        break;
                    }
                }
                if (found)
                {
                    uintptr_t foundAddress = startAddress + processedSize + i;
                    MEMORY_BASIC_INFORMATION memInfo;
                    if (VirtualQueryEx(process.GetProcessHandle(),
                                       reinterpret_cast<LPCVOID>(foundAddress), &memInfo,
                                       sizeof(memInfo)) != 0)
                    {
                        matches.push_back({foundAddress, memInfo});
                    }
                }
            }

            processedSize += currentBlockSize;
        }
    }
    catch (const std::exception &e)
    {
        spdlog::error("PatternScan failed:{}", e.what());
    }
    catch (...)
    {
        spdlog::error("PatternScan failed");
    }

    return matches;
}
