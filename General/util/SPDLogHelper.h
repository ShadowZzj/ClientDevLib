#ifndef _SPDLOGHELP_H_
#define _SPDLOGHELP_H_

#include <json.hpp>
#include <string>
#include <fstream>

#include "File.h"

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
    inline static std::unordered_map<std::string, spdlog::level::level_enum> m_SPDLevelMap = 
    {
        {TRACE, spdlog::level::level_enum::trace},
        {DEBUG, spdlog::level::level_enum::debug},
        {INFO, spdlog::level::level_enum::info},
        {WARN, spdlog::level::level_enum::warn}, 
        {ERR, spdlog::level::level_enum::info},
        {CRITICAL, spdlog::level::level_enum::critical},
        {OFF, spdlog::level::level_enum::off}
    };
};

#define SPDHELP SPDLogHelp::CreateInstance()

#endif