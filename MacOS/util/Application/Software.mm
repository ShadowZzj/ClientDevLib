#include <General/util/Application/Software.h>
#include <boost/process.hpp>
#include <json.hpp>
#include <spdlog/spdlog.h>
namespace bp = boost::process;
std::tuple<int, std::vector<zzj::SoftInfo>> zzj::SoftInfoManager::GetInstalledSoftware()
{
    std::vector<zzj::SoftInfo> softList;
    std::string output;
    bp::ipstream is; // Output stream to read the child process stdout
    bp::child c("/bin/bash", bp::args({"-c", "system_profiler -json SPApplicationsDataType"}), bp::std_out > is);

    // Read the child process stdout
    std::string line;
    while (std::getline(is, line))
    {
        output += line + "\n";
    }

    // Wait for the process to finish and get the exit code
    c.wait();
    int exit_code = c.exit_code();
    if (exit_code != 0)
        return std::make_tuple(exit_code, softList);

    try
    {
        nlohmann::json j = nlohmann::json::parse(output);
        auto apps        = j["SPApplicationsDataType"];
        for (const auto &app : apps)
        {
            zzj::SoftInfo softInfo;
            if (app.find("_name") == app.end() || app["_name"].get<std::string>().empty())
                continue;
            softInfo.m_strSoftName        = app["_name"];
            softInfo.m_strSoftVersion     = app.find("version") != app.end() ? app["version"].get<std::string>() : "";
            softInfo.m_strInstallLocation = app.find("path") != app.end() ? app["path"].get<std::string>() : "";

            std::string publisher;
            if (app.find("signed_by") != app.end())
            {
                for (const auto &signedBy : app["signed_by"])
                {
                    publisher += signedBy.get<std::string>() + ";";
                }
            }

            softInfo.m_strPublisher    = publisher;
            softInfo.m_strMainProPath  = app.find("path") != app.end() ? app["path"].get<std::string>() : "";
            softInfo.m_strUninstallPth = softInfo.m_strMainProPath;
            softList.push_back(softInfo);
        }
        return std::make_tuple(exit_code, softList);
    }
    catch (const std::exception &e)
    {
        spdlog::error("parse json error: {}", e.what());
    }
    catch (...)
    {
        spdlog::error("parse json error");
    }
}