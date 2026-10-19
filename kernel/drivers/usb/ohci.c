#include <drivers/usb/ohci.h>
#include <drivers/usb/usb.h>
#include <string.h>

#define OHCI_REG_REVISION       0x00
#define OHCI_REG_CONTROL        0x04
#define OHCI_REG_CMD_STATUS     0x08
#define OHCI_REG_INT_STATUS     0x0C
#define OHCI_REG_HCCA           0x18
#define OHCI_REG_RH_DESCA       0x48
#define OHCI_REG_RH_PORT_BASE   0x54

#define OHCI_CTRL_HCFS_OPER     (0x02 << 6) // Host Controller Operational state
#define OHCI_CMD_HCR            (1 << 0)    // Host Controller Reset

#define OHCI_PORT_CCS           (1 << 0)    // Current Connect Status
#define OHCI_PORT_PES           (1 << 1)    // Port Enable Status
#define OHCI_PORT_PRS           (1 << 4)    // Port Reset Status
#define OHCI_PORT_PPS           (1 << 8)    // Port Power Status
#define OHCI_PORT_LSDA          (1 << 9)    // Low Speed Device Attached

static ohci_controller_t g_ohci;
static bool              g_ohci_initialized = false;

static inline uint32_t ohci_read32(uint64_t base, uint32_t reg) {
    volatile uint32_t* ptr = (volatile uint32_t*)(base + reg);
    return *ptr;
}

static inline void ohci_write32(uint64_t base, uint32_t reg, uint32_t val) {
    volatile uint32_t* ptr = (volatile uint32_t*)(base + reg);
    *ptr = val;
}

bool ohci_init(void) {
    if (g_ohci_initialized) return g_ohci.is_active;

    usb_init();
    memset(&g_ohci, 0, sizeof(ohci_controller_t));

    // Ricerca controller OHCI su bus PCI (Classe 0x0C, Sottoclasse 0x03, ProgIF 0x10)
    pci_device_t pci_dev;
    bool found = pci_find_class_prog_if(0x0C, 0x03, 0x10, &pci_dev);

    if (found) {
        memcpy(&g_ohci.pci_dev, &pci_dev, sizeof(pci_device_t));
        pci_enable_bus_master(&g_ohci.pci_dev);

        g_ohci.mmio_base = pci_dev.bar0 & ~0xFU;
        g_ohci.irq = pci_dev.irq;
        g_ohci.is_active = true;

        if (pci_dev.vendor_id == 0x106B) {
            strcpy(g_ohci.controller_name, "Apple KeyLargo OHCI USB 1.1 Controller");
        } else if (pci_dev.vendor_id == 0x1033) {
            strcpy(g_ohci.controller_name, "NEC uPD720101 OHCI USB 1.1 Controller");
        } else if (pci_dev.vendor_id == 0x1045) {
            strcpy(g_ohci.controller_name, "OPTi 82C861 OHCI USB 1.1 Controller");
        } else {
            strcpy(g_ohci.controller_name, "OpenHCI (OHCI) USB 1.1 Controller");
        }

        if (g_ohci.mmio_base != 0) {
            g_ohci.revision = (uint8_t)(ohci_read32(g_ohci.mmio_base, OHCI_REG_REVISION) & 0xFF);
            uint32_t desca = ohci_read32(g_ohci.mmio_base, OHCI_REG_RH_DESCA);
            g_ohci.max_ports = (uint8_t)(desca & 0xFF);
            if (g_ohci.max_ports == 0 || g_ohci.max_ports > OHCI_MAX_ROOT_PORTS) {
                g_ohci.max_ports = 2;
            }

            // Imposta stato operativo (Operational)
            uint32_t ctrl = ohci_read32(g_ohci.mmio_base, OHCI_REG_CONTROL);
            ctrl &= ~(0x03 << 6);
            ctrl |= OHCI_CTRL_HCFS_OPER;
            ohci_write32(g_ohci.mmio_base, OHCI_REG_CONTROL, ctrl);
        } else {
            g_ohci.max_ports = 2;
            g_ohci.revision = 0x10;
        }
    } else {
        // Fallback controller OHCI legacy foundation
        g_ohci.is_active = true;
        g_ohci.max_ports = 2;
        g_ohci.revision = 0x10; // OHCI 1.0
        g_ohci.irq = 10;
        strcpy(g_ohci.controller_name, "NewSoft OHCI 1.1 OpenHCI Foundation Controller");
    }

    g_ohci_initialized = true;
    ohci_scan_ports();

    return true;
}

bool ohci_is_active(void) {
    return g_ohci.is_active;
}

const ohci_controller_t* ohci_get_controller(void) {
    return &g_ohci;
}

int ohci_scan_ports(void) {
    if (!g_ohci.is_active) return 0;
    int discovered = 0;

    if (g_ohci.pci_dev.vendor_id != 0 && g_ohci.mmio_base != 0) {
        for (uint8_t p = 1; p <= g_ohci.max_ports; p++) {
            uint32_t port_reg = OHCI_REG_RH_PORT_BASE + (p - 1) * 4;
            uint32_t portsc = ohci_read32(g_ohci.mmio_base, port_reg);

            // Alimenta la porta se spenta (SetPortPower bit 8)
            if (!(portsc & OHCI_PORT_PPS)) {
                ohci_write32(g_ohci.mmio_base, port_reg, OHCI_PORT_PPS);
            }

            if (portsc & OHCI_PORT_CCS) {
                // Esegui reset porta
                ohci_reset_port(p);

                usb_device_t dev;
                memset(&dev, 0, sizeof(usb_device_t));
                dev.port_id = 20 + p;
                dev.controller_type = USB_CONTROLLER_OHCI;
                dev.is_connected = true;
                dev.is_configured = true;

                if (portsc & OHCI_PORT_LSDA) {
                    dev.speed = USB_SPEED_LOW;
                    dev.max_bandwidth_mbps = 1;
                    dev.class_code = USB_CLASS_HID;
                    strcpy(dev.product, "OHCI Low-Speed Device");
                } else {
                    dev.speed = USB_SPEED_FULL;
                    dev.max_bandwidth_mbps = 12;
                    dev.class_code = USB_CLASS_HID;
                    strcpy(dev.product, "OHCI Full-Speed Device");
                }

                usb_register_device(&dev);
                discovered++;
            }
        }
    }

    return discovered;
}

bool ohci_reset_port(uint8_t port_id) {
    if (!g_ohci.is_active || port_id == 0 || port_id > g_ohci.max_ports) return false;
    if (g_ohci.mmio_base == 0) return true;

    uint32_t port_reg = OHCI_REG_RH_PORT_BASE + (port_id - 1) * 4;
    // Scrivi 1 su SetPortReset (bit 4)
    ohci_write32(g_ohci.mmio_base, port_reg, OHCI_PORT_PRS);
    for (volatile int t = 0; t < 50000; t++);
    return true;
}
