#include "VMWare.h"
#include <Windows.h>
#include <Windows/util/ApiLoader/Apis.h>
#include <Windows/util/Registry/WinReg.hpp>
#include <boost/filesystem.hpp>
#include <string>
#include <vector>


namespace zzj::AntiVM
{

static BOOL Firmware()
{
    auto apiLoader = zzj::ApiLoader::API::GetInstance();
    if (!apiLoader->IsAvailable(zzj::ApiLoader::API_IDENTIFIER::API_GetSystemFirmwareTable))
    {
        return FALSE;
    }

    auto GetSystemFirmwareTable = static_cast<zzj::ApiLoader::pGetSystemFirmwareTable>(
        apiLoader->GetAPI(zzj::ApiLoader::API_IDENTIFIER::API_GetSystemFirmwareTable));
    if (GetSystemFirmwareTable == nullptr)
    {
        return FALSE;
    }

    DWORD dwSize = GetSystemFirmwareTable('RSMB', 0, NULL, 0);
    if (dwSize == 0)
    {
        return FALSE;
    }

    std::vector<char> buffer(dwSize);
    dwSize = GetSystemFirmwareTable('RSMB', 0, buffer.data(), dwSize);
    if (dwSize == 0)
    {
        return FALSE;
    }

    std::string firmwareTableString(buffer.begin(), buffer.end());
    if (firmwareTableString.find("VMware") != std::string::npos)
    {
        return TRUE;
    }

    return FALSE;
}
static BOOL DriverFiles()
{
    /* Array of strings of blacklisted paths */
    std::vector<std::string> drivers = {"System32\\drivers\\vmmouse.sys", "System32\\drivers\\vm3dmp.sys"};

    /* Getting Windows Directory */
    char szWinDir[MAX_PATH] = {0};
    PVOID OldValue          = NULL;

    GetWindowsDirectoryA(szWinDir, MAX_PATH);

    BOOL isWow64 = FALSE;
    if (IsWow64Process(GetCurrentProcess(), &isWow64) && isWow64)
    {
        Wow64DisableWow64FsRedirection(&OldValue);
    }

    BOOL ret = FALSE;
    /* Check one by one */
    for (int i = 0; i < drivers.size(); i++)
    {
        boost::filesystem::path driverFile = szWinDir;
        driverFile /= drivers[i];
        if (boost::filesystem::exists(driverFile))
        {
            ret = TRUE;
            break;
        }
    }

    if (IsWow64Process(GetCurrentProcess(), &isWow64) && isWow64)
    {
        Wow64RevertWow64FsRedirection(&OldValue);
    }

    return ret;
}
static BOOL RegistryCheck()
{
    try
    {
        std::wstring sysManuFacturer;
        winreg::RegKey key{HKEY_LOCAL_MACHINE,
                           L"SYSTEM\\ControlSet001\\Control\\SystemInformation\\SystemManufacturer"};
        if (key.IsValid())
        {
            sysManuFacturer = key.GetStringValue(L"SystemManufacturer");
            if (sysManuFacturer.find(L"VMware") != std::wstring::npos)
            {
                return TRUE;
            }
        }

        key.Open(HKEY_LOCAL_MACHINE, L"SYSTEM\\ControlSet001\\Control\\SystemInformation\\SystemProductName");
        if (key.IsValid())
        {
            sysManuFacturer = key.GetStringValue(L"SystemProductName");
            if (sysManuFacturer.find(L"VMware") != std::wstring::npos)
            {
                return TRUE;
            }
        }

        return FALSE;
    }
    catch (const std::exception &e)
    {
        return FALSE;
    }
    catch (...)
    {
        return FALSE;
    }
}
int VMWareCheck()
{
    if (RegistryCheck())
    {
        return 1;
    }

    if (DriverFiles())
    {
        return 2;
    }

    if (Firmware())
    {
        return 3;
    }
    return 0;
};
}; // namespace zzj::AntiVM