#include "dll_libc.h"
#include <arch/x86_64/io.h>
#include <drivers/net/e1000.h>
#include <drivers/net/net_nic.h>
#include <drivers/net/r8169.h>
#include <drivers/net/rtl8139.h>
#include <drivers/net/virtio_net.h>
#include <drivers/net/wifi.h>
#include <drivers/pci.h>
#include <drivers/timer.h>
#include <mm/paging.h>
#include <drivers/usb/usb.h>
#include <drivers/usb/usb_net.h>
#include <drivers/usb/xhci.h>
#include <mm/heap.h>
#include <net/arp.h>
#include <net/dhcp.h>
#include <net/dns.h>
#include <net/eth.h>
#include <net/icmp.h>
#include <net/ipv4.h>
#include <net/socket.h>
#include <net/tcp.h>
#include <net/tls.h>
#include <net/udp.h>
#include <string.h>

__attribute__((section(".header"), used)) module_header_t g_netstack_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size = 0,
    .entry_offset = 64,
    .checksum = 0,
    .security_flags =
        SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "NETSTACK",
    .signature = MODULE_AUTH_TAG};

static volatile uint32_t g_reloc_info[2] = { 0x52454C4Fu, 0xCAFEBABEu };

static void netstack_apply_relocs(void) {
    if (g_reloc_info[0] != 0x52454C4Fu || g_reloc_info[1] == 0xCAFEBABEu) return;
    uintptr_t base = (uintptr_t)&g_netstack_header;
    const uint32_t *tbl = (const uint32_t *)(base + g_reloc_info[1]);
    uint32_t count = tbl[0];
    for (uint32_t i = 0; i < count; i++) {
        uint32_t off = tbl[1 + i];
        *(uint64_t *)(base + off) += (uint64_t)base;
    }
    g_reloc_info[0] = 0;
}

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

__attribute__((force_align_arg_pointer))
int module_main(const kernel_api_t *api, netstack_params_t *p) {
    netstack_apply_relocs();
    if (!api || !p) return -1;
    g_dll_api = api;
    switch (p->cmd) {
        case 1: net_init(); break;
        case 2: net_poll(); break;
        case 3: p->ret_i32 = net_socket(p->iarg1, p->iarg2, (int)p->uarg1); break;
        case 4: p->ret_i32 = net_connect(p->iarg1, (const sockaddr_in_t *)p->cptr1); break;
        case 5: p->ret_i32 = net_send(p->iarg1, p->cptr1, p->uarg1, p->iarg2); break;
        case 6: p->ret_i32 = net_recv(p->iarg1, p->ptr1, p->uarg1, p->iarg2); break;
        case 7: p->ret_i32 = net_close(p->iarg1); break;
        case 8: p->ret_u32 = dns_resolve((const char *)p->cptr1); break;
        case 9: p->ret_bool = net_nic_init(); break;
        case 10: p->ret_ptr = net_nic_get_active(); break;
        case 11: net_nic_poll(); break;
        case 12: p->ret_bool = dhcp_discover(); break;
        case 13: p->ret_i32 = (int32_t)dhcp_get_state(); break;
        case 14: p->ret_str = dhcp_state_to_str((dhcp_state_t)p->iarg1); break;
        case 15: p->ret_bool = icmp_ping_ip(p->uarg1, p->uarg2, (ping_stats_t *)p->ptr1, (void (*)(int, uint32_t, uint8_t))p->ptr2); break;
        case 16: p->ret_ptr = tcp_create_conn(); break;
        case 17: p->ret_bool = tcp_connect((tcp_conn_t *)p->ptr1, p->uarg1, (uint16_t)p->uarg2); break;
        case 18: p->ret_i32 = tcp_send((tcp_conn_t *)p->ptr1, p->cptr1, (uint16_t)p->uarg1); break;
        case 19: p->ret_i32 = tcp_recv((tcp_conn_t *)p->ptr1, p->ptr2, (uint16_t)p->uarg1); break;
        case 20: tcp_close((tcp_conn_t *)p->ptr1); break;
        case 21: tcp_destroy_conn((tcp_conn_t *)p->ptr1); break;
        case 22: p->ret_i32 = tcp_get_connections_count(); break;
        case 23: p->ret_ptr = (void *)tcp_get_connection(p->iarg1); break;
        case 24: p->ret_str = tcp_state_to_str((tcp_state_t)p->iarg1); break;
        case 25: p->ret_i32 = wifi_scan((wifi_ap_t *)p->ptr1, p->iarg1); break;
        case 26: p->ret_bool = wifi_connect((const char *)p->cptr1, (const char *)p->cptr2); break;
        case 27: p->ret_bool = wifi_disconnect(); break;
        case 28: p->ret_bool = wifi_get_info((wifi_info_t *)p->ptr1); break;
        case 29: wifi_set_hardware_cap(p->uarg1, (const char *)p->cptr1); break;
        case 30: p->ret_str = wifi_bus_to_str((wifi_bus_t)p->iarg1); break;
        case 31: p->ret_str = wifi_band_to_str((wifi_band_t)p->iarg1); break;
        default: return -1;
    }
    return 0;
}

#include "dll_ext_macros.h"

#undef net_nic_get_active
#undef dhcp_discover
#undef net_nic_poll
#undef icmp_ping_ip
#undef tcp_create_conn
#undef tcp_connect
#undef tcp_send
#undef tcp_recv
#undef tcp_close
#undef tcp_destroy_conn
#undef net_socket
#undef net_connect
#undef net_send
#undef net_recv
#undef net_close
#undef net_poll
#undef dns_resolve
#undef net_format_ip
#undef net_format_mac
#undef net_parse_ip
#undef wifi_scan
#undef wifi_connect
#undef wifi_disconnect
#undef wifi_get_info
#undef wifi_set_hardware_cap
#undef wifi_bus_to_str
#undef wifi_band_to_str
#undef dhcp_get_state
#undef dhcp_state_to_str
#undef tcp_get_connections_count
#undef tcp_get_connection
#undef tcp_state_to_str

#include "../kernel/drivers/usb/usb_net.c"
#define serial_log nic_serial_log
#include "../kernel/drivers/net/net_nic.c"
#undef serial_log

#define serial_log e1000_serial_log
#define g_tx_buffers e1000_tx_buffers
#define g_tx_cur e1000_tx_cur
#define g_nic_ref e1000_nic_ref
#include "../kernel/drivers/net/e1000.c"
#undef serial_log
#undef g_tx_buffers
#undef g_tx_cur
#undef g_nic_ref

#define serial_log rtl_serial_log
#define g_tx_buffers rtl_tx_buffers
#define g_tx_cur rtl_tx_cur
#define g_nic_ref rtl_nic_ref
#include "../kernel/drivers/net/rtl8139.c"
#undef serial_log
#undef g_tx_buffers
#undef g_tx_cur
#undef g_nic_ref

#include "../kernel/drivers/net/r8169.c"
#include "../kernel/drivers/net/virtio_net.c"

#define serial_log wifi_serial_log
#include "../kernel/drivers/net/wifi.c"
#undef serial_log

#include "../kernel/net/eth.c"
#include "../kernel/net/arp.c"
#include "../kernel/net/ipv4.c"
#include "../kernel/net/icmp.c"
#include "../kernel/net/udp.c"
#include "../kernel/net/tcp.c"
#include "../kernel/net/dhcp.c"
#include "../kernel/net/dns.c"
#include "../kernel/net/tls.c"
#define serial_log sock_serial_log
#include "../kernel/net/socket.c"
#undef serial_log
