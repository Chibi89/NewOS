#ifndef NET_UDP_H
#define NET_UDP_H

#include <types.h>

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t length;
    uint16_t checksum;
} udp_header_t;

typedef void (*udp_callback_t)(uint32_t src_ip, uint16_t src_port, uint16_t dest_port, const uint8_t* data, uint16_t len);

// Inizializza modulo UDP
void udp_init(void);

// Registra un ascoltatore per una porta UDP specifica
bool udp_bind_port(uint16_t port, udp_callback_t cb);

// Rimuove l'ascoltatore di una porta UDP
void udp_unbind_port(uint16_t port);

// Invia un datagramma UDP
bool udp_send(uint32_t dest_ip, uint16_t src_port, uint16_t dest_port, const void* payload, uint16_t len);

// Gestisce datagramma UDP in ingresso
void udp_handle_packet(uint32_t src_ip, uint32_t dest_ip, const uint8_t* payload, uint16_t len);

#endif // NET_UDP_H
