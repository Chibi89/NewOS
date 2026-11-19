#include "imgcodec.h"
#include <fs/vfs.h>
#include <mm/heap.h>
#include <string.h>

typedef int (*imgcodec_entry_fn)(const kernel_api_t *api, imgcodec_params_t *p);

static imgcodec_entry_fn g_ic_entry = NULL;
static uint8_t *g_ic_buf = NULL;

bool imgcodec_init(void) {
    if (g_ic_entry) return true;

    const char *paths[] = {
        "C:\\NewOS\\SystemLibrary\\IMGCODEC.DLL",
        "C:\\NewOS\\Boot\\IMGCODEC.DLL",
        "/NewOS/SystemLibrary/IMGCODEC.DLL",
        "IMGCODEC.DLL"
    };

    uint32_t sz = 0;
    for (int i = 0; i < 4 && !g_ic_entry; i++) {
        uint8_t *buf = (uint8_t *)vfs_read_entire_file(paths[i], &sz);
        if (buf && sz >= sizeof(module_header_t)) {
            if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY, NULL)) {
                uint8_t *exec_buf = (uint8_t *)kmalloc(sz + 65536);
                if (exec_buf) {
                    memcpy(exec_buf, buf, sz);
                    memset(exec_buf + sz, 0, 65536);
                    const module_header_t *h = (const module_header_t *)exec_buf;
                    g_ic_entry = (imgcodec_entry_fn)(exec_buf + h->entry_offset);
                    g_ic_buf = exec_buf;
                }
            }
        }
        if (buf) kfree(buf);
    }

    if (!g_ic_entry) return false;

    imgcodec_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = IMGCODEC_CMD_INIT;
    return (g_ic_entry(screen_mgr_get_api(), &p) == 0);
}

bool imgcodec_decode(const uint8_t *data, uint32_t len, int max_w, int max_h,
                     uint32_t **out_pixels, int *out_w, int *out_h, int *out_format) {
    if (!imgcodec_init() || !data || len == 0 || !out_pixels || !out_w || !out_h) return false;

    imgcodec_params_t p;
    memset(&p, 0, sizeof(p));
    p.cmd = IMGCODEC_CMD_DECODE;
    p.data = data;
    p.data_len = len;
    p.max_w = max_w;
    p.max_h = max_h;

    if (g_ic_entry(screen_mgr_get_api(), &p) != 0 || !p.out_pixels) {
        return false;
    }

    *out_pixels = p.out_pixels;
    *out_w = p.out_w;
    *out_h = p.out_h;
    if (out_format) *out_format = p.out_format;
    return true;
}

void imgcodec_free(uint32_t *pixels) {
    if (!pixels) return;
    kfree(pixels);
}
