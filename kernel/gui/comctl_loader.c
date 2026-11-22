/**
 * kernel/gui/comctl_loader.c
 * Client Loader out-of-kernel per COMCTL.DLL (Libreria Standardizzata Controlli GUI).
 */
#include <fs/vfs.h>
#include <fs/vfs_mount.h>
#include <gui/screen_mgr.h>
#include <mm/heap.h>
#include <string.h>
#include <comctl.h>

typedef struct {
    const comctl_api_t* out_api;
} comctl_init_params_t;

typedef int (*comctl_entry_fn)(const kernel_api_t* api, comctl_init_params_t* params);

static const comctl_api_t* g_comctl_cached_api = NULL;

const comctl_api_t* comctl_get_api(void) {
    if (g_comctl_cached_api) return g_comctl_cached_api;
    if (!vfs_is_drive_mounted('C')) return NULL;

    const char* paths[] = {
        "C:\\NewOS\\SystemLibrary\\COMCTL.DLL",
        "/NewOS/SystemLibrary/COMCTL.DLL"
    };
    comctl_entry_fn entry = NULL;
    uint32_t sz = 0;
    for (int i = 0; i < 2 && !entry; i++) {
        uint8_t* buf = (uint8_t*)vfs_read_entire_file(paths[i], &sz);
        if (buf && sz >= sizeof(module_header_t)) {
            if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY, "COMCTL")) {
                const module_header_t* h = (const module_header_t*)buf;
                uint32_t alloc_sz = (h->code_size > sz ? h->code_size : sz) + 32768;
                uint8_t* exec_buf = (uint8_t*)kmalloc(alloc_sz);
                if (exec_buf) {
                    memcpy(exec_buf, buf, sz);
                    memset(exec_buf + sz, 0, alloc_sz - sz);
                    entry = (comctl_entry_fn)(exec_buf + h->entry_offset);
                }
            }
        }
        if (buf) kfree(buf);
    }
    if (!entry) return NULL;

    comctl_init_params_t p;
    p.out_api = NULL;
    if (entry(screen_mgr_get_api(), &p) == 0 && p.out_api) {
        g_comctl_cached_api = p.out_api;
    }
    return g_comctl_cached_api;
}
