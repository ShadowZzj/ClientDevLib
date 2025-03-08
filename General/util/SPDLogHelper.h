#ifndef _SPDLOGHELP_H_
#define _SPDLOGHELP_H_

#include <json.hpp>
#include <string>
#include <fstream>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#define LEVEL "level"
#define WRITEPATH "write_path"
#define MODULENAME "module_name"
#define LOGMAXSIZE "max_size"
#define LOGMAXFILES "max_files"

#define TRACE "trace"
#define DEBUG "debug"
#define INFO "info"
#define WARN "warn"
#define ERR "err"
#define CRITICAL "critical"
#define OFF "off"
#define FILE_NAME std::filesystem::path(__FILE__).filename().string()


#define SPDLOG_EXPAND(...) __VA_ARGS__
#define SPDLOG_CONCAT_(a, b) a##b
#define SPDLOG_CONCAT(a, b) SPDLOG_CONCAT_(a, b)


#define LOG_MODULE_ARG_COUNT(...) SPDLOG_EXPAND(LOG_MODULE_ARG_COUNT_IMPL(__VA_ARGS__, 5, 4, 3, 2, 1))
#define LOG_MODULE_ARG_COUNT_IMPL(_1, _2, _3, _4, _5, N, ...) N

#define LOG_MODULE_GENERIC(level, ...) \
    SPDLOG_EXPAND(SPDLOG_CONCAT(LOG_MODULE_, LOG_MODULE_ARG_COUNT(__VA_ARGS__))(level, __VA_ARGS__))


#define LOG_MODULE_2(level, module, msg)                                           \
    do                                                                             \
    {                                                                              \
        auto logger_ = spdlog::get(module);                                        \
        if (logger_)                                                               \
        {                                                                          \
            logger_->level("[{}:{}:{}] {}", FILE_NAME, __func__, __LINE__, (msg)); \
        }                                                                          \
        else                                                                       \
        {                                                                          \
            spdlog::level("[{}:{}:{}] {}", FILE_NAME, __func__, __LINE__, (msg));  \
        }                                                                          \
    } while (0)

#define LOG_MODULE_3(level, module, fmt_str, ...)                          \
    do                                                                     \
    {                                                                      \
        auto logger_ = spdlog::get(module);                                \
        if (logger_)                                                       \
        {                                                                  \
            logger_->level("[{}:{}:{}] {}", FILE_NAME, __func__, __LINE__, \
                           fmt::format(fmt_str, __VA_ARGS__));             \
        }                                                                  \
        else                                                               \
        {                                                                  \
            spdlog::level("[{}:{}:{}] {}", FILE_NAME, __func__, __LINE__,  \
                          fmt::format(fmt_str, __VA_ARGS__));              \
        }                                                                  \
    } while (0)

#define LOG_MODULE_4 LOG_MODULE_3
#define LOG_MODULE_5 LOG_MODULE_3

#define LOG_MODULE_INFO(...) LOG_MODULE_GENERIC(info, __VA_ARGS__)
#define LOG_MODULE_ERROR(...) LOG_MODULE_GENERIC(error, __VA_ARGS__)
#define LOG_MODULE_WARN(...) LOG_MODULE_GENERIC(warn, __VA_ARGS__)
#define LOG_MODULE_DEBUG(...) LOG_MODULE_GENERIC(debug, __VA_ARGS__)

#define LOG_GENERIC(level, ...) \
    SPDLOG_EXPAND(SPDLOG_CONCAT(LOG_, LOG_MODULE_ARG_COUNT(__VA_ARGS__))(level, __VA_ARGS__))

#define LOG_1(level, msg)                                                     \
    do                                                                        \
    {                                                                         \
        spdlog::level("[{}:{}:{}] {}", FILE_NAME, __func__, __LINE__, (msg)); \
    } while (0)

#define LOG_2(level, fmt_str, ...) \
	do                                                                \
    {                                                                 \
        spdlog::level("[{}:{}:{}] {}", FILE_NAME, __func__, __LINE__, \
                      fmt::format(fmt_str, __VA_ARGS__));             \
    } while (0)

#define LOG_3 LOG_2
#define LOG_4 LOG_2
#define LOG_5 LOG_2


#define LOG_INFO(...) LOG_GENERIC(info, __VA_ARGS__)
#define LOG_ERROR(...) LOG_GENERIC(error, __VA_ARGS__)
#define LOG_WARN(...) LOG_GENERIC(warn, __VA_ARGS__)
#define LOG_DEBUG(...) LOG_GENERIC(debug, __VA_ARGS__)

class SPDLogHelp
{
   public:
    SPDLogHelp();
    ~SPDLogHelp();
    int InitLog(std::string cfgFileName, std::string logFileName, std::string moduleName);

    static SPDLogHelp *CreateInstance()
    {
        static SPDLogHelp *spdHelp = new SPDLogHelp();
        return spdHelp;
    }

   private:
    int _getJson(std::string &cfgFileName, nlohmann::json &json);
    int createSPDLog(nlohmann::json &json);

   private:
    inline static std::unordered_map<std::string, spdlog::level::level_enum> m_SPDLevelMap = {
        {TRACE, spdlog::level::level_enum::trace}, {DEBUG, spdlog::level::level_enum::debug},
        {INFO, spdlog::level::level_enum::info},   {WARN, spdlog::level::level_enum::warn},
        {ERR, spdlog::level::level_enum::info},    {CRITICAL, spdlog::level::level_enum::critical},
        {OFF, spdlog::level::level_enum::off}};
};

#define SPDHELP SPDLogHelp::CreateInstance()

#endif