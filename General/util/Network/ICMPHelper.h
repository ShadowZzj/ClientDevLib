#ifndef ICMPHELPER_H
#define ICMPHELPER_H

#include "Common.h"

namespace zzj
{

struct ICMPPacket
{
    unsigned char m_type;
    unsigned char m_code;
    uint16_t m_checksum;
    uint32_t m_rest_header;
};

static_assert(sizeof(ICMPPacket) == 8, "Size of ICMP must be 8");
} // namespace zzj

#endif // ICMPHELPER_H
