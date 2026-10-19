#include <drivers/usb/usb_hub.h>
#include <drivers/usb/usb.h>
#include <string.h>

static usb_hub_t g_hub;
static bool      g_hub_initialized = false;

bool usb_hub_init(void) {
    if (g_hub_initialized) return g_hub.is_active;

    memset(&g_hub, 0, sizeof(usb_hub_t));
    g_hub.is_active = true;
    g_hub.hub_address = 1;
    g_hub.port_count = USB_HUB_MAX_PORTS;
    strcpy(g_hub.name, "Generic USB 1.1 4-Port Root Hub");

    for (uint8_t p = 0; p < USB_HUB_MAX_PORTS; p++) {
        g_hub.ports[p].port_id = p + 1;
        g_hub.ports[p].power_on = true;
        g_hub.ports[p].device_connected = false;
        g_hub.ports[p].speed = USB_SPEED_FULL;
    }

    g_hub_initialized = true;
    usb_hub_scan_ports();

    return true;
}

bool usb_hub_is_active(void) {
    return g_hub.is_active;
}

const usb_hub_t* usb_hub_get(void) {
    if (!g_hub_initialized) usb_hub_init();
    return &g_hub;
}

int usb_hub_scan_ports(void) {
    if (!g_hub.is_active) return 0;
    int connected = 0;

    int total_devs = usb_get_device_count();
    for (int i = 0; i < total_devs; i++) {
        const usb_device_t* dev = usb_get_device(i);
        if (dev && dev->is_connected && dev->port_id > 0 && dev->port_id <= USB_HUB_MAX_PORTS) {
            g_hub.ports[dev->port_id - 1].device_connected = true;
            g_hub.ports[dev->port_id - 1].speed = dev->speed;
            connected++;
        }
    }

    return connected;
}
