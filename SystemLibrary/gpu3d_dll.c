#include "dll_libc.h"
#include <arch/x86_64/io.h>
#include <drivers/pci.h>
#include <drivers/timer.h>
#include <gpu3d.h>
#include <mm/heap.h>
#include <mm/paging.h>
#include <string.h>

__attribute__((section(".header"), used)) module_header_t g_gpu3d_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size = 0,
    .entry_offset = 64,
    .checksum = 0,
    .security_flags =
        SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "GPU3D",
    .signature = MODULE_AUTH_TAG};

static volatile uint32_t g_reloc_info[2] = { 0x52454C4Fu, 0xCAFEBABEu };

static void gpu3d_apply_relocs(void) {
    if (g_reloc_info[0] != 0x52454C4Fu || g_reloc_info[1] == 0xCAFEBABEu) return;
    uintptr_t base = (uintptr_t)&g_gpu3d_header;
    const uint32_t *tbl = (const uint32_t *)(base + g_reloc_info[1]);
    uint32_t count = tbl[0];
    for (uint32_t i = 0; i < count; i++) {
        uint32_t off = tbl[1 + i];
        *(uint64_t *)(base + off) += (uint64_t)base;
    }
    g_reloc_info[0] = 0;
}

typedef struct {
    uint32_t cmd;
    uint32_t w;
    uint32_t h;
    uint64_t lfb_phys;
    int32_t sx, sy, dx, dy;
    uint32_t color;
    void *buf;
    const void *verts;
    uint32_t tri_count;
    float angle_x;
    float angle_y;
    bool ret_bool;
    const void *ret_ptr;
} gpu3d_dll_params_t;

static gpu3d_info_t g_gpu3d_info;

bool vmsvga_init(bool* out_has_3d);
bool vmsvga_rect_copy(int32_t sx, int32_t sy, int32_t dx, int32_t dy, int32_t w, int32_t h);
bool vmsvga_update_rect(int32_t x, int32_t y, int32_t w, int32_t h);
bool virtio_gpu_init(uint32_t width, uint32_t height, uint64_t lfb_phys, bool* out_has_3d);
bool virtio_gpu_flush_rect(int32_t x, int32_t y, int32_t w, int32_t h);
bool intel_gpu_init(uint16_t* out_dev_id);
bool soft3d_clear(uint32_t* color_buf, uint32_t w, uint32_t h, uint32_t argb);
bool soft3d_draw_mesh(uint32_t* color_buf, uint32_t w, uint32_t h,
                      const gpu3d_vertex_t* verts, uint32_t tri_count,
                      float angle_x, float angle_y);

static bool gpu3d_internal_init(uint32_t w, uint32_t h, uint64_t lfb_phys);
static bool gpu3d_internal_rect_copy(int32_t sx, int32_t sy, int32_t dx, int32_t dy, int32_t w, int32_t h);
static bool gpu3d_internal_update_rect(int32_t x, int32_t y, int32_t w, int32_t h);

__attribute__((force_align_arg_pointer))
int module_main(const kernel_api_t *api, gpu3d_dll_params_t *p) {
    gpu3d_apply_relocs();
    if (!api || !p) return -1;
    g_dll_api = api;
    switch (p->cmd) {
        case 1: p->ret_bool = gpu3d_internal_init(p->w, p->h, p->lfb_phys); break;
        case 2: p->ret_ptr = &g_gpu3d_info; break;
        case 3: p->ret_bool = gpu3d_internal_rect_copy(p->sx, p->sy, p->dx, p->dy, (int32_t)p->w, (int32_t)p->h); break;
        case 4: p->ret_bool = gpu3d_internal_update_rect(p->dx, p->dy, (int32_t)p->w, (int32_t)p->h); break;
        case 5: p->ret_bool = soft3d_clear((uint32_t *)p->buf, p->w, p->h, p->color); break;
        case 6: p->ret_bool = soft3d_draw_mesh((uint32_t *)p->buf, p->w, p->h, (const gpu3d_vertex_t *)p->verts, p->tri_count, p->angle_x, p->angle_y); break;
        default: return -1;
    }
    return 0;
}

#include "dll_ext_macros.h"

#include "../kernel/drivers/gpu/vmsvga.c"
#include "../kernel/drivers/gpu/virtio_gpu.c"
#include "../kernel/drivers/gpu/intel_gpu.c"
#include "../kernel/drivers/gpu/soft3d.c"

static bool gpu3d_internal_init(uint32_t w, uint32_t h, uint64_t lfb_phys) {
    dll_memset(&g_gpu3d_info, 0, sizeof(g_gpu3d_info));
    g_gpu3d_info.width = w;
    g_gpu3d_info.height = h;

    bool has_3d = false;
    uint16_t intel_id = 0;

    if (vmsvga_init(&has_3d)) {
        g_gpu3d_info.backend = GPU_BACKEND_VMSVGA;
        g_gpu3d_info.hw_2d_accel = true;
        g_gpu3d_info.hw_3d_accel = has_3d;
        dll_strcpy(g_gpu3d_info.adapter_name, "VMware SVGA II 2D/3D FIFO Accelerator");
    } else if (virtio_gpu_init(w, h, lfb_phys, &has_3d)) {
        g_gpu3d_info.backend = GPU_BACKEND_VIRTIO_GPU;
        g_gpu3d_info.hw_2d_accel = true;
        g_gpu3d_info.hw_3d_accel = has_3d;
        dll_strcpy(g_gpu3d_info.adapter_name, "VirtIO-GPU 2D/Virgl Accelerator");
    } else if (intel_gpu_init(&intel_id)) {
        g_gpu3d_info.backend = GPU_BACKEND_INTEL_GEN;
        g_gpu3d_info.hw_2d_accel = true;
        g_gpu3d_info.hw_3d_accel = false;
        dll_strcpy(g_gpu3d_info.adapter_name, "Intel UHD/Iris Graphics Blitter Engine");
    } else {
        g_gpu3d_info.backend = GPU_BACKEND_SOFT3D;
        g_gpu3d_info.hw_2d_accel = false;
        g_gpu3d_info.hw_3d_accel = false;
        dll_strcpy(g_gpu3d_info.adapter_name, "NewOS SIMD Soft3D + PAT WC Fallback");
    }
    g_gpu3d_info.initialized = true;
    return true;
}

static bool gpu3d_internal_rect_copy(int32_t sx, int32_t sy, int32_t dx, int32_t dy, int32_t w, int32_t h) {
    if (g_gpu3d_info.backend == GPU_BACKEND_VMSVGA) {
        return vmsvga_rect_copy(sx, sy, dx, dy, w, h);
    }
    return false;
}

static bool gpu3d_internal_update_rect(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (g_gpu3d_info.backend == GPU_BACKEND_VMSVGA) {
        return vmsvga_update_rect(x, y, w, h);
    }
    if (g_gpu3d_info.backend == GPU_BACKEND_VIRTIO_GPU) {
        return virtio_gpu_flush_rect(x, y, w, h);
    }
    return false;
}
