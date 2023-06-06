#include <General/util/Exception/Exception.h>
#include <Windows/util/Device/DXError/dxerr.h>
using namespace zzj;
Win32Exception::Win32Exception(int line, const std::string &file, const std::string &func, HRESULT hr) noexcept
    : Exception(line, file, func, TranslateErrorCode(hr)), hr(hr)
{
}

std::string Win32Exception::TranslateErrorCode(HRESULT hr) noexcept
{
    char *pMsgBuf = nullptr;
    // windows will allocate memory for err string and make our pointer point to it
    DWORD nMsgLen = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, hr,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&pMsgBuf), 0, nullptr);
    // 0 string length returned indicates a failure
    if (nMsgLen == 0)
    {
        return "Unidentified error code";
    }
    // copy error string from windows-allocated buffer to std::string
    std::string errorString = pMsgBuf;
    // free windows buffer
    LocalFree(pMsgBuf);
    return errorString;
}

HRESULT Win32Exception::GetErrorCode() const noexcept
{
    return hr;
}

std::string Win32Exception::GetErrorString() const noexcept
{
    return TranslateErrorCode(hr);
}
DXException::DXException(int line, const std::string &file, const std::string &func, HRESULT hr) noexcept
    : Exception(line, file, func, TranslateErrorCode(hr)), hr(hr)
{
}
std::string DXException::TranslateErrorCode(HRESULT hr) noexcept
{
    char buf[512];
	DXGetErrorDescriptionA( hr,buf,sizeof( buf ) );
	return buf;
}
HRESULT DXException::GetErrorCode() const noexcept
{
    return hr;
}
std::string DXException::GetErrorString() const noexcept
{
    return TranslateErrorCode(hr);
}
