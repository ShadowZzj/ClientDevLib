#include <General/util/Exception/Exception.h>
#include <Windows/util/Device/DXError/dxerr.h>
using namespace zzj;
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
