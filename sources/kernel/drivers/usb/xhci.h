#ifndef DRIVERS_USB_XHCI_H
#define DRIVERS_USB_XHCI_H

#include <types.h>
#include <drivers/pci.h>
#include <drivers/usb/usb.h>

#define XHCI_MAX_ROOT_PORTS 16

typedef struct {
    pci_device_t pci_dev;
    uint64_t     mmio_base;
    uint8_t      cap_length;
    uint16_t     hci_version;
    uint8_t      max_slots;
    uint8_t      max_ports;
    uint32_t     op_offset;
    bool         is_active;
    char         controller_name[48];
} xhci_controller_t;

bool                     xhci_init(void);
bool                     xhci_is_active(void);
const xhci_controller_t* xhci_get_controller(void);
int                      xhci_scan_root_ports(void);
bool                     xhci_reset_port(uint8_t port_id);

#endif // DRIVERS_USB_XHCI_H
