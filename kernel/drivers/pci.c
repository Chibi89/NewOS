#include <drivers/pci.h>
#include <arch/x86_64/io.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static uint32_t pci_get_addr(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return (uint32_t)((1U << 31) |
                     ((uint32_t)bus << 16) |
                     ((uint32_t)slot << 11) |
                     ((uint32_t)func << 8) |
                     (offset & 0xFC));
}

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_get_addr(bus, slot, func, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t val = pci_read32(bus, slot, func, offset);
    return (uint16_t)((val >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t val = pci_read32(bus, slot, func, offset);
    return (uint8_t)((val >> ((offset & 3) * 8)) & 0xFF);
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    outl(PCI_CONFIG_ADDRESS, pci_get_addr(bus, slot, func, offset));
    outl(PCI_CONFIG_DATA, val);
}

void pci_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val) {
    uint32_t old_val = pci_read32(bus, slot, func, offset);
    int shift = (offset & 2) * 8;
    uint32_t mask = 0xFFFFU << shift;
    uint32_t new_val = (old_val & ~mask) | (((uint32_t)val & 0xFFFFU) << shift);
    pci_write32(bus, slot, func, offset, new_val);
}

void pci_enable_bus_master(pci_device_t* dev) {
    if (!dev) return;
    uint16_t cmd = pci_read16(dev->bus, dev->slot, dev->func, 0x04);
    cmd |= 0x0007; // I/O Space (bit 0), Memory Space (bit 1), Bus Master (bit 2)
    pci_write16(dev->bus, dev->slot, dev->func, 0x04, cmd);
}

static void pci_fill_device_info(uint8_t bus, uint8_t slot, uint8_t func, pci_device_t* dev) {
    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;
    dev->vendor_id = pci_read16(bus, slot, func, 0x00);
    dev->device_id = pci_read16(bus, slot, func, 0x02);
    dev->class_id = pci_read8(bus, slot, func, 0x0B);
    dev->subclass_id = pci_read8(bus, slot, func, 0x0A);
    dev->prog_if = pci_read8(bus, slot, func, 0x09);
    dev->bar0 = pci_read32(bus, slot, func, 0x10);
    dev->bar1 = pci_read32(bus, slot, func, 0x14);
    dev->bar2 = pci_read32(bus, slot, func, 0x18);
    dev->bar3 = pci_read32(bus, slot, func, 0x1C);
    dev->bar4 = pci_read32(bus, slot, func, 0x20);
    dev->bar5 = pci_read32(bus, slot, func, 0x24);
    dev->irq = pci_read8(bus, slot, func, 0x3C);
}

bool pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t* dev) {
    for (uint16_t bus = 0; bus < 8; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint16_t v = pci_read16((uint8_t)bus, slot, 0, 0x00);
            if (v == 0xFFFF) continue;

            uint8_t header_type = pci_read8((uint8_t)bus, slot, 0, 0x0E);
            uint8_t max_func = (header_type & 0x80) ? 8 : 1;

            for (uint8_t func = 0; func < max_func; func++) {
                uint16_t cur_vendor = pci_read16((uint8_t)bus, slot, func, 0x00);
                if (cur_vendor == 0xFFFF) continue;
                uint16_t cur_device = pci_read16((uint8_t)bus, slot, func, 0x02);

                if (cur_vendor == vendor_id && cur_device == device_id) {
                    if (dev) pci_fill_device_info((uint8_t)bus, slot, func, dev);
                    return true;
                }
            }
        }
    }
    return false;
}

bool pci_find_class(uint8_t class_id, uint8_t subclass_id, pci_device_t* dev) {
    for (uint16_t bus = 0; bus < 8; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint16_t v = pci_read16((uint8_t)bus, slot, 0, 0x00);
            if (v == 0xFFFF) continue;

            uint8_t header_type = pci_read8((uint8_t)bus, slot, 0, 0x0E);
            uint8_t max_func = (header_type & 0x80) ? 8 : 1;

            for (uint8_t func = 0; func < max_func; func++) {
                uint16_t cur_vendor = pci_read16((uint8_t)bus, slot, func, 0x00);
                if (cur_vendor == 0xFFFF) continue;

                uint8_t c = pci_read8((uint8_t)bus, slot, func, 0x0B);
                uint8_t sc = pci_read8((uint8_t)bus, slot, func, 0x0A);

                if (c == class_id && sc == subclass_id) {
                    if (dev) pci_fill_device_info((uint8_t)bus, slot, func, dev);
                    return true;
                }
            }
        }
    }
    return false;
}

bool pci_find_class_prog_if(uint8_t class_id, uint8_t subclass_id, uint8_t prog_if, pci_device_t* dev) {
    for (uint16_t bus = 0; bus < 8; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint16_t v = pci_read16((uint8_t)bus, slot, 0, 0x00);
            if (v == 0xFFFF) continue;

            uint8_t header_type = pci_read8((uint8_t)bus, slot, 0, 0x0E);
            uint8_t max_func = (header_type & 0x80) ? 8 : 1;

            for (uint8_t func = 0; func < max_func; func++) {
                uint16_t cur_vendor = pci_read16((uint8_t)bus, slot, func, 0x00);
                if (cur_vendor == 0xFFFF) continue;

                uint8_t c = pci_read8((uint8_t)bus, slot, func, 0x0B);
                uint8_t sc = pci_read8((uint8_t)bus, slot, func, 0x0A);
                uint8_t pi = pci_read8((uint8_t)bus, slot, func, 0x09);

                if (c == class_id && sc == subclass_id && pi == prog_if) {
                    if (dev) pci_fill_device_info((uint8_t)bus, slot, func, dev);
                    return true;
                }
            }
        }
    }
    return false;
}

