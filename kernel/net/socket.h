#ifndef NET_SOCKET_H
#define NET_SOCKET_H

#include <types.h>
#include <net/icmp.h>
#include <net/tcp.h>

#define AF_INET     2
#define SOCK_STREAM 1 // TCP
#define SOCK_DGRAM  2 // UDP
#define SOCK_RAW    3 // RAW / ICMP

typedef struct {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    uint8_t  sin_zero[8];
} sockaddr_in_t;

// Inizializza l'intero stack di rete di NewOS V1 (NIC, ETH, ARP, IPv4, ICMP, UDP, TCP, DHCP, TLS)
void net_init(void);

// Polling globale della rete (chiamato ad ogni frame dal kernel)
void net_poll(void);

// BSD Socket API
int  net_socket(int domain, int type, int protocol);
int  net_bind(int sockfd, const sockaddr_in_t* addr);
int  net_listen(int sockfd, int backlog);
int  net_connect(int sockfd, const sockaddr_in_t* addr);
int  net_send(int sockfd, const void* buf, size_t len, int flags);
int  net_recv(int sockfd, void* buf, size_t len, int flags);
int  net_close(int sockfd);

// Funzione Ping ad alto livello per CLI e applicazioni
bool net_ping(const char* ip_str, uint32_t count, ping_stats_t* stats, void (*on_reply)(int seq, uint32_t rtt_ms, uint8_t ttl));

#endif // NET_SOCKET_H
