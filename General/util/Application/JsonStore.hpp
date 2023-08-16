#pragma once
#include <General/util/config.h>
#include <algorithm>
#include <boost/filesystem.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <json.hpp>
#include <mutex>
#include <unordered_map>
#include <variant>
#include <spdlog/spdlog.h>
#include <General/util/Sync/ProcessSync.hpp>
namespace zzj
{
class JsonStore
{
  public:
    struct KeyNotExist
    {
    };
    struct ExceptionOccured
    {
        std::string message;
    };

    template <typename T> using Result = std::variant<T, KeyNotExist, ExceptionOccured>;

    Result<nlohmann::json> Get()
    {
        auto path = GetStorePath();
        if (path.empty())
            return ExceptionOccured{"Path does not exist"};
        auto mutexPtr = GetMutexForPath(path);
        std::lock_guard<zzj::ProcessSync> lock(mutexPtr);

        if (!boost::filesystem::exists(path))
        {
            try
            {
                std::ofstream ofs(path.string());
                ofs << "{}";
            }
            catch (const std::exception &e)
            {
                return ExceptionOccured{e.what()};
            }
            catch (...)
            {
                return ExceptionOccured{"Unknown exception"};
            }
        }
        try
        {
            std::ifstream ifs(path.string());
            nlohmann::json j;
            ifs >> j;
            return j;
        }
        catch (const std::exception &e)
        {
            return ExceptionOccured{e.what()};
        }
        catch (...)
        {
            return ExceptionOccured{"Unknown exception"};
        }
    }
    template <typename T> Result<T> Get(const std::string &key)
    {
        auto path = GetStorePath();
        if (path.empty())
            return ExceptionOccured{"Path does not exist"};

        auto mutexPtr = GetMutexForPath(path);
        std::lock_guard<zzj::ProcessSync> lock(mutexPtr);

        if (!boost::filesystem::exists(path))
        {
            try
            {
                std::ofstream ofs(path.string());
                ofs << "{}";
            }
            catch (const std::exception &e)
            {
                return ExceptionOccured{e.what()};
            }
            catch (...)
            {
                return ExceptionOccured{"Unknown exception"};
            }
        }
        try
        {
            std::ifstream ifs(path.string());
            nlohmann::json j;
            ifs >> j;
            if (j.find(key) == j.end())
                return KeyNotExist();
            return j[key].get<T>();
        }
        catch (const std::exception &e)
        {
            return ExceptionOccured{e.what()};
        }
        catch (...)
        {
            return ExceptionOccured{"Unknown exception"};
        }
    }

    Result<nlohmann::json> Set(const nlohmann::json &content)
    {
        auto path = GetStorePath();

        // If the path does not exist, and the path is valid, create it
        if (path.empty())
            return ExceptionOccured{"Path is empty"};

        auto mutexPtr = GetMutexForPath(path);
        std::lock_guard<zzj::ProcessSync> lock(mutexPtr);

        try
        {
            std::ofstream ofs(path.string());
            ofs << content.dump(4);
            return content;
        }
        catch (const std::exception &e)
        {
            return ExceptionOccured{e.what()};
        }
        catch (...)
        {
            return ExceptionOccured{"Unknown exception"};
        }
    }
    template <typename T> Result<T> Set(const std::string &key, const T &value)
    {
        auto path = GetStorePath();

        // If the path does not exist, and the path is valid, create it
        if (path.empty())
            return ExceptionOccured{"Path is empty"};

        auto mutexPtr = GetMutexForPath(path);
        std::lock_guard<zzj::ProcessSync> lock(mutexPtr);

        if (!boost::filesystem::exists(path))
        {
            try
            {
                std::ofstream ofs(path.string());
                ofs << "{}";
            }
            catch (const std::exception &e)
            {
                return ExceptionOccured{e.what()};
            }
            catch (...)
            {
                return ExceptionOccured{"Unknown exception"};
            }
        }

        try
        {
            std::ifstream ifs(path.string());
            nlohmann::json j;
            ifs >> j;
            j[key] = value;
            std::ofstream ofs(path.string());
            ofs << j.dump(4);
            return value;
        }
        catch (const std::exception &e)
        {
            return ExceptionOccured{e.what()};
        }
        catch (...)
        {
            return ExceptionOccured{"Unknown exception"};
        }
    }

    virtual boost::filesystem::path GetStorePath() = 0;

  private:
    inline static std::mutex mapMutex;
    inline static std::unordered_map<std::string, std::unique_ptr<zzj::ProcessSync>> mutexes;

    zzj::ProcessSync GetMutexForPath(const boost::filesystem::path &path)
    {
        static const std::string suffix = "/zzj/jsonstore";
        std::lock_guard<std::mutex> lock(mapMutex);
        std::string uniqueName = ClientDevLibConfig::clientDevLibUUID + path.string() + suffix;
        return zzj::ProcessSync(uniqueName);
    }
};
}; // namespace zzj
