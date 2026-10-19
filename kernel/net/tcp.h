#ifndef NET_TCP_H
#define NET_TCP_H

#include <types.h>

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20

typedef enum {
    TCP_STATE_CLOSED = 0,
    TCP_STATE_LISTEN,
    TCP_STATE_SYN_SENT,
    TCP_STATE_SYN_RECEIVED,
    TCP_STATE_ESTABLISHED,
    TCP_STATE_FIN_WAIT_1,
    TCP_STATE_FIN_WAIT_2,
    TCP_STATE_CLOSE_WAIT,
    TCP_STATE_CLOSING,
    TCP_STATE_LAST_ACK,
    TCP_STATE_TIME_WAIT
} tcp_state_t;

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dest_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint16_t data_offset_flags; // Data offset (4 bit) + Reserved (6 bit) + Flags (6 bit)
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_ptr;
} tcp_header_t;

#define TCP_MAX_SOCKETS 16
#define TCP_BUFFER_SIZE 4096

typedef struct {
    int         id;
    tcp_state_t state;
    uint32_t    local_ip;
    uint16_t    local_port;
    uint32_t    remote_ip;
    uint16_t    remote_port;
    uint32_t    snd_una;     // Send unacknowledged
    uint32_t    snd_nxt;     // Next sequence to send
    uint32_t    rcv_nxt;     // Next sequence expected to receive
    uint16_t    rcv_wnd;     // Receive window
    uint8_t     rx_buf[TCP_BUFFER_SIZE];
    uint32_t    rx_head;
    uint32_t    rx_tail;
    bool        is_listening;
    bool        active;
} tcp_conn_t;

// Inizializza il layer TCP
void tcp_init(void);

// Crea una nuova connessione TCP
tcp_conn_t* tcp_create_conn(void);

// Rilascia connessione TCP
void tcp_destroy_conn(tcp_conn_t* conn);

// Connette a un host remoto (Handshake a 3 vie SYN -> SYN/ACK -> ACK)
bool tcp_connect(tcp_conn_t* conn, uint32_t remote_ip, uint16_t remote_port);

// Invia dati su una connessione attiva
int tcp_send(tcp_conn_t* conn, const void* data, uint16_t len);

// Riceve dati da una connessione attiva
int tcp_recv(tcp_conn_t* conn, void* buf, uint16_t max_len);

// Chiude la connessione (invio FIN)
void tcp_close(tcp_conn_t* conn);

// Mette la connessione in ascolto
bool tcp_listen(tcp_conn_t* conn, uint16_t port);

// Gestisce pacchetti TCP in ingresso
void tcp_handle_packet(uint32_t src_ip, uint32_t dest_ip, const uint8_t* payload, uint16_t len);

// Restituisce il numero di connessioni attive per comando `netstat`
int tcp_get_connections_count(void);
const tcp_conn_t* tcp_get_connection(int idx);
const char* tcp_state_to_str(tcp_state_t st);

#endif // NET_TCP_H
