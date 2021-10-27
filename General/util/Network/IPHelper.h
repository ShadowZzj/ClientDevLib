#ifndef IPHELPER_H
#define IPHELPER_H

#include "Common.h"
#include <string>

namespace zzj
{

struct IPPacket
{

    unsigned char m_ip_vhl; /* version << 4 | header length >> 2 */
    unsigned char m_ip_tos; /* type of service */
    uint16_t m_ip_len;      /* total length */
    uint16_t m_ip_id;       /* identification */
    uint16_t m_ip_off;      /* fragment offset field */
#define IP_RF 0x8000        /* reserved fragment flag */
#define IP_DF 0x4000 /* dont fragment flag */
#define IP_MF 0x2000 /* more fragments flag */
#define IP_OFFMASK 0x1fff /* mask for fragmenting bits */
    unsigned char m_ip_ttl; /* time to live */
    unsigned char m_ip_p;   /* protocol */
    uint16_t m_ip_sum;      /* checksum */
    unsigned char m_ip_src[4];
    unsigned char m_ip_dst[4];
    
    //Network order
    inline static std::string IPAddrToStr(const unsigned char ipAddr[4])
    {
        std::string ret;
        for(int i=0;i<4;i++)
        {
            ret += std::to_string((int)ipAddr[i]);
            if(i != 3)
                ret +=".";
        }
        return ret;
    }
};

static_assert(sizeof(IPPacket) == 20, "Size of IP must be 20");
} // namespace zzj

#endif // IPHELPER_H
