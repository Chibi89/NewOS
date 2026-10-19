#include <net/dhcp.h>
#include <net/udp.h>
#include <drivers/net/net_nic.h>
#include <drivers/timer.h>
#include <arch/x86_64/io.h>
#include <string.h>

static void serial_log(const char* s) {
    if (!s) return;
    while (*s) outb(0x3F8, *s++);
}

#define DHCP_PORT_SERVER 67
#define DHCP_PORT_CLIENT 68
#define DHCP_MAGIC       0x63825363

typedef struct __attribute__((packed)) {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[16];
    char     sname[64];
    char     file[128];
    uint32_t magic_cookie;
    uint8_t  options[312];
} dhcp_packet_t;

static dhcp_state_t g_dhcp_state = DHCP_STATE_IDLE;
static uint32_t     g_xid = 0x39102456;
static uint32_t     g_offered_ip = 0;
static uint32_t     g_server_ip = 0;
static uint32_t     g_offered_mask = 0;
static uint32_t     g_offered_gw = 0;
static uint32_t     g_offered_dns = 0;

static void send_dhcp_request(uint32_t req_ip, uint32_t server_ip) {
    net_nic_t* nic = net_nic_get_active();
    if (!nic) return;

    dhcp_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.op = 1;
    pkt.htype = 1;
    pkt.hlen = 6;
    pkt.xid = g_xid;
    pkt.flags = net_htons(0x8000);
    memcpy(pkt.chaddr, nic->mac, 6);
    pkt.magic_cookie = net_htonl(DHCP_MAGIC);

    int o = 0;
    // Opt 53: Request
    pkt.options[o++] = 53; pkt.options[o++] = 1; pkt.options[o++] = 3;
    // Opt 50: Requested IP
    pkt.options[o++] = 50; pkt.options[o++] = 4;
    memcpy(&pkt.options[o], &req_ip, 4); o += 4;
    // Opt 54: Server ID
    if (server_ip) {
        pkt.options[o++] = 54; pkt.options[o++] = 4;
        memcpy(&pkt.options[o], &server_ip, 4); o += 4;
    }
    // Opt 55: Parameter Request List
    pkt.options[o++] = 55; pkt.options[o++] = 3;
    pkt.options[o++] = 1; pkt.options[o++] = 3; pkt.options[o++] = 6;
    // End
    pkt.options[o++] = 255;

    uint16_t send_len = (uint16_t)(sizeof(dhcp_packet_t) - 312 + o);
    udp_send(0xFFFFFFFF, DHCP_PORT_CLIENT, DHCP_PORT_SERVER, &pkt, send_len);
}

static void dhcp_on_udp_packet(uint32_t src_ip, uint16_t src_port, uint16_t dest_port, const uint8_t* data, uint16_t len) {
    (void)src_ip; (void)src_port; (void)dest_port;
    if (!data || len < sizeof(dhcp_packet_t) - 312) return;

    const dhcp_packet_t* p = (const dhcp_packet_t*)data;
    if (net_ntohl(p->magic_cookie) != DHCP_MAGIC) return;
    if (p->xid != g_xid) return;
    if (p->op != 2) return; // BOOTREPLY

    // Analizza opzioni DHCP
    const uint8_t* opt = p->options;
    size_t opt_len = len - (sizeof(dhcp_packet_t) - 312);
    size_t idx = 0;
    uint8_t msg_type = 0;

    while (idx < opt_len && opt[idx] != 255) {
        uint8_t code = opt[idx++];
        if (code == 0) continue; // Pad
        if (idx >= opt_len) break;
        uint8_t olen = opt[idx++];
        if (idx + olen > opt_len) break;

        if (code == 53 && olen >= 1) { // DHCP Message Type
            msg_type = opt[idx];
        } else if (code == 1 && olen >= 4) { // Subnet Mask
            memcpy(&g_offered_mask, opt + idx, 4);
        } else if (code == 3 && olen >= 4) { // Router (Gateway)
            memcpy(&g_offered_gw, opt + idx, 4);
        } else if (code == 6 && olen >= 4) { // DNS Server
            memcpy(&g_offered_dns, opt + idx, 4);
        } else if (code == 54 && olen >= 4) { // Server Identifier
            memcpy(&g_server_ip, opt + idx, 4);
        }
        idx += olen;
    }

    if (msg_type == 2) { // DHCPOFFER
        serial_log("[DHCP] Ricevuto DHCPOFFER valido dalla rete!\n");
        g_offered_ip = p->yiaddr;
        g_dhcp_state = DHCP_STATE_REQUEST_SENT;
        send_dhcp_request(g_offered_ip, g_server_ip);
    } else if (msg_type == 5) { // DHCPACK
        serial_log("[DHCP] Ricevuto DHCPACK: lease acquisito dinamicamente!\n");
        net_nic_t* nic = net_nic_get_active();
        if (nic) {
            nic->ip = p->yiaddr;
            if (g_offered_mask) nic->netmask = g_offered_mask;
            if (g_offered_gw)   nic->gateway = g_offered_gw;
            if (g_offered_dns)  nic->dns = g_offered_dns;

            char ip_str[24];
            net_format_ip(nic->ip, ip_str, sizeof(ip_str));
            serial_log("[DHCP] Indirizzo IP assegnato alla NIC: ");
            serial_log(ip_str);
            serial_log("\n");
        }
        g_dhcp_state = DHCP_STATE_BOUND;
    }
}

void dhcp_init(void) {
    udp_bind_port(DHCP_PORT_CLIENT, dhcp_on_udp_packet);
    g_dhcp_state = DHCP_STATE_IDLE;
}

bool dhcp_discover(void) {
    net_nic_t* nic = net_nic_get_active();
    if (!nic) return false;

    g_xid += 0x1000;
    g_dhcp_state = DHCP_STATE_DISCOVER_SENT;

    dhcp_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.op = 1; // BOOTREQUEST
    pkt.htype = 1;
    pkt.hlen = 6;
    pkt.xid = g_xid;
    pkt.flags = net_htons(0x8000); // Broadcast flag
    memcpy(pkt.chaddr, nic->mac, 6);
    pkt.magic_cookie = net_htonl(DHCP_MAGIC);

    int o = 0;
    // Option 53: DHCP Message Type = Discover (1)
    pkt.options[o++] = 53; pkt.options[o++] = 1; pkt.options[o++] = 1;
    // Option 55: Parameter Request List (1=Subnet, 3=Router, 6=DNS)
    pkt.options[o++] = 55; pkt.options[o++] = 3;
    pkt.options[o++] = 1; pkt.options[o++] = 3; pkt.options[o++] = 6;
    // Option 255: End
    pkt.options[o++] = 255;

    uint16_t send_len = (uint16_t)(sizeof(dhcp_packet_t) - 312 + o);
    udp_send(0xFFFFFFFF, DHCP_PORT_CLIENT, DHCP_PORT_SERVER, &pkt, send_len);

    return true;
}

dhcp_state_t dhcp_get_state(void) {
    return g_dhcp_state;
}

const char* dhcp_state_to_str(dhcp_state_t st) {
    switch (st) {
        case DHCP_STATE_IDLE:           return "Inattivo";
        case DHCP_STATE_DISCOVER_SENT:  return "Discover Inviato";
        case DHCP_STATE_OFFER_RECEIVED: return "Offerta Ricevuta";
        case DHCP_STATE_REQUEST_SENT:   return "Richiesta Inviata";
        case DHCP_STATE_BOUND:          return "Connesso (Bound)";
        case DHCP_STATE_FAILED:         return "Fallito";
        default:                        return "Sconosciuto";
    }
}
