#include <General/util/Application/Deeplink.h>
#include <Windows/util/Registry/WinReg.hpp>
#include <General/util/StrUtil.h>
namespace zzj
{
int Deeplink::Register(const std::string &module, const std::string &path)
{
    std::wstring wmodule = zzj::str::utf82w(module);
    winreg::RegKey rootKey(HKEY_CLASSES_ROOT, wmodule);
    rootKey.SetStringValue(L"URL Protocol", L"");
    winreg::RegKey shellKey(HKEY_CLASSES_ROOT, wmodule + L"\\shell");
    winreg::RegKey openKey(HKEY_CLASSES_ROOT, wmodule + L"\\shell\\open");
    winreg::RegKey commandKey(HKEY_CLASSES_ROOT, wmodule + L"\\shell\\open\\command");
    commandKey.SetStringValue(L"", L"\"" + zzj::str::utf82w(path) + L"\" \"%1\"");
    return 0;
}

int Deeplink::Unregister(const std::string &module, const std::string &path) 
{ 
    std::wstring wmodule = zzj::str::utf82w(module);
    winreg::RegKey rootKey(HKEY_CLASSES_ROOT);
    rootKey.DeleteTree(wmodule);
    return 0;
}
};  // namespace zzj