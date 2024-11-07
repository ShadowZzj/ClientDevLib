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
#define LOG_INFO(...) \
    spdlog::info("[{}:{}:{}] {}", FILE_NAME, __func__, __LINE__, fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) \
    spdlog::error("[{}:{}:{}] {}", FILE_NAME, __func__, __LINE__, fmt::format(__VA_ARGS__))
#define LOG_WARN(...) \
    spdlog::warn("[{}:{}:{}] {}", FILE_NAME, __func__, __LINE__, fmt::format(__VA_ARGS__))
#define LOG_DEBUG(...) \
    spdlog::debug("[{}:{}:{}] {}", FILE_NAME, __func__, __LINE__, fmt::format(__VA_ARGS__))

#define LOG_MODULE_INFO(module, ...) \
if(spdlog::get(module) != nullptr) \
spdlog::get(module)->info("[{}:{}:{}] {}", FILE_NAME, __func__, __LINE__, fmt::format(__VA_ARGS__));\
else \
spdlog::info("[{}:{}:{}] {}", FILE_NAME, __func__, __LINE__, fmt::format(__VA_ARGS__))

#define LOG_MODULE_ERROR(module, ...) \
if(spdlog::get(module) != nullptr) \
spdlog::get(module)->error("[{}:{}:{}] {}", FILE_NAME, __func__, __LINE__, fmt::format(__VA_ARGS__));\
else \
spdlog::error("[{}:{}:{}] {}", FILE_NAME, __func__, __LINE__, fmt::format(__VA_ARGS__))

#define LOG_MODULE_WARN(module, ...) \
if(spdlog::get(module) != nullptr) \
spdlog::get(module)->warn("[{}:{}:{}] {}", FILE_NAME, __func__, __LINE__, fmt::format(__VA_ARGS__));\
else \
spdlog::warn("[{}:{}:{}] {}", FILE_NAME, __func__, __LINE__, fmt::format(__VA_ARGS__))

#define LOG_MODULE_DEBUG(module, ...) \
if(spdlog::get(module) != nullptr) \
spdlog::get(module)->debug("[{}:{}:{}] {}", FILE_NAME, __func__, __LINE__, fmt::format(__VA_ARGS__));\
else \
spdlog::debug("[{}:{}:{}] {}", FILE_NAME, __func__, __LINE__, fmt::format(__VA_ARGS__))

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