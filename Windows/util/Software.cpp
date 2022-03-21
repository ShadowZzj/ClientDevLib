#include "SoftWare.h"
#include <windows.h>
int zzj::SoftInfoManager::GetInstalledSoftware(std::vector<SoftInfo> &softInfos)
{
    HKEY RootKey;         // A primary key
    HKEY hkResult = NULL; // Handle to the key to be opened
    HKEY hkRKey;
    LONG lReturn; // Record whether the registry was read successfully
    std::string strBuffer;
    std::string strMidReg;
    char szKeyName[255]             = {0}; // Registry key name
    char szBuffer[255]              = {0};
    DWORD dwKeyLen                  = 255;
    DWORD dwNameLen                 = 255;
    DWORD dwType                    = REG_BINARY | REG_DWORD | REG_EXPAND_SZ | REG_MULTI_SZ | REG_NONE | REG_SZ;
    RootKey                         = HKEY_LOCAL_MACHINE;
    std::vector<std::string> subKey = {"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
                                       "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"};
    std::vector<std::string> vec;
    int i = 0;
    for (auto key : subKey)
    {
        DWORD index = 0;
        lReturn     = RegOpenKeyExA(RootKey, key.c_str(), 0, KEY_READ, &hkResult);
        if (lReturn != ERROR_SUCCESS)
            return -1;
        while (1)
        {
            SoftInfo softinfo;
            DWORD ret = RegEnumKeyExA(hkResult, index, szKeyName, &dwKeyLen, 0, NULL, NULL, NULL);
            index++;
            strBuffer = szKeyName;
            if (strBuffer.empty())
                continue;

            strMidReg = (std::string)key + "\\" + strBuffer;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, strMidReg.c_str(), 0, KEY_READ, &hkRKey) != ERROR_SUCCESS)
                continue;

            RegQueryValueExA(hkRKey, "DisplayName", 0, &dwType, (LPBYTE)szBuffer, &dwNameLen);
            softinfo.m_strSoftName = szBuffer;
            dwNameLen              = 255;
            memset(szBuffer, 0, 255);

            RegQueryValueExA(hkRKey, "DisplayVersion", 0, &dwType, (LPBYTE)szBuffer, &dwNameLen);
            softinfo.m_strSoftVersion = szBuffer;
            dwNameLen                 = 255;
            memset(szBuffer, 0, 255);

            RegQueryValueExA(hkRKey, "InstallLocation", 0, &dwType, (LPBYTE)szBuffer, &dwNameLen);
            softinfo.m_strInstallLocation = szBuffer;
            dwNameLen                     = 255;
            memset(szBuffer, 0, 255);

            RegQueryValueExA(hkRKey, "Publisher", 0, &dwType, (LPBYTE)szBuffer, &dwNameLen);
            softinfo.m_strPublisher = szBuffer;
            dwNameLen               = 255;
            memset(szBuffer, 0, 255);

            RegQueryValueExA(hkRKey, "InstallLocation", 0, &dwType, (LPBYTE)szBuffer, &dwNameLen);
            softinfo.m_strMainProPath = szBuffer;
            dwNameLen                 = 255;
            memset(szBuffer, 0, 255);

            RegQueryValueExA(hkRKey, "UninstallString", 0, &dwType, (LPBYTE)szBuffer, &dwNameLen);
            softinfo.m_strUninstallPth = szBuffer;
            dwNameLen                  = 255;
            memset(szBuffer, 0, 255);

            if (!softinfo.m_strSoftName.empty())
            {
                if (strBuffer[0] == 'K' && strBuffer[1] == 'B')
                    continue;
                else
                {

                    if (auto iter = std::find_if(softInfos.begin(), softInfos.end(),
                                                 [&softinfo](const SoftInfo &vecSoftInfo) {
                                                     return softinfo.m_strSoftName == vecSoftInfo.m_strSoftName;
                                                 });
                        iter == softInfos.end())
                        softInfos.push_back(softinfo);
                }
            }
        }
        RegCloseKey(hkResult);
    }
    return 0;
}