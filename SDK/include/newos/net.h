/**
 * newos/net.h — Helper IPv4 e Rete per NewSoft SDK
 */
#ifndef NEWOS_SDK_NET_H
#define NEWOS_SDK_NET_H

#include "types.h"

#define NS_IP4(a, b, c, d) \
    ((((uint32_t)(a) & 0xFFu) << 24) | (((uint32_t)(b) & 0xFFu) << 16) | \
     (((uint32_t)(c) & 0xFFu) << 8)  | ((uint32_t)(d) & 0xFFu))

static inline uint8_t ns_ip4_octet(uint32_t ip, int idx) {
    return (uint8_t)((ip >> ((3 - idx) * 8)) & 0xFFu);
}

#endif /* NEWOS_SDK_NET_H */
