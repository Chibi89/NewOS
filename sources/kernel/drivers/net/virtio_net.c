#include <drivers/net/virtio_net.h>
#include <arch/x86_64/io.h>
#include <mm/paging.h>
#include <string.h>

#define VIRTIO_REG_DEV_FEATURES 0x00
#define VIRTIO_REG_DRV_FEATURES 0x04
#define VIRTIO_REG_QUEUE_PFN    0x08
#define VIRTIO_REG_QUEUE_SIZE   0x0C
#define VIRTIO_REG_QUEUE_SEL    0x0E
#define VIRTIO_REG_QUEUE_NOTIFY 0x10
#define VIRTIO_REG_DEV_STATUS   0x12
#define VIRTIO_REG_ISR_STATUS   0x13
#define VIRTIO_REG_NET_MAC      0x14

#define VQ_DESC_F_NEXT          1
#define VQ_DESC_F_WRITE         2

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed)) vq_desc_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed)) vq_avail_t;

typedef struct {
    uint32_t id;
    uint32_t len;
} __attribute__((packed)) vq_used_elem_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    vq_used_elem_t ring[];
} __attribute__((packed)) vq_used_t;

typedef struct {
    uint8_t  flags;
    uint8_t  gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} __attribute__((packed)) virtio_net_hdr_t;

typedef struct {
    uint16_t    qsz;
    vq_desc_t*  desc;
    vq_avail_t* avail;
    vq_used_t*  used;
    uint16_t    last_used;
    uint8_t*    bufs;
    uint64_t    bufs_phys;
} virtq_t;

static uint16_t   g_vio_iobase = 0;
static net_nic_t* g_vio_nic = NULL;
static virtq_t    g_vq_rx;
static virtq_t    g_vq_tx;
static uint16_t   g_vio_tx_cur = 0;

static bool virtio_setup_vq(uint16_t qidx, virtq_t* vq, bool is_rx) {
    outw(g_vio_iobase + VIRTIO_REG_QUEUE_SEL, qidx);
    uint16_t qsz = inw(g_vio_iobase + VIRTIO_REG_QUEUE_SIZE);
    if (qsz == 0) return false;
    if (qsz > 64) qsz = 64;
    vq->qsz = qsz;

    uint32_t ring_desc_avail = qsz * 16 + 6 + 2 * qsz;
    uint32_t used_off = (ring_desc_avail + 4095U) & ~4095U;
    uint32_t total_sz = used_off + 4096U;

    uint64_t qphys = 0;
    uint8_t* qmem = (uint8_t*)dma_alloc(total_sz, 4096, &qphys);
    if (!qmem) return false;

    vq->desc = (vq_desc_t*)qmem;
    vq->avail = (vq_avail_t*)(qmem + qsz * 16);
    vq->used = (vq_used_t*)(qmem + used_off);
    vq->last_used = 0;

    vq->bufs = (uint8_t*)dma_alloc(qsz * 2048U, 64, &vq->bufs_phys);
    if (!vq->bufs) return false;

    if (is_rx) {
        for (uint16_t i = 0; i < qsz; i++) {
            vq->desc[i].addr = vq->bufs_phys + (uint64_t)i * 2048U;
            vq->desc[i].len = 2048U;
            vq->desc[i].flags = VQ_DESC_F_WRITE;
            vq->desc[i].next = 0;
            vq->avail->ring[i] = i;
        }
        vq->avail->idx = qsz;
    }

    outl(g_vio_iobase + VIRTIO_REG_QUEUE_PFN, (uint32_t)(qphys >> 12));
    return true;
}

bool virtio_net_init(pci_device_t* dev, net_nic_t* nic) {
    if (!dev || !nic || !(dev->bar0 & 1U)) return false;
    pci_enable_bus_master(dev);

    g_vio_iobase = (uint16_t)(dev->bar0 & ~0x3U);
    g_vio_nic = nic;

    outb(g_vio_iobase + VIRTIO_REG_DEV_STATUS, 0x00); // Reset
    outb(g_vio_iobase + VIRTIO_REG_DEV_STATUS, 0x01 | 0x02); // ACKNOWLEDGE | DRIVER

    uint32_t feat = inl(g_vio_iobase + VIRTIO_REG_DEV_FEATURES);
    outl(g_vio_iobase + VIRTIO_REG_DRV_FEATURES, feat & (1U << 5)); // VIRTIO_NET_F_MAC

    if (!virtio_setup_vq(0, &g_vq_rx, true) || !virtio_setup_vq(1, &g_vq_tx, false)) {
        return false;
    }

    outb(g_vio_iobase + VIRTIO_REG_DEV_STATUS, 0x01 | 0x02 | 0x04); // DRIVER_OK
    outw(g_vio_iobase + VIRTIO_REG_QUEUE_NOTIFY, 0);

    nic->type = NIC_TYPE_VIRTIO;
    strncpy(nic->name, "VirtIO-Net Paravirtual Gigabit", sizeof(nic->name) - 1);
    nic->io_base = g_vio_iobase;
    nic->irq = dev->irq;
    for (int i = 0; i < 6; i++) {
        nic->mac[i] = inb(g_vio_iobase + VIRTIO_REG_NET_MAC + (uint16_t)i);
    }
    nic->link_up = true;
    return true;
}

bool virtio_net_send(const void* data, uint16_t len) {
    if (!g_vio_iobase || !data || len == 0 || len > 1514) return false;
    uint16_t idx = g_vio_tx_cur;
    uint8_t* buf = g_vq_tx.bufs + idx * 2048U;
    memset(buf, 0, sizeof(virtio_net_hdr_t));
    memcpy(buf + sizeof(virtio_net_hdr_t), data, len);

    g_vq_tx.desc[idx].addr = g_vq_tx.bufs_phys + (uint64_t)idx * 2048U;
    g_vq_tx.desc[idx].len = (uint32_t)sizeof(virtio_net_hdr_t) + len;
    g_vq_tx.desc[idx].flags = 0;
    g_vq_tx.desc[idx].next = 0;

    uint16_t aidx = g_vq_tx.avail->idx % g_vq_tx.qsz;
    g_vq_tx.avail->ring[aidx] = idx;
    __asm__ volatile("" ::: "memory");
    g_vq_tx.avail->idx++;
    outw(g_vio_iobase + VIRTIO_REG_QUEUE_NOTIFY, 1);

    g_vio_tx_cur = (idx + 1) % g_vq_tx.qsz;
    if (g_vio_nic) {
        g_vio_nic->tx_packets++;
        g_vio_nic->tx_bytes += len;
    }
    return true;
}

void virtio_net_poll(net_rx_handler_t rx_handler) {
    if (!g_vio_iobase || !rx_handler) return;
    while (g_vq_rx.last_used != g_vq_rx.used->idx) {
        uint16_t uidx = g_vq_rx.last_used % g_vq_rx.qsz;
        uint16_t d_id = (uint16_t)g_vq_rx.used->ring[uidx].id;
        uint32_t total = g_vq_rx.used->ring[uidx].len;
        if (total > sizeof(virtio_net_hdr_t)) {
            uint16_t pkt_len = (uint16_t)(total - sizeof(virtio_net_hdr_t));
            if (g_vio_nic) {
                g_vio_nic->rx_packets++;
                g_vio_nic->rx_bytes += pkt_len;
            }
            rx_handler(g_vq_rx.bufs + (uint64_t)d_id * 2048U + sizeof(virtio_net_hdr_t), pkt_len);
        }
        uint16_t aidx = g_vq_rx.avail->idx % g_vq_rx.qsz;
        g_vq_rx.avail->ring[aidx] = d_id;
        __asm__ volatile("" ::: "memory");
        g_vq_rx.avail->idx++;
        g_vq_rx.last_used++;
    }
    outw(g_vio_iobase + VIRTIO_REG_QUEUE_NOTIFY, 0);
}
