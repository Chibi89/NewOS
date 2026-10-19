#ifndef NET_IPV4_H
#define NET_IPV4_H

#include <types.h>

#define IPV4_PROTO_ICMP 1
#define IPV4_PROTO_TCP  6
#define IPV4_PROTO_UDP  17

typedef struct __attribute__((packed)) {
    uint8_t  ver_ihl;       // Version (4) | IHL (5) -> 0x45
    uint8_t  tos;           // Type of Service
    uint16_t total_len;     // Lunghezza totale (header + dati)
    uint16_t id;            // Identificatore pacchetto
    uint16_t flags_frag;    // Flags e offset frammentazione
    uint8_t  ttl;           // Time to live
    uint8_t  protocol;      // 1=ICMP, 6=TCP, 17=UDP
    uint16_t checksum;      // Header checksum
    uint32_t src_ip;
    uint32_t dest_ip;
} ipv4_header_t;

// Inizializza il layer IPv4
void ipv4_init(void);

// Calcola il checksum standard RFC 791 / RFC 1071
uint16_t net_checksum(const void* data, size_t len);

// Invia un pacchetto IPv4
bool ipv4_send(uint32_t dest_ip, uint8_t protocol, const void* payload, uint16_t len);

// Gestisce un pacchetto IPv4 in ingresso
void ipv4_handle_packet(const uint8_t* payload, uint16_t len);

#endif // NET_IPV4_H
