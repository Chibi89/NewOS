#include <net/ipv4.h>
#include <net/eth.h>
#include <net/arp.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/tcp.h>
#include <drivers/net/net_nic.h>
#include <drivers/timer.h>
#include <mm/heap.h>
#include <string.h>

static uint16_t g_ip_packet_id = 1;

uint16_t net_checksum(const void* data, size_t len) {
    const uint16_t* ptr = (const uint16_t*)data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }

    if (len == 1) {
        sum += *(const uint8_t*)ptr;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}

void ipv4_init(void) {
    g_ip_packet_id = 1;
}

bool ipv4_send(uint32_t dest_ip, uint8_t protocol, const void* payload, uint16_t len) {
    net_nic_t* nic = net_nic_get_active();
    if (!nic || !payload || len == 0) return false;

    uint16_t total_len = sizeof(ipv4_header_t) + len;
    uint8_t* packet = (uint8_t*)kmalloc(total_len);
    if (!packet) return false;

    ipv4_header_t* hdr = (ipv4_header_t*)packet;
    hdr->ver_ihl = 0x45; // IPv4, Header 20 byte (5 parole da 32-bit)
    hdr->tos = 0;
    hdr->total_len = net_htons(total_len);
    hdr->id = net_htons(g_ip_packet_id++);
    hdr->flags_frag = net_htons(0x4000); // Don't fragment
    hdr->ttl = 64;
    hdr->protocol = protocol;
    hdr->checksum = 0;
    hdr->src_ip = nic->ip;
    hdr->dest_ip = dest_ip;
    hdr->checksum = net_checksum(hdr, sizeof(ipv4_header_t));

    memcpy(packet + sizeof(ipv4_header_t), payload, len);

    // Risoluzione indirizzo MAC di destinazione
    uint8_t target_mac[6];
    if (dest_ip == 0xFFFFFFFF || dest_ip == 0) {
        memset(target_mac, 0xFF, 6);
    } else {
        uint32_t next_hop = dest_ip;
        // Se abbiamo un gateway e l'IP e' fuori dalla subnet locale, instarda al gateway
        if (nic->gateway != 0 && nic->netmask != 0 && (dest_ip & nic->netmask) != (nic->ip & nic->netmask)) {
            next_hop = nic->gateway;
        }

        if (!arp_lookup(next_hop, target_mac)) {
            arp_send_request(next_hop);
            for (int retry = 0; retry < 30; retry++) {
                net_nic_poll();
                if (arp_lookup(next_hop, target_mac)) break;
                timer_sleep(2);
            }
            if (!arp_lookup(next_hop, target_mac)) {
                // Fallback broadcast Ethernet
                memset(target_mac, 0xFF, 6);
            }
        }
    }

    bool res = eth_send(target_mac, ETHERTYPE_IPV4, packet, total_len);
    kfree(packet);
    return res;
}

void ipv4_handle_packet(const uint8_t* payload, uint16_t len) {
    if (!payload || len < sizeof(ipv4_header_t)) return;

    const ipv4_header_t* hdr = (const ipv4_header_t*)payload;
    if ((hdr->ver_ihl >> 4) != 4) return; // Non e' IPv4

    uint8_t ihl_bytes = (hdr->ver_ihl & 0x0F) * 4;
    if (len < ihl_bytes) return;

    net_nic_t* nic = net_nic_get_active();
    if (!nic) return;

    // Eccezione per pacchetti DHCP diretti al client (UDP porta 68):
    // devono essere accettati anche prima che un indirizzo IP venga assegnato
    bool is_dhcp = false;
    if (hdr->protocol == IPV4_PROTO_UDP && len >= ihl_bytes + sizeof(udp_header_t)) {
        const udp_header_t* uhdr = (const udp_header_t*)(payload + ihl_bytes);
        if (net_ntohs(uhdr->dest_port) == 68) {
            is_dhcp = true;
        }
    }

    // Controlla se il pacchetto e' per noi o broadcast
    if (!is_dhcp && hdr->dest_ip != nic->ip && hdr->dest_ip != 0xFFFFFFFF && hdr->dest_ip != 0) {
        return; // Non destinato a questo host
    }

    const uint8_t* proto_data = payload + ihl_bytes;
    uint16_t total_len = net_ntohs(hdr->total_len);
    if (total_len < ihl_bytes || total_len > len) return;
    uint16_t proto_len = total_len - ihl_bytes;

    if (hdr->protocol == IPV4_PROTO_ICMP) {
        icmp_handle_packet(hdr->src_ip, hdr->ttl, proto_data, proto_len);
    } else if (hdr->protocol == IPV4_PROTO_UDP) {
        udp_handle_packet(hdr->src_ip, hdr->dest_ip, proto_data, proto_len);
    } else if (hdr->protocol == IPV4_PROTO_TCP) {
        tcp_handle_packet(hdr->src_ip, hdr->dest_ip, proto_data, proto_len);
    }
}
