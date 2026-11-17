#pragma once

#include <string>
#include <tuple>
#include <vector>

namespace zzj
{

// Registry run entry information (Windows only)
class RegistryStartUpInfo
{
   public:
    std::string hive;       // HKCU / HKLM
    std::string keyPath;    // Full subkey path
    std::string valueName;  // Value name
    std::string command;    // Start command
};

// Windows service start information (Windows only)
class ServiceStartUpInfo
{
   public:
    std::string name;         // Service name
    std::string displayName;  // Display name
    std::string status;       // Current status
    std::string startType;    // Start type
    std::string binaryPath;   // Binary path
    std::string account;      // Logon account
};

// Scheduled task start information (Windows only)
class TaskStartUpInfo
{
   public:
    std::string name;      // Task name
    std::string path;      // Task path (including folder)
    std::string state;     // Task state
    std::string enabled;   // Whether task is enabled ("true"/"false")
    std::string triggers;  // Trigger descriptions (semicolon separated)
    std::string actions;   // Actions (executable path and args, semicolon separated)
};

// Startup folder entry information (Windows only)
class FolderStartUpInfo
{
   public:
    std::string location;  // CommonStartup / UserStartup
    std::string folder;    // Startup folder path
    std::string fileName;  // File name
    std::string fullPath;  // Full file path
};

// WMI event subscription information (filter + consumer) (Windows only)
class WmiEventSubscriptionInfo
{
   public:
    std::string filterName;
    std::string filterQuery;
    std::string consumerName;
    std::string consumerType;
    std::string consumerExecutablePath;
};

// Windows startup collection class, implemented on Windows only.
class StartUp
{
   public:
    // Return value: <0 on error, 0 on success
    std::tuple<int, std::vector<RegistryStartUpInfo>> GetRegistryStartUp();
    std::tuple<int, std::vector<ServiceStartUpInfo>> GetServiceStartUp();
    std::tuple<int, std::vector<TaskStartUpInfo>> GetTaskStartUp();
    std::tuple<int, std::vector<FolderStartUpInfo>> GetFolderStartUp();
    std::tuple<int, std::vector<WmiEventSubscriptionInfo>> GetWmiEventSubscriptions();
};

}  // namespace zzj
