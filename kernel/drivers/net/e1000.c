#include <drivers/net/e1000.h>
#include <mm/heap.h>
#include <string.h>
#include <arch/x86_64/io.h>

#define E1000_REG_CTRL      0x0000
#define E1000_REG_STATUS    0x0008
#define E1000_REG_EECD      0x0010
#define E1000_REG_EERD      0x0014
#define E1000_REG_ICR       0x00C0
#define E1000_REG_IMS       0x00D0
#define E1000_REG_IMC       0x00D8
#define E1000_REG_RCTL      0x0100
#define E1000_REG_TCTL      0x0400
#define E1000_REG_TIPG      0x0410
#define E1000_REG_RDBAL     0x2800
#define E1000_REG_RDBAH     0x2804
#define E1000_REG_RDLEN     0x2808
#define E1000_REG_RDH       0x2810
#define E1000_REG_RDT       0x2818
#define E1000_REG_TDBAL     0x3800
#define E1000_REG_TDBAH     0x3804
#define E1000_REG_TDLEN     0x3808
#define E1000_REG_TDH       0x3810
#define E1000_REG_TDT       0x3818
#define E1000_REG_MTA       0x5200
#define E1000_REG_RAL       0x5400
#define E1000_REG_RAH       0x5404

#define E1000_RCTL_EN       (1U << 1)   // Receiver Enable
#define E1000_RCTL_SBP      (1U << 2)   // Store Bad Packets
#define E1000_RCTL_UPE      (1U << 3)   // Unicast Promiscuous Enable
#define E1000_RCTL_MPE      (1U << 4)   // Multicast Promiscuous Enable
#define E1000_RCTL_LPE      (1U << 5)   // Long Packet Enable
#define E1000_RCTL_BAM      (1U << 15)  // Broadcast Accept Mode
#define E1000_RCTL_BSIZE_2K (0U << 16)  // Buffer Size 2048
#define E1000_RCTL_SECRC    (1U << 26)  // Strip Ethernet CRC

#define E1000_TCTL_EN       (1U << 1)   // Transmit Enable
#define E1000_TCTL_PSP      (1U << 3)   // Pad Short Packets
#define E1000_TCTL_CT_SHIFT 4           // Collision Threshold (15)
#define E1000_TCTL_COLD_SHF 12          // Collision Distance (64)

#define NUM_RX_DESCRIPTORS  32
#define NUM_TX_DESCRIPTORS  16
#define RX_BUFFER_SIZE      2048

typedef struct __attribute__((packed)) {
    uint64_t buffer_addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} e1000_rx_desc_t;

typedef struct __attribute__((packed)) {
    uint64_t buffer_addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} e1000_tx_desc_t;

static uint64_t g_mmio_base = 0;

static __attribute__((aligned(128))) e1000_rx_desc_t g_rx_descs[NUM_RX_DESCRIPTORS];
static __attribute__((aligned(128))) uint8_t         g_rx_buffers[NUM_RX_DESCRIPTORS][RX_BUFFER_SIZE];
static uint16_t                                      g_rx_cur = 0;

static __attribute__((aligned(128))) e1000_tx_desc_t g_tx_descs[NUM_TX_DESCRIPTORS];
static __attribute__((aligned(128))) uint8_t         g_tx_buffers[NUM_TX_DESCRIPTORS][RX_BUFFER_SIZE];
static uint16_t                                      g_tx_cur = 0;
static net_nic_t*                                    g_nic_ref = NULL;

static void serial_log(const char* s) {
    if (!s) return;
    while (*s) outb(0x3F8, *s++);
}

static inline void e1000_write32(uint32_t reg, uint32_t val) {
    if (!g_mmio_base) return;
    *(volatile uint32_t*)(g_mmio_base + reg) = val;
}

static inline uint32_t e1000_read32(uint32_t reg) {
    if (!g_mmio_base) return 0;
    return *(volatile uint32_t*)(g_mmio_base + reg);
}

static uint16_t e1000_read_eeprom(uint8_t addr) {
    e1000_write32(E1000_REG_EERD, (1U) | ((uint32_t)addr << 8));
    uint32_t tmp = 0;
    for (int i = 0; i < 10000; i++) {
        tmp = e1000_read32(E1000_REG_EERD);
        if (tmp & (1U << 4)) break;
    }
    return (uint16_t)((tmp >> 16) & 0xFFFF);
}

bool e1000_init(pci_device_t* dev, net_nic_t* nic) {
    if (!dev || !nic) return false;

    // BAR0 contiene l'indirizzo MMIO (Memory Mapped I/O)
    uint32_t bar0 = dev->bar0 & ~0x0F;
    if (!bar0) return false;

    pci_enable_bus_master(dev);

    g_mmio_base = (uint64_t)bar0;
    g_nic_ref = nic;
    nic->type = NIC_TYPE_E1000;
    strncpy(nic->name, "Intel PRO/1000 MT (82540EM) Gigabit", sizeof(nic->name) - 1);
    nic->irq = dev->irq;
    nic->mmio_base = g_mmio_base;
    nic->io_base = (dev->bar1 & 1) ? (dev->bar1 & ~3) : 0;

    // Disabilita interrupt durante il setup
    e1000_write32(E1000_REG_IMC, 0xFFFFFFFF);

    // Reset software del controller
    uint32_t ctrl = e1000_read32(E1000_REG_CTRL);
    e1000_write32(E1000_REG_CTRL, ctrl | (1U << 26)); // Device Reset
    for (volatile int i = 0; i < 50000; i++);

    e1000_write32(E1000_REG_IMC, 0xFFFFFFFF);

    // Lettura MAC Address: prima prova dai registri RAL/RAH
    uint32_t ral = e1000_read32(E1000_REG_RAL);
    uint32_t rah = e1000_read32(E1000_REG_RAH);

    if (ral != 0 && (rah & 0xFFFF) != 0) {
        nic->mac[0] = (uint8_t)(ral & 0xFF);
        nic->mac[1] = (uint8_t)((ral >> 8) & 0xFF);
        nic->mac[2] = (uint8_t)((ral >> 16) & 0xFF);
        nic->mac[3] = (uint8_t)((ral >> 24) & 0xFF);
        nic->mac[4] = (uint8_t)(rah & 0xFF);
        nic->mac[5] = (uint8_t)((rah >> 8) & 0xFF);
    } else {
        // Fallback su lettura EEPROM
        uint16_t w0 = e1000_read_eeprom(0);
        uint16_t w1 = e1000_read_eeprom(1);
        uint16_t w2 = e1000_read_eeprom(2);
        nic->mac[0] = (uint8_t)(w0 & 0xFF);
        nic->mac[1] = (uint8_t)((w0 >> 8) & 0xFF);
        nic->mac[2] = (uint8_t)(w1 & 0xFF);
        nic->mac[3] = (uint8_t)((w1 >> 8) & 0xFF);
        nic->mac[4] = (uint8_t)(w2 & 0xFF);
        nic->mac[5] = (uint8_t)((w2 >> 8) & 0xFF);
    }

    // Se per qualche motivo il MAC e' vuoto, assegna un MAC predefinito valido
    if (nic->mac[0] == 0 && nic->mac[1] == 0 && nic->mac[2] == 0 &&
        nic->mac[3] == 0 && nic->mac[4] == 0 && nic->mac[5] == 0) {
        nic->mac[0] = 0x52; nic->mac[1] = 0x54; nic->mac[2] = 0x00;
        nic->mac[3] = 0x12; nic->mac[4] = 0x34; nic->mac[5] = 0x56;
    }

    // Reset tabella multicast
    for (int i = 0; i < 128; i++) {
        e1000_write32(E1000_REG_MTA + i * 4, 0);
    }

    // 1. Configurazione Receive Descriptors Ring (allineato rigorosamente a 128 byte)
    memset(g_rx_descs, 0, sizeof(g_rx_descs));
    for (int i = 0; i < NUM_RX_DESCRIPTORS; i++) {
        memset(g_rx_buffers[i], 0, RX_BUFFER_SIZE);
        g_rx_descs[i].buffer_addr = (uint64_t)g_rx_buffers[i];
        g_rx_descs[i].status = 0;
    }

    uint64_t rx_ring_phys = (uint64_t)g_rx_descs;
    e1000_write32(E1000_REG_RDBAL, (uint32_t)(rx_ring_phys & 0xFFFFFFFF));
    e1000_write32(E1000_REG_RDBAH, (uint32_t)(rx_ring_phys >> 32));
    e1000_write32(E1000_REG_RDLEN, NUM_RX_DESCRIPTORS * sizeof(e1000_rx_desc_t));
    e1000_write32(E1000_REG_RDH, 0);
    e1000_write32(E1000_REG_RDT, NUM_RX_DESCRIPTORS - 1);
    g_rx_cur = 0;

    // 2. Configurazione Transmit Descriptors Ring (allineato rigorosamente a 128 byte)
    memset(g_tx_descs, 0, sizeof(g_tx_descs));
    for (int i = 0; i < NUM_TX_DESCRIPTORS; i++) {
        memset(g_tx_buffers[i], 0, RX_BUFFER_SIZE);
        g_tx_descs[i].buffer_addr = (uint64_t)g_tx_buffers[i];
        g_tx_descs[i].status = 1; // Descriptor Done iniziale
    }

    uint64_t tx_ring_phys = (uint64_t)g_tx_descs;
    e1000_write32(E1000_REG_TDBAL, (uint32_t)(tx_ring_phys & 0xFFFFFFFF));
    e1000_write32(E1000_REG_TDBAH, (uint32_t)(tx_ring_phys >> 32));
    e1000_write32(E1000_REG_TDLEN, NUM_TX_DESCRIPTORS * sizeof(e1000_tx_desc_t));
    e1000_write32(E1000_REG_TDH, 0);
    e1000_write32(E1000_REG_TDT, 0);
    g_tx_cur = 0;

    // 3. Avvio Receiver e Transmitter
    e1000_write32(E1000_REG_RCTL, E1000_RCTL_EN | E1000_RCTL_SBP | E1000_RCTL_UPE |
                                  E1000_RCTL_MPE | E1000_RCTL_BAM | E1000_RCTL_SECRC);

    e1000_write32(E1000_REG_TIPG, 0x0060200A); // IPG standard
    e1000_write32(E1000_REG_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP |
                                  (15U << E1000_TCTL_CT_SHIFT) |
                                  (64U << E1000_TCTL_COLD_SHF));

    // La ricezione e trasmissione di rete opera in modalita' net_poll() (polling cooperativo)
    // Disabilita gli interrupt hardware E1000 per evitare collisioni sulla linea IRQ PCI condivisa
    e1000_write32(E1000_REG_IMC, 0xFFFFFFFF);

    // Link status
    uint32_t status = e1000_read32(E1000_REG_STATUS);
    nic->link_up = (status & (1U << 1)) != 0;

    return true;
}

bool e1000_send(const void* data, uint16_t len) {
    if (!g_mmio_base || len == 0 || len > RX_BUFFER_SIZE) return false;

    uint16_t cur = g_tx_cur;
    e1000_tx_desc_t* desc = &g_tx_descs[cur];

    // Attendi se il descrittore precedente non e' ancora completato
    for (int timeout = 0; timeout < 50000; timeout++) {
        if (desc->status & 1) break;
    }

    memcpy(g_tx_buffers[cur], data, len);
    desc->length = len;
    desc->cso = 0;
    desc->cmd = (1U << 0) | (1U << 1) | (1U << 3); // EOP | IFCS | RS (Report Status)
    desc->status = 0;
    desc->css = 0;
    desc->special = 0;

    g_tx_cur = (g_tx_cur + 1) % NUM_TX_DESCRIPTORS;
    e1000_write32(E1000_REG_TDT, g_tx_cur);

    if (g_nic_ref) {
        g_nic_ref->tx_packets++;
        g_nic_ref->tx_bytes += len;
    }
    return true;
}

void e1000_poll(net_rx_handler_t rx_handler) {
    if (!g_mmio_base || !rx_handler) return;

    // Legge ICR per confermare eventuali interrupt
    e1000_read32(E1000_REG_ICR);

    while (g_rx_descs[g_rx_cur].status & 1) { // DD (Descriptor Done)
        e1000_rx_desc_t* desc = &g_rx_descs[g_rx_cur];
        uint16_t len = desc->length;

        if (len > 0 && len <= RX_BUFFER_SIZE) {
            rx_handler(g_rx_buffers[g_rx_cur], len);
            if (g_nic_ref) {
                g_nic_ref->rx_packets++;
                g_nic_ref->rx_bytes += len;
            }
        }

        desc->status = 0;
        uint16_t old_cur = g_rx_cur;
        g_rx_cur = (g_rx_cur + 1) % NUM_RX_DESCRIPTORS;
        e1000_write32(E1000_REG_RDT, old_cur);
    }
}
