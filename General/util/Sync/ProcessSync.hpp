#pragma once
#ifdef _WIN32
#include <Windows/util/File/FileHelper.h>
#include <direct.h>
#include <windows.h>
#include <AclAPI.h>
#include <shlobj.h>
#else
#include <MacOS/util/FileUtil.h>
#endif
#include <General/util/File/File.h>
#include <algorithm>
#include <boost/filesystem.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <stdlib.h>
#include <string>

namespace zzj
{
class ProcessSync
{
    inline static const std::string folderName = "zzjProcessSync";

  public:
    ProcessSync(ProcessSync &&other) noexcept = default;
    ProcessSync &operator=(ProcessSync &&other) noexcept = default;

    ProcessSync(const ProcessSync &) = delete;
    ProcessSync &operator=(const ProcessSync &) = delete;

    ProcessSync(const std::string &globalUniqueName)
    {
        boost::filesystem::path ret;

#ifdef _WIN32
        std::string filePath = zzj::FileHelper::GetProgramDataPath(folderName);
        if (!zzj::IsDirExist(filePath.c_str()))
            _mkdir(filePath.c_str());
        ret = filePath;
#else
        std::string filePath = zzj::File::GetSystemAppDataFolder() + "/" + folderName;
        if (!zzj::IsDirExist(filePath.c_str()))
            zzj::File::MkdirRecursive(filePath, S_IRWXG | S_IRWXO | S_IRWXU);
        else
        {
            //检查文件夹权限是否是777
            if (boost::filesystem::status(filePath).permissions() != (boost::filesystem::perms::owner_all | boost::filesystem::perms::group_all | boost::filesystem::perms::others_all))
            {
                boost::filesystem::permissions(filePath, boost::filesystem::all_all);
            }
        }
        ret = filePath;
#endif
        boost::filesystem::path globalLockPath = ret / "global.lock";
        {
            std::ofstream ofs(globalLockPath.string(), std::ios::app);
            #ifdef _WIN32
            SetNamedSecurityInfoA((LPSTR)globalLockPath.string().c_str(), 
                                 SE_FILE_OBJECT,                       
                                 DACL_SECURITY_INFORMATION,            
                                 NULL,                                 
                                 NULL,                                 
                                 NULL,                                
                                 NULL                                  
                                 );
            #else
            if (boost::filesystem::status(globalLockPath).permissions() != (boost::filesystem::perms::owner_all | boost::filesystem::perms::group_all | boost::filesystem::perms::others_all))
            {
                boost::filesystem::permissions(globalLockPath, boost::filesystem::all_all);
            }
            #endif
        }
        boost::interprocess::file_lock globalLock(globalLockPath.string().c_str());

        boost::interprocess::scoped_lock<boost::interprocess::file_lock> lock(globalLock);
        std::string fileName                 = std::to_string(std::hash<std::string>{}(globalUniqueName));
        boost::filesystem::path fileLockPath = ret / (fileName + ".lock");
        {
            std::ofstream ofs(fileLockPath.string(), std::ios::app);
#ifdef _WIN32
            SetNamedSecurityInfoA((LPSTR)fileLockPath.string().c_str(), 
                                  SE_FILE_OBJECT,                         
                                  DACL_SECURITY_INFORMATION,             
                                  NULL,                                   
                                  NULL,                                   
                                  NULL,                                   
                                  NULL                                    
            );
#else
            if (boost::filesystem::status(fileLockPath).permissions() != (boost::filesystem::perms::owner_all | boost::filesystem::perms::group_all | boost::filesystem::perms::others_all))
            {
                boost::filesystem::permissions(fileLockPath, boost::filesystem::all_all);
            }
#endif
        }
        m_fileLock         = std::make_unique<boost::interprocess::file_lock>(fileLockPath.string().c_str());
        m_globalUniqueName = globalUniqueName;
    }
    ~ProcessSync()
    {
    }
    void lock()
    {
        m_fileLock->lock();
    }
    void unlock()
    {
        m_fileLock->unlock();
    }
    bool try_lock()
    {
        return m_fileLock->try_lock();
    }

  private:
    std::string m_globalUniqueName;
    std::unique_ptr<boost::interprocess::file_lock> m_fileLock;
};
} // namespace zzj
