#ifndef NET_ETH_H
#define NET_ETH_H

#include <types.h>

#define ETHERTYPE_IPV4  0x0800
#define ETHERTYPE_ARP   0x0806

typedef struct __attribute__((packed)) {
    uint8_t  dest_mac[6];
    uint8_t  src_mac[6];
    uint16_t ethertype;
} eth_header_t;

// Inizializza il layer Ethernet II
void eth_init(void);

// Invia un frame Ethernet II
bool eth_send(const uint8_t dest_mac[6], uint16_t ethertype, const void* payload, uint16_t len);

// Gestisce la ricezione di un frame Ethernet grezzo dalla scheda di rete
void eth_handle_packet(const uint8_t* packet, uint16_t len);

#endif // NET_ETH_H
