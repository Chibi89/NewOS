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

#define PCI_MAX_DEVICES 128

static pci_device_t g_pci_devs[PCI_MAX_DEVICES];
static int          g_pci_dev_count = 0;
static bool         g_pci_scanned = false;

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

void pci_scan_all(void) {
    g_pci_dev_count = 0;
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint16_t v = pci_read16((uint8_t)bus, slot, 0, 0x00);
            if (v == 0xFFFF) continue;

            uint8_t header_type = pci_read8((uint8_t)bus, slot, 0, 0x0E);
            uint8_t max_func = (header_type & 0x80) ? 8 : 1;

            for (uint8_t func = 0; func < max_func; func++) {
                uint16_t cur_vendor = pci_read16((uint8_t)bus, slot, func, 0x00);
                if (cur_vendor == 0xFFFF) continue;
                if (g_pci_dev_count < PCI_MAX_DEVICES) {
                    pci_fill_device_info((uint8_t)bus, slot, func, &g_pci_devs[g_pci_dev_count++]);
                }
            }
        }
    }
    g_pci_scanned = true;
}

static void pci_ensure_scanned(void) {
    if (!g_pci_scanned) pci_scan_all();
}

int pci_get_device_count(void) {
    pci_ensure_scanned();
    return g_pci_dev_count;
}

const pci_device_t* pci_get_device(int index) {
    pci_ensure_scanned();
    if (index < 0 || index >= g_pci_dev_count) return NULL;
    return &g_pci_devs[index];
}

uint64_t pci_get_bar_addr(const pci_device_t* dev, int bar_idx) {
    if (!dev || bar_idx < 0 || bar_idx > 5) return 0;
    const uint32_t bars[6] = { dev->bar0, dev->bar1, dev->bar2, dev->bar3, dev->bar4, dev->bar5 };
    uint32_t lo = bars[bar_idx];
    if (lo & 0x1U) {
        return (uint64_t)(lo & ~0x3U);
    }
    uint32_t type = (lo >> 1) & 0x3U;
    uint64_t addr = (uint64_t)(lo & ~0xFU);
    if (type == 0x2U && bar_idx < 5) {
        addr |= ((uint64_t)bars[bar_idx + 1]) << 32;
    }
    return addr;
}

bool pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t* dev) {
    pci_ensure_scanned();
    for (int i = 0; i < g_pci_dev_count; i++) {
        if (g_pci_devs[i].vendor_id == vendor_id && g_pci_devs[i].device_id == device_id) {
            if (dev) *dev = g_pci_devs[i];
            return true;
        }
    }
    return false;
}

bool pci_find_class(uint8_t class_id, uint8_t subclass_id, pci_device_t* dev) {
    pci_ensure_scanned();
    for (int i = 0; i < g_pci_dev_count; i++) {
        if (g_pci_devs[i].class_id == class_id && g_pci_devs[i].subclass_id == subclass_id) {
            if (dev) *dev = g_pci_devs[i];
            return true;
        }
    }
    return false;
}

bool pci_find_class_prog_if(uint8_t class_id, uint8_t subclass_id, uint8_t prog_if, pci_device_t* dev) {
    pci_ensure_scanned();
    for (int i = 0; i < g_pci_dev_count; i++) {
        if (g_pci_devs[i].class_id == class_id &&
            g_pci_devs[i].subclass_id == subclass_id &&
            g_pci_devs[i].prog_if == prog_if) {
            if (dev) *dev = g_pci_devs[i];
            return true;
        }
    }
    return false;
}

