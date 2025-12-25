#include <Windows/util/StartUp/StartUp.h>

#include <General/util/StrUtil.h>
#include <Windows.h>

#include <Windows/util/COM/WMIHelper.h>
#include <Windows/util/File/FileHelper.h>
#include <Windows/util/Registry/WinReg.hpp>
#include <Windows/util/TaskScheduler/TaskScheduler.h>

#include <map>

using winreg::RegKey;

namespace zzj
{
namespace
{
// Convert TASK_STATE to human-readable string
std::string ServiceStateToString(TASK_STATE state)
{
    switch (state)
    {
        case TASK_STATE_DISABLED:
            return "Disabled";
        case TASK_STATE_QUEUED:
            return "Queued";
        case TASK_STATE_READY:
            return "Ready";
        case TASK_STATE_RUNNING:
            return "Running";
        case TASK_STATE_UNKNOWN:
        default:
            return "Unknown";
    }
}

// Convert service start type to string
std::string ServiceStartTypeToString(DWORD startType)
{
    switch (startType)
    {
        case SERVICE_BOOT_START:
            return "Boot";
        case SERVICE_SYSTEM_START:
            return "System";
        case SERVICE_AUTO_START:
            return "Auto";
        case SERVICE_DEMAND_START:
            return "Manual";
        case SERVICE_DISABLED:
            return "Disabled";
        default:
            return "Unknown";
    }
}

// Convert service status to string
std::string ServiceStatusToString(DWORD state)
{
    switch (state)
    {
        case SERVICE_STOPPED:
            return "Stopped";
        case SERVICE_START_PENDING:
            return "StartPending";
        case SERVICE_STOP_PENDING:
            return "StopPending";
        case SERVICE_RUNNING:
            return "Running";
        case SERVICE_CONTINUE_PENDING:
            return "ContinuePending";
        case SERVICE_PAUSE_PENDING:
            return "PausePending";
        case SERVICE_PAUSED:
            return "Paused";
        default:
            return "Unknown";
    }
}

// Open and enumerate a specific Run-like registry key
void EnumRunKey(HKEY root, const std::wstring &subKey, const std::string &hiveName, REGSAM access,
                std::vector<RegistryStartUpInfo> &outItems)
{
    try
    {
        RegKey key(root, subKey, access);
        if (!key)
        {
            // Key does not exist, nothing to do
            return;
        }

        auto values = key.EnumValues();
        for (const auto &valInfo : values)
        {
            const std::wstring &valueName = valInfo.first;
            DWORD type = valInfo.second;

            if (type != REG_SZ && type != REG_EXPAND_SZ)
            {
                continue;
            }

            auto valueResult = key.TryGetStringValue(valueName);
            if (!valueResult || !valueResult.IsValid())
            {
                continue;
            }

            const auto &valueStrW = valueResult.GetValue();

            RegistryStartUpInfo info;
            info.hive = hiveName;
            info.keyPath = zzj::str::WstrToUTF8Str(subKey);
            info.valueName = zzj::str::WstrToUTF8Str(valueName);
            info.command = zzj::str::WstrToUTF8Str(valueStrW);
            outItems.emplace_back(std::move(info));
        }
    }
    catch (const std::exception &)
    {
        // Be robust: ignore this key on exception
        return;
    }
}

// Enumerate startup folder entries
void EnumStartFolder(const std::string &folderPath, const std::string &locationName,
                     std::vector<FolderStartUpInfo> &outItems)
{
    if (folderPath.empty())
    {
        return;
    }

    std::string searchPath = folderPath;
    if (!searchPath.empty() && searchPath.back() != '\\')
    {
        searchPath.push_back('\\');
    }
    searchPath += "*";

    WIN32_FIND_DATAA findData;
    HANDLE hFind = ::FindFirstFileA(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        return;
    }

    do
    {
        const char *name = findData.cFileName;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        {
            continue;
        }

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            // In startup folders we only care about files, ignore sub-directories
            continue;
        }

        FolderStartUpInfo info;
        info.location = locationName;
        info.folder = folderPath;
        info.fileName = name;

        std::string fullPath = folderPath;
        if (!fullPath.empty() && fullPath.back() != '\\')
        {
            fullPath.push_back('\\');
        }
        fullPath += name;
        info.fullPath = fullPath;

        outItems.emplace_back(std::move(info));
    } while (::FindNextFileA(hFind, &findData));

    ::FindClose(hFind);
}

struct WmiFilterInfo
{
    std::wstring name;
    std::wstring query;
    std::wstring relPath;
};

struct WmiConsumerInfo
{
    std::wstring name;
    std::wstring type;
    std::wstring executablePath;
    std::wstring relPath;
};

// Query __EventFilter
void QueryWmiFilters(zzj::WMIWrapper &wmi, std::map<std::wstring, WmiFilterInfo> &filters)
{
    HRESULT hres = wmi.WMIExecQuery("WQL", "SELECT * FROM __EventFilter");
    if (S_OK != hres)
    {
        return;
    }

    VARIANT var;
    while (S_OK == wmi.WMIGetNextObject())
    {
        WmiFilterInfo info;

        if (S_OK == wmi.WMIGetProperty(L"Name", var))
        {
            info.name = (var.vt == VT_BSTR && var.bstrVal) ? var.bstrVal : L"";
            wmi.WMIReleaseProperty(var);
        }

        if (S_OK == wmi.WMIGetProperty(L"Query", var))
        {
            info.query = (var.vt == VT_BSTR && var.bstrVal) ? var.bstrVal : L"";
            wmi.WMIReleaseProperty(var);
        }

        if (S_OK == wmi.WMIGetProperty(L"__RELPATH", var))
        {
            info.relPath = (var.vt == VT_BSTR && var.bstrVal) ? var.bstrVal : L"";
            wmi.WMIReleaseProperty(var);
        }

        if (!info.relPath.empty())
        {
            filters[info.relPath] = info;
        }

        wmi.WMIReleaseThisObject();
    }
}

// Query CommandLineEventConsumer
void QueryWmiCommandLineConsumers(zzj::WMIWrapper &wmi,
                                  std::map<std::wstring, WmiConsumerInfo> &consumers)
{
    HRESULT hres = wmi.WMIExecQuery("WQL", "SELECT * FROM CommandLineEventConsumer");
    if (S_OK != hres)
    {
        return;
    }

    VARIANT var;
    while (S_OK == wmi.WMIGetNextObject())
    {
        WmiConsumerInfo info;
        info.type = L"CommandLineEventConsumer";

        if (S_OK == wmi.WMIGetProperty(L"Name", var))
        {
            info.name = (var.vt == VT_BSTR && var.bstrVal) ? var.bstrVal : L"";
            wmi.WMIReleaseProperty(var);
        }

        if (S_OK == wmi.WMIGetProperty(L"ExecutablePath", var))
        {
            info.executablePath = (var.vt == VT_BSTR && var.bstrVal) ? var.bstrVal : L"";
            wmi.WMIReleaseProperty(var);
        }

        if (S_OK == wmi.WMIGetProperty(L"__RELPATH", var))
        {
            info.relPath = (var.vt == VT_BSTR && var.bstrVal) ? var.bstrVal : L"";
            wmi.WMIReleaseProperty(var);
        }

        if (!info.relPath.empty())
        {
            consumers[info.relPath] = info;
        }

        wmi.WMIReleaseThisObject();
    }
}

}  // namespace

std::tuple<int, std::vector<RegistryStartUpInfo>> StartUp::GetRegistryStartUp()
{
    std::vector<RegistryStartUpInfo> items;

    // HKCU Run / RunOnce / Policies\Explorer\Run
    const std::wstring hkcuBase = L"Software\\Microsoft\\Windows\\CurrentVersion\\";
    EnumRunKey(HKEY_CURRENT_USER, hkcuBase + L"Run", "HKCU", KEY_READ, items);
    EnumRunKey(HKEY_CURRENT_USER, hkcuBase + L"RunOnce", "HKCU", KEY_READ, items);
    EnumRunKey(HKEY_CURRENT_USER, hkcuBase + L"Policies\\Explorer\\Run", "HKCU", KEY_READ, items);

    // HKLM 64-bit view
    const std::wstring hklmBase = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\";
    EnumRunKey(HKEY_LOCAL_MACHINE, hklmBase + L"Run", "HKLM", KEY_READ | KEY_WOW64_64KEY, items);
    EnumRunKey(HKEY_LOCAL_MACHINE, hklmBase + L"RunOnce", "HKLM", KEY_READ | KEY_WOW64_64KEY,
               items);
    EnumRunKey(HKEY_LOCAL_MACHINE, hklmBase + L"Policies\\Explorer\\Run", "HKLM",
               KEY_READ | KEY_WOW64_64KEY, items);

    // HKLM WOW6432Node 32-bit view
    const std::wstring hklmWowBase = L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\";
    EnumRunKey(HKEY_LOCAL_MACHINE, hklmWowBase + L"Run", "HKLM", KEY_READ | KEY_WOW64_32KEY, items);
    EnumRunKey(HKEY_LOCAL_MACHINE, hklmWowBase + L"RunOnce", "HKLM", KEY_READ | KEY_WOW64_32KEY,
               items);
    EnumRunKey(HKEY_LOCAL_MACHINE, hklmWowBase + L"Policies\\Explorer\\Run", "HKLM",
               KEY_READ | KEY_WOW64_32KEY, items);

    return std::make_tuple(0, std::move(items));
}

std::tuple<int, std::vector<ServiceStartUpInfo>> StartUp::GetServiceStartUp()
{
    std::vector<ServiceStartUpInfo> items;

    SC_HANDLE scm = ::OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm)
    {
        return std::make_tuple(-1, std::move(items));
    }

    DWORD bytesNeeded = 0;
    DWORD serviceCount = 0;
    DWORD resumeHandle = 0;
    DWORD serviceType = SERVICE_WIN32;
    DWORD serviceStates = SERVICE_STATE_ALL;

    if (!::EnumServicesStatusExA(scm, SC_ENUM_PROCESS_INFO, serviceType, serviceStates, nullptr, 0,
                                 &bytesNeeded, &serviceCount, &resumeHandle, nullptr) &&
        ::GetLastError() != ERROR_MORE_DATA)
    {
        ::CloseServiceHandle(scm);
        return std::make_tuple(-2, std::move(items));
    }

    std::vector<BYTE> buffer(bytesNeeded);
    auto *services = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSA *>(buffer.data());

    if (!::EnumServicesStatusExA(scm, SC_ENUM_PROCESS_INFO, serviceType, serviceStates,
                                 reinterpret_cast<LPBYTE>(services), bytesNeeded, &bytesNeeded,
                                 &serviceCount, &resumeHandle, nullptr))
    {
        ::CloseServiceHandle(scm);
        return std::make_tuple(-3, std::move(items));
    }

    for (DWORD i = 0; i < serviceCount; ++i)
    {
        const auto &svc = services[i];

        ServiceStartUpInfo info;
        info.name = zzj::str::ansi2utf8(svc.lpServiceName ? svc.lpServiceName : "");
        info.displayName = zzj::str::ansi2utf8(svc.lpDisplayName ? svc.lpDisplayName : "");
        info.status = ServiceStatusToString(svc.ServiceStatusProcess.dwCurrentState);

        SC_HANDLE hService =
            ::OpenServiceA(scm, svc.lpServiceName, SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS);
        if (hService)
        {
            DWORD bytesNeededCfg = 0;
            ::QueryServiceConfigA(hService, nullptr, 0, &bytesNeededCfg);
            if (::GetLastError() == ERROR_INSUFFICIENT_BUFFER && bytesNeededCfg > 0)
            {
                std::vector<BYTE> cfgBuffer(bytesNeededCfg);
                auto *config = reinterpret_cast<QUERY_SERVICE_CONFIGA *>(cfgBuffer.data());
                if (::QueryServiceConfigA(hService, config, bytesNeededCfg, &bytesNeededCfg))
                {
                    info.startType = ServiceStartTypeToString(config->dwStartType);
                    info.binaryPath = zzj::str::ansi2utf8(
                        config->lpBinaryPathName ? config->lpBinaryPathName : "");
                    info.account = zzj::str::ansi2utf8(
                        config->lpServiceStartName ? config->lpServiceStartName : "");
                }
            }
            ::CloseServiceHandle(hService);
        }

        items.emplace_back(std::move(info));
    }

    ::CloseServiceHandle(scm);
    return std::make_tuple(0, std::move(items));
}

std::tuple<int, std::vector<TaskStartUpInfo>> StartUp::GetTaskStartUp()
{
    std::vector<TaskStartUpInfo> items;

    try
    {
        auto tasks = zzj::TaskScheduler::GetTasks(L"\\");
        for (const auto &task : tasks)
        {
            TaskStartUpInfo info;
            auto nameW = task.GetName();
            auto pathW = task.GetPath();
            auto state = task.GetState();
            bool enable = task.IsEnabled();

            info.name = zzj::str::WstrToUTF8Str(nameW);
            info.path = zzj::str::WstrToUTF8Str(pathW);
            info.state = ServiceStateToString(state);
            info.enabled = enable ? "true" : "false";

            // Triggers
            auto triggerList = task.GetTriggers();
            std::string triggersJoined;
            for (size_t i = 0; i < triggerList.size(); ++i)
            {
                if (i != 0)
                {
                    triggersJoined += ";";
                }
                triggersJoined += zzj::str::WstrToUTF8Str(triggerList[i]);
            }
            info.triggers = std::move(triggersJoined);

            // Actions
            auto actionList = task.GetActions();
            std::string actionsJoined;
            for (size_t i = 0; i < actionList.size(); ++i)
            {
                if (i != 0)
                {
                    actionsJoined += ";";
                }
                actionsJoined += zzj::str::WstrToUTF8Str(actionList[i]);
            }
            info.actions = std::move(actionsJoined);

            items.emplace_back(std::move(info));
        }
    }
    catch (const std::exception &)
    {
        return std::make_tuple(-1, std::move(items));
    }

    return std::make_tuple(0, std::move(items));
}

std::tuple<int, std::vector<FolderStartUpInfo>> StartUp::GetFolderStartUp()
{
    std::vector<FolderStartUpInfo> items;

    // Common startup folder: %ProgramData%\Microsoft\Windows\Start Menu\Programs\StartUp
    std::string programData = zzj::FileHelper::GetProgramDataPath();
    if (!programData.empty())
    {
        std::string commonStartup =
            programData + "\\Microsoft\\Windows\\Start Menu\\Programs\\StartUp";
        EnumStartFolder(commonStartup, "CommonStartup", items);
    }

    // Current user startup folder: %AppData%\Microsoft\Windows\Start Menu\Programs\Startup
    std::string appData = zzj::FileHelper::GetCurrentUserProgramDataFolder();
    if (!appData.empty())
    {
        std::string userStartup = appData + "\\Microsoft\\Windows\\Start Menu\\Programs\\Startup";
        EnumStartFolder(userStartup, "UserStartup", items);
    }

    return std::make_tuple(0, std::move(items));
}

std::tuple<int, std::vector<WmiEventSubscriptionInfo>> StartUp::GetWmiEventSubscriptions()
{
    std::vector<WmiEventSubscriptionInfo> items;

    zzj::WMIWrapper wmi;
    HRESULT hres = wmi.WMIConnectServer(L"ROOT\\subscription");
    if (S_OK != hres)
    {
        return std::make_tuple(-1, std::move(items));
    }

    std::map<std::wstring, WmiFilterInfo> filters;
    std::map<std::wstring, WmiConsumerInfo> consumers;

    QueryWmiFilters(wmi, filters);

    // For the next query use a fresh WMIWrapper instance instead of touching private methods
    {
        zzj::WMIWrapper wmiConsumers;
        if (S_OK == wmiConsumers.WMIConnectServer(L"ROOT\\subscription"))
        {
            QueryWmiCommandLineConsumers(wmiConsumers, consumers);
        }
    }

    // Bind filters and consumers using a new enumeration instance
    zzj::WMIWrapper wmiBinding;
    hres = wmiBinding.WMIConnectServer(L"ROOT\\subscription");
    if (S_OK != hres)
    {
        return std::make_tuple(0, std::move(items));
    }

    hres = wmiBinding.WMIExecQuery("WQL", "SELECT * FROM __FilterToConsumerBinding");
    if (S_OK != hres)
    {
        return std::make_tuple(0, std::move(items));
    }

    VARIANT var;
    while (S_OK == wmiBinding.WMIGetNextObject())
    {
        std::wstring filterPath;
        std::wstring consumerPath;

        if (S_OK == wmiBinding.WMIGetProperty(L"Filter", var))
        {
            filterPath = (var.vt == VT_BSTR && var.bstrVal) ? var.bstrVal : L"";
            wmiBinding.WMIReleaseProperty(var);
        }

        if (S_OK == wmiBinding.WMIGetProperty(L"Consumer", var))
        {
            consumerPath = (var.vt == VT_BSTR && var.bstrVal) ? var.bstrVal : L"";
            wmiBinding.WMIReleaseProperty(var);
        }

        WmiEventSubscriptionInfo info;

        auto fit = filters.find(filterPath);
        if (fit != filters.end())
        {
            info.filterName = zzj::str::WstrToUTF8Str(fit->second.name);
            info.filterQuery = zzj::str::WstrToUTF8Str(fit->second.query);
        }
        else
        {
            info.filterName = zzj::str::WstrToUTF8Str(filterPath);
            info.filterQuery = "";
        }

        auto cit = consumers.find(consumerPath);
        if (cit != consumers.end())
        {
            info.consumerName = zzj::str::WstrToUTF8Str(cit->second.name);
            info.consumerType = zzj::str::WstrToUTF8Str(cit->second.type);
            info.consumerExecutablePath = zzj::str::WstrToUTF8Str(cit->second.executablePath);
        }
        else
        {

            info.consumerName = zzj::str::WstrToUTF8Str(consumerPath);
            info.consumerType = "";
            info.consumerExecutablePath = "";
        }

        items.emplace_back(std::move(info));
        wmiBinding.WMIReleaseThisObject();
    }

    return std::make_tuple(0, std::move(items));
}

}  // namespace zzj
