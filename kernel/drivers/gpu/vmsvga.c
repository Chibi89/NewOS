#include <arch/x86_64/io.h>
#include <drivers/pci.h>
#include <drivers/timer.h>
#include <mm/paging.h>
#include <string.h>

#define SVGA_INDEX_PORT    0x0
#define SVGA_VALUE_PORT    0x1

#define SVGA_REG_ID            0
#define SVGA_REG_ENABLE        1
#define SVGA_REG_WIDTH         2
#define SVGA_REG_HEIGHT        3
#define SVGA_REG_BPP           7
#define SVGA_REG_FB_START      13
#define SVGA_REG_FIFO_START    18
#define SVGA_REG_FIFO_SIZE     19
#define SVGA_REG_CAPABILITIES  17
#define SVGA_REG_CONFIG_DONE   20

#define SVGA_ID_2              0x90000002U
#define SVGA_CAP_RECT_COPY     0x00000002U
#define SVGA_CAP_3D            0x00004000U

#define SVGA_FIFO_MIN          0
#define SVGA_FIFO_MAX          1
#define SVGA_FIFO_NEXT_CMD     2
#define SVGA_FIFO_STOP         3

#define SVGA_CMD_UPDATE        1
#define SVGA_CMD_RECT_COPY     3

static uint16_t           g_svga_io = 0;
static volatile uint32_t* g_svga_fifo = NULL;
static uint32_t           g_svga_fifo_sz = 0;
static uint32_t           g_svga_caps = 0;
static bool               g_svga_ready = false;

static inline void svga_write_reg(uint32_t idx, uint32_t val) {
    outl(g_svga_io + SVGA_INDEX_PORT, idx);
    outl(g_svga_io + SVGA_VALUE_PORT, val);
}

static inline uint32_t svga_read_reg(uint32_t idx) {
    outl(g_svga_io + SVGA_INDEX_PORT, idx);
    return inl(g_svga_io + SVGA_VALUE_PORT);
}

static void svga_fifo_write(const uint32_t* words, uint32_t count) {
    if (!g_svga_fifo || count == 0) return;
    uint32_t min_off = g_svga_fifo[SVGA_FIFO_MIN];
    uint32_t max_off = g_svga_fifo[SVGA_FIFO_MAX];
    uint32_t next_cmd = g_svga_fifo[SVGA_FIFO_NEXT_CMD];

    for (uint32_t i = 0; i < count; i++) {
        g_svga_fifo[next_cmd / 4U] = words[i];
        next_cmd += 4U;
        if (next_cmd >= max_off) next_cmd = min_off;
    }
    __asm__ volatile("" ::: "memory");
    g_svga_fifo[SVGA_FIFO_NEXT_CMD] = next_cmd;
}

bool vmsvga_init(bool* out_has_3d) {
    if (g_svga_ready) {
        if (out_has_3d) *out_has_3d = (g_svga_caps & SVGA_CAP_3D) != 0;
        return true;
    }

    pci_device_t dev;
    if (!pci_find_device(0x15AD, 0x0405, &dev)) {
        return false;
    }
    if (!(dev.bar0 & 1U)) return false;

    pci_enable_bus_master(&dev);
    g_svga_io = (uint16_t)(dev.bar0 & ~0x3U);

    svga_write_reg(SVGA_REG_ID, SVGA_ID_2);
    if (svga_read_reg(SVGA_REG_ID) != SVGA_ID_2) {
        return false;
    }

    g_svga_caps = svga_read_reg(SVGA_REG_CAPABILITIES);
    uint64_t fifo_phys = (uint64_t)svga_read_reg(SVGA_REG_FIFO_START);
    g_svga_fifo_sz = svga_read_reg(SVGA_REG_FIFO_SIZE);
    if (!fifo_phys || g_svga_fifo_sz < 4096) return false;

    g_svga_fifo = (volatile uint32_t*)paging_map_mmio(fifo_phys, g_svga_fifo_sz);
    if (!g_svga_fifo) return false;

    uint32_t min_bytes = 293U * 4U;
    g_svga_fifo[SVGA_FIFO_MIN] = min_bytes;
    g_svga_fifo[SVGA_FIFO_MAX] = g_svga_fifo_sz;
    g_svga_fifo[SVGA_FIFO_NEXT_CMD] = min_bytes;
    g_svga_fifo[SVGA_FIFO_STOP] = min_bytes;

    svga_write_reg(SVGA_REG_CONFIG_DONE, 1);

    if (out_has_3d) *out_has_3d = (g_svga_caps & SVGA_CAP_3D) != 0;
    g_svga_ready = true;
    return true;
}

bool vmsvga_rect_copy(int32_t sx, int32_t sy, int32_t dx, int32_t dy, int32_t w, int32_t h) {
    if (!g_svga_ready || !(g_svga_caps & SVGA_CAP_RECT_COPY) || w <= 0 || h <= 0) return false;
    uint32_t cmd[7] = {
        SVGA_CMD_RECT_COPY,
        (uint32_t)sx, (uint32_t)sy,
        (uint32_t)dx, (uint32_t)dy,
        (uint32_t)w, (uint32_t)h
    };
    svga_fifo_write(cmd, 7);
    return true;
}

bool vmsvga_update_rect(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!g_svga_ready || w <= 0 || h <= 0) return false;
    uint32_t cmd[5] = {
        SVGA_CMD_UPDATE,
        (uint32_t)x, (uint32_t)y,
        (uint32_t)w, (uint32_t)h
    };
    svga_fifo_write(cmd, 5);
    return true;
}
