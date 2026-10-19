#ifndef DRIVERS_USB_HUB_H
#define DRIVERS_USB_HUB_H

#include <types.h>
#include <drivers/usb/usb.h>

#define USB_HUB_MAX_PORTS 4

typedef struct {
    uint8_t port_id;
    bool    power_on;
    bool    device_connected;
    uint8_t speed;
} usb_hub_port_t;

typedef struct {
    bool           is_active;
    uint8_t        hub_address;
    uint8_t        port_count;
    usb_hub_port_t ports[USB_HUB_MAX_PORTS];
    char           name[32];
} usb_hub_t;

bool              usb_hub_init(void);
bool              usb_hub_is_active(void);
const usb_hub_t*  usb_hub_get(void);
int               usb_hub_scan_ports(void);

#endif // DRIVERS_USB_HUB_H
