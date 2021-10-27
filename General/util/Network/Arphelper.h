

#ifndef ARPHELPER_H
#define ARPHELPER_H

#include "Common.h"

namespace zzj
{

struct ArpPacket
{
    uint16_t m_hardware_type;
    uint16_t m_protocol;
    unsigned char m_hardware_address_length;
    unsigned char m_protocol_address_length;
    uint16_t m_opcode;
    unsigned char m_sender_hardware_address[ethernet_addr_len];
    unsigned char m_sender_ip_address[ip_addr_len];
    unsigned char m_target_harware_address[ethernet_addr_len];
    unsigned char m_target_ip_address[ip_addr_len];
};

static_assert(sizeof(ArpPacket) == 28, "Size of ARP must be 28");
} // namespace zzj

#endif // ARPHELPER_H