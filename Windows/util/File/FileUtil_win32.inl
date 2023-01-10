#include "FileUtil_win32.h"
#include <Windows/WinUtilLib/WindowsUtilLib/PEhelper.h>
#include <Windows/WinUtilLib/WindowsUtilLib/StrUtil.h>
#include <boost/filesystem.hpp>
#include <general/util/Md5.h>
__declspec(selectany) std::mutex FileUtils_Win32::m_instanceLck;
inline bool FileUtils_Win32::_init()
{
    if (m_inited)
        return true;

    m_inited = true;
    return m_inited;
}

inline FileUtils_Win32::FileUtils_Win32()
{
    m_inited = false;
}

inline bool FileUtils_Win32::getFileMD5(const std::string &filePath, std::string &md5)
{
    return MD5::GetFileMD5(filePath, md5);
}

inline bool FileUtils_Win32::getFileSize(const std::string &filePath, std::int32_t &size)
{
    size = boost::filesystem::file_size(filePath);
    return true;
}

inline bool FileUtils_Win32::getFileSign(const std::string &filePath, std::string &sign)
{
    zzj::PEFile::FileSign::PSPROG_PUBLISHERINFO info = new zzj::PEFile::FileSign::SPROG_PUBLISHERINFO();
    if (zzj::PEFile::FileSign(filePath).GetProgAndPublisherInfo(info))
    {
        sign = str::Wstr2Str(info->lpszProgramName);
    }
    delete info;
    return true;
}
