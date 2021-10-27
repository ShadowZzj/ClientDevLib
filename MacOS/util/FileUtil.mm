#include "FileUtil.h"
#import <foundation/foundation.h>
#include <fstream>
#include <fts.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
using namespace std;

int zzj::File::ReadFileAtOffset(const char *fileName, char *buf, size_t size, int pos)
{
    if (0 != IsFileExist(fileName))
        return -1;
    
    ifstream file(fileName, ios::in | ios::binary);
    if (file.is_open())
    {
        file.seekg(0, ios::beg);
        file.read(buf, size);
        file.close();
    }
    else
    {
        cout << "Unable to open file";
        return 0;
    }
    return size;
}

std::string zzj::File::GetTempDir()
{
    NSString *tempDir = NSTemporaryDirectory();
    if (tempDir == nil)
        tempDir = @"/tmp";
    
    NSString *tmp                = [tempDir stringByAppendingPathComponent:@"temp.kepm.NetworkManager"];
    const char *fsTemplate       = [tmp fileSystemRepresentation];
    NSMutableData *bufferData    = [NSMutableData dataWithBytes:fsTemplate length:strlen(fsTemplate) + 1];
    char *buffer                 = (char *)[bufferData mutableBytes];
    char *result                 = mkdtemp(buffer);
    NSString *temporaryDirectory = [[NSFileManager defaultManager] stringWithFileSystemRepresentation:buffer
                                                                                               length:strlen(buffer)];
    return [temporaryDirectory UTF8String];
}

int zzj::File::IsFileExist(const char *filePath)
{
    ifstream file(filePath, ios::in | ios::binary);
    if (file.is_open())
        return 0;
    return -1;
}

std::string zzj::File::GetCurrentModulePath()
{
    char curPath[PATH_MAX];
    std::string path;
    if (!getcwd(curPath, sizeof(curPath)))
    {
        path = "";
        goto exit;
    }
    
    path = curPath;
    path += "/";
exit:
    return path;
}

int zzj::File::IsDirExist(const char *dirPath)
{
    struct stat info;
    if (stat(dirPath, &info) != 0)
        return -1;
    else if (info.st_mode & S_IFDIR)
        return 0;
    else
        return -2;
}

unsigned long zzj::File::GetFileSize(const char *filePath)
{
    struct stat buf;
    if (stat(filePath, &buf) < 0)
    {
        return 0;
    }
    return (unsigned long)buf.st_size;
}

int zzj::File::RemoveDirRecursive(const char *dir)
{
    int ret   = 0;
    FTS *ftsp = NULL;
    FTSENT *curr;
    
    // Cast needed (in C) because fts_open() takes a "char * const *", instead
    // of a "const char * const *", which is only allowed in C++. fts_open()
    // does not modify the argument.
    char *files[] = {(char *)dir, NULL};
    
    // FTS_NOCHDIR  - Avoid changing cwd, which could cause unexpected behavior
    //                in multithreaded programs
    // FTS_PHYSICAL - Don't follow symlinks. Prevents deletion of files outside
    //                of the specified directory
    // FTS_XDEV     - Don't cross filesystem boundaries
    ftsp = fts_open(files, FTS_NOCHDIR | FTS_PHYSICAL | FTS_XDEV, NULL);
    if (!ftsp)
    {
        fprintf(stderr, "%s: fts_open failed: %s\n", dir, strerror(errno));
        ret = -1;
        goto finish;
    }
    
    while ((curr = fts_read(ftsp)))
    {
        switch (curr->fts_info)
        {
            case FTS_NS:
            case FTS_DNR:
            case FTS_ERR:
                fprintf(stderr, "%s: fts_read error: %s\n", curr->fts_accpath, strerror(curr->fts_errno));
                break;
                
            case FTS_DC:
            case FTS_DOT:
            case FTS_NSOK:
                // Not reached unless FTS_LOGICAL, FTS_SEEDOT, or FTS_NOSTAT were
                // passed to fts_open()
                break;
                
            case FTS_D:
                // Do nothing. Need depth-first search, so directories are deleted
                // in FTS_DP
                break;
                
            case FTS_DP:
            case FTS_F:
            case FTS_SL:
            case FTS_SLNONE:
            case FTS_DEFAULT:
                if (remove(curr->fts_accpath) < 0)
                {
                    fprintf(stderr, "%s: Failed to remove: %s\n", curr->fts_path, strerror(curr->fts_errno));
                    ret = -1;
                }
                break;
        }
    }
    
finish:
    if (ftsp)
    {
        fts_close(ftsp);
    }
    
    return ret;
}

// with "/"
int zzj::File::CopyFilesFromTo(const char *srcPath, const char *dstPath)
{
    boost::filesystem::path source = srcPath;
    boost::filesystem::path destination = dstPath;
    namespace fs = boost::filesystem;
    try
    {
        // Check whether the function call is valid
        if(
           !fs::exists(source) ||
           !fs::is_directory(source)
           )
        {
            std::cerr << "Source directory " << source.string()
            << " does not exist or is not a directory." << '\n'
            ;
            return -1;
        }
        if(!fs::exists(destination))
        {
            // Create the destination directory
            if(!fs::create_directory(destination))
            {
                std::cerr << "Unable to create destination directory"
                << destination.string() << '\n'
                ;
                return -3;
            }        }
    }
    catch(fs::filesystem_error const & e)
    {
        std::cerr << e.what() << '\n';
        return -4;
    }
    // Iterate through the source directory
    for(
        fs::directory_iterator file(source);
        file != fs::directory_iterator(); ++file
        )
    {
        try
        {
            fs::path current(file->path());
            if(fs::is_directory(current))
            {
                // Found directory: Recursion
                if(0 != CopyFilesFromTo(current.c_str(),(destination / current.filename()).c_str()))
                {
                    return -5;
                }
            }
            else
            {
                // Found file: Copy
                fs::copy_file(
                              current,
                              destination / current.filename(),
                              fs::copy_option::overwrite_if_exists
                              );
            }
        }
        catch(fs::filesystem_error const & e)
        {
            std:: cerr << e.what() << '\n';
        }
    }
    return 0;
}
#include <dlfcn.h>
#include <mach-o/dyld.h>
std::string zzj::File::GetExecutablePath()
{
    char exePath[PATH_MAX];
    uint32_t len = sizeof(exePath);
    if (_NSGetExecutablePath(exePath, &len) != 0)
    {
        exePath[0] = '\0'; // buffer too small (!)
    }
    else
    {
        *(strrchr(exePath, '/')) = '\0';
        // resolve symlinks, ., .. if possible
        char *canonicalPath = realpath(exePath, NULL);
        if (canonicalPath != NULL)
        {
            strncpy(exePath, canonicalPath, len);
            free(canonicalPath);
        }
    }
    return exePath;
}

int zzj::File::MkdirRecursive(std::string dirPath, int mode)
{
    char tmp[PATH_MAX];
    char *p = NULL;
    size_t len;
    int result = 0;
    umask(0);
    snprintf(tmp, sizeof(tmp), "%s", dirPath.c_str());
    len = strlen(tmp);
    if (tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    for (p = tmp + 1; *p; p++)
    if (*p == '/')
    {
        *p     = 0;
        result = mkdir(tmp, mode);
        if (0 != result && errno != EEXIST)
        {
            result = errno;
            goto exit;
        }
        
        result = chmod(tmp, mode);
        *p = '/';
    }
    result = mkdir(tmp, mode);
    if (0 != result && errno != EEXIST)
    {
        result = errno;
        goto exit;
    }
    result = chmod(tmp, mode);
exit:
    return result;
}

int zzj::File::ChmodIfExe(std::string fileFullPath,mode_t mode)
{
    std::string fileLastComponent = fileFullPath.substr(fileFullPath.find_last_of("\\/") + 1);
    if (fileLastComponent.find(".") != std::string::npos)
        return 0;

    int result = chmod(fileFullPath.c_str(), mode);
    if (0 != result)
        return -1;
    else
        return 0;
}
