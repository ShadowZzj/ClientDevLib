#include "ProfileUtil.h"
#include "Process.h"
#include <vector>
#include "StringUtil.h"
int zzj::Profile::InstallUserProfile(const std::string &userName, const std::string &profileFilePath)
{
    int result = 0;
    std::vector<std::string> args;
    args.push_back("/System/Library/PreferencePanes/Profiles.prefPane");
    args.push_back(profileFilePath);
    std::wstring output;
    result = zzj::Process::CreateUserProcess("/usr/bin/open", userName.c_str(), args,true);
    if (0 != result)
    {
        result = -1;
        return result;
    }
    sleep(1);
    zzj::Process::ActivateApplication("com.apple.systempreferences");
    return result;
}
int zzj::Profile::InstallSystemProfile(const std::string &profileFilePath)
{
    int result = 0;
    std::vector<std::string> args;
    args.push_back("/System/Library/PreferencePanes/Profiles.prefPane");
    args.push_back(profileFilePath);
    std::wstring output;
    result = zzj::Process::CreateProcess("/usr/bin/open", args, output, true);
    if (0 != result)
    {
        result = -1;
        return result;
    }
    sleep(1);
    zzj::Process::ActivateApplication("com.apple.systempreferences");
    return result;
}
std::optional<bool> zzj::Profile ::IsUserProfileInstalled(const std::string &userName, const std::string &profileIdentifier)
{
    int result = 0;
    std::vector<std::string> args;
    std::wstring outPut;
    std::string strOutPut;
    args.push_back("-L");

    result = zzj::Process::CreateUserProcess("/usr/bin/profiles", userName.c_str(), args, outPut);
    if (0 != result)
        return {};

    if (outPut.empty())
        return {};

    strOutPut = Wstr2Str(outPut);
    if (strOutPut.empty())
        return {};

    if (strOutPut.npos != strOutPut.find(profileIdentifier.c_str()))
        return true;

    return false;
}
std::optional<bool> zzj::Profile ::IsSystemProfileInstalled(const std::string &profileUUID)
{
    int result = 0;
    std::vector<std::string> args;
    std::wstring outPut;
    std::string strOutPut;
    args.push_back("-L");

    result = zzj::Process::CreateProcess("/usr/bin/profiles", args, outPut, true);
    if (0 != result)
        return {};

    if (outPut.empty())
        return {};

    strOutPut = Wstr2Str(outPut);
    if (strOutPut.empty())
        return {};

    if (strOutPut.npos != strOutPut.find(profileUUID.c_str()))
        return true;

    return false;
}
