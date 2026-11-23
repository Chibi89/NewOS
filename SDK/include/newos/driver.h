/**
 * newos/driver.h — Namespace Hardware & Bus Reali (Bare-Metal x86_64)
 */
#ifndef NEWOS_SDK_DRIVER_H
#define NEWOS_SDK_DRIVER_H

#include "types.h"

typedef enum {
    NS_BUS_UNKNOWN   = 0,
    NS_BUS_SATA_AHCI = 1,
    NS_BUS_ATA_IDE   = 2,
    NS_BUS_ATAPI_CD  = 3,
    NS_BUS_USB_XHCI  = 4,
    NS_BUS_USB_EHCI  = 5,
    NS_BUS_USB_UHCI  = 6,
    NS_BUS_FDC       = 7,
    NS_BUS_RAMFS     = 8,
    NS_BUS_NET_ETH   = 9,
    NS_BUS_NVME      = 10
} ns_bus_type_t;

typedef struct {
    uint8_t  drive_letter;
    uint8_t  bus_type;
    uint16_t reserved;
    uint32_t bus_max_kbps;
    char     controller[32];
} ns_bus_info_t;

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint8_t  class_code;
} ns_pci_id_t;

#endif /* NEWOS_SDK_DRIVER_H */
