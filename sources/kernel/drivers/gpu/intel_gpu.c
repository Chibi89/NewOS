#include <drivers/pci.h>
#include <mm/paging.h>
#include <string.h>

#define INTEL_BCS_RING_TAIL  0x22030
#define INTEL_BCS_RING_HEAD  0x22034
#define INTEL_BCS_RING_START 0x22038
#define INTEL_BCS_RING_CTL   0x2203C

static volatile uint8_t* g_intel_mmio = NULL;
static bool              g_intel_ready = false;
static uint16_t          g_intel_dev_id = 0;

bool intel_gpu_init(uint16_t* out_dev_id) {
    if (g_intel_ready) {
        if (out_dev_id) *out_dev_id = g_intel_dev_id;
        return true;
    }

    pci_device_t dev;
    if (!pci_find_class(0x03, 0x00, &dev) || dev.vendor_id != 0x8086) {
        return false;
    }

    pci_enable_bus_master(&dev);
    uint64_t gttmmadr = pci_get_bar_addr(&dev, 0);
    if (!gttmmadr) return false;

    g_intel_mmio = (volatile uint8_t*)paging_map_mmio(gttmmadr, 0x40000);
    if (!g_intel_mmio) return false;

    g_intel_dev_id = dev.device_id;
    if (out_dev_id) *out_dev_id = g_intel_dev_id;

    // Verifica accessibilita' Blitter Command Streamer (BCS_RING_CTL)
    uint32_t bcs_ctl = *(volatile uint32_t*)(g_intel_mmio + INTEL_BCS_RING_CTL);
    (void)bcs_ctl;

    g_intel_ready = true;
    return true;
}
