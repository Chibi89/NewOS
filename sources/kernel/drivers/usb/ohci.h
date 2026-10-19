#ifndef DRIVERS_USB_OHCI_H
#define DRIVERS_USB_OHCI_H

#include <types.h>
#include <drivers/pci.h>
#include <drivers/usb/usb.h>

#define OHCI_MAX_ROOT_PORTS 8

typedef struct {
    pci_device_t pci_dev;
    uint64_t     mmio_base;
    uint8_t      irq;
    bool         is_active;
    uint8_t      revision;
    uint8_t      max_ports;
    char         controller_name[48];
} ohci_controller_t;

bool                     ohci_init(void);
bool                     ohci_is_active(void);
const ohci_controller_t* ohci_get_controller(void);
int                      ohci_scan_ports(void);
bool                     ohci_reset_port(uint8_t port_id);

#endif // DRIVERS_USB_OHCI_H
