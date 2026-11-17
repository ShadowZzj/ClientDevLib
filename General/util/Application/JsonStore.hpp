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
#include <General/util/Exception/Exception.h>
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
        enum class Type
        {
            JsonParseError,
            UnknownError
        };
        std::string message;
        Type type = Type::UnknownError;
    };

    template <typename T>
    using Result = std::variant<T, KeyNotExist, ExceptionOccured>;

    Result<nlohmann::json> Get()
    {
        auto path = GetStorePath();
        if (path.empty()) return ExceptionOccured{"Path does not exist"};
        auto mutexPtr = GetMutexForPath(path);
        std::lock_guard<zzj::ProcessSync> lock(mutexPtr);

        if (!boost::filesystem::exists(path))
        {
            try
            {
                std::ofstream ofs(path.string(), std::ios::binary);
                ofs << Encrypt("{}");
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
            std::ifstream ifs(path.string(), std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(ifs)),
                                (std::istreambuf_iterator<char>()));
            ifs.close();
            nlohmann::json j = nlohmann::json::parse(content, nullptr, false);
            if (j.is_discarded())
            {
                // try to decrypt
                content = Decrypt(content);
                j = nlohmann::json::parse(content, nullptr, false);
                if (j.is_discarded())
                {
                    return ExceptionOccured{"Failed to parse json",
                                            ExceptionOccured::Type::JsonParseError};
                }
            }

            return j;
        }
        catch (const nlohmann::json::parse_error &e)
        {
            return ExceptionOccured{e.what(), ExceptionOccured::Type::JsonParseError};
        }
        catch (const CryptoException &e)
        {
            return ExceptionOccured{e.what(), ExceptionOccured::Type::JsonParseError};
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
    template <typename T>
    Result<T> Get(const std::string &key)
    {
        auto path = GetStorePath();
        if (path.empty()) return ExceptionOccured{"Path does not exist"};

        auto mutexPtr = GetMutexForPath(path);
        std::lock_guard<zzj::ProcessSync> lock(mutexPtr);

        if (!boost::filesystem::exists(path))
        {
            try
            {
                std::ofstream ofs(path.string(), std::ios::binary);
                ofs << Encrypt("{}");
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
            std::ifstream ifs(path.string(), std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(ifs)),
                                (std::istreambuf_iterator<char>()));
            ifs.close();
            nlohmann::json j = nlohmann::json::parse(content, nullptr, false);
            if (j.is_discarded())
            {
                // try to decrypt
                content = Decrypt(content);
                j = nlohmann::json::parse(content, nullptr, false);
                if (j.is_discarded())
                {
                    return ExceptionOccured{"Failed to parse json",
                                            ExceptionOccured::Type::JsonParseError};
                }
            }

            if (j.find(key) == j.end()) return KeyNotExist();
            return j[key].get<T>();
        }
        catch (const nlohmann::json::parse_error &e)
        {
            return ExceptionOccured{e.what(), ExceptionOccured::Type::JsonParseError};
        }
        catch (const CryptoException &e)
        {
            return ExceptionOccured{e.what(), ExceptionOccured::Type::JsonParseError};
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
        if (path.empty()) return ExceptionOccured{"Path is empty"};

        auto mutexPtr = GetMutexForPath(path);
        std::lock_guard<zzj::ProcessSync> lock(mutexPtr);

        try
        {
            std::ofstream ofs(path.string(), std::ios::binary);
            ofs << Encrypt(content.dump(4));
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
    template <typename T>
    Result<T> Set(const std::string &key, const T &value)
    {
        auto path = GetStorePath();

        // If the path does not exist, and the path is valid, create it
        if (path.empty()) return ExceptionOccured{"Path is empty"};

        auto mutexPtr = GetMutexForPath(path);
        std::lock_guard<zzj::ProcessSync> lock(mutexPtr);

        if (!boost::filesystem::exists(path))
        {
            try
            {
                std::ofstream ofs(path.string(), std::ios::binary);
                ofs << Encrypt("{}");
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
            std::ifstream ifs(path.string(), std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(ifs)),
                                (std::istreambuf_iterator<char>()));
            ifs.close();
            nlohmann::json j = nlohmann::json::parse(content, nullptr, false);
            if (j.is_discarded())
            {
                // try to decrypt
                content = Decrypt(content);
                j = nlohmann::json::parse(content, nullptr, false);
                if (j.is_discarded())
                {
                    return ExceptionOccured{"Failed to parse json",
                                            ExceptionOccured::Type::JsonParseError};
                }
            }

            j[key] = value;
            boost::filesystem::path temp_path = path.string() + ".tmp";
            std::ofstream ofs(temp_path.string(), std::ios::binary);
            if (!ofs.is_open())
            {
                throw std::runtime_error("Failed to open file for writing");
            }

            ofs << Encrypt(j.dump(4));
            ofs.close();

            boost::filesystem::rename(temp_path, path);
            return value;
        }
        catch (const nlohmann::json::parse_error &e)
        {
            return ExceptionOccured{e.what(), ExceptionOccured::Type::JsonParseError};
        }
        catch (const CryptoException &e)
        {
            return ExceptionOccured{e.what(), ExceptionOccured::Type::JsonParseError};
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
    virtual std::string Encrypt(const std::string &content) { return content; }
    virtual std::string Decrypt(const std::string &content) { return content; }

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
};  // namespace zzj
