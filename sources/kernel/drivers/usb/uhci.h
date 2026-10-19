#ifndef DRIVERS_USB_UHCI_H
#define DRIVERS_USB_UHCI_H

#include <types.h>
#include <drivers/pci.h>
#include <drivers/usb/usb.h>

#define UHCI_MAX_ROOT_PORTS 2

typedef struct {
    pci_device_t pci_dev;
    uint16_t     io_base;
    uint8_t      irq;
    bool         is_active;
    char         controller_name[48];
    uint8_t      port_count;
} uhci_controller_t;

bool                     uhci_init(void);
bool                     uhci_is_active(void);
const uhci_controller_t* uhci_get_controller(void);
int                      uhci_scan_ports(void);
bool                     uhci_reset_port(uint8_t port_id);

#endif // DRIVERS_USB_UHCI_H
