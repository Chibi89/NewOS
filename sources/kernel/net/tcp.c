#include <net/tcp.h>
#include <net/ipv4.h>
#include <drivers/net/net_nic.h>
#include <drivers/timer.h>
#include <mm/heap.h>
#include <string.h>

static tcp_conn_t g_conns[TCP_MAX_SOCKETS];
static uint16_t   g_next_ephemeral_port = 49152;

typedef struct __attribute__((packed)) {
    uint32_t src_ip;
    uint32_t dest_ip;
    uint8_t  zero;
    uint8_t  protocol;
    uint16_t tcp_len;
} tcp_pseudo_header_t;

static uint16_t tcp_calc_checksum(uint32_t src_ip, uint32_t dest_ip, const void* tcp_data, uint16_t tcp_len) {
    tcp_pseudo_header_t pseudo;
    pseudo.src_ip = src_ip;
    pseudo.dest_ip = dest_ip;
    pseudo.zero = 0;
    pseudo.protocol = IPV4_PROTO_TCP;
    pseudo.tcp_len = net_htons(tcp_len);

    uint32_t sum = 0;
    const uint16_t* ptr = (const uint16_t*)&pseudo;
    for (size_t i = 0; i < sizeof(pseudo) / 2; i++) {
        sum += *ptr++;
    }

    ptr = (const uint16_t*)tcp_data;
    size_t len = tcp_len;
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

void tcp_init(void) {
    memset(g_conns, 0, sizeof(g_conns));
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        g_conns[i].id = i;
        g_conns[i].state = TCP_STATE_CLOSED;
    }
}

tcp_conn_t* tcp_create_conn(void) {
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        if (!g_conns[i].active) {
            memset(&g_conns[i], 0, sizeof(tcp_conn_t));
            g_conns[i].id = i;
            g_conns[i].state = TCP_STATE_CLOSED;
            g_conns[i].rcv_wnd = TCP_BUFFER_SIZE;
            g_conns[i].active = true;
            return &g_conns[i];
        }
    }
    return NULL;
}

void tcp_destroy_conn(tcp_conn_t* conn) {
    if (!conn) return;
    conn->active = false;
    conn->state = TCP_STATE_CLOSED;
}

static bool tcp_send_segment(tcp_conn_t* conn, uint8_t flags, const void* data, uint16_t len) {
    if (!conn) return false;

    uint16_t tcp_total_len = sizeof(tcp_header_t) + len;
    uint8_t* buf = (uint8_t*)kmalloc(tcp_total_len);
    if (!buf) return false;

    tcp_header_t* hdr = (tcp_header_t*)buf;
    hdr->src_port = net_htons(conn->local_port);
    hdr->dest_port = net_htons(conn->remote_port);
    hdr->seq_num = net_htonl(conn->snd_nxt);
    hdr->ack_num = net_htonl(conn->rcv_nxt);
    // Data offset = 5 (20 byte) in 4-bit, flags in lower 6-bit
    hdr->data_offset_flags = net_htons((5U << 12) | (flags & 0x3F));
    hdr->window_size = net_htons(conn->rcv_wnd);
    hdr->checksum = 0;
    hdr->urgent_ptr = 0;

    if (data && len > 0) {
        memcpy(buf + sizeof(tcp_header_t), data, len);
    }

    hdr->checksum = tcp_calc_checksum(conn->local_ip, conn->remote_ip, buf, tcp_total_len);

    bool ok = ipv4_send(conn->remote_ip, IPV4_PROTO_TCP, buf, tcp_total_len);
    kfree(buf);

    if (flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) {
        conn->snd_nxt++;
    } else {
        conn->snd_nxt += len;
    }
    return ok;
}

bool tcp_connect(tcp_conn_t* conn, uint32_t remote_ip, uint16_t remote_port) {
    if (!conn) return false;
    net_nic_t* nic = net_nic_get_active();
    if (!nic) return false;

    conn->local_ip = nic->ip;
    conn->local_port = g_next_ephemeral_port++;
    if (g_next_ephemeral_port > 65000) g_next_ephemeral_port = 49152;
    conn->remote_ip = remote_ip;
    conn->remote_port = remote_port;
    conn->snd_nxt = (uint32_t)timer_get_ticks() * 1000 + 12345;
    conn->snd_una = conn->snd_nxt;
    conn->rcv_nxt = 0;
    conn->state = TCP_STATE_SYN_SENT;

    // 1. Invia SYN
    tcp_send_segment(conn, TCP_FLAG_SYN, NULL, 0);

    // 2. Attendi risposta SYN+ACK con timeout
    for (int timeout = 0; timeout < 150; timeout++) {
        net_nic_poll();
        if (conn->state == TCP_STATE_ESTABLISHED) {
            return true;
        }
        timer_sleep(10);
    }

    conn->state = TCP_STATE_CLOSED;
    return false;
}

int tcp_send(tcp_conn_t* conn, const void* data, uint16_t len) {
    if (!conn || conn->state != TCP_STATE_ESTABLISHED || !data || len == 0) return -1;
    if (tcp_send_segment(conn, TCP_FLAG_ACK | TCP_FLAG_PSH, data, len)) {
        return len;
    }
    return -1;
}

int tcp_recv(tcp_conn_t* conn, void* buf, uint16_t max_len) {
    if (!conn || !buf || max_len == 0) return -1;

    uint16_t available = 0;
    if (conn->rx_head >= conn->rx_tail) {
        available = conn->rx_head - conn->rx_tail;
    } else {
        available = TCP_BUFFER_SIZE - conn->rx_tail + conn->rx_head;
    }

    if (available == 0) {
        if (conn->state == TCP_STATE_CLOSED || conn->state == TCP_STATE_CLOSE_WAIT) return 0;
        return -1; // Non ci sono dati al momento
    }

    uint16_t read_bytes = (available < max_len) ? available : max_len;
    uint8_t* dst = (uint8_t*)buf;

    for (uint16_t i = 0; i < read_bytes; i++) {
        dst[i] = conn->rx_buf[conn->rx_tail];
        conn->rx_tail = (conn->rx_tail + 1) % TCP_BUFFER_SIZE;
    }
    return read_bytes;
}

void tcp_close(tcp_conn_t* conn) {
    if (!conn || conn->state == TCP_STATE_CLOSED) return;

    if (conn->state == TCP_STATE_ESTABLISHED) {
        tcp_send_segment(conn, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
        conn->state = TCP_STATE_FIN_WAIT_1;
    } else {
        conn->state = TCP_STATE_CLOSED;
    }
}

bool tcp_listen(tcp_conn_t* conn, uint16_t port) {
    if (!conn) return false;
    net_nic_t* nic = net_nic_get_active();
    conn->local_ip = nic ? nic->ip : 0;
    conn->local_port = port;
    conn->is_listening = true;
    conn->state = TCP_STATE_LISTEN;
    return true;
}

void tcp_handle_packet(uint32_t src_ip, uint32_t dest_ip, const uint8_t* payload, uint16_t len) {
    if (!payload || len < sizeof(tcp_header_t)) return;

    const tcp_header_t* hdr = (const tcp_header_t*)payload;
    uint16_t src_port = net_ntohs(hdr->src_port);
    uint16_t dest_port = net_ntohs(hdr->dest_port);
    uint32_t seq = net_ntohl(hdr->seq_num);
    uint32_t ack = net_ntohl(hdr->ack_num);
    uint16_t flags_word = net_ntohs(hdr->data_offset_flags);
    uint8_t  flags = (uint8_t)(flags_word & 0x3F);
    uint8_t  hdr_len = (uint8_t)((flags_word >> 12) * 4);

    if (hdr_len > len) return;

    const uint8_t* data = payload + hdr_len;
    uint16_t data_len = len - hdr_len;

    // Trova connessione attiva corrispondente
    tcp_conn_t* conn = NULL;
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        if (g_conns[i].active) {
            if (g_conns[i].local_port == dest_port &&
                (g_conns[i].remote_port == src_port || g_conns[i].is_listening) &&
                (g_conns[i].remote_ip == src_ip || g_conns[i].is_listening)) {
                conn = &g_conns[i];
                break;
            }
        }
    }

    if (!conn) return;

    if (conn->state == TCP_STATE_SYN_SENT) {
        if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
            conn->rcv_nxt = seq + 1;
            conn->snd_una = ack;
            conn->state = TCP_STATE_ESTABLISHED;
            // Invia ACK di completamento handshake a 3 vie
            tcp_send_segment(conn, TCP_FLAG_ACK, NULL, 0);
        }
    } else if (conn->state == TCP_STATE_LISTEN) {
        if (flags & TCP_FLAG_SYN) {
            conn->remote_ip = src_ip;
            conn->remote_port = src_port;
            conn->rcv_nxt = seq + 1;
            conn->snd_nxt = (uint32_t)timer_get_ticks() * 1000 + 5678;
            conn->state = TCP_STATE_SYN_RECEIVED;
            tcp_send_segment(conn, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
        }
    } else if (conn->state == TCP_STATE_SYN_RECEIVED) {
        if (flags & TCP_FLAG_ACK) {
            conn->snd_una = ack;
            conn->state = TCP_STATE_ESTABLISHED;
        }
    } else if (conn->state == TCP_STATE_ESTABLISHED) {
        if (data_len > 0) {
            // Bufferizza i dati in arrivo
            for (uint16_t i = 0; i < data_len; i++) {
                uint32_t next_head = (conn->rx_head + 1) % TCP_BUFFER_SIZE;
                if (next_head != conn->rx_tail) {
                    conn->rx_buf[conn->rx_head] = data[i];
                    conn->rx_head = next_head;
                }
            }
            conn->rcv_nxt = seq + data_len;
            tcp_send_segment(conn, TCP_FLAG_ACK, NULL, 0);
        }

        if (flags & TCP_FLAG_FIN) {
            conn->rcv_nxt = seq + 1;
            tcp_send_segment(conn, TCP_FLAG_ACK, NULL, 0);
            conn->state = TCP_STATE_CLOSE_WAIT;
        }
    } else if (conn->state == TCP_STATE_FIN_WAIT_1) {
        if (flags & TCP_FLAG_ACK) {
            conn->state = TCP_STATE_FIN_WAIT_2;
        }
        if (flags & TCP_FLAG_FIN) {
            conn->rcv_nxt = seq + 1;
            tcp_send_segment(conn, TCP_FLAG_ACK, NULL, 0);
            conn->state = TCP_STATE_CLOSED;
        }
    }
}

int tcp_get_connections_count(void) {
    int cnt = 0;
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        if (g_conns[i].active) cnt++;
    }
    return cnt;
}

const tcp_conn_t* tcp_get_connection(int idx) {
    int cur = 0;
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        if (g_conns[i].active) {
            if (cur == idx) return &g_conns[i];
            cur++;
        }
    }
    return NULL;
}

const char* tcp_state_to_str(tcp_state_t st) {
    switch (st) {
        case TCP_STATE_CLOSED:       return "CLOSED";
        case TCP_STATE_LISTEN:       return "LISTEN";
        case TCP_STATE_SYN_SENT:     return "SYN_SENT";
        case TCP_STATE_SYN_RECEIVED: return "SYN_RCVD";
        case TCP_STATE_ESTABLISHED:  return "ESTABLISHED";
        case TCP_STATE_FIN_WAIT_1:   return "FIN_WAIT_1";
        case TCP_STATE_FIN_WAIT_2:   return "FIN_WAIT_2";
        case TCP_STATE_CLOSE_WAIT:   return "CLOSE_WAIT";
        case TCP_STATE_CLOSING:      return "CLOSING";
        case TCP_STATE_LAST_ACK:     return "LAST_ACK";
        case TCP_STATE_TIME_WAIT:    return "TIME_WAIT";
        default:                     return "UNKNOWN";
    }
}
