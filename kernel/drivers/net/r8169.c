#include <drivers/net/r8169.h>
#include <drivers/timer.h>
#include <mm/paging.h>
#include <string.h>

#define R8169_REG_IDR0     0x00
#define R8169_REG_TNPDS    0x20
#define R8169_REG_CMD      0x37
#define R8169_REG_TPPOLL   0x38
#define R8169_REG_IMR      0x3C
#define R8169_REG_ISR      0x3E
#define R8169_REG_TCR      0x40
#define R8169_REG_RCR      0x44
#define R8169_REG_9346CR   0x50
#define R8169_REG_PHYAR    0x60
#define R8169_REG_PHYSTS   0x6C
#define R8169_REG_RMS      0xDA
#define R8169_REG_CPCR     0xE0
#define R8169_REG_RDSAR    0xE4
#define R8169_REG_MTPS     0xEC

#define R8169_NUM_RX       32
#define R8169_NUM_TX       16
#define R8169_BUF_SZ       2048

#define R8169_DESC_OWN     (1U << 31)
#define R8169_DESC_EOR     (1U << 30)
#define R8169_DESC_FS      (1U << 29)
#define R8169_DESC_LS      (1U << 28)

typedef struct {
    uint32_t opts1;
    uint32_t opts2;
    uint64_t addr;
} __attribute__((packed)) r8169_desc_t;

static volatile uint8_t* g_r8169_mmio = NULL;
static net_nic_t*        g_r8169_nic = NULL;
static r8169_desc_t*     g_r8169_rx_ring = NULL;
static r8169_desc_t*     g_r8169_tx_ring = NULL;
static uint8_t*          g_r8169_rx_bufs = NULL;
static uint8_t*          g_r8169_tx_bufs = NULL;
static uint64_t          g_r8169_rx_bufs_phys = 0;
static uint64_t          g_r8169_tx_bufs_phys = 0;
static uint32_t          g_r8169_rx_cur = 0;
static uint32_t          g_r8169_tx_cur = 0;

static inline uint8_t r8169_r8(uint32_t o) { return *(volatile uint8_t*)(g_r8169_mmio + o); }
static inline uint16_t r8169_r16(uint32_t o) { return *(volatile uint16_t*)(g_r8169_mmio + o); }
static inline uint32_t r8169_r32(uint32_t o) { return *(volatile uint32_t*)(g_r8169_mmio + o); }
static inline void r8169_w8(uint32_t o, uint8_t v) { *(volatile uint8_t*)(g_r8169_mmio + o) = v; }
static inline void r8169_w16(uint32_t o, uint16_t v) { *(volatile uint16_t*)(g_r8169_mmio + o) = v; }
static inline void r8169_w32(uint32_t o, uint32_t v) { *(volatile uint32_t*)(g_r8169_mmio + o) = v; }

bool r8169_init(pci_device_t* dev, net_nic_t* nic) {
    if (!dev || !nic) return false;
    pci_enable_bus_master(dev);

    uint64_t bar = pci_get_bar_addr(dev, 2);
    if (!bar || (dev->bar2 & 1U)) bar = pci_get_bar_addr(dev, 0);
    if (!bar) return false;

    g_r8169_mmio = (volatile uint8_t*)paging_map_mmio(bar, 0x1000);
    if (!g_r8169_mmio) return false;

    g_r8169_nic = nic;
    nic->type = NIC_TYPE_R8169;
    strncpy(nic->name, "Realtek RTL8111/8168 Gigabit Ethernet", sizeof(nic->name) - 1);
    nic->irq = dev->irq;
    nic->mmio_base = bar;

    // 1. Reset hardware
    r8169_w8(R8169_REG_CMD, 0x10);
    for (int t = 0; t < 100; t++) {
        if ((r8169_r8(R8169_REG_CMD) & 0x10) == 0) break;
        timer_sleep(1);
    }

    // 2. Lettura MAC Address
    for (int i = 0; i < 6; i++) {
        nic->mac[i] = r8169_r8(R8169_REG_IDR0 + (uint32_t)i);
    }

    // 3. Allocazione ring RX/TX 256-byte aligned + buffer DMA
    uint64_t rx_ring_phys = 0, tx_ring_phys = 0;
    g_r8169_rx_ring = (r8169_desc_t*)dma_alloc(R8169_NUM_RX * sizeof(r8169_desc_t), 256, &rx_ring_phys);
    g_r8169_tx_ring = (r8169_desc_t*)dma_alloc(R8169_NUM_TX * sizeof(r8169_desc_t), 256, &tx_ring_phys);
    g_r8169_rx_bufs = (uint8_t*)dma_alloc(R8169_NUM_RX * R8169_BUF_SZ, 64, &g_r8169_rx_bufs_phys);
    g_r8169_tx_bufs = (uint8_t*)dma_alloc(R8169_NUM_TX * R8169_BUF_SZ, 64, &g_r8169_tx_bufs_phys);
    if (!g_r8169_rx_ring || !g_r8169_tx_ring || !g_r8169_rx_bufs || !g_r8169_tx_bufs) return false;

    for (uint32_t i = 0; i < R8169_NUM_RX; i++) {
        uint32_t eor = (i == R8169_NUM_RX - 1) ? R8169_DESC_EOR : 0;
        g_r8169_rx_ring[i].opts1 = R8169_DESC_OWN | eor | (R8169_BUF_SZ & 0x3FFFU);
        g_r8169_rx_ring[i].opts2 = 0;
        g_r8169_rx_ring[i].addr = g_r8169_rx_bufs_phys + (uint64_t)i * R8169_BUF_SZ;
    }
    for (uint32_t i = 0; i < R8169_NUM_TX; i++) {
        g_r8169_tx_ring[i].opts1 = (i == R8169_NUM_TX - 1) ? R8169_DESC_EOR : 0;
        g_r8169_tx_ring[i].opts2 = 0;
        g_r8169_tx_ring[i].addr = g_r8169_tx_bufs_phys + (uint64_t)i * R8169_BUF_SZ;
    }
    g_r8169_rx_cur = 0;
    g_r8169_tx_cur = 0;

    // 4. Sblocco registri config + C+ mode + DMA rings
    r8169_w8(R8169_REG_9346CR, 0xC0);
    r8169_w16(R8169_REG_IMR, 0x0000); // Polling mode
    r8169_w16(R8169_REG_ISR, 0xFFFF);
    r8169_w16(R8169_REG_RMS, 0x05F3); // Max RX 1523 byte
    r8169_w8(R8169_REG_MTPS, 0x3B);   // Max TX packet size
    r8169_w16(R8169_REG_CPCR, 0x0000);

    r8169_w32(R8169_REG_TNPDS, (uint32_t)(tx_ring_phys & 0xFFFFFFFFULL));
    r8169_w32(R8169_REG_TNPDS + 4, (uint32_t)(tx_ring_phys >> 32));
    r8169_w32(R8169_REG_RDSAR, (uint32_t)(rx_ring_phys & 0xFFFFFFFFULL));
    r8169_w32(R8169_REG_RDSAR + 4, (uint32_t)(rx_ring_phys >> 32));

    r8169_w8(R8169_REG_CMD, 0x0C); // Enable RE + TE
    r8169_w32(R8169_REG_TCR, (0x03U << 24) | (0x07U << 8));
    r8169_w32(R8169_REG_RCR, (0x07U << 13) | (0x07U << 8) | 0x0EU); // Accept Broadcast/Multicast/PhysMatch
    r8169_w8(R8169_REG_9346CR, 0x00);

    nic->link_up = (r8169_r8(R8169_REG_PHYSTS) & 0x02U) != 0;
    return true;
}

bool r8169_send(const void* data, uint16_t len) {
    if (!g_r8169_mmio || !data || len == 0 || len > 1514) return false;
    uint32_t idx = g_r8169_tx_cur;
    for (int t = 0; t < 50000; t++) {
        if ((g_r8169_tx_ring[idx].opts1 & R8169_DESC_OWN) == 0) break;
    }
    if (g_r8169_tx_ring[idx].opts1 & R8169_DESC_OWN) return false;

    uint16_t tx_len = (len < 60) ? 60 : len;
    uint8_t* dst = g_r8169_tx_bufs + idx * R8169_BUF_SZ;
    memset(dst, 0, tx_len);
    memcpy(dst, data, len);

    uint32_t eor = (idx == R8169_NUM_TX - 1) ? R8169_DESC_EOR : 0;
    g_r8169_tx_ring[idx].opts2 = 0;
    g_r8169_tx_ring[idx].opts1 = R8169_DESC_OWN | eor | R8169_DESC_FS | R8169_DESC_LS | (tx_len & 0x3FFFU);
    r8169_w8(R8169_REG_TPPOLL, 0x40); // Normal Priority Queue Poll

    g_r8169_tx_cur = (idx + 1) % R8169_NUM_TX;
    if (g_r8169_nic) {
        g_r8169_nic->tx_packets++;
        g_r8169_nic->tx_bytes += tx_len;
    }
    return true;
}

void r8169_poll(net_rx_handler_t rx_handler) {
    if (!g_r8169_mmio || !rx_handler) return;
    for (int limit = 0; limit < 16; limit++) {
        uint32_t idx = g_r8169_rx_cur;
        uint32_t opts1 = g_r8169_rx_ring[idx].opts1;
        if (opts1 & R8169_DESC_OWN) break;

        uint16_t pkt_len = (uint16_t)(opts1 & 0x3FFFU);
        if (pkt_len >= 4) pkt_len -= 4; // Rimuove CRC
        if (pkt_len > 0 && pkt_len <= R8169_BUF_SZ) {
            if (g_r8169_nic) {
                g_r8169_nic->rx_packets++;
                g_r8169_nic->rx_bytes += pkt_len;
            }
            rx_handler(g_r8169_rx_bufs + idx * R8169_BUF_SZ, pkt_len);
        }

        uint32_t eor = (idx == R8169_NUM_RX - 1) ? R8169_DESC_EOR : 0;
        g_r8169_rx_ring[idx].opts1 = R8169_DESC_OWN | eor | (R8169_BUF_SZ & 0x3FFFU);
        g_r8169_rx_cur = (idx + 1) % R8169_NUM_RX;
    }
}
