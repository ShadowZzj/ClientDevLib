#ifndef UDPHELPER_H
#define UDPHELPER_H

#include "Common.h"

namespace zzj {

struct UDPPacket {
    uint16_t m_th_sport; /* source port */
    uint16_t m_th_dport; /* destination port */
    uint16_t m_length;
    uint16_t m_checksum;
};

static_assert(sizeof(UDPPacket) == 8, "Size of UDP must be 8");
}

#endif // UDPHELPER_H
