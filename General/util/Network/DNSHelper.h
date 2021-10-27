
#ifndef DNSHELPER_H
#define DNSHELPER_H

#include "Common.h"

namespace zzj {

struct DNSQuery {
    unsigned char *m_query;
    uint16_t m_type;
    uint16_t m_class;
};

struct DNSQuestion {
    uint16_t m_transation;
    uint16_t m_flags;
    uint16_t m_questions;
    uint16_t m_answers;
    uint16_t m_authority;
    uint16_t m_additional;
};

struct DNSAnswer {
    uint16_t m_name;
    uint16_t m_type;
    uint16_t m_class;
    uint16_t m_time_to_live[4];
    uint16_t data_length;
    uint16_t m_address[4];
};
}

#endif // DNSHELPER_H