#pragma once
#if _WIN32 || _WIN64
#if _WIN64
#define ENV64BIT
#else
#define ENV32BIT
#endif
#endif

#include <Windows.h>
#include <General/util/File/File.h>
#include <General/util/StrUtil.h>
#include <boost/filesystem.hpp>
#include <Windows/util/HandleHelper.h>
#include <General/util/BaseUtil.hpp>
#include <spdlog/fmt/fmt.h>


namespace zzj
{
inline static std::wstring ConvertUTCTimeToLocalTime(const FILETIME &utcTime)
{
    SYSTEMTIME stUTC, stLocal;
    FileTimeToSystemTime(&utcTime, &stUTC);
    SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal);

    std::string timeStr = fmt::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.{:03d}",
                                      stLocal.wYear, stLocal.wMonth, stLocal.wDay, stLocal.wHour,
                                      stLocal.wMinute, stLocal.wSecond, stLocal.wMilliseconds);
    return zzj::str::utf82w(timeStr);
}
}  // namespace zzj