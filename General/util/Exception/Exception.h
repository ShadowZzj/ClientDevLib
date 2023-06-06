#pragma once
#include <exception>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace zzj
{
class Exception : public std::exception
{
  public:
    Exception(int line, const std::string &file, const std::string &func, const std::string &msg) noexcept;
    const char *what() const noexcept override;

  protected:
    int line;
    std::string file;
    std::string msg;
    std::string func;
};
#define ZZJ_EXCEPTION(msg) Exception(__LINE__, __FILE__, __func__, msg)
#ifdef _WIN32
class Win32Exception : public Exception
{
  public:
    Win32Exception(int line, const std::string &file, const std::string &func, HRESULT hr) noexcept;
    static std::string TranslateErrorCode(HRESULT hr) noexcept;
    HRESULT GetErrorCode() const noexcept;
    std::string GetErrorString() const noexcept;

  private:
    HRESULT hr;
};
class DXException : public Exception
{
  public:
    DXException(int line, const std::string &file, const std::string &func, HRESULT hr) noexcept;
    static std::string TranslateErrorCode(HRESULT hr) noexcept;
    HRESULT GetErrorCode() const noexcept;
    std::string GetErrorString() const noexcept;

  private:
    HRESULT hr;
};

#define ZZJ_WIN32_EXCEPTION(hr) zzj::Win32Exception(__LINE__, __FILE__, __func__, hr)
#define ZZJ_LAST_WIN32_EXCEPTION() zzj::Win32Exception(__LINE__, __FILE__, __func__, GetLastError())
#define ZZJ_DX_EXCEPTION(hr) zzj::DXException(__LINE__, __FILE__, __func__, hr)
#endif
}; // namespace zzj