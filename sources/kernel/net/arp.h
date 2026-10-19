#ifndef NET_ARP_H
#define NET_ARP_H

#include <types.h>

#define ARP_OP_REQUEST  1
#define ARP_OP_REPLY    2

typedef struct __attribute__((packed)) {
    uint16_t hw_type;       // 1 = Ethernet
    uint16_t proto_type;    // 0x0800 = IPv4
    uint8_t  hw_len;        // 6
    uint8_t  proto_len;     // 4
    uint16_t opcode;        // 1 = Request, 2 = Reply
    uint8_t  src_mac[6];
    uint32_t src_ip;
    uint8_t  dest_mac[6];
    uint32_t dest_ip;
} arp_packet_t;

// Inizializza la tabella ARP
void arp_init(void);

// Invia richiesta ARP per risolvere un indirizzo IP in MAC
void arp_send_request(uint32_t target_ip);

// Gestisce pacchetto ARP in ingresso
void arp_handle_packet(const uint8_t* payload, uint16_t len);

// Cerca MAC address associato a un IP nella tabella cache
bool arp_lookup(uint32_t ip, uint8_t mac_out[6]);

#endif // NET_ARP_H
