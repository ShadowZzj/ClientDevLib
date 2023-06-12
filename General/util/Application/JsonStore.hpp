#pragma once
#include <boost/filesystem.hpp>
#include <json.hpp>
#include <mutex>
#include <unordered_map>
#include <variant>
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

    template <typename T> Result<T> Get(const std::string &key)
    {
        auto path = GetStorePath();
        if (path.empty())
            return ExceptionOccured{"Path does not exist"};

        auto mutexPtr = GetMutexForPath(path);
        std::lock_guard<std::mutex> lock(*mutexPtr);

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

    template <typename T> Result<T> Set(const std::string &key, const T &value)
    {
        auto path = GetStorePath();

        // If the path does not exist, and the path is valid, create it
        if (path.empty())
            return ExceptionOccured{"Path is empty"};

        auto mutexPtr = GetMutexForPath(path);
        std::lock_guard<std::mutex> lock(*mutexPtr);

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
    inline static std::unordered_map<std::string, std::unique_ptr<std::mutex>> mutexes;

    std::mutex *GetMutexForPath(const boost::filesystem::path &path)
    {
        std::lock_guard<std::mutex> lock(mapMutex);
        auto it = mutexes.find(path.string());
        if (it == mutexes.end())
        {
            // If the mutex for this path does not exist yet, create one
            auto newMutexPtr       = std::make_unique<std::mutex>();
            auto ptr               = newMutexPtr.get();
            mutexes[path.string()] = std::move(newMutexPtr);
            return ptr;
        }
        else
        {
            // If the mutex for this path exists, return it
            return it->second.get();
        }
    }
};
}; // namespace zzj