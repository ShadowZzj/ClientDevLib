#ifndef _MAC_PROFILEUTIL_H
#define _MAC_PROFILEUTIL_H
#include <optional>
#include <string>
namespace zzj
{

class Profile
{
  public:
    static int InstallUserProfile(const std::string &userName, const std::string &profileFilePath);
    static int InstallSystemProfile(const std::string &profileFilePath);
    static std::optional<bool> IsUserProfileInstalled(const std::string &userName, const std::string &profileUUID);
    static std::optional<bool> IsSystemProfileInstalled(const std::string &profileIdentifier);
};
}; // namespace zzj
#endif
