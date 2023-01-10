#include "File.h"
#include <Windows/util/HandleHelper.h>

namespace zzj
{

File::FileInfo::FileInfo(const std::string &imageName) : m_imageName(imageName)
{
}

File::FileInfo::~FileInfo()
{
}

 std::int64_t File::FileInfo::GetFileSize()
{
    if (m_imageName.empty())
        return -1;

    ScopeKernelHandle fileHandle = CreateFileA(m_imageName.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (fileHandle == INVALID_HANDLE_VALUE)
        return -2;

    return ::GetFileSize(fileHandle, NULL);
}

File::File(const std::string &imageName) : m_imageName(imageName)
{
}

File::~File()
{
}


zzj::File::FileInfo File::GetFileInfo()
{
    return FileInfo(m_imageName);
}


} // namespace zzj