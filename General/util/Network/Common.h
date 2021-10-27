#ifndef PCAPCOMMON_H
#define PCAPCOMMON_H

#include <cstddef>

/* default snap length (maximum bytes per package to capture) */
constexpr auto snap_len = std::size_t{1518};

/* ethernet headers are always exactly 14 bytes [1] */
constexpr auto size_ethernet = std::size_t{14};

/* Ethernet addresses are 6 bytes */
constexpr auto ethernet_addr_len = std::size_t{6};

constexpr auto ip_addr_len = std::size_t{4};

#define IP_HL(ip) (((ip)->m_ip_vhl) & 0x0f)
#define IP_V(ip) (((ip)->m_ip_vhl) >> 4)

#endif // PCAPCOMMON_H