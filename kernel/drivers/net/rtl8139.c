#include <drivers/net/rtl8139.h>
#include <mm/heap.h>
#include <string.h>
#include <arch/x86_64/io.h>

#define RTL_REG_MAC0        0x00
#define RTL_REG_MAR0        0x08
#define RTL_REG_TSD0        0x10
#define RTL_REG_TSAD0       0x20
#define RTL_REG_RBSTART     0x30
#define RTL_REG_CR          0x37
#define RTL_REG_CAPR        0x38
#define RTL_REG_CBR         0x3A
#define RTL_REG_IMR         0x3C
#define RTL_REG_ISR         0x3E
#define RTL_REG_TCR         0x40
#define RTL_REG_RCR         0x44
#define RTL_REG_CONFIG1     0x52

#define RTL_RX_BUF_SIZE     (8192 + 16 + 1500)
#define RTL_TX_BUF_SIZE     2048

static uint16_t   g_io_base = 0;
static uint8_t*   g_rx_buffer = NULL;
static uint32_t   g_rx_offset = 0;

static uint8_t*   g_tx_buffers[4];
static uint8_t    g_tx_cur = 0;
static net_nic_t* g_nic_ref = NULL;

bool rtl8139_init(pci_device_t* dev, net_nic_t* nic) {
    if (!dev || !nic) return false;

    // BAR0 e' I/O Port
    uint32_t bar0 = dev->bar0 & ~3;
    if (!bar0) return false;

    pci_enable_bus_master(dev);

    g_io_base = (uint16_t)bar0;
    g_nic_ref = nic;
    nic->type = NIC_TYPE_RTL8139;
    strncpy(nic->name, "Realtek RTL8139 Fast Ethernet", sizeof(nic->name) - 1);
    nic->irq = dev->irq;
    nic->io_base = g_io_base;
    nic->mmio_base = 0;

    // 1. Accensione chip (Power ON via Config1)
    outb(g_io_base + RTL_REG_CONFIG1, 0x00);

    // 2. Reset Software
    outb(g_io_base + RTL_REG_CR, 0x10);
    for (int timeout = 0; timeout < 10000; timeout++) {
        if ((inb(g_io_base + RTL_REG_CR) & 0x10) == 0) break;
    }

    // 3. Lettura MAC Address dalle porte I/O 0x00..0x05
    for (int i = 0; i < 6; i++) {
        nic->mac[i] = inb(g_io_base + RTL_REG_MAC0 + i);
    }
    if (nic->mac[0] == 0 && nic->mac[1] == 0 && nic->mac[2] == 0 &&
        nic->mac[3] == 0 && nic->mac[4] == 0 && nic->mac[5] == 0) {
        nic->mac[0] = 0x52; nic->mac[1] = 0x54; nic->mac[2] = 0x00;
        nic->mac[3] = 0x12; nic->mac[4] = 0x34; nic->mac[5] = 0x57;
    }

    // 4. Alloca buffer di ricezione (RX Ring)
    g_rx_buffer = (uint8_t*)kmalloc_aligned(RTL_RX_BUF_SIZE, 16);
    if (!g_rx_buffer) return false;
    memset(g_rx_buffer, 0, RTL_RX_BUF_SIZE);
    outl(g_io_base + RTL_REG_RBSTART, (uint32_t)(uint64_t)g_rx_buffer);

    // 5. Configura RCR (AAP, APM, AM, AB, Wrap)
    outl(g_io_base + RTL_REG_RCR, 0x0000008F);

    // 6. Alloca 4 buffer di trasmissione
    for (int i = 0; i < 4; i++) {
        g_tx_buffers[i] = (uint8_t*)kmalloc_aligned(RTL_TX_BUF_SIZE, 16);
        if (!g_tx_buffers[i]) return false;
        memset(g_tx_buffers[i], 0, RTL_TX_BUF_SIZE);
        outl(g_io_base + RTL_REG_TSAD0 + i * 4, (uint32_t)(uint64_t)g_tx_buffers[i]);
    }
    g_tx_cur = 0;
    g_rx_offset = 0;

    // 7. Abilita Interrupt e Avvia RX/TX
    outw(g_io_base + RTL_REG_IMR, 0x0005); // ROK | TOK
    outb(g_io_base + RTL_REG_CR, 0x0C);    // RE | TE

    nic->link_up = true;
    return true;
}

bool rtl8139_send(const void* data, uint16_t len) {
    if (!g_io_base || !data || len == 0 || len > RTL_TX_BUF_SIZE) return false;

    uint8_t desc = g_tx_cur;
    // Attendi che il descrittore sia libero (TOK o OWN)
    for (int timeout = 0; timeout < 50000; timeout++) {
        uint32_t status = inl(g_io_base + RTL_REG_TSD0 + desc * 4);
        if (status & (1U << 13)) break; // OWN = 1 (host owns it)
    }

    memcpy(g_tx_buffers[desc], data, len);
    // Dimensione minima Ethernet frame 60 byte
    uint32_t tx_len = (len < 60) ? 60 : len;
    outl(g_io_base + RTL_REG_TSD0 + desc * 4, tx_len & 0x1FFF);

    g_tx_cur = (g_tx_cur + 1) % 4;

    if (g_nic_ref) {
        g_nic_ref->tx_packets++;
        g_nic_ref->tx_bytes += len;
    }
    return true;
}

void rtl8139_poll(net_rx_handler_t rx_handler) {
    if (!g_io_base || !g_rx_buffer || !rx_handler) return;

    // Reset interrupt status
    uint16_t isr = inw(g_io_base + RTL_REG_ISR);
    if (isr != 0) {
        outw(g_io_base + RTL_REG_ISR, isr);
    }

    while ((inb(g_io_base + RTL_REG_CR) & 1) == 0) { // Buffer non vuoto
        uint8_t* p = g_rx_buffer + g_rx_offset;
        uint16_t status = *(uint16_t*)p;
        uint16_t len = *(uint16_t*)(p + 2);

        if (!(status & 1)) break; // Pacchetto non valido o incompleto
        if (len < 4 || len > 1792) {
            // Errore sincronizzazione: reset puntatore
            g_rx_offset = 0;
            outw(g_io_base + RTL_REG_CAPR, 0);
            break;
        }

        uint16_t packet_len = len - 4; // Togli CRC
        rx_handler(p + 4, packet_len);

        if (g_nic_ref) {
            g_nic_ref->rx_packets++;
            g_nic_ref->rx_bytes += packet_len;
        }

        // Calcola nuovo offset con allineamento a 4 byte
        g_rx_offset = (g_rx_offset + len + 4 + 3) & ~3;
        if (g_rx_offset >= 8192) g_rx_offset -= 8192;

        outw(g_io_base + RTL_REG_CAPR, (uint16_t)(g_rx_offset - 16));
    }
}
