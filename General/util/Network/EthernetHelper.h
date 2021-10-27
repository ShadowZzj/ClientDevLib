#ifndef ETHERNETHELPER_H
#define ETHERNETHELPER_H

#include "Common.h"

namespace zzj {

struct EthernetPacket {
    enum EtherType : uint16_t
    {
        PUP = 0x0200,
        IP = 0x0800,
        ARP = 0x0806,
        RARP = 0x8035
    };
    unsigned char m_ether_dhost[ethernet_addr_len]; /* destination host address */
    unsigned char m_ether_shost[ethernet_addr_len]; /* source host address */
    EtherType m_ether_type;                    /* IP? ARP? RARP? etc */
};

static_assert(sizeof(EthernetPacket) == 14, "Size of Ethernet must be 14");
}

#endif // ETHERNETHELPER_H
