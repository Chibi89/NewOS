#include <net/eth.h>
#include <net/arp.h>
#include <net/ipv4.h>
#include <drivers/net/net_nic.h>
#include <mm/heap.h>
#include <string.h>

void eth_init(void) {
    arp_init();
    net_nic_set_rx_handler(eth_handle_packet);
}

bool eth_send(const uint8_t dest_mac[6], uint16_t ethertype, const void* payload, uint16_t len) {
    net_nic_t* nic = net_nic_get_active();
    if (!nic || !payload || len == 0) return false;

    uint16_t total_len = sizeof(eth_header_t) + len;
    // Dimensione minima Ethernet 60 byte
    uint16_t frame_len = (total_len < 60) ? 60 : total_len;

    uint8_t* frame = (uint8_t*)kmalloc(frame_len);
    if (!frame) return false;
    memset(frame, 0, frame_len);

    eth_header_t* hdr = (eth_header_t*)frame;
    memcpy(hdr->dest_mac, dest_mac, 6);
    memcpy(hdr->src_mac, nic->mac, 6);
    hdr->ethertype = net_htons(ethertype);

    memcpy(frame + sizeof(eth_header_t), payload, len);

    bool res = net_nic_send_packet(frame, frame_len);
    kfree(frame);
    return res;
}

void eth_handle_packet(const uint8_t* packet, uint16_t len) {
    if (!packet || len < sizeof(eth_header_t)) return;

    const eth_header_t* hdr = (const eth_header_t*)packet;
    net_nic_t* nic = net_nic_get_active();
    if (!nic) return;

    // Verifica se il pacchetto e' per noi, per broadcast o multicast
    bool is_bcast = (hdr->dest_mac[0] == 0xFF && hdr->dest_mac[1] == 0xFF && hdr->dest_mac[2] == 0xFF &&
                     hdr->dest_mac[3] == 0xFF && hdr->dest_mac[4] == 0xFF && hdr->dest_mac[5] == 0xFF);
    bool is_mcast = (hdr->dest_mac[0] & 0x01) != 0;
    bool is_for_us = (memcmp(hdr->dest_mac, nic->mac, 6) == 0);

    if (!is_bcast && !is_mcast && !is_for_us) {
        return; // Pacchetto non indirizzato a questa interfaccia
    }

    uint16_t ethertype = net_ntohs(hdr->ethertype);
    const uint8_t* payload = packet + sizeof(eth_header_t);
    uint16_t payload_len = len - sizeof(eth_header_t);

    if (ethertype == ETHERTYPE_ARP) {
        arp_handle_packet(payload, payload_len);
    } else if (ethertype == ETHERTYPE_IPV4) {
        ipv4_handle_packet(payload, payload_len);
    }
}
