#include "DxgiInfoManager.h"
#include <General/util/Exception/Exception.h>
#include "dxerr.h"

using namespace zzj;
DXException::DXException(int line, const std::string &file, const std::string &func, HRESULT hr) noexcept
    : Exception(line, file, func, TranslateErrorCode(hr)), hr(hr)
{
}
std::string DXException::TranslateErrorCode(HRESULT hr) noexcept
{
    char buf[512];
    DXGetErrorDescriptionA(hr, buf, sizeof(buf));
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

DXInfoException::DXInfoException(int line, const std::string &file, const std::string &func,
                                 const std::vector<std::string> &messages) noexcept
    : Exception(line, file, func, GetErrorMessage(messages))
{
}
std::string DXInfoException::GetErrorMessage(const std::vector<std::string> &messages) noexcept
{
    std::string errorMessage;
    for (const auto &message : messages)
    {
        errorMessage += message;
        errorMessage += "\n";
    }
    if (errorMessage.empty())
    {
        errorMessage = "No error message";
    }
    return errorMessage;
}