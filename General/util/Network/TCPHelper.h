#ifndef TCPHELPER_H
#define TCPHELPER_H

#include "Common.h"

namespace zzj
{

struct TCPPacket
{
    uint16_t m_th_sport;       /* source port */
    uint16_t m_th_dport;       /* destination port */
    uint32_t m_th_seq;         /* sequence number */
    uint32_t m_th_ack;         /* acknowledgement number */
    unsigned char m_th_offx2; /* data offset, rsvd */
#define TH_OFF(th) (((th)->m_th_offx2 & 0xf0) >> 4)
    unsigned char m_th_flags;
#define TH_FIN 0x01
#define TH_SYN 0x02
#define TH_RST 0x04
#define TH_PUSH 0x08
#define TH_ACK 0x10
#define TH_URG 0x20
#define TH_ECE 0x40
#define TH_CWR 0x80
#define TH_FLAGS (TH_FIN | TH_SYN | TH_RST | TH_ACK | TH_URG | TH_ECE | TH_CWR)
    uint16_t m_th_win; /* window */
    uint16_t m_th_sum; /* checksum */
    uint16_t m_th_urp; /* urgent pointer */
};

struct TCPOpt
{
    unsigned char opt[40];
};

static_assert(sizeof(TCPPacket) == 20, "Size of TCP must be 20");
} // namespace zzj

#endif // TCPHELPER_H
