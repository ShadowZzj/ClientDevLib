#include "NetworkUtil.h"
#include <boost/process.hpp>
#include <regex>

std::set<std::string> zzj::NetworkUtil::GetDNSServers()
{
    boost::process::ipstream is;
    boost::process::child c("/bin/bash", boost::process::args = {"-c", "scutil --dns | grep 'nameserver\\[[0-9]*\\]'"},
                            (boost::process::std_out & boost::process::std_err) > is);

    std::set<std::string> dnsServers;
    std::string line;
    while (std::getline(is, line) || c.running())
    {
        if (line.empty())
        {
            continue;
        }
        std::regex re(".*nameserver\\[[0-9]*\\] : ([0-9]*\\.[0-9]*\\.[0-9]*\\.[0-9]*)");
        std::smatch m;
        if (std::regex_match(line, m, re))
        {
            dnsServers.insert(m[1].str());
        }
    }
    c.wait();
    return dnsServers;
}
