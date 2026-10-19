#ifndef DRIVERS_PCI_H
#define DRIVERS_PCI_H

#include <types.h>

typedef struct {
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_id;
    uint8_t  subclass_id;
    uint8_t  prog_if;
    uint32_t bar0;
    uint32_t bar1;
    uint32_t bar2;
    uint32_t bar3;
    uint32_t bar4;
    uint32_t bar5;
    uint8_t  irq;
} pci_device_t;

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint8_t  pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
void pci_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val);

bool pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t* dev);
bool pci_find_class(uint8_t class_id, uint8_t subclass_id, pci_device_t* dev);
bool pci_find_class_prog_if(uint8_t class_id, uint8_t subclass_id, uint8_t prog_if, pci_device_t* dev);
void pci_enable_bus_master(pci_device_t* dev);

#endif // DRIVERS_PCI_H
