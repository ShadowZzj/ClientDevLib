#pragma once
#include <string>
namespace zzj
{
    class Deeplink
    {
        public:
        static int Register(const std::string &module, const std::string &path);
        static int Unregister(const std::string &module, const std::string &path);
    };
};