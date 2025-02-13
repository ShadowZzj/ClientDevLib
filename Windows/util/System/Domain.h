#pragma once
#include <string>
namespace zzj
{
    class Domain
    {
        public:
        static bool IsDomainJoined();
        static bool IsAADJoined();
        static Domain GetDomain();

        private:
        Domain() = default;
        Domain(const Domain&) = default;

        public:

        std::string domainName;
        std::string dnsDomainName;
        std::string domainGUID;
        std::string domainControllerName;
        


    };
    
} // namespace zzj

