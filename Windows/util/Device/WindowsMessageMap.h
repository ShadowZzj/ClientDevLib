
#pragma once
#include <Windows.h>
#include <string>
#include <unordered_map>


namespace zzj
{
class WindowsMessageMap
{
  public:
    WindowsMessageMap() noexcept;
    std::string operator()(DWORD msg, LPARAM lp, WPARAM wp) const noexcept;

  private:
    std::unordered_map<DWORD, std::string> map;
};
}; // namespace zzj