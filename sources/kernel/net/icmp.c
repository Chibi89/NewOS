#include <net/icmp.h>
#include <net/ipv4.h>
#include <drivers/net/net_nic.h>
#include <drivers/timer.h>
#include <mm/heap.h>
#include <string.h>

static volatile bool     g_ping_reply_received = false;
static volatile uint16_t g_ping_reply_seq = 0;
static volatile uint32_t g_ping_reply_time = 0;
static volatile uint8_t  g_ping_reply_ttl = 64;

void icmp_init(void) {
    g_ping_reply_received = false;
}

void icmp_handle_packet(uint32_t src_ip, uint8_t ttl, const uint8_t* payload, uint16_t len) {
    if (!payload || len < sizeof(icmp_header_t)) return;

    const icmp_header_t* hdr = (const icmp_header_t*)payload;

    if (hdr->type == ICMP_TYPE_ECHO_REQUEST) {
        // Rispondi con Echo Reply (Type 0)
        uint8_t* reply_buf = (uint8_t*)kmalloc(len);
        if (!reply_buf) return;

        memcpy(reply_buf, payload, len);
        icmp_header_t* reply_hdr = (icmp_header_t*)reply_buf;
        reply_hdr->type = ICMP_TYPE_ECHO_REPLY;
        reply_hdr->code = 0;
        reply_hdr->checksum = 0;
        reply_hdr->checksum = net_checksum(reply_buf, len);

        ipv4_send(src_ip, IPV4_PROTO_ICMP, reply_buf, len);
        kfree(reply_buf);
    } else if (hdr->type == ICMP_TYPE_ECHO_REPLY) {
        g_ping_reply_received = true;
        g_ping_reply_seq = net_ntohs(hdr->seq);
        g_ping_reply_time = (uint32_t)timer_get_ticks();
        g_ping_reply_ttl = ttl;
    }
}

bool icmp_send_echo(uint32_t dest_ip, uint16_t id, uint16_t seq, const void* data, uint16_t len) {
    uint16_t total_len = sizeof(icmp_header_t) + len;
    uint8_t* buf = (uint8_t*)kmalloc(total_len);
    if (!buf) return false;

    icmp_header_t* hdr = (icmp_header_t*)buf;
    hdr->type = ICMP_TYPE_ECHO_REQUEST;
    hdr->code = 0;
    hdr->checksum = 0;
    hdr->id = net_htons(id);
    hdr->seq = net_htons(seq);

    if (data && len > 0) {
        memcpy(buf + sizeof(icmp_header_t), data, len);
    }

    hdr->checksum = net_checksum(buf, total_len);

    bool res = ipv4_send(dest_ip, IPV4_PROTO_ICMP, buf, total_len);
    kfree(buf);
    return res;
}

bool icmp_ping_ip(uint32_t ip, uint32_t count, ping_stats_t* stats, void (*on_reply)(int seq, uint32_t rtt_ms, uint8_t ttl)) {
    if (count == 0) count = 4;
    if (stats) {
        memset(stats, 0, sizeof(ping_stats_t));
        stats->min_time_ms = 999999;
    }

    const char* ping_payload = "NewOS V1 Network Ping Payload 32";
    uint16_t payload_len = (uint16_t)strlen(ping_payload);

    uint32_t total_time = 0;

    for (uint32_t i = 0; i < count; i++) {
        g_ping_reply_received = false;
        uint64_t start_tick = timer_get_ticks();

        icmp_send_echo(ip, 0x1234, (uint16_t)(i + 1), ping_payload, payload_len);
        if (stats) stats->packets_sent++;

        // Attendi risposta con timeout di 1000ms
        bool got_it = false;
        uint32_t rtt = 0;

        for (int wait = 0; wait < 100; wait++) {
            net_nic_poll();
            if (g_ping_reply_received && g_ping_reply_seq == (uint16_t)(i + 1)) {
                got_it = true;
                uint64_t end_tick = timer_get_ticks();
                // 1 tick = ~10ms (100 Hz timer)
                rtt = (uint32_t)((end_tick - start_tick) * 10);
                if (rtt == 0) rtt = 1; // < 1ms
                break;
            }
            timer_sleep(10);
        }

        if (got_it) {
            if (stats) {
                stats->packets_recv++;
                if (rtt < stats->min_time_ms) stats->min_time_ms = rtt;
                if (rtt > stats->max_time_ms) stats->max_time_ms = rtt;
                total_time += rtt;
            }
            if (on_reply) {
                on_reply((int)(i + 1), rtt, g_ping_reply_ttl);
            }
        } else {
            if (on_reply) {
                on_reply((int)(i + 1), 0xFFFFFFFF, 0); // Timeout
            }
        }

        if (i < count - 1) {
            timer_sleep(300); // Pausa fra un ping e il successivo
        }
    }

    if (stats && stats->packets_recv > 0) {
        stats->avg_time_ms = total_time / stats->packets_recv;
        stats->success = true;
    }

    return (stats && stats->packets_recv > 0);
}
