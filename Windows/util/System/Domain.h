#pragma once
#include <string>
namespace zzj
{
    class Domain
    {
        public:
        static bool IsDomainJoined();
        static Domain GetDomain();

        private:
        Domain() = default;
        Domain(const Domain&) = default;

        public:

        std::string domainName;
        std::string dnsDomainName;
        


    };
    
} // namespace zzj

