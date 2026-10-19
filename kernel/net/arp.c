#include <net/arp.h>
#include <net/eth.h>
#include <drivers/net/net_nic.h>
#include <string.h>

#define ARP_TABLE_SIZE 16

typedef struct {
    uint32_t ip;
    uint8_t  mac[6];
    bool     valid;
} arp_entry_t;

static arp_entry_t g_arp_table[ARP_TABLE_SIZE];

void arp_init(void) {
    memset(g_arp_table, 0, sizeof(g_arp_table));

    // Inserisci entry solo per broadcast Ethernet (FF:FF:FF:FF:FF:FF)
    // Tutti gli altri indirizzi (compreso gateway) vengono risolti dinamicamente via ARP
    g_arp_table[0].ip = 0xFFFFFFFF;
    memset(g_arp_table[0].mac, 0xFF, 6);
    g_arp_table[0].valid = true;
}

void arp_send_request(uint32_t target_ip) {
    net_nic_t* nic = net_nic_get_active();
    if (!nic) return;

    arp_packet_t arp;
    arp.hw_type = net_htons(1);
    arp.proto_type = net_htons(ETHERTYPE_IPV4);
    arp.hw_len = 6;
    arp.proto_len = 4;
    arp.opcode = net_htons(ARP_OP_REQUEST);
    memcpy(arp.src_mac, nic->mac, 6);
    arp.src_ip = nic->ip;
    memset(arp.dest_mac, 0x00, 6);
    arp.dest_ip = target_ip;

    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    eth_send(bcast, ETHERTYPE_ARP, &arp, sizeof(arp));
}

void arp_handle_packet(const uint8_t* payload, uint16_t len) {
    if (len < sizeof(arp_packet_t)) return;
    const arp_packet_t* arp = (const arp_packet_t*)payload;

    if (net_ntohs(arp->hw_type) != 1 || net_ntohs(arp->proto_type) != ETHERTYPE_IPV4) {
        return;
    }

    net_nic_t* nic = net_nic_get_active();
    if (!nic) return;

    // Aggiorna cache con il mittente
    bool updated = false;
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (g_arp_table[i].valid && g_arp_table[i].ip == arp->src_ip) {
            memcpy(g_arp_table[i].mac, arp->src_mac, 6);
            updated = true;
            break;
        }
    }
    if (!updated) {
        for (int i = 1; i < ARP_TABLE_SIZE; i++) {
            if (!g_arp_table[i].valid) {
                g_arp_table[i].ip = arp->src_ip;
                memcpy(g_arp_table[i].mac, arp->src_mac, 6);
                g_arp_table[i].valid = true;
                break;
            }
        }
    }

    uint16_t op = net_ntohs(arp->opcode);
    // Se e' una richiesta diretta al nostro IP, rispondi con ARP Reply
    if (op == ARP_OP_REQUEST && arp->dest_ip == nic->ip) {
        arp_packet_t reply;
        reply.hw_type = net_htons(1);
        reply.proto_type = net_htons(ETHERTYPE_IPV4);
        reply.hw_len = 6;
        reply.proto_len = 4;
        reply.opcode = net_htons(ARP_OP_REPLY);
        memcpy(reply.src_mac, nic->mac, 6);
        reply.src_ip = nic->ip;
        memcpy(reply.dest_mac, arp->src_mac, 6);
        reply.dest_ip = arp->src_ip;

        eth_send(arp->src_mac, ETHERTYPE_ARP, &reply, sizeof(reply));
    }
}

bool arp_lookup(uint32_t ip, uint8_t mac_out[6]) {
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (g_arp_table[i].valid && g_arp_table[i].ip == ip) {
            memcpy(mac_out, g_arp_table[i].mac, 6);
            return true;
        }
    }
    // Fallback broadcast o gateway
    memset(mac_out, 0xFF, 6);
    return false;
}
