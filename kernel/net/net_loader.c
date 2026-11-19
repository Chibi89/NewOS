#include <drivers/net/net_nic.h>
#include <drivers/net/wifi.h>
#include <fs/vfs.h>
#include <fs/vfs_mount.h>
#include <gui/screen_mgr.h>
#include <mm/heap.h>
#include <net/dhcp.h>
#include <net/dns.h>
#include <net/icmp.h>
#include <net/socket.h>
#include <net/tcp.h>
#include <string.h>

typedef struct {
    uint32_t cmd;
    int32_t iarg1;
    int32_t iarg2;
    uint32_t uarg1;
    uint32_t uarg2;
    const void *cptr1;
    const void *cptr2;
    void *ptr1;
    void *ptr2;
    int32_t ret_i32;
    uint32_t ret_u32;
    bool ret_bool;
    void *ret_ptr;
    const char *ret_str;
} netstack_params_t;

typedef int (*netstack_entry_fn)(const kernel_api_t *api, netstack_params_t *p);

static netstack_entry_fn g_netstack_entry = NULL;
static bool g_netstack_inited = false;

static bool ensure_netstack_loaded(void) {
    if (g_netstack_entry) {
        if (!g_netstack_inited) {
            g_netstack_inited = true;
            netstack_params_t p;
            memset(&p, 0, sizeof(p));
            p.cmd = 1;
            g_netstack_entry(screen_mgr_get_api(), &p);
        }
        return true;
    }
    if (!vfs_is_drive_mounted('C')) return false;

    const char *paths[] = {
        "C:\\NewOS\\SystemLibrary\\NETSTACK.DLL",
        "/NewOS/SystemLibrary/NETSTACK.DLL"
    };
    for (int i = 0; i < 2 && !g_netstack_entry; i++) {
        uint32_t sz = 0;
        uint8_t *buf = (uint8_t *)vfs_read_entire_file(paths[i], &sz);
        if (buf && sz >= sizeof(module_header_t)) {
            if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY, NULL)) {
                uint8_t *exec_buf = (uint8_t *)kmalloc(sz + 163840);
                if (exec_buf) {
                    memcpy(exec_buf, buf, sz);
                    memset(exec_buf + sz, 0, 163840);
                    const module_header_t *h = (const module_header_t *)exec_buf;
                    g_netstack_entry = (netstack_entry_fn)(exec_buf + h->entry_offset);
                }
            }
        }
        if (buf) kfree(buf);
    }
    if (g_netstack_entry && !g_netstack_inited) {
        g_netstack_inited = true;
        netstack_params_t p;
        memset(&p, 0, sizeof(p));
        p.cmd = 1;
        g_netstack_entry(screen_mgr_get_api(), &p);
    }
    return (g_netstack_entry != NULL);
}

void net_init(void) {
    ensure_netstack_loaded();
}

void net_poll(void) {
    if (!ensure_netstack_loaded()) return;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 2;
    g_netstack_entry(screen_mgr_get_api(), &p);
}

int net_socket(int domain, int type, int protocol) {
    if (!ensure_netstack_loaded()) return -1;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 3;
    p.iarg1 = domain;
    p.iarg2 = type;
    p.uarg1 = (uint32_t)protocol;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return p.ret_i32;
}

int net_connect(int sockfd, const sockaddr_in_t *addr) {
    if (!ensure_netstack_loaded()) return -1;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 4;
    p.iarg1 = sockfd;
    p.cptr1 = addr;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return p.ret_i32;
}

int net_send(int sockfd, const void *buf, size_t len, int flags) {
    if (!ensure_netstack_loaded()) return -1;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 5;
    p.iarg1 = sockfd;
    p.cptr1 = buf;
    p.uarg1 = (uint32_t)len;
    p.iarg2 = flags;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return p.ret_i32;
}

int net_recv(int sockfd, void *buf, size_t len, int flags) {
    if (!ensure_netstack_loaded()) return -1;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 6;
    p.iarg1 = sockfd;
    p.ptr1 = buf;
    p.uarg1 = (uint32_t)len;
    p.iarg2 = flags;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return p.ret_i32;
}

int net_close(int sockfd) {
    if (!ensure_netstack_loaded()) return -1;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 7;
    p.iarg1 = sockfd;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return p.ret_i32;
}

uint32_t dns_resolve(const char *hostname) {
    if (!ensure_netstack_loaded()) return 0;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 8;
    p.cptr1 = hostname;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return p.ret_u32;
}

bool net_nic_init(void) {
    if (!ensure_netstack_loaded()) return false;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 9;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return p.ret_bool;
}

net_nic_t *net_nic_get_active(void) {
    if (!ensure_netstack_loaded()) return NULL;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 10;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return (net_nic_t *)p.ret_ptr;
}

void net_nic_poll(void) {
    if (!ensure_netstack_loaded()) return;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 11;
    g_netstack_entry(screen_mgr_get_api(), &p);
}

bool dhcp_discover(void) {
    if (!ensure_netstack_loaded()) return false;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 12;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return p.ret_bool;
}

dhcp_state_t dhcp_get_state(void) {
    if (!ensure_netstack_loaded()) return DHCP_STATE_IDLE;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 13;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return (dhcp_state_t)p.ret_i32;
}

const char *dhcp_state_to_str(dhcp_state_t st) {
    if (!ensure_netstack_loaded()) return "INIT";
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 14;
    p.iarg1 = (int32_t)st;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return p.ret_str ? p.ret_str : "INIT";
}

bool icmp_ping_ip(uint32_t ip, uint32_t count, ping_stats_t *stats, void (*on_reply)(int seq, uint32_t rtt_ms, uint8_t ttl)) {
    if (!ensure_netstack_loaded()) return false;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 15;
    p.uarg1 = ip;
    p.uarg2 = count;
    p.ptr1 = stats;
    p.ptr2 = (void *)on_reply;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return p.ret_bool;
}

tcp_conn_t *tcp_create_conn(void) {
    if (!ensure_netstack_loaded()) return NULL;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 16;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return (tcp_conn_t *)p.ret_ptr;
}

bool tcp_connect(tcp_conn_t *conn, uint32_t remote_ip, uint16_t remote_port) {
    if (!ensure_netstack_loaded()) return false;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 17;
    p.ptr1 = conn;
    p.uarg1 = remote_ip;
    p.uarg2 = remote_port;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return p.ret_bool;
}

int tcp_send(tcp_conn_t *conn, const void *data, uint16_t len) {
    if (!ensure_netstack_loaded()) return -1;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 18;
    p.ptr1 = conn;
    p.cptr1 = data;
    p.uarg1 = len;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return p.ret_i32;
}

int tcp_recv(tcp_conn_t *conn, void *buf, uint16_t max_len) {
    if (!ensure_netstack_loaded()) return -1;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 19;
    p.ptr1 = conn;
    p.ptr2 = buf;
    p.uarg1 = max_len;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return p.ret_i32;
}

void tcp_close(tcp_conn_t *conn) {
    if (!ensure_netstack_loaded()) return;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 20;
    p.ptr1 = conn;
    g_netstack_entry(screen_mgr_get_api(), &p);
}

void tcp_destroy_conn(tcp_conn_t *conn) {
    if (!ensure_netstack_loaded()) return;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 21;
    p.ptr1 = conn;
    g_netstack_entry(screen_mgr_get_api(), &p);
}

int tcp_get_connections_count(void) {
    if (!ensure_netstack_loaded()) return 0;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 22;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return p.ret_i32;
}

const tcp_conn_t *tcp_get_connection(int index) {
    if (!ensure_netstack_loaded()) return NULL;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 23;
    p.iarg1 = index;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return (const tcp_conn_t *)p.ret_ptr;
}

const char *tcp_state_to_str(tcp_state_t st) {
    if (!ensure_netstack_loaded()) return "CLOSED";
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 24;
    p.iarg1 = (int32_t)st;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return p.ret_str ? p.ret_str : "CLOSED";
}

int wifi_scan(wifi_ap_t *out_list, int max_entries) {
    if (!ensure_netstack_loaded()) return 0;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 25;
    p.ptr1 = out_list;
    p.iarg1 = max_entries;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return p.ret_i32;
}

bool wifi_connect(const char *ssid, const char *passphrase) {
    if (!ensure_netstack_loaded()) return false;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 26;
    p.cptr1 = ssid;
    p.cptr2 = passphrase;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return p.ret_bool;
}

bool wifi_disconnect(void) {
    if (!ensure_netstack_loaded()) return false;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 27;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return p.ret_bool;
}

bool wifi_get_info(wifi_info_t *out_info) {
    if (!ensure_netstack_loaded()) return false;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 28;
    p.ptr1 = out_info;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return p.ret_bool;
}

void wifi_set_hardware_cap(uint32_t max_speed_mbps, const char *standard_name) {
    if (!ensure_netstack_loaded()) return;
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 29;
    p.uarg1 = max_speed_mbps;
    p.cptr1 = standard_name;
    g_netstack_entry(screen_mgr_get_api(), &p);
}

const char *wifi_bus_to_str(wifi_bus_t bus) {
    if (!ensure_netstack_loaded()) return "PCIe";
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 30;
    p.iarg1 = (int32_t)bus;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return p.ret_str ? p.ret_str : "PCIe";
}

const char *wifi_band_to_str(wifi_band_t band) {
    if (!ensure_netstack_loaded()) return "5 GHz";
    netstack_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 31;
    p.iarg1 = (int32_t)band;
    g_netstack_entry(screen_mgr_get_api(), &p);
    return p.ret_str ? p.ret_str : "5 GHz";
}

void net_format_ip(uint32_t ip, char *buf, size_t buf_size) {
    if (!buf || buf_size < 16) return;
    uint8_t *b = (uint8_t *)&ip;
    mini_snprintf(buf, buf_size, "%u.%u.%u.%u", (unsigned int)b[0],
                  (unsigned int)b[1], (unsigned int)b[2], (unsigned int)b[3]);
}

void net_format_mac(const uint8_t mac[6], char *buf, size_t buf_size) {
    if (!buf || buf_size < 18) return;
    mini_snprintf(buf, buf_size, "%02X:%02X:%02X:%02X:%02X:%02X",
                  (unsigned int)mac[0], (unsigned int)mac[1], (unsigned int)mac[2],
                  (unsigned int)mac[3], (unsigned int)mac[4], (unsigned int)mac[5]);
}

uint32_t net_parse_ip(const char *ip_str) {
    if (!ip_str) return 0;
    uint32_t b[4] = {0, 0, 0, 0};
    int idx = 0;
    const char *p = ip_str;
    while (*p && idx < 4) {
        if (*p >= '0' && *p <= '9') {
            b[idx] = b[idx] * 10 + (uint32_t)(*p - '0');
        } else if (*p == '.') {
            idx++;
        } else {
            break;
        }
        p++;
    }
    if (idx != 3) return 0;
    return (b[0] & 0xFF) | ((b[1] & 0xFF) << 8) | ((b[2] & 0xFF) << 16) | ((b[3] & 0xFF) << 24);
}
