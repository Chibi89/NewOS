#include <fs/vfs.h>
#include <fs/vfs_mount.h>
#include <gpu3d.h>
#include <gui/screen_mgr.h>
#include <mm/heap.h>
#include <string.h>

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

typedef int (*gpu3d_entry_fn)(const kernel_api_t *api, gpu3d_dll_params_t *p);

static gpu3d_entry_fn g_gpu3d_entry = NULL;
static gpu3d_info_t   g_gpu3d_fallback;

static bool ensure_gpu3d_loaded(void) {
    if (g_gpu3d_entry) return true;
    if (!vfs_is_drive_mounted('C')) return false;

    const char *paths[] = {
        "C:\\NewOS\\SystemLibrary\\GPU3D.DLL",
        "/NewOS/SystemLibrary/GPU3D.DLL"
    };
    for (int i = 0; i < 2 && !g_gpu3d_entry; i++) {
        uint32_t sz = 0;
        uint8_t *buf = (uint8_t *)vfs_read_entire_file(paths[i], &sz);
        if (buf && sz >= sizeof(module_header_t)) {
            if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY, NULL)) {
                uint8_t *exec_buf = (uint8_t *)kmalloc(sz + 32768);
                if (exec_buf) {
                    memcpy(exec_buf, buf, sz);
                    memset(exec_buf + sz, 0, 32768);
                    const module_header_t *h = (const module_header_t *)exec_buf;
                    g_gpu3d_entry = (gpu3d_entry_fn)(exec_buf + h->entry_offset);
                }
            }
        }
        if (buf) kfree(buf);
    }
    return (g_gpu3d_entry != NULL);
}

bool gpu3d_init(uint32_t width, uint32_t height, uint64_t lfb_phys) {
    if (!ensure_gpu3d_loaded()) return false;
    gpu3d_dll_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 1;
    p.w = width;
    p.h = height;
    p.lfb_phys = lfb_phys;
    g_gpu3d_entry(screen_mgr_get_api(), &p);
    return p.ret_bool;
}

const gpu3d_info_t *gpu3d_get_info(void) {
    if (g_gpu3d_entry) {
        gpu3d_dll_params_t p;
        memset(&p, 0, sizeof(p));
        p.cmd = 2;
        g_gpu3d_entry(screen_mgr_get_api(), &p);
        if (p.ret_ptr) return (const gpu3d_info_t *)p.ret_ptr;
    }
    return &g_gpu3d_fallback;
}

bool gpu3d_rect_copy(int32_t sx, int32_t sy, int32_t dx, int32_t dy, int32_t w, int32_t h) {
    if (!g_gpu3d_entry) return false;
    gpu3d_dll_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 3;
    p.sx = sx; p.sy = sy; p.dx = dx; p.dy = dy;
    p.w = (uint32_t)w; p.h = (uint32_t)h;
    g_gpu3d_entry(screen_mgr_get_api(), &p);
    return p.ret_bool;
}

bool gpu3d_update_rect(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!g_gpu3d_entry) return false;
    gpu3d_dll_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 4;
    p.dx = x; p.dy = y; p.w = (uint32_t)w; p.h = (uint32_t)h;
    g_gpu3d_entry(screen_mgr_get_api(), &p);
    return p.ret_bool;
}

bool gpu3d_clear_3d(uint32_t *color_buf, uint32_t w, uint32_t h, uint32_t argb) {
    if (!ensure_gpu3d_loaded()) return false;
    gpu3d_dll_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 5;
    p.buf = color_buf; p.w = w; p.h = h; p.color = argb;
    g_gpu3d_entry(screen_mgr_get_api(), &p);
    return p.ret_bool;
}

bool gpu3d_draw_mesh(uint32_t *color_buf, uint32_t w, uint32_t h,
                     const gpu3d_vertex_t *verts, uint32_t tri_count,
                     float angle_x, float angle_y) {
    if (!ensure_gpu3d_loaded()) return false;
    gpu3d_dll_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = 6;
    p.buf = color_buf; p.w = w; p.h = h;
    p.verts = verts; p.tri_count = tri_count;
    p.angle_x = angle_x; p.angle_y = angle_y;
    g_gpu3d_entry(screen_mgr_get_api(), &p);
    return p.ret_bool;
}
