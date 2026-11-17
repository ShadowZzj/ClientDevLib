#ifndef FileUtil_win32_h
#define FileUtil_win32_h

#include <memory>
#include <mutex>

class FileUtils_Win32
{

  public:
    inline bool getFileMD5(const std::string &filePath, std::string &md5);
    inline bool getFileSize(const std::string &filePath, std::int32_t &md5);
    inline bool getFileSign(const std::string &filePath, std::string &sign);

  public:
    static inline FileUtils_Win32 *getInstancePtr()
    {
        std::lock_guard<std::mutex> lck(m_instanceLck);
        static FileUtils_Win32 *_instance = nullptr;
        if (nullptr == _instance)
        {
            _instance = new FileUtils_Win32();
            _instance->_init();
        }
        return _instance;
    }

  protected:
    inline FileUtils_Win32();

  protected:
    inline bool _init();

    std::mutex m_initLck;
    static std::mutex m_instanceLck;
    bool m_inited;
};
#include "FileUtil_win32.inl"
#endif