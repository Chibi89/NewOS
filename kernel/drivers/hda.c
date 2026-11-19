#include <drivers/hda.h>
#include <drivers/pci.h>
#include <drivers/timer.h>
#include <mm/heap.h>
#include <mm/paging.h>
#include <string.h>

#define HDA_REG_GCAP     0x00
#define HDA_REG_GCTL     0x08
#define HDA_REG_STATESTS 0x0E
#define HDA_REG_INTCTL   0x20
#define HDA_REG_IC       0x60
#define HDA_REG_IR       0x64
#define HDA_REG_IRS      0x68

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint32_t flags;
} __attribute__((packed)) hda_bdl_entry_t;

static volatile uint8_t* g_hda_mmio = NULL;
static bool              g_hda_present = false;
static uint8_t           g_hda_cad = 0;
static uint32_t          g_hda_sd_off = 0;
static uint16_t          g_hda_dac_nid = 0;
static uint16_t          g_hda_pin_nid = 0;
static uint8_t           g_hda_vol = 100;
static char              g_hda_name[48] = "Intel High Definition Audio (HDA)";

static hda_bdl_entry_t*  g_hda_bdl = NULL;
static uint64_t          g_hda_bdl_phys = 0;
static uint8_t*          g_hda_dma_pcm = NULL;
static uint64_t          g_hda_dma_phys = 0;
static uint32_t          g_hda_dma_cap = 0;

static inline uint8_t hda_r8(uint32_t o) { return *(volatile uint8_t*)(g_hda_mmio + o); }
static inline uint16_t hda_r16(uint32_t o) { return *(volatile uint16_t*)(g_hda_mmio + o); }
static inline uint32_t hda_r32(uint32_t o) { return *(volatile uint32_t*)(g_hda_mmio + o); }
static inline void hda_w8(uint32_t o, uint8_t v) { *(volatile uint8_t*)(g_hda_mmio + o) = v; }
static inline void hda_w16(uint32_t o, uint16_t v) { *(volatile uint16_t*)(g_hda_mmio + o) = v; }
static inline void hda_w32(uint32_t o, uint32_t v) { *(volatile uint32_t*)(g_hda_mmio + o) = v; }

static bool hda_send_verb(uint8_t cad, uint16_t nid, uint32_t verb, uint32_t* out_resp) {
    uint32_t cmd = ((uint32_t)(cad & 0xF) << 28) | ((uint32_t)(nid & 0xFF) << 20) | (verb & 0xFFFFF);
    for (int t = 0; t < 10000; t++) {
        if ((hda_r16(HDA_REG_IRS) & 1U) == 0) break;
    }
    hda_w16(HDA_REG_IRS, 0x0002); // Clear IRV
    hda_w32(HDA_REG_IC, cmd);
    hda_w16(HDA_REG_IRS, 0x0001); // Set ICB
    for (int t = 0; t < 50000; t++) {
        uint16_t irs = hda_r16(HDA_REG_IRS);
        if ((irs & 1U) == 0 && (irs & 2U) != 0) {
            if (out_resp) *out_resp = hda_r32(HDA_REG_IR);
            hda_w16(HDA_REG_IRS, 0x0002);
            return true;
        }
    }
    return false;
}

static uint32_t hda_get_param(uint8_t cad, uint16_t nid, uint8_t param) {
    uint32_t r = 0;
    hda_send_verb(cad, nid, 0xF0000U | param, &r);
    return r;
}

bool hda_init(void) {
    if (g_hda_present) return true;

    pci_device_t dev;
    if (!pci_find_class(0x04, 0x03, &dev)) {
        return false;
    }

    pci_enable_bus_master(&dev);
    uint64_t bar0 = pci_get_bar_addr(&dev, 0);
    if (!bar0) return false;

    g_hda_mmio = (volatile uint8_t*)paging_map_mmio(bar0, 0x4000);
    if (!g_hda_mmio) return false;

    // 1. Reset controller (CRST 0 -> 1)
    uint32_t gctl = hda_r32(HDA_REG_GCTL);
    hda_w32(HDA_REG_GCTL, gctl & ~1U);
    for (int t = 0; t < 100; t++) {
        if ((hda_r32(HDA_REG_GCTL) & 1U) == 0) break;
        timer_sleep(1);
    }
    hda_w32(HDA_REG_GCTL, (gctl & ~1U) | 1U);
    for (int t = 0; t < 100; t++) {
        if (hda_r32(HDA_REG_GCTL) & 1U) break;
        timer_sleep(1);
    }
    timer_sleep(2);

    uint16_t gcap = hda_r16(HDA_REG_GCAP);
    uint32_t num_iss = (gcap >> 8) & 0xF;
    g_hda_sd_off = 0x80U + num_iss * 0x20U; // Primo Output Stream Descriptor (SD0_OUT)

    uint16_t statests = hda_r16(HDA_REG_STATESTS);
    if (statests == 0) return false;
    for (uint8_t c = 0; c < 15; c++) {
        if (statests & (1U << c)) {
            g_hda_cad = c;
            break;
        }
    }

    // 2. Trova Audio Function Group (AFG) e i nodi DAC + Pin Output
    uint32_t sub = hda_get_param(g_hda_cad, 0, 0x04);
    uint16_t fg_start = (uint16_t)((sub >> 16) & 0xFF);
    uint16_t fg_cnt = (uint16_t)(sub & 0xFF);
    uint16_t afg_nid = 0;
    for (uint16_t i = 0; i < fg_cnt; i++) {
        uint32_t ftype = hda_get_param(g_hda_cad, fg_start + i, 0x05);
        if ((ftype & 0xFF) == 0x01) { // Audio Function Group
            afg_nid = fg_start + i;
            break;
        }
    }
    if (!afg_nid) return false;

    hda_send_verb(g_hda_cad, afg_nid, 0x70500U, NULL); // Power D0
    uint32_t wsub = hda_get_param(g_hda_cad, afg_nid, 0x04);
    uint16_t w_start = (uint16_t)((wsub >> 16) & 0xFF);
    uint16_t w_cnt = (uint16_t)(wsub & 0xFF);

    for (uint16_t i = 0; i < w_cnt; i++) {
        uint16_t nid = w_start + i;
        uint32_t wcap = hda_get_param(g_hda_cad, nid, 0x09);
        uint8_t wtype = (uint8_t)((wcap >> 20) & 0xF);
        if (wtype == 0x0 && !g_hda_dac_nid) { // Audio Output (DAC)
            g_hda_dac_nid = nid;
            hda_send_verb(g_hda_cad, nid, 0x70500U, NULL); // D0
            hda_send_verb(g_hda_cad, nid, 0x70610U, NULL); // Stream 1, Channel 0
            hda_send_verb(g_hda_cad, nid, 0x3B07FU, NULL); // Unmute & max gain
        } else if (wtype == 0x4 && !g_hda_pin_nid) { // Pin Complex
            uint32_t pincap = hda_get_param(g_hda_cad, nid, 0x0C);
            if (pincap & (1U << 4)) { // Output capable
                g_hda_pin_nid = nid;
                hda_send_verb(g_hda_cad, nid, 0x70500U, NULL); // D0
                hda_send_verb(g_hda_cad, nid, 0x70740U, NULL); // Pin Out Enable
                hda_send_verb(g_hda_cad, nid, 0x70C02U, NULL); // EAPD Enable
                hda_send_verb(g_hda_cad, nid, 0x3B07FU, NULL); // Unmute & max gain
            }
        }
    }
    if (!g_hda_dac_nid) return false;

    g_hda_bdl = (hda_bdl_entry_t*)dma_alloc(4096, 128, &g_hda_bdl_phys);
    if (!g_hda_bdl) return false;

    g_hda_present = true;
    return true;
}

bool hda_is_available(void) { return g_hda_present; }
const char* hda_get_device_name(void) { return g_hda_name; }

void hda_stop(void) {
    if (!g_hda_present) return;
    uint32_t ctl = hda_r32(g_hda_sd_off + 0x00);
    hda_w32(g_hda_sd_off + 0x00, ctl & ~0x2U); // Clear RUN
    hda_w32(g_hda_sd_off + 0x00, 0x01U);       // Stream Reset
    for (int t = 0; t < 100; t++) {
        if (hda_r32(g_hda_sd_off + 0x00) & 0x01U) break;
    }
    hda_w32(g_hda_sd_off + 0x00, 0x00U);       // Exit Reset
}

static uint16_t hda_calc_fmt(uint32_t sample_rate) {
    // Base 44.1 kHz (bit 14 = 1) o 48 kHz (bit 14 = 0), 16-bit (bits 6:4 = 001), 2ch (bits 3:0 = 1)
    if (sample_rate == 44100) return 0x4011;
    if (sample_rate == 22050) return 0x4811;
    return 0x0011; // 48 kHz 16-bit stereo
}

void hda_play_pcm(const void* data, uint32_t bytes, uint32_t sample_rate) {
    if (!g_hda_present || !data || bytes < 16) return;
    hda_stop();

    if (bytes > g_hda_dma_cap) {
        if (g_hda_dma_pcm) kfree(g_hda_dma_pcm);
        g_hda_dma_pcm = (uint8_t*)dma_alloc(bytes, 128, &g_hda_dma_phys);
        g_hda_dma_cap = g_hda_dma_pcm ? bytes : 0;
    }
    if (!g_hda_dma_pcm) return;
    memcpy(g_hda_dma_pcm, data, bytes);

    uint32_t half = bytes / 2;
    g_hda_bdl[0].addr = g_hda_dma_phys;
    g_hda_bdl[0].len = half;
    g_hda_bdl[0].flags = 0;
    g_hda_bdl[1].addr = g_hda_dma_phys + half;
    g_hda_bdl[1].len = bytes - half;
    g_hda_bdl[1].flags = 1; // IOC

    uint16_t fmt = hda_calc_fmt(sample_rate);
    hda_send_verb(g_hda_cad, g_hda_dac_nid, 0x20000U | fmt, NULL);

    hda_w8(g_hda_sd_off + 0x03, 0x1CU); // Clear status
    hda_w32(g_hda_sd_off + 0x08, bytes); // CBL
    hda_w16(g_hda_sd_off + 0x0C, 1);     // LVI (2 BDL entries: 0 e 1)
    hda_w16(g_hda_sd_off + 0x12, fmt);   // SDFMT
    hda_w32(g_hda_sd_off + 0x18, (uint32_t)(g_hda_bdl_phys & 0xFFFFFFFFULL));
    hda_w32(g_hda_sd_off + 0x1C, (uint32_t)(g_hda_bdl_phys >> 32));

    // Stream ID = 1 (bits 23:20 = 1) + RUN (bit 1 = 1)
    hda_w32(g_hda_sd_off + 0x00, (1U << 20) | 0x02U);
}

bool hda_is_playing(void) {
    if (!g_hda_present) return false;
    return (hda_r32(g_hda_sd_off + 0x00) & 0x02U) != 0 && (hda_r8(g_hda_sd_off + 0x03) & 0x04U) == 0;
}

void hda_set_volume(uint8_t volume_percent) {
    if (volume_percent > 100) volume_percent = 100;
    g_hda_vol = volume_percent;
    if (!g_hda_present || !g_hda_dac_nid) return;
    uint8_t gain = (uint8_t)((volume_percent * 127U) / 100U);
    uint32_t mute = (volume_percent == 0) ? 0x80U : 0U;
    hda_send_verb(g_hda_cad, g_hda_dac_nid, 0x3B000U | mute | gain, NULL);
}
