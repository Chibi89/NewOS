#ifndef DRIVERS_USB_H
#define DRIVERS_USB_H

#include <types.h>

#define USB_MAX_DEVICES 16

typedef enum {
    USB_SPEED_UNKNOWN         = 0,
    USB_SPEED_LOW             = 1, // 1.5 Mbps (USB 1.0)
    USB_SPEED_FULL            = 2, // 12 Mbps (USB 1.1)
    USB_SPEED_HIGH            = 3, // 480 Mbps / ~40 MB/s (USB 2.0)
    USB_SPEED_SUPER           = 4, // 5 Gbps / ~500 MB/s (USB 3.0 / 3.2 Gen 1)
    USB_SPEED_SUPER_PLUS      = 5, // 10 Gbps / ~1200 MB/s (USB 3.2 Gen 2)
    USB_SPEED_SUPER_PLUS_40G  = 6  // 40 Gbps / fino a 5000 MB/s (USB4 / Thunderbolt 4)
} usb_speed_t;

// Classi USB standard
#define USB_CLASS_PER_INTERFACE 0x00
#define USB_CLASS_AUDIO         0x01
#define USB_CLASS_COMM          0x02 // CDC
#define USB_CLASS_HID           0x03 // Tastiera, Mouse
#define USB_CLASS_PHYSICAL      0x05
#define USB_CLASS_IMAGE         0x06
#define USB_CLASS_PRINTER       0x07
#define USB_CLASS_MASS_STORAGE  0x08 // Flash Drive, HDD USB
#define USB_CLASS_HUB           0x09
#define USB_CLASS_CDC_DATA      0x0A
#define USB_CLASS_SMART_CARD    0x0B
#define USB_CLASS_VIDEO         0x0E
#define USB_CLASS_WIRELESS      0xE0 // Dongle Wi-Fi, Bluetooth
#define USB_CLASS_VENDOR        0xFF // Chipset proprietari Realtek / MediaTek

typedef enum {
    USB_CONTROLLER_NONE  = 0,
    USB_CONTROLLER_UHCI  = 1, // USB 1.1 Intel Universal HCI (I/O Space)
    USB_CONTROLLER_OHCI  = 2, // USB 1.1 Open HCI (MMIO Space)
    USB_CONTROLLER_EHCI  = 3, // USB 2.0 Enhanced HCI
    USB_CONTROLLER_XHCI  = 4  // USB 3.x / USB4 Extensible HCI
} usb_controller_type_t;

typedef struct {
    uint8_t               port_id;
    usb_speed_t           speed;
    usb_controller_type_t controller_type;
    uint8_t               address;
    uint16_t              vendor_id;
    uint16_t              device_id;
    uint8_t               class_code;
    uint8_t               subclass;
    uint8_t               protocol;
    char                  manufacturer[32];
    char                  product[48];
    char                  serial[32];
    uint32_t              max_bandwidth_mbps;
    bool                  is_connected;
    bool                  is_configured;
    void*                 driver_priv;
} usb_device_t;

void          usb_init(void);
void          usb_subsystem_init(void);
int           usb_get_device_count(void);
usb_device_t* usb_get_device(int index);
bool          usb_register_device(const usb_device_t* dev);
usb_device_t* usb_find_by_class(uint8_t class_code);
usb_device_t* usb_find_wireless_device(void);
const char*   usb_speed_to_str(usb_speed_t speed);
const char*   usb_class_to_str(uint8_t class_code);
const char*   usb_controller_type_to_str(usb_controller_type_t type);

#endif // DRIVERS_USB_H
