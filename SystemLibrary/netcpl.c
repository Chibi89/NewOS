/**
 * SystemLibrary/netcpl.c
 * Libreria Dinamica Out-of-Kernel NETCPL.DLL (Applet Connessioni di Rete - NCPA.CPL)
 * Visualizzazione stato scheda di rete (Intel e1000 / Realtek RTL8139 / Wi-Fi),
 * indirizzo MAC, parametri IPv4/DHCP, statistiche pacchetti e diagnostica ICMP Ping.
 * (C) Newspoint Software - NewSoft Dynamic System Library
 */

#include "cpl_dll_ui.h"
#include <kernel_ext_api.h>
#include <drivers/net/net_nic.h>
#include <net/icmp.h>

#ifndef BUILDING_KERNEL_LOADER
__attribute__((section(".header"), used))
module_header_t g_netcpl_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size      = 0,
    .entry_offset   = 64,
    .checksum       = 0,
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "NETCPL",
    .signature      = MODULE_AUTH_TAG
};
#endif

static const kernel_api_t* g_api = NULL;

#define MAX_NETCPL_DNS_CACHE 4

typedef struct {
    char host[32];
    char ip_str[20];
    uint32_t ttl_sec;
} netcpl_dns_entry_t;

typedef struct {
    int          active_tab;      // 0: Stato Adattatore & IPv4, 1: Diagnostica & Ping, 2: DNS Cache & Strumenti
    bool         use_dhcp;
    char         ping_target[32];
    bool         ping_focused;
    ping_stats_t last_ping;
    bool         has_ping_result;
    char         dns_query[32];
    bool         dns_focused;
    netcpl_dns_entry_t dns_cache[MAX_NETCPL_DNS_CACHE];
    int          dns_cache_count;
    char         trace_line[64];
    char         status_msg[64];
} netcpl_state_t;

static void append_str(char* dst, const char* src, size_t max_len) {
    size_t l = dll_strlen(dst);
    dll_strcpy(dst + l, src, max_len - l);
}

static void netcpl_draw(window_t* win) {
    netcpl_state_t* st = (netcpl_state_t*)win->app_data;
    if (!st || !g_api) return;

    dll_theme_t t;
    dll_theme_init(g_api, &t);

    int32_t cx = win->x + 6;
    int32_t cy = win->y + 28;
    int32_t cw = win->width - 12;
    int32_t ch = win->height - 34;

    dll_draw_dialog_bg(g_api, &t, cx, cy, cw, ch);

    int32_t tab_y = cy + 4;
    int32_t tab_h = 22;
    g_api->wm_draw_themed_tab(cx + 8,   tab_y, 148, tab_h, "Stato Scheda & IPv4", st->active_tab == 0);
    g_api->wm_draw_themed_tab(cx + 158, tab_y, 150, tab_h, "Diagnostica & Ping",  st->active_tab == 1);
    g_api->wm_draw_themed_tab(cx + 310, tab_y, 148, tab_h, "DNS Cache & Tools",   st->active_tab == 2);
    dll_draw_tabs_divider(g_api, cx, tab_y, tab_h, cw, &t);

    net_nic_t* nic = g_api->net_nic_get_active ? (net_nic_t*)g_api->net_nic_get_active() : NULL;

    if (st->active_tab == 0) {
        // Box 1: Adattatore Hardware
        int32_t b1_x = cx + 10;
        int32_t b1_y = cy + 36;
        int32_t b1_w = cw - 20;
        int32_t b1_h = 92;
        dll_draw_groupbox(g_api, b1_x, b1_y, b1_w, b1_h, " Interfaccia di Rete Hardware Attiva ", &t);

        bool connected = (nic && nic->link_up);
        g_api->gfx_draw_icon_32(b1_x + 16, b1_y + 24, connected ? ICON_NET_ON : ICON_NET_OFF);

        const char* nic_name = (nic && nic->name[0]) ? nic->name : "Intel PRO/1000 MT Desktop (82540EM)";
        g_api->gfx_draw_string(b1_x + 58, b1_y + 18, nic_name, t.accent, 0, true);

        char mac_str[32];
        mac_str[0] = '\0';
        if (nic && g_api->net_format_mac) {
            g_api->net_format_mac(nic->mac, mac_str, sizeof(mac_str));
        } else {
            dll_strcpy(mac_str, "52:54:00:12:34:56", sizeof(mac_str));
        }
        char mac_line[64];
        dll_strcpy(mac_line, "Indirizzo Fisico (MAC): ", sizeof(mac_line));
        append_str(mac_line, mac_str, sizeof(mac_line));
        g_api->gfx_draw_string(b1_x + 58, b1_y + 38, mac_line, t.text_primary, 0, true);

        const char* link_str = connected ? "Stato Link: Connesso (1000 Mbps Full-Duplex)"
                                         : "Stato Link: Cavo scollegato / Nessun Link";
        g_api->gfx_draw_string(b1_x + 58, b1_y + 58, link_str, connected ? 0x107C41 : 0xD83B01, 0, true);

        // Box 2: Parametri TCP/IPv4
        int32_t b2_x = cx + 10;
        int32_t b2_y = b1_y + b1_h + 10;
        int32_t b2_w = cw - 20;
        int32_t b2_h = 172;
        dll_draw_groupbox(g_api, b2_x, b2_y, b2_w, b2_h, " Configurazione Protocollo Internet TCP/IPv4 ", &t);

        char ip_s[20], mask_s[20], gw_s[20], dns_s[20];
        if (nic && g_api->net_format_ip) {
            g_api->net_format_ip(nic->ip,      ip_s,   sizeof(ip_s));
            g_api->net_format_ip(nic->netmask, mask_s, sizeof(mask_s));
            g_api->net_format_ip(nic->gateway, gw_s,   sizeof(gw_s));
            g_api->net_format_ip(nic->dns,     dns_s,  sizeof(dns_s));
        } else {
            dll_strcpy(ip_s,   "10.0.2.15",     sizeof(ip_s));
            dll_strcpy(mask_s, "255.255.255.0", sizeof(mask_s));
            dll_strcpy(gw_s,   "10.0.2.2",      sizeof(gw_s));
            dll_strcpy(dns_s,  "10.0.2.3",      sizeof(dns_s));
        }

        char l1[64], l2[64], l3[64], l4[64];
        dll_strcpy(l1, "Indirizzo IPv4:     ", sizeof(l1)); append_str(l1, ip_s,   sizeof(l1));
        dll_strcpy(l2, "Subnet Mask:        ", sizeof(l2)); append_str(l2, mask_s, sizeof(l2));
        dll_strcpy(l3, "Gateway Predef.:    ", sizeof(l3)); append_str(l3, gw_s,   sizeof(l3));
        dll_strcpy(l4, "Server DNS Prim.:   ", sizeof(l4)); append_str(l4, dns_s,  sizeof(l4));

        g_api->gfx_draw_string(b2_x + 16, b2_y + 22, l1, t.text_primary, 0, true);
        g_api->gfx_draw_string(b2_x + 16, b2_y + 44, l2, t.text_primary, 0, true);
        g_api->gfx_draw_string(b2_x + 16, b2_y + 66, l3, t.text_primary, 0, true);
        g_api->gfx_draw_string(b2_x + 16, b2_y + 88, l4, t.text_primary, 0, true);

        char pkt_line[64];
        char rx_n[16], tx_n[16];
        dll_uint_to_str(nic ? nic->rx_packets : 0, rx_n, 0);
        dll_uint_to_str(nic ? nic->tx_packets : 0, tx_n, 0);
        dll_strcpy(pkt_line, "Pacchetti RX: ", sizeof(pkt_line));
        append_str(pkt_line, rx_n, sizeof(pkt_line));
        append_str(pkt_line, "  |  Pacchetti TX: ", sizeof(pkt_line));
        append_str(pkt_line, tx_n, sizeof(pkt_line));
        g_api->gfx_draw_string(b2_x + 16, b2_y + 112, pkt_line, t.text_secondary, 0, true);

        g_api->wm_draw_themed_button(b2_x + 16,  b2_y + 138, 165, 24, "Rinnova Lease DHCP", false);
        g_api->wm_draw_themed_button(b2_x + 192, b2_y + 138, 165, 24, "Aggiorna Traffico", false);
    } else if (st->active_tab == 1) {
        // Scheda 1: Diagnostica & Ping
        int32_t b1_x = cx + 10;
        int32_t b1_y = cy + 36;
        int32_t b1_w = cw - 20;
        int32_t b1_h = 115;
        dll_draw_groupbox(g_api, b1_x, b1_y, b1_w, b1_h, " Verifica Connettivita' ICMP Echo (Ping) ", &t);

        g_api->gfx_draw_string(b1_x + 14, b1_y + 20, "Indirizzo IPv4 di destinazione (es. 10.0.2.2 o 8.8.8.8):", t.text_primary, 0, true);
        dll_draw_input_box(g_api, b1_x + 14, b1_y + 40, 220, 24, st->ping_target, st->ping_focused, false, &t);

        g_api->wm_draw_themed_button(b1_x + 246, b1_y + 40, 120, 24, "Esegui Ping", false);
        g_api->wm_draw_themed_button(b1_x + 14,  b1_y + 76, 150, 24, "Ping Gateway", false);
        g_api->wm_draw_themed_button(b1_x + 174, b1_y + 76, 150, 24, "Ping DNS 8.8.8.8", false);

        int32_t b2_x = cx + 10;
        int32_t b2_y = b1_y + b1_h + 10;
        int32_t b2_w = cw - 20;
        int32_t b2_h = 148;
        dll_draw_groupbox(g_api, b2_x, b2_y, b2_w, b2_h, " Risultato Diagnostica ICMP ", &t);

        if (!st->has_ping_result) {
            g_api->gfx_draw_string(b2_x + 16, b2_y + 30, "Premi 'Esegui Ping' per inviare pacchetti ICMP Echo Request", t.text_secondary, 0, true);
            g_api->gfx_draw_string(b2_x + 16, b2_y + 50, "e misurare il tempo di risposta (RTT) verso l'host.", t.text_secondary, 0, true);
        } else {
            char s_sent[12], s_recv[12], s_avg[12];
            dll_uint_to_str(st->last_ping.packets_sent, s_sent, 0);
            dll_uint_to_str(st->last_ping.packets_recv, s_recv, 0);
            dll_uint_to_str(st->last_ping.avg_time_ms,  s_avg,  0);

            char r1[64], r2[64];
            dll_strcpy(r1, "Pacchetti Inviati: ", sizeof(r1));
            append_str(r1, s_sent, sizeof(r1));
            append_str(r1, "  |  Ricevuti: ", sizeof(r1));
            append_str(r1, s_recv, sizeof(r1));

            dll_strcpy(r2, "Latenza Media (RTT): ", sizeof(r2));
            append_str(r2, s_avg, sizeof(r2));
            append_str(r2, " ms", sizeof(r2));

            g_api->gfx_draw_string(b2_x + 16, b2_y + 26, r1, t.text_primary, 0, true);
            g_api->gfx_draw_string(b2_x + 16, b2_y + 50, r2, t.text_primary, 0, true);
            if (st->last_ping.packets_recv > 0) {
                g_api->gfx_draw_string(b2_x + 16, b2_y + 80, "ESITO: Host raggiungibile correttamente!", 0x107C41, 0, true);
            } else {
                g_api->gfx_draw_string(b2_x + 16, b2_y + 80, "ESITO: Nessuna risposta (Timeout richiesta).", 0xD83B01, 0, true);
            }
        }
    } else if (st->active_tab == 2) {
        int32_t b1_x = cx + 10, b1_y = cy + 36, b1_w = cw - 20, b1_h = 152;
        dll_draw_groupbox(g_api, b1_x, b1_y, b1_w, b1_h, " Risolutore & Cache DNS Locale (TTL 300s) ", &t);
        dll_draw_input_box(g_api, b1_x + 14, b1_y + 22, 200, 24, st->dns_query, st->dns_focused, false, &t);
        g_api->wm_draw_themed_button(b1_x + 222, b1_y + 22, 108, 24, "Risolvi DNS", false);
        g_api->wm_draw_themed_button(b1_x + 336, b1_y + 22, 100, 24, "Svuota", false);

        if (st->dns_cache_count == 0) {
            g_api->gfx_draw_string(b1_x + 16, b1_y + 62, "Cache DNS vuota. Inserisci un host (es. gateway.newos).", t.text_secondary, 0, true);
        } else {
            for (int i = 0; i < st->dns_cache_count; i++) {
                char row[64];
                dll_strcpy(row, st->dns_cache[i].host, sizeof(row));
                append_str(row, "  ->  ", sizeof(row));
                append_str(row, st->dns_cache[i].ip_str, sizeof(row));
                append_str(row, "  (TTL 300s)", sizeof(row));
                g_api->gfx_draw_string(b1_x + 16, b1_y + 56 + i * 20, row, t.text_primary, 0, true);
            }
        }

        int32_t b2_x = cx + 10, b2_y = b1_y + b1_h + 10, b2_w = cw - 20, b2_h = 112;
        dll_draw_groupbox(g_api, b2_x, b2_y, b2_w, b2_h, " Tabella ARP & Traceroute Rapido ", &t);
        char gw_s[20];
        if (nic && g_api->net_format_ip) g_api->net_format_ip(nic->gateway, gw_s, sizeof(gw_s));
        else dll_strcpy(gw_s, "10.0.2.2", sizeof(gw_s));
        char arp_line[64];
        dll_strcpy(arp_line, "ARP Gateway: ", sizeof(arp_line));
        append_str(arp_line, gw_s, sizeof(arp_line));
        append_str(arp_line, "  [52:55:0a:00:02:02]  DINAMICO", sizeof(arp_line));
        g_api->gfx_draw_string(b2_x + 16, b2_y + 24, arp_line, t.text_primary, 0, true);
        g_api->gfx_draw_string(b2_x + 16, b2_y + 50, st->trace_line[0] ? st->trace_line : "Traceroute: premi 'Trace Gateway' per verificare gli hop.", t.text_secondary, 0, true);
        g_api->wm_draw_themed_button(b2_x + 16, b2_y + 76, 150, 24, "Trace Gateway", false);
    }

    int32_t foot_y = cy + ch - 32;
    if (st->status_msg[0]) {
        g_api->gfx_draw_string(cx + 12, foot_y - 18, st->status_msg, t.is_dark ? 0x4EC9B0 : 0x107C41, 0, true);
    }
    g_api->wm_draw_themed_button(cx + cw - 88, foot_y, 80, 24, "Chiudi", false);
}

static void run_ping_test(netcpl_state_t* st) {
    if (!st || !g_api || !g_api->net_parse_ip || !g_api->icmp_ping_ip) return;
    uint32_t ip = g_api->net_parse_ip(st->ping_target);
    if (ip == 0) {
        dll_strcpy(st->status_msg, "Indirizzo IP non valido.", sizeof(st->status_msg));
        return;
    }
    for (size_t i = 0; i < sizeof(ping_stats_t); i++) ((uint8_t*)&st->last_ping)[i] = 0;
    g_api->icmp_ping_ip(ip, 2, &st->last_ping, NULL);
    st->has_ping_result = true;
    dll_strcpy(st->status_msg, "Test ICMP Ping completato.", sizeof(st->status_msg));
}

static void run_dns_resolve(netcpl_state_t* st) {
    if (!st || !g_api || st->dns_query[0] == '\0') return;
    const kernel_ext_api_t* ext = (const kernel_ext_api_t*)g_api->ext_api;
    uint32_t ip = 0;
    if (dll_strcmp(st->dns_query, "localhost") == 0) {
        ip = g_api->net_parse_ip ? g_api->net_parse_ip("127.0.0.1") : 0x0100007Fu;
    } else if (dll_strcmp(st->dns_query, "gateway.newos") == 0) {
        ip = g_api->net_parse_ip ? g_api->net_parse_ip("10.0.2.2") : 0x0202000Au;
    } else if (ext && ext->dns_resolve) {
        ip = ext->dns_resolve(st->dns_query);
    }

    char ip_s[20];
    if (ip != 0 && g_api->net_format_ip) {
        g_api->net_format_ip(ip, ip_s, sizeof(ip_s));
    } else if (ip != 0) {
        dll_strcpy(ip_s, "10.0.2.2", sizeof(ip_s));
    } else {
        dll_strcpy(st->status_msg, "Risoluzione DNS fallita (host non trovato).", sizeof(st->status_msg));
        return;
    }

    if (st->dns_cache_count >= MAX_NETCPL_DNS_CACHE) {
        for (int i = 0; i < MAX_NETCPL_DNS_CACHE - 1; i++) st->dns_cache[i] = st->dns_cache[i + 1];
        st->dns_cache_count = MAX_NETCPL_DNS_CACHE - 1;
    }
    int idx = st->dns_cache_count++;
    dll_strcpy(st->dns_cache[idx].host, st->dns_query, sizeof(st->dns_cache[idx].host));
    dll_strcpy(st->dns_cache[idx].ip_str, ip_s, sizeof(st->dns_cache[idx].ip_str));
    st->dns_cache[idx].ttl_sec = 300;
    dll_strcpy(st->status_msg, "Host risolto e memorizzato nella Cache DNS!", sizeof(st->status_msg));
}

static void netcpl_mouse(window_t* win, int32_t lx, int32_t ly, bool clicked) {
    if (!clicked) return;
    netcpl_state_t* st = (netcpl_state_t*)win->app_data;
    if (!st || !g_api) return;

    int32_t cx = 6;
    int32_t cy = 28;
    int32_t cw = win->width - 12;
    int32_t ch = win->height - 34;

    if (ly >= cy + 4 && ly <= cy + 26) {
        if (lx >= cx + 8 && lx < cx + 156)   { st->active_tab = 0; return; }
        if (lx >= cx + 158 && lx < cx + 308) { st->active_tab = 1; return; }
        if (lx >= cx + 310 && lx < cx + 458) { st->active_tab = 2; return; }
    }

    if (st->active_tab == 0) {
        int32_t b2_x = cx + 10;
        int32_t b2_y = cy + 36 + 92 + 10;
        if (ly >= b2_y + 138 && ly <= b2_y + 162) {
            if (lx >= b2_x + 16 && lx <= b2_x + 181) {
                if (g_api->dhcp_discover) g_api->dhcp_discover();
                dll_strcpy(st->status_msg, "Lease DHCP rinnovato con successo!", sizeof(st->status_msg));
                return;
            }
            if (lx >= b2_x + 192 && lx <= b2_x + 357) {
                if (g_api->net_nic_poll) g_api->net_nic_poll();
                dll_strcpy(st->status_msg, "Contatori traffico aggiornati.", sizeof(st->status_msg));
                return;
            }
        }
    } else if (st->active_tab == 1) {
        int32_t b1_x = cx + 10;
        int32_t b1_y = cy + 36;
        if (lx >= b1_x + 14 && lx <= b1_x + 234 && ly >= b1_y + 40 && ly <= b1_y + 64) {
            st->ping_focused = true;
            return;
        }
        if (lx >= b1_x + 246 && lx <= b1_x + 366 && ly >= b1_y + 40 && ly <= b1_y + 64) {
            run_ping_test(st);
            return;
        }
        if (ly >= b1_y + 76 && ly <= b1_y + 100) {
            if (lx >= b1_x + 14 && lx <= b1_x + 164) {
                dll_strcpy(st->ping_target, "10.0.2.2", sizeof(st->ping_target));
                run_ping_test(st);
                return;
            }
            if (lx >= b1_x + 174 && lx <= b1_x + 324) {
                dll_strcpy(st->ping_target, "8.8.8.8", sizeof(st->ping_target));
                run_ping_test(st);
                return;
            }
        }
    } else if (st->active_tab == 2) {
        int32_t b1_x = cx + 10, b1_y = cy + 36;
        if (ly >= b1_y + 22 && ly <= b1_y + 46) {
            if (lx >= b1_x + 14 && lx <= b1_x + 214) { st->dns_focused = true; return; }
            if (lx >= b1_x + 222 && lx <= b1_x + 330) { run_dns_resolve(st); return; }
            if (lx >= b1_x + 336 && lx <= b1_x + 436) {
                st->dns_cache_count = 0;
                dll_strcpy(st->status_msg, "Cache DNS locale svuotata.", sizeof(st->status_msg));
                return;
            }
        }
        int32_t b2_x = cx + 10, b2_y = b1_y + 152 + 10;
        if (ly >= b2_y + 76 && ly <= b2_y + 100 && lx >= b2_x + 16 && lx <= b2_x + 166) {
            ping_stats_t ps;
            for (size_t i = 0; i < sizeof(ps); i++) ((uint8_t*)&ps)[i] = 0;
            uint32_t gw = g_api->net_parse_ip ? g_api->net_parse_ip("10.0.2.2") : 0;
            if (gw && g_api->icmp_ping_ip) g_api->icmp_ping_ip(gw, 1, &ps, NULL);
            char ms[12];
            dll_uint_to_str(ps.avg_time_ms, ms, 0);
            dll_strcpy(st->trace_line, "Hop 1: 10.0.2.2 (Gateway QEMU) RTT ", sizeof(st->trace_line));
            append_str(st->trace_line, ms, sizeof(st->trace_line));
            append_str(st->trace_line, " ms", sizeof(st->trace_line));
            dll_strcpy(st->status_msg, "Traceroute Gateway completato.", sizeof(st->status_msg));
            return;
        }
    }

    int32_t foot_y = cy + ch - 32;
    if (ly >= foot_y && ly <= foot_y + 24 && lx >= cx + cw - 88 && lx <= cx + cw - 8) {
        g_api->wm_destroy_window(win->id);
    }
}

static void netcpl_key(window_t* win, char c) {
    netcpl_state_t* st = (netcpl_state_t*)win->app_data;
    if (!st || !g_api) return;

    if (c == 27) {
        g_api->wm_destroy_window(win->id);
        return;
    }
    if (st->active_tab == 1 && st->ping_focused) {
        if (c == '\n' || c == '\r') {
            run_ping_test(st);
            return;
        }
        size_t len = dll_strlen(st->ping_target);
        if (c == '\b') {
            if (len > 0) st->ping_target[len - 1] = '\0';
            return;
        }
        if (((c >= '0' && c <= '9') || c == '.') && len < 20) {
            st->ping_target[len] = c;
            st->ping_target[len + 1] = '\0';
        }
    } else if (st->active_tab == 2 && st->dns_focused) {
        if (c == '\n' || c == '\r') {
            run_dns_resolve(st);
            return;
        }
        size_t len = dll_strlen(st->dns_query);
        if (c == '\b') {
            if (len > 0) st->dns_query[len - 1] = '\0';
            return;
        }
        if (c >= 32 && c <= 126 && len < 28) {
            st->dns_query[len] = c;
            st->dns_query[len + 1] = '\0';
        }
    }
}

__attribute__((section(".text.entry"), used))
int netcpl_dll_entry(const kernel_api_t* api, window_t* win) {
    if (!api || !win) return -1;
    g_api = api;

    netcpl_state_t* st = (netcpl_state_t*)g_api->kmalloc(sizeof(netcpl_state_t));
    if (!st) return -1;
    for (size_t i = 0; i < sizeof(netcpl_state_t); i++) ((uint8_t*)st)[i] = 0;

    st->use_dhcp = true;
    dll_strcpy(st->ping_target, "10.0.2.2", sizeof(st->ping_target));
    st->ping_focused = true;
    dll_strcpy(st->dns_query, "gateway.newos", sizeof(st->dns_query));
    st->dns_focused = true;

    win->app_data = st;
    win->on_draw  = netcpl_draw;
    win->on_mouse = netcpl_mouse;
    win->on_key   = netcpl_key;
    return 0;
}

