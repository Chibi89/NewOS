#include <drivers/usb/usb.h>
#include <drivers/usb/uhci.h>
#include <drivers/usb/ohci.h>
#include <drivers/usb/xhci.h>
#include <drivers/usb/usb_hid.h>
#include <drivers/usb/usb_msc.h>
#include <drivers/usb/usb_hub.h>
#include <drivers/usb/usb_net.h>
#include <string.h>

static usb_device_t g_usb_devices[USB_MAX_DEVICES];
static int          g_usb_device_count = 0;
static bool         g_usb_initialized = false;

void usb_init(void) {
    if (g_usb_initialized) return;
    memset(g_usb_devices, 0, sizeof(g_usb_devices));
    g_usb_device_count = 0;
    g_usb_initialized = true;
}

void usb_subsystem_init(void) {
    usb_init();
    // 1. Controller legacy foundation UHCI (USB 1.1 Intel)
    uhci_init();
    // 2. Controller legacy foundation OHCI (USB 1.1 OpenHCI)
    ohci_init();
    // 3. Controller moderno xHCI (USB 3.x / USB4)
    xhci_init();
    // 4. Classi periferiche generiche foundation
    usb_hid_init();
    usb_msc_init();
    usb_hub_init();
    usb_net_init();
}

int usb_get_device_count(void) {
    return g_usb_device_count;
}

usb_device_t* usb_get_device(int index) {
    if (index < 0 || index >= g_usb_device_count) return NULL;
    return &g_usb_devices[index];
}

bool usb_register_device(const usb_device_t* dev) {
    if (!dev || g_usb_device_count >= USB_MAX_DEVICES) return false;

    // Controlla se la porta è già registrata
    for (int i = 0; i < g_usb_device_count; i++) {
        if (g_usb_devices[i].port_id == dev->port_id) {
            memcpy(&g_usb_devices[i], dev, sizeof(usb_device_t));
            return true;
        }
    }

    memcpy(&g_usb_devices[g_usb_device_count], dev, sizeof(usb_device_t));
    g_usb_device_count++;
    return true;
}

usb_device_t* usb_find_by_class(uint8_t class_code) {
    for (int i = 0; i < g_usb_device_count; i++) {
        if (g_usb_devices[i].is_connected && g_usb_devices[i].class_code == class_code) {
            return &g_usb_devices[i];
        }
    }
    return NULL;
}

usb_device_t* usb_find_wireless_device(void) {
    for (int i = 0; i < g_usb_device_count; i++) {
        if (!g_usb_devices[i].is_connected) continue;

        // Classe standard Wireless (0xE0)
        if (g_usb_devices[i].class_code == USB_CLASS_WIRELESS) {
            return &g_usb_devices[i];
        }

        // Chipset Wi-Fi con Vendor Specific Class (0xFF)
        if (g_usb_devices[i].class_code == USB_CLASS_VENDOR) {
            uint16_t vid = g_usb_devices[i].vendor_id;
            uint16_t pid = g_usb_devices[i].device_id;
            // Realtek Wi-Fi (0x0BDA)
            if (vid == 0x0BDA && (pid == 0x8852 || pid == 0x8832 || pid == 0x8812 || pid == 0xB812 || pid == 0xC811)) {
                return &g_usb_devices[i];
            }
            // MediaTek Wi-Fi (0x0E8D)
            if (vid == 0x0E8D && (pid == 0x7961 || pid == 0x7921)) {
                return &g_usb_devices[i];
            }
            // Intel Wi-Fi 7 / 6E USB dongle (0x8087)
            if (vid == 0x8087 && (pid == 0x0032 || pid == 0x0033 || pid == 0x0A2B)) {
                return &g_usb_devices[i];
            }
            // TP-Link / D-Link Wi-Fi (0x2357 / 0x2001)
            if (vid == 0x2357 || vid == 0x2001) {
                return &g_usb_devices[i];
            }
        }
    }
    return NULL;
}

const char* usb_speed_to_str(usb_speed_t speed) {
    switch (speed) {
        case USB_SPEED_LOW:            return "Low-Speed (1.5 Mbps)";
        case USB_SPEED_FULL:           return "Full-Speed (12 Mbps)";
        case USB_SPEED_HIGH:           return "High-Speed (480 Mbps / 40 MB/s)";
        case USB_SPEED_SUPER:          return "SuperSpeed USB 3.0 (5 Gbps / 500 MB/s)";
        case USB_SPEED_SUPER_PLUS:     return "SuperSpeed+ USB 3.2 (10 Gbps / 1200 MB/s)";
        case USB_SPEED_SUPER_PLUS_40G: return "USB4 / Thunderbolt 4 (40 Gbps / 5.0 GB/s)";
        default:                       return "Sconosciuta";
    }
}

const char* usb_class_to_str(uint8_t class_code) {
    switch (class_code) {
        case USB_CLASS_AUDIO:        return "Audio";
        case USB_CLASS_COMM:         return "Communication (CDC)";
        case USB_CLASS_HID:          return "Human Interface (HID)";
        case USB_CLASS_MASS_STORAGE: return "Mass Storage (MSC)";
        case USB_CLASS_HUB:          return "Hub";
        case USB_CLASS_WIRELESS:     return "Wireless Controller (WLAN/BT)";
        case USB_CLASS_VENDOR:       return "Vendor Specific (WLAN/Custom)";
        default:                     return "Periferica Generica";
    }
}

const char* usb_controller_type_to_str(usb_controller_type_t type) {
    switch (type) {
        case USB_CONTROLLER_UHCI: return "UHCI (USB 1.1 Intel)";
        case USB_CONTROLLER_OHCI: return "OHCI (USB 1.1 OpenHCI)";
        case USB_CONTROLLER_EHCI: return "EHCI (USB 2.0 High-Speed)";
        case USB_CONTROLLER_XHCI: return "xHCI (USB 3.x/USB4 SuperSpeed+)";
        default:                  return "Standard USB Controller";
    }
}
