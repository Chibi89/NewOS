#include <net/udp.h>
#include <net/ipv4.h>
#include <drivers/net/net_nic.h>
#include <mm/heap.h>
#include <string.h>

#define MAX_UDP_BINDINGS 16

typedef struct {
    uint16_t       port;
    udp_callback_t callback;
    bool           active;
} udp_binding_t;

static udp_binding_t g_bindings[MAX_UDP_BINDINGS];

void udp_init(void) {
    memset(g_bindings, 0, sizeof(g_bindings));
}

bool udp_bind_port(uint16_t port, udp_callback_t cb) {
    if (!cb) return false;

    // Se già presente, aggiorna callback
    for (int i = 0; i < MAX_UDP_BINDINGS; i++) {
        if (g_bindings[i].active && g_bindings[i].port == port) {
            g_bindings[i].callback = cb;
            return true;
        }
    }

    // Altrimenti trova slot libero
    for (int i = 0; i < MAX_UDP_BINDINGS; i++) {
        if (!g_bindings[i].active) {
            g_bindings[i].port = port;
            g_bindings[i].callback = cb;
            g_bindings[i].active = true;
            return true;
        }
    }
    return false;
}

void udp_unbind_port(uint16_t port) {
    for (int i = 0; i < MAX_UDP_BINDINGS; i++) {
        if (g_bindings[i].active && g_bindings[i].port == port) {
            g_bindings[i].active = false;
            g_bindings[i].callback = NULL;
            break;
        }
    }
}

bool udp_send(uint32_t dest_ip, uint16_t src_port, uint16_t dest_port, const void* payload, uint16_t len) {
    uint16_t total_len = sizeof(udp_header_t) + len;
    uint8_t* buf = (uint8_t*)kmalloc(total_len);
    if (!buf) return false;

    udp_header_t* hdr = (udp_header_t*)buf;
    hdr->src_port = net_htons(src_port);
    hdr->dest_port = net_htons(dest_port);
    hdr->length = net_htons(total_len);
    hdr->checksum = 0; // Checksum UDP opzionale in IPv4

    if (payload && len > 0) {
        memcpy(buf + sizeof(udp_header_t), payload, len);
    }

    bool res = ipv4_send(dest_ip, IPV4_PROTO_UDP, buf, total_len);
    kfree(buf);
    return res;
}

void udp_handle_packet(uint32_t src_ip, uint32_t dest_ip, const uint8_t* payload, uint16_t len) {
    (void)dest_ip;
    if (!payload || len < sizeof(udp_header_t)) return;

    const udp_header_t* hdr = (const udp_header_t*)payload;
    uint16_t src_port = net_ntohs(hdr->src_port);
    uint16_t dest_port = net_ntohs(hdr->dest_port);
    uint16_t ulen = net_ntohs(hdr->length);

    if (ulen < sizeof(udp_header_t) || ulen > len) return;

    const uint8_t* data = payload + sizeof(udp_header_t);
    uint16_t data_len = ulen - sizeof(udp_header_t);

    for (int i = 0; i < MAX_UDP_BINDINGS; i++) {
        if (g_bindings[i].active && g_bindings[i].port == dest_port) {
            if (g_bindings[i].callback) {
                g_bindings[i].callback(src_ip, src_port, dest_port, data, data_len);
            }
            break;
        }
    }
}
