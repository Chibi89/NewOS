#ifndef NET_ICMP_H
#define NET_ICMP_H

#include <types.h>

#define ICMP_TYPE_ECHO_REPLY    0
#define ICMP_TYPE_ECHO_REQUEST  8

typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} icmp_header_t;

typedef struct {
    uint32_t packets_sent;
    uint32_t packets_recv;
    uint32_t min_time_ms;
    uint32_t max_time_ms;
    uint32_t avg_time_ms;
    bool     success;
} ping_stats_t;

// Inizializza modulo ICMP
void icmp_init(void);

// Gestisce pacchetto ICMP in ricezione con TTL del pacchetto IPv4
void icmp_handle_packet(uint32_t src_ip, uint8_t ttl, const uint8_t* payload, uint16_t len);

// Invia richiesta ICMP Echo (Ping)
bool icmp_send_echo(uint32_t dest_ip, uint16_t id, uint16_t seq, const void* data, uint16_t len);

// Funzione di utilità per ping con statistiche (richiesta da cmd `ping`)
bool icmp_ping_ip(uint32_t ip, uint32_t count, ping_stats_t* stats, void (*on_reply)(int seq, uint32_t rtt_ms, uint8_t ttl));

#endif // NET_ICMP_H
