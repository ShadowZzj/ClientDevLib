#include "SPDLogHelper.h"


SPDLogHelp::SPDLogHelp()
{
}

SPDLogHelp::~SPDLogHelp()
{
}

int SPDLogHelp::InitLog(std::string cfgPath, std::string fileName, std::string moduleName)
{
    nlohmann::json json;
    if (_getJson(cfgPath, json) != 0)
        return -1;

    json[WRITEPATH]   = GetLogFolder() + fileName;
    json[MODULENAME] = moduleName;
    json[LOGMAXFILES] = 3;
    json[LOGMAXSIZE] = 1025 * 5120;

     if (createSPDLog(json) != 0)
        return -1;

    return 0;
}

int SPDLogHelp::_getJson(std::string &cfgFileName, nlohmann::json &json)
{
    std::string fileName = cfgFileName;
    std::ifstream iStrategyFile;

     try
    {
        iStrategyFile.open(fileName);
        if (iStrategyFile.is_open())
        {
       
                iStrategyFile >> json;
        }
        else
        {
            json[LEVEL] = INFO;
        }
    }
    catch (const std::exception &)
    {
        return -1;
    }
    return 0;
}

int SPDLogHelp::createSPDLog(nlohmann::json &json)
{
    if (json.find(LEVEL) == json.end() || json.find(WRITEPATH) == json.end() || json.find(MODULENAME) == json.end() ||
        json.find(LOGMAXFILES) == json.end() || json.find(LOGMAXSIZE) == json.end())
        return -1;

    spdlog::flush_on(m_SPDLevelMap[json.find(LEVEL)->get<std::string>()]);
    auto logger = spdlog::rotating_logger_mt(json.find(MODULENAME)->get<std::string>(),
                                             json.find(WRITEPATH)->get<std::string>(),
                                             json.find(LOGMAXSIZE)->get<int>(), 
                                             json.find(LOGMAXFILES)->get<int>());
    spdlog::set_level(m_SPDLevelMap[json.find(LEVEL)->get<std::string>()]);
    spdlog::set_default_logger(logger);

    return 0;
}
