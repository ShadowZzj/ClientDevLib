#pragma once
#include <exception>
#include <string>
#include <vector>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace zzj
{
class Exception : public std::exception
{
   public:
    Exception(int line, const std::string &file, const std::string &func, const std::string &msg,
              int code = 0) noexcept;
    const char *what() const noexcept override;
    int GetCode() const;
    std::string GetMsg() const;

   protected: 
    int line;
    std::string file;
    std::string msg;
    mutable std::string exceptionMessage;
    std::string func;
    int code;
};

class CryptoException : public Exception
{
   public:
    CryptoException(int line, const std::string &file, const std::string &func,
                    const std::string &msg) noexcept;
};

#define ZZJ_EXCEPTION(msg) Exception(__LINE__, __FILE__, __func__, msg)
#define ZZJ_MESSAGE_EXCEPTION(msg) zzj::Exception(__LINE__, __FILE__, __func__, msg)
#define ZZJ_CODE_EXCEPTION(code, msg) zzj::Exception(__LINE__, __FILE__, __func__, msg, code)
#define ZZJ_CRYPTO_EXCEPTION(msg) zzj::CryptoException(__LINE__, __FILE__, __func__, msg)
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

class DXInfoException : public Exception
{
   public:
    DXInfoException(int line, const std::string &file, const std::string &func,
                    const std::vector<std::string> &messages) noexcept;
    static std::string GetErrorMessage(const std::vector<std::string> &messages) noexcept;
};
#define ZZJ_WIN32_EXCEPTION(hr) zzj::Win32Exception(__LINE__, __FILE__, __func__, hr)
#define ZZJ_LAST_WIN32_EXCEPTION() zzj::Win32Exception(__LINE__, __FILE__, __func__, GetLastError())
#define ZZJ_DX_EXCEPTION(hr) zzj::DXException(__LINE__, __FILE__, __func__, hr)
#define ZZJ_DX_INFO_EXCEPTION(messages) zzj::DXInfoException(__LINE__, __FILE__, __func__, messages)
#endif
};  // namespace zzj