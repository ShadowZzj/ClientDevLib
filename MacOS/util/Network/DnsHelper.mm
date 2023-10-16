#include <General/util/Network/DNSHelper.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <resolv.h>
#include <stdio.h>
#include <string>

std::tuple<zzj::DNSRelolver::Error, std::string> zzj::DNSRelolver::ResolveARecord(const std::string &domain)
{
    unsigned char response[NS_PACKETSZ]; // Buffer for the DNS response
    ns_msg message;
    ns_rr rr;

    // Query for the A record of the domain
    int len = res_query(domain.c_str(), ns_c_in, ns_t_a, response, sizeof(response));

    if (len < 0)
    {
        switch (h_errno)
        {
        case HOST_NOT_FOUND:
            return std::make_tuple(zzj::DNSRelolver::Error::NoHost, "");

        case TRY_AGAIN:
            return std::make_tuple(zzj::DNSRelolver::Error::Fail, "");
        default:
            return std::make_tuple(zzj::DNSRelolver::Error::Fail, "");
        }
    }

    // Parse the DNS response
    if (ns_initparse(response, len, &message) < 0)
    {
        return std::make_tuple(zzj::DNSRelolver::Error::Fail, "");
    }

    int count = ns_msg_count(message, ns_s_an); // Number of answers in the response
    for (int i = 0; i < count; i++)
    {
        if (ns_parserr(&message, ns_s_an, i, &rr) == 0)
        {
            // Convert the RR data to a readable IP address
            const u_char *rd = ns_rr_rdata(rr);
            struct sockaddr_in sa;
            memset(&sa, 0, sizeof(struct sockaddr_in));
            memcpy(&sa.sin_addr, rd, ns_rr_rdlen(rr));
            return std::make_tuple(zzj::DNSRelolver::Error::Success, inet_ntoa(sa.sin_addr));
        }
    }

    return std::make_tuple(zzj::DNSRelolver::Error::NoHost, "");
}