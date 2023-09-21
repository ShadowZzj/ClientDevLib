#include "SandBox.h"
#include <Windows.h>
#include <spdlog/spdlog.h>
#include <string>
#include <tchar.h>
#include <vector>

namespace zzj::AntiVM
{
std::string LoadedDlls()
{
    /* Some vars */
    HMODULE hDll;

    /* Array of strings of blacklisted dlls */
    std::vector<std::string> szDlls = {
        "avghookx.dll",  // AVG
        "avghooka.dll",  // AVG
        "snxhk.dll",     // Avast
        "sbiedll.dll",   // Sandboxie
        "dbghelp.dll",   // WindBG
        "api_log.dll",   // iDefense Lab
        "dir_watch.dll", // iDefense Lab
        "pstorec.dll",   // SunBelt Sandbox
        "vmcheck.dll",   // Virtual PC
        "wpespy.dll",    // WPE Pro
        "cmdvrt64.dll",  // Comodo Container
        "cmdvrt32.dll",  // Comodo Container

    };

    for (int i = 0; i < szDlls.size(); i++)
    {
        hDll = GetModuleHandleA(szDlls[i].c_str());
        if (hDll != NULL)
        {
            return szDlls[i];
        }
    }
    return "";
}
int SandBoxCheck()
{
    std::string dllName = LoadedDlls();
    if (dllName != "")
    {
        spdlog::info("SBCheck: {}", dllName);
        return 1;
    }
    return 0;
}

}; // namespace zzj::AntiVM