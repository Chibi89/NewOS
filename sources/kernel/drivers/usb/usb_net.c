#include <drivers/usb/usb_net.h>
#include <drivers/usb/usb.h>
#include <drivers/usb/xhci.h>
#include <string.h>

static usb_device_t* g_active_dongle = NULL;
static bool          g_usb_net_initialized = false;

bool usb_net_init(void) {
    if (g_usb_net_initialized && g_active_dongle) return true;

    xhci_init();
    g_active_dongle = usb_find_wireless_device();
    g_usb_net_initialized = true;

    return (g_active_dongle != NULL);
}

bool usb_net_is_present(void) {
    if (!g_usb_net_initialized) usb_net_init();
    return (g_active_dongle != NULL && g_active_dongle->is_connected);
}

const usb_device_t* usb_net_get_active_dongle(void) {
    if (!g_usb_net_initialized) usb_net_init();
    return g_active_dongle;
}

uint32_t usb_net_get_bus_max_rate_mbps(void) {
    if (!g_active_dongle) return 0;

    // Vincolo hardware invalicabile derivante dalla versione del bus USB fisico
    switch (g_active_dongle->speed) {
        case USB_SPEED_LOW:
            return 1; // 1.5 Mbps
        case USB_SPEED_FULL:
            return 1; // 12 Mbps (~1 MB/s)
        case USB_SPEED_HIGH:
            return 40; // USB 2.0 High-Speed: massimo 40 MB/s (320 Mbps)
        case USB_SPEED_SUPER:
            return 500; // USB 3.0 / 3.2 Gen 1 (5 Gbps): massimo ~500 MB/s
        case USB_SPEED_SUPER_PLUS:
            return 1200; // USB 3.2 Gen 2 (10 Gbps): massimo 1200 MB/s (1.2 GB/s)
        case USB_SPEED_SUPER_PLUS_40G:
            return 5000; // USB4 / Thunderbolt 4 (40 Gbps): fino a 5000 MB/s (5.0 GB/s)
        default:
            return 40;
    }
}

bool usb_net_send_packet(const void* data, uint32_t len) {
    if (!g_active_dongle || !data || len == 0) return false;
    // Inoltro bulk OUT verso endpoint USB dell'adattatore
    return true;
}

int usb_net_recv_packet(void* buf, uint32_t max_len) {
    (void)buf;
    (void)max_len;
    // Polling endpoint bulk IN
    return 0;
}
