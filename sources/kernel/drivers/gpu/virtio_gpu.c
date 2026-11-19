#include <drivers/pci.h>
#include <mm/paging.h>
#include <string.h>

#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2

#define VGPU_CMD_GET_DISPLAY_INFO       0x0100
#define VGPU_CMD_RESOURCE_CREATE_2D     0x0101
#define VGPU_CMD_RESOURCE_UNREF         0x0102
#define VGPU_CMD_SET_SCANOUT            0x0103
#define VGPU_CMD_RESOURCE_FLUSH         0x0104
#define VGPU_CMD_TRANSFER_TO_HOST_2D    0x0105
#define VGPU_CMD_RESOURCE_ATTACH_BACKING 0x0106

#define VGPU_FORMAT_B8G8R8X8_UNORM      2

typedef struct {
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    uint16_t msix_config;
    uint16_t num_queues;
    uint8_t  device_status;
    uint8_t  config_generation;
    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint64_t queue_desc;
    uint64_t queue_avail;
    uint64_t queue_used;
} __attribute__((packed)) virtio_pci_common_cfg_t;

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed)) vgpu_desc_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[64];
} __attribute__((packed)) vgpu_avail_t;

typedef struct {
    uint32_t id;
    uint32_t len;
} __attribute__((packed)) vgpu_used_elem_t;

typedef struct {
    uint16_t         flags;
    uint16_t         idx;
    vgpu_used_elem_t ring[64];
} __attribute__((packed)) vgpu_used_t;

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} __attribute__((packed)) vgpu_ctrl_hdr_t;

typedef struct {
    uint32_t x, y, width, height;
} __attribute__((packed)) vgpu_rect_t;

typedef struct {
    vgpu_ctrl_hdr_t hdr;
    uint32_t        resource_id;
    uint32_t        format;
    uint32_t        width;
    uint32_t        height;
} __attribute__((packed)) vgpu_res_create_2d_t;

typedef struct {
    vgpu_ctrl_hdr_t hdr;
    vgpu_rect_t     r;
    uint32_t        scanout_id;
    uint32_t        resource_id;
} __attribute__((packed)) vgpu_set_scanout_t;

typedef struct {
    vgpu_ctrl_hdr_t hdr;
    vgpu_rect_t     r;
    uint64_t        offset;
    uint32_t        resource_id;
    uint32_t        padding;
} __attribute__((packed)) vgpu_transfer_2d_t;

typedef struct {
    vgpu_ctrl_hdr_t hdr;
    vgpu_rect_t     r;
    uint32_t        resource_id;
    uint32_t        padding;
} __attribute__((packed)) vgpu_res_flush_t;

typedef struct {
    vgpu_ctrl_hdr_t hdr;
    uint32_t        resource_id;
    uint32_t        nr_entries;
    uint64_t        addr;
    uint32_t        length;
    uint32_t        padding;
} __attribute__((packed)) vgpu_attach_backing_t;

static volatile virtio_pci_common_cfg_t* g_vgpu_cfg = NULL;
static volatile uint8_t*                 g_vgpu_notify_base = NULL;
static uint32_t                          g_vgpu_notify_mult = 0;
static uint16_t                          g_vgpu_qsz = 16;
static vgpu_desc_t*                      g_vgpu_desc = NULL;
static vgpu_avail_t*                     g_vgpu_avail = NULL;
static vgpu_used_t*                      g_vgpu_used = NULL;
static uint16_t                          g_vgpu_last_used = 0;
static uint16_t                          g_vgpu_q_notify_off = 0;
static uint8_t*                          g_vgpu_cmd_buf = NULL;
static uint64_t                          g_vgpu_cmd_phys = 0;
static uint8_t*                          g_vgpu_rsp_buf = NULL;
static uint64_t                          g_vgpu_rsp_phys = 0;
static bool                              g_vgpu_ready = false;
static uint32_t                          g_vgpu_w = 0;
static uint32_t                          g_vgpu_h = 0;
static uint64_t                          g_vgpu_lfb = 0;

static bool vgpu_send_cmd(const void* req, uint32_t req_len, uint32_t rsp_len) {
    if (!g_vgpu_cfg || !g_vgpu_cmd_buf || !g_vgpu_rsp_buf) return false;
    memcpy(g_vgpu_cmd_buf, req, req_len);
    memset(g_vgpu_rsp_buf, 0, rsp_len);

    g_vgpu_desc[0].addr = g_vgpu_cmd_phys;
    g_vgpu_desc[0].len = req_len;
    g_vgpu_desc[0].flags = 1; // NEXT
    g_vgpu_desc[0].next = 1;

    g_vgpu_desc[1].addr = g_vgpu_rsp_phys;
    g_vgpu_desc[1].len = rsp_len;
    g_vgpu_desc[1].flags = 2; // WRITE
    g_vgpu_desc[1].next = 0;

    uint16_t aidx = g_vgpu_avail->idx % g_vgpu_qsz;
    g_vgpu_avail->ring[aidx] = 0;
    __asm__ volatile("" ::: "memory");
    g_vgpu_avail->idx++;

    if (g_vgpu_notify_base) {
        volatile uint16_t* n = (volatile uint16_t*)(g_vgpu_notify_base + (uint32_t)g_vgpu_q_notify_off * g_vgpu_notify_mult);
        *n = 0;
    }

    for (uint32_t t = 0; t < 1000000; t++) {
        if (g_vgpu_used->idx != g_vgpu_last_used) {
            g_vgpu_last_used = g_vgpu_used->idx;
            return true;
        }
    }
    return false;
}

bool virtio_gpu_init(uint32_t width, uint32_t height, uint64_t lfb_phys, bool* out_has_3d) {
    if (g_vgpu_ready) {
        if (out_has_3d) *out_has_3d = false;
        return true;
    }

    pci_device_t dev;
    if (!pci_find_device(0x1AF4, 0x1050, &dev)) {
        return false;
    }
    pci_enable_bus_master(&dev);

    // Scansiona PCI Capability List (offset 0x34)
    uint16_t status = pci_read16(dev.bus, dev.slot, dev.func, 0x06);
    if (!(status & (1U << 4))) return false;
    uint8_t cap_ptr = pci_read8(dev.bus, dev.slot, dev.func, 0x34);

    while (cap_ptr >= 0x40) {
        uint8_t cap_id = pci_read8(dev.bus, dev.slot, dev.func, cap_ptr);
        uint8_t next = pci_read8(dev.bus, dev.slot, dev.func, cap_ptr + 1);
        if (cap_id == 0x09) { // Vendor-specific VirtIO cap
            uint8_t cfg_type = pci_read8(dev.bus, dev.slot, dev.func, cap_ptr + 3);
            uint8_t bar = pci_read8(dev.bus, dev.slot, dev.func, cap_ptr + 4);
            uint32_t offset = pci_read32(dev.bus, dev.slot, dev.func, cap_ptr + 8);
            uint32_t length = pci_read32(dev.bus, dev.slot, dev.func, cap_ptr + 12);
            uint64_t bar_addr = pci_get_bar_addr(&dev, bar);
            if (bar_addr && cfg_type == VIRTIO_PCI_CAP_COMMON_CFG) {
                g_vgpu_cfg = (volatile virtio_pci_common_cfg_t*)paging_map_mmio(bar_addr + offset, length ? length : 4096);
            } else if (bar_addr && cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
                g_vgpu_notify_base = (volatile uint8_t*)paging_map_mmio(bar_addr + offset, length ? length : 4096);
                g_vgpu_notify_mult = pci_read32(dev.bus, dev.slot, dev.func, cap_ptr + 16);
            }
        }
        cap_ptr = next;
    }

    if (!g_vgpu_cfg || !g_vgpu_notify_base) return false;

    // VirtIO 1.0 Init Handshake
    g_vgpu_cfg->device_status = 0x00;
    g_vgpu_cfg->device_status = 0x01; // ACKNOWLEDGE
    g_vgpu_cfg->device_status = 0x03; // DRIVER

    g_vgpu_cfg->device_feature_select = 0;
    uint32_t f0 = g_vgpu_cfg->device_feature;
    if (out_has_3d) *out_has_3d = (f0 & 1U) != 0; // VIRTIO_GPU_F_VIRGL
    g_vgpu_cfg->driver_feature_select = 0;
    g_vgpu_cfg->driver_feature = 0;
    g_vgpu_cfg->device_feature_select = 1;
    g_vgpu_cfg->driver_feature_select = 1;
    g_vgpu_cfg->driver_feature = 1; // VIRTIO_F_VERSION_1

    g_vgpu_cfg->device_status = 0x0B; // FEATURES_OK
    if (!(g_vgpu_cfg->device_status & 0x08)) return false;

    // Setup Control Virtqueue (Queue 0)
    g_vgpu_cfg->queue_select = 0;
    uint16_t max_q = g_vgpu_cfg->queue_size;
    if (max_q == 0) return false;
    g_vgpu_qsz = (max_q > 16) ? 16 : max_q;
    g_vgpu_cfg->queue_size = g_vgpu_qsz;
    g_vgpu_q_notify_off = g_vgpu_cfg->queue_notify_off;

    uint64_t desc_p = 0, avail_p = 0, used_p = 0;
    g_vgpu_desc = (vgpu_desc_t*)dma_alloc(4096, 16, &desc_p);
    g_vgpu_avail = (vgpu_avail_t*)dma_alloc(4096, 16, &avail_p);
    g_vgpu_used = (vgpu_used_t*)dma_alloc(4096, 16, &used_p);
    g_vgpu_cmd_buf = (uint8_t*)dma_alloc(4096, 64, &g_vgpu_cmd_phys);
    g_vgpu_rsp_buf = (uint8_t*)dma_alloc(4096, 64, &g_vgpu_rsp_phys);
    if (!g_vgpu_desc || !g_vgpu_avail || !g_vgpu_used || !g_vgpu_cmd_buf || !g_vgpu_rsp_buf) return false;

    g_vgpu_cfg->queue_desc = desc_p;
    g_vgpu_cfg->queue_avail = avail_p;
    g_vgpu_cfg->queue_used = used_p;
    g_vgpu_cfg->queue_enable = 1;
    g_vgpu_cfg->device_status = 0x0F; // DRIVER_OK

    g_vgpu_w = width;
    g_vgpu_h = height;
    g_vgpu_lfb = lfb_phys;
    g_vgpu_ready = true;

    // Registra resource 1 sul LFB se disponibile
    if (lfb_phys && width && height) {
        vgpu_res_create_2d_t rc;
        memset(&rc, 0, sizeof(rc));
        rc.hdr.type = VGPU_CMD_RESOURCE_CREATE_2D;
        rc.resource_id = 1;
        rc.format = VGPU_FORMAT_B8G8R8X8_UNORM;
        rc.width = width;
        rc.height = height;
        vgpu_send_cmd(&rc, sizeof(rc), sizeof(vgpu_ctrl_hdr_t));

        vgpu_attach_backing_t ab;
        memset(&ab, 0, sizeof(ab));
        ab.hdr.type = VGPU_CMD_RESOURCE_ATTACH_BACKING;
        ab.resource_id = 1;
        ab.nr_entries = 1;
        ab.addr = lfb_phys;
        ab.length = width * height * 4U;
        vgpu_send_cmd(&ab, sizeof(ab), sizeof(vgpu_ctrl_hdr_t));

        vgpu_set_scanout_t ss;
        memset(&ss, 0, sizeof(ss));
        ss.hdr.type = VGPU_CMD_SET_SCANOUT;
        ss.r.width = width;
        ss.r.height = height;
        ss.scanout_id = 0;
        ss.resource_id = 1;
        vgpu_send_cmd(&ss, sizeof(ss), sizeof(vgpu_ctrl_hdr_t));
    }

    return true;
}

bool virtio_gpu_flush_rect(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!g_vgpu_ready || !g_vgpu_lfb || w <= 0 || h <= 0) return false;
    vgpu_transfer_2d_t tr;
    memset(&tr, 0, sizeof(tr));
    tr.hdr.type = VGPU_CMD_TRANSFER_TO_HOST_2D;
    tr.r.x = (uint32_t)x;
    tr.r.y = (uint32_t)y;
    tr.r.width = (uint32_t)w;
    tr.r.height = (uint32_t)h;
    tr.offset = ((uint64_t)y * g_vgpu_w + (uint64_t)x) * 4ULL;
    tr.resource_id = 1;
    if (!vgpu_send_cmd(&tr, sizeof(tr), sizeof(vgpu_ctrl_hdr_t))) return false;

    vgpu_res_flush_t fl;
    memset(&fl, 0, sizeof(fl));
    fl.hdr.type = VGPU_CMD_RESOURCE_FLUSH;
    fl.r = tr.r;
    fl.resource_id = 1;
    return vgpu_send_cmd(&fl, sizeof(fl), sizeof(vgpu_ctrl_hdr_t));
}
