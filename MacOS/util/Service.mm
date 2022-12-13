#include "Service.h"
#import "FileUtil.h"
#import "Process.h"
#import <Foundation/Foundation.h>
#include <map>
#include <plist/Plist.hpp>
#include <spdlog/spdlog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

std::string PlistTemplate = R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
<key>KeepAlive</key>
<true />
<key>Label</key>
<string>placeholder</string>
<key>ExitTimeOut</key>
<integer>10</integer>
<key>ProgramArguments</key>
<array>
<string>placeholder</string>
</array>
<key>RunAtLoad</key>
<true />
</dict>
</plist>
)";
/*
<key>ExitTimeOut</key>
<integer>10</integer> //control the time interval between SIGTERM & SIGKILL
<key>inetdCompatibility</key>//Give the socket to the app, the app should call accept directly
   <dict>
       <key>Wait</key>
       <true/>
   </dict>
<key>Sockets</key> // when other app connect to the port, the plist service will start.
<dict>
   <key>Listeners</key>
   <dict>
       <key>SockServiceName</key>
       <string>12312</string>
       <key>SockType</key>
       <string>stream</string>
       <key>SockFamily</key>
       <string>IPv4</string>
   </dict>
</dict>*/
zzj::Service *runningService = nullptr;
using namespace std;
int zzj::Service::GetMutex()
{
    return 0;
}

int zzj::Service::ReleaseMutex()
{
    return 0;
}

int zzj::Service::CloseMutex()
{
    return 0;
}
int zzj::Service::Install(ServiceInterface *otherService)
{

    int result = 0;
    if (otherService != nullptr)
        return otherService->Install();
    char curPath[512] = {0};
    string path;
    string exeName;
    string plistName;
    std::map<string, boost::any> rootDict;
    std::vector<boost::any> programArgs;
    string tempPath;
    string arg1;
    exeName = binPath;
    try
    {
        Plist::readPlist(PlistTemplate.c_str(), PlistTemplate.length(), rootDict);
        rootDict["Label"]            = serviceName;
        programArgs                  = boost::any_cast<decltype(programArgs)>(rootDict["ProgramArguments"]);
        programArgs[0]               = exeName;
        rootDict["ProgramArguments"] = programArgs;

        path = GetPlistFileFullName();
        if ([[NSFileManager defaultManager] fileExistsAtPath:[NSString stringWithUTF8String:path.c_str()]])
            [[NSFileManager defaultManager] removeItemAtPath:[NSString stringWithUTF8String:path.c_str()] error:nil];
        Plist::writePlistXML(path.c_str(), rootDict);
        chmod(path.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    }
    catch (...)
    {
        printf("error");
        result = -2;
        goto exit;
    }
exit:
    return result;
}

int zzj::Service::Uninstall()
{
    Stop();
    [[NSFileManager defaultManager] removeItemAtPath:[NSString stringWithUTF8String:GetPlistFileFullName().c_str()]
                                               error:nil];
    return 0;
}
int zzj::Service::Start()
{
    std::string cmd = "launchctl load ";
    cmd += GetPlistFileFullName();
    system(cmd.c_str());
    return 0;
}
int zzj::Service::Stop()
{
    std::string cmd = "launchctl unload ";
    cmd += GetPlistFileFullName();
    system(cmd.c_str());
    return 0;
}
int zzj::Service::IsServiceBinExist(bool &isExist)
{
    isExist = zzj::File::IsFileExist(binPath.c_str()) == 0 ? true : false;
    return 0;
}

int zzj::Service::IsServiceInstalled(bool &isInstlled)
{
    @autoreleasepool
    {
        NSString *plistFile;
        plistFile  = [NSString stringWithUTF8String:GetPlistFileFullName().c_str()];
        isInstlled = [[NSFileManager defaultManager] fileExistsAtPath:plistFile];
        return 0;
    }
}
int zzj::Service::IsServiceRunning(bool &isRunning)
{
    @autoreleasepool
    {
        int result = 0;
        std::vector<std::string> args;
        std::wstring standardOut;
        args.push_back("list");
        args.push_back(serviceName);
        result = zzj::Process::CreateProcess("/bin/launchctl", args, standardOut, true);
        if (0 != result)
        {
            spdlog::error("Protect launchctl error {}", result);
            return result;
        }
        if (standardOut.empty())
        {
            isRunning = false;
            return result;
        }
        if (standardOut.find(L"\"OnDemand\" = true") != std::wstring::npos)
        {
            isRunning = true;
            return result;
        }
        else if (standardOut.find(L"PID") != std::wstring::npos)
        {
            isRunning = true;
            return result;
        }
        isRunning = false;
        return result;
    }
}
void zzj::Service::Run()
{
    runningService = this;
    signal(SIGTERM, SIG_IGN);
    dispatch_queue_t queue   = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
    dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, SIGTERM, 0, queue);
    if (source)
    {
        dispatch_source_set_event_handler(source, ^{
          zzj::Service::TermHandler();
        });
        spdlog::info("dispatch source up ");
        dispatch_resume(source);
    }
    ServiceFunc();
}

void zzj::Service::TermHandler()
{
    if (nullptr == runningService)
        exit(-1);

    std::unique_lock<std::mutex> lck(runningService->m_mutex);
    runningService->requestStop = true;
    runningService->m_cv.notify_all();
}

zzj::Service::ControlStatus zzj::Service::CheckSafeStop(int seconds)
{
    std::unique_lock<std::mutex> lck(m_mutex);
    bool res = m_cv.wait_for(lck, std::chrono::seconds(seconds), [this]() { return this->requestStop; });
    return res == false ? ControlStatus::Timeout : ControlStatus::RequestStop;
}

int zzj::Service::EnableService()
{
    std::string cmd = "launchctl enable system/";
    cmd             = cmd + serviceName;
    return system(cmd.c_str());
}
int zzj::Service::DisableService()
{
    std::string cmd = "launchctl disable system/";
    cmd             = cmd + serviceName;
    return system(cmd.c_str());
}
int zzj::Service::SetServiceStartType(zzj::ServiceInterface::StartUpType startType)
{
    std::map<string, boost::any> rootDict;
    // declare string ostream
    std::ostringstream oldPlistStream;
    std::ostringstream newPlistStream;
    int result = 0;

    try
    {
        Plist::readPlist(PlistTemplate.c_str(), PlistTemplate.length(), rootDict);
        Plist::writePlistXML(oldPlistStream, rootDict);

        switch (startType)
        {
        case zzj::ServiceInterface::StartUpType::Auto:
            rootDict["KeepAlive"] = true;
            Plist::writePlistXML(newPlistStream, rootDict);
            PlistTemplate = newPlistStream.str();
            result        = Install();
            if (0 != result)
            {
                result = -1;
                break;
            }
            result = EnableService();
            if (0 != result)
            {
                result = -2;
                break;
            }
            break;
        case zzj::ServiceInterface::StartUpType::Manual:
            rootDict["KeepAlive"] = false;
            Plist::writePlistXML(newPlistStream, rootDict);
            PlistTemplate = newPlistStream.str();
            result = Install();
            if (0 != result)
            {
                result = -3;
                break;
            }
            result = EnableService();
            if (0 != result)
            {
                result = -4;
                break;
            }
            break;
        case zzj::ServiceInterface::StartUpType::Disabled:
            result = DisableService();
            if (0 != result)
            {
                result = -5;
                break;
            }
            break;
        default:
            break;
        }
    }
    catch (...)
    {
        if (!oldPlistStream.str().empty())
            PlistTemplate = oldPlistStream.str();
        return -6;
    }
    if (!oldPlistStream.str().empty())
        PlistTemplate = oldPlistStream.str();
    return result;
}
