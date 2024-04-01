#include "File.h"
#include <Windows/util/HandleHelper.h>
#include <iostream>
#include <fstream>
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

std::vector<BYTE> File::Read(std::uintptr_t offset, std::size_t size)
{
    if (m_imageName.empty())
		throw std::runtime_error("image name is empty");
    
    std::ifstream file(m_imageName, std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("open file failed");

    file.seekg(offset, std::ios::beg);
    std::vector<BYTE> buffer(size);
    file.read(reinterpret_cast<char *>(buffer.data()), size);
    file.close();
    return buffer;
}

File::~File()
{
}


zzj::File::FileInfo File::GetFileInfo()
{
    return FileInfo(m_imageName);
}


} // namespace zzj