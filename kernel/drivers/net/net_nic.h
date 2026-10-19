#ifndef DRIVERS_NET_NET_NIC_H
#define DRIVERS_NET_NET_NIC_H

#include <types.h>

typedef enum {
    NIC_TYPE_NONE = 0,
    NIC_TYPE_E1000,     // Intel 82540EM Gigabit Ethernet
    NIC_TYPE_RTL8139,   // Realtek RTL8139 Fast Ethernet
    NIC_TYPE_WIFI       // Wi-Fi 7/6E Ultra-High Throughput (fino a 5.0 GB/s con vincolo HW reale)
} nic_type_t;

typedef struct {
    nic_type_t  type;
    char        name[48];
    uint8_t     mac[6];
    uint32_t    ip;          // IPv4 address in network byte order
    uint32_t    netmask;     // Subnet mask
    uint32_t    gateway;     // Default gateway
    uint32_t    dns;         // Primary DNS server
    bool        link_up;
    uint32_t    rx_packets;
    uint32_t    tx_packets;
    uint64_t    rx_bytes;
    uint64_t    tx_bytes;
    uint8_t     irq;
    uint32_t    io_base;
    uint64_t    mmio_base;
} net_nic_t;

// Inizializza i controller di rete hardware PCI (E1000, RTL8139)
bool net_nic_init(void);

// Restituisce la scheda di rete attiva
net_nic_t* net_nic_get_active(void);

// Trasmissione di un frame Ethernet grezzo
bool net_nic_send_packet(const void* data, uint16_t len);

// Polling di ricezione pacchetti (chiamato periodicamente o su interrupt)
void net_nic_poll(void);

// Registra la ricezione di pacchetto grezzo
typedef void (*net_rx_handler_t)(const uint8_t* packet, uint16_t len);
void net_nic_set_rx_handler(net_rx_handler_t handler);

// Formatta indirizzo IP in stringa "xxx.xxx.xxx.xxx"
void net_format_ip(uint32_t ip, char* buf, size_t buf_size);

// Formatta indirizzo MAC in stringa "XX:XX:XX:XX:XX:XX"
void net_format_mac(const uint8_t mac[6], char* buf, size_t buf_size);

// Converte stringa "192.168.1.1" in uint32_t (network byte order)
uint32_t net_parse_ip(const char* ip_str);

// Helpers byte-order
uint16_t net_htons(uint16_t v);
uint16_t net_ntohs(uint16_t v);
uint32_t net_htonl(uint32_t v);
uint32_t net_ntohl(uint32_t v);

#endif // DRIVERS_NET_NET_NIC_H
