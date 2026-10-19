#include <net/socket.h>
#include <drivers/net/net_nic.h>
#include <drivers/timer.h>
#include <net/eth.h>
#include <net/ipv4.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/tcp.h>
#include <net/dhcp.h>
#include <net/tls.h>
#include <mm/heap.h>
#include <string.h>
#include <arch/x86_64/io.h>

#define MAX_SOCKETS 16

typedef struct {
    int         id;
    int         domain;
    int         type;
    int         protocol;
    tcp_conn_t* tcp_conn;
    uint16_t    udp_port;
    bool        active;
} socket_desc_t;

static socket_desc_t g_sockets[MAX_SOCKETS];

static void serial_log(const char* s) {
    if (!s) return;
    while (*s) outb(0x3F8, *s++);
}

void net_init(void) {
    serial_log("[NET] Inizializzazione Network Stack NewOS V1 (ETH, IP, ICMP, UDP, TCP, DHCP, TLS)...\n");

    memset(g_sockets, 0, sizeof(g_sockets));
    for (int i = 0; i < MAX_SOCKETS; i++) {
        g_sockets[i].id = i;
    }

    net_nic_init();
    eth_init();
    ipv4_init();
    icmp_init();
    udp_init();
    tcp_init();
    dhcp_init();
    tls_init();

    // Avvia discovery DHCP reale su cavo/interfaccia virtuale
    serial_log("[NET] Invio richiesta DHCP Discover...\n");
    dhcp_discover();

    // Attendi handshake DHCP (massimo 500ms)
    for (int wait_i = 0; wait_i < 50; wait_i++) {
        net_nic_poll();
        if (dhcp_get_state() == DHCP_STATE_BOUND) {
            break;
        }
        timer_sleep(10);
    }

    net_nic_t* nic = net_nic_get_active();
    if (nic && nic->ip != 0) {
        char ip_str[20], gw_str[20], mask_str[20];
        net_format_ip(nic->ip, ip_str, sizeof(ip_str));
        net_format_ip(nic->netmask, mask_str, sizeof(mask_str));
        net_format_ip(nic->gateway, gw_str, sizeof(gw_str));
        serial_log("[NET] Configurazione DHCP completata con successo!\n");
        serial_log("[NET] IP: ");
        serial_log(ip_str);
        serial_log(" Subnet: ");
        serial_log(mask_str);
        serial_log(" Gateway: ");
        serial_log(gw_str);
        serial_log("\n");
    } else {
        serial_log("[NET] Nessun server DHCP rilevato. IP non assegnato (0.0.0.0).\n");
    }
}

void net_poll(void) {
    net_nic_poll();
}

int net_socket(int domain, int type, int protocol) {
    if (domain != AF_INET) return -1;

    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!g_sockets[i].active) {
            g_sockets[i].domain = domain;
            g_sockets[i].type = type;
            g_sockets[i].protocol = protocol;
            g_sockets[i].tcp_conn = NULL;
            g_sockets[i].udp_port = 0;
            g_sockets[i].active = true;

            if (type == SOCK_STREAM) {
                g_sockets[i].tcp_conn = tcp_create_conn();
                if (!g_sockets[i].tcp_conn) {
                    g_sockets[i].active = false;
                    return -1;
                }
            }
            return i;
        }
    }
    return -1;
}

int net_bind(int sockfd, const sockaddr_in_t* addr) {
    if (sockfd < 0 || sockfd >= MAX_SOCKETS || !g_sockets[sockfd].active || !addr) return -1;

    socket_desc_t* s = &g_sockets[sockfd];
    if (s->type == SOCK_STREAM && s->tcp_conn) {
        return tcp_listen(s->tcp_conn, net_ntohs(addr->sin_port)) ? 0 : -1;
    } else if (s->type == SOCK_DGRAM) {
        s->udp_port = net_ntohs(addr->sin_port);
        return 0;
    }
    return -1;
}

int net_listen(int sockfd, int backlog) {
    (void)backlog;
    if (sockfd < 0 || sockfd >= MAX_SOCKETS || !g_sockets[sockfd].active) return -1;

    socket_desc_t* s = &g_sockets[sockfd];
    if (s->type == SOCK_STREAM && s->tcp_conn) {
        s->tcp_conn->is_listening = true;
        s->tcp_conn->state = TCP_STATE_LISTEN;
        return 0;
    }
    return -1;
}

int net_connect(int sockfd, const sockaddr_in_t* addr) {
    if (sockfd < 0 || sockfd >= MAX_SOCKETS || !g_sockets[sockfd].active || !addr) return -1;

    socket_desc_t* s = &g_sockets[sockfd];
    if (s->type == SOCK_STREAM && s->tcp_conn) {
        uint32_t rip = addr->sin_addr;
        uint16_t rport = net_ntohs(addr->sin_port);
        return tcp_connect(s->tcp_conn, rip, rport) ? 0 : -1;
    }
    return -1;
}

int net_send(int sockfd, const void* buf, size_t len, int flags) {
    (void)flags;
    if (sockfd < 0 || sockfd >= MAX_SOCKETS || !g_sockets[sockfd].active || !buf || len == 0) return -1;

    socket_desc_t* s = &g_sockets[sockfd];
    if (s->type == SOCK_STREAM && s->tcp_conn) {
        return tcp_send(s->tcp_conn, buf, (uint16_t)len);
    }
    return -1;
}

int net_recv(int sockfd, void* buf, size_t len, int flags) {
    (void)flags;
    if (sockfd < 0 || sockfd >= MAX_SOCKETS || !g_sockets[sockfd].active || !buf || len == 0) return -1;

    socket_desc_t* s = &g_sockets[sockfd];
    if (s->type == SOCK_STREAM && s->tcp_conn) {
        return tcp_recv(s->tcp_conn, buf, (uint16_t)len);
    }
    return -1;
}

int net_close(int sockfd) {
    if (sockfd < 0 || sockfd >= MAX_SOCKETS || !g_sockets[sockfd].active) return -1;

    socket_desc_t* s = &g_sockets[sockfd];
    if (s->type == SOCK_STREAM && s->tcp_conn) {
        tcp_close(s->tcp_conn);
        tcp_destroy_conn(s->tcp_conn);
        s->tcp_conn = NULL;
    }
    s->active = false;
    return 0;
}

bool net_ping(const char* ip_str, uint32_t count, ping_stats_t* stats, void (*on_reply)(int seq, uint32_t rtt_ms, uint8_t ttl)) {
    if (!ip_str) return false;
    uint32_t target_ip = net_parse_ip(ip_str);
    if (target_ip == 0 && strcmp(ip_str, "0.0.0.0") != 0) return false;

    return icmp_ping_ip(target_ip, count, stats, on_reply);
}
