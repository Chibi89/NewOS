/**
 * kernel/apps/seccpl_loader.c
 * Client Loader out-of-kernel per SECCPL.DLL (Centro Sicurezza e Permessi - SECCPL.CPL)
 */
#include <control_panel.h>
#include <fs/registry.h>
#include <fs/vfs.h>
#include <gui/screen_mgr.h>
#include <mm/heap.h>
#include <string.h>

typedef struct {
    bool (*commit_kernel_policy)(const char* subkey, const char* val_name, uint32_t new_val);
} seccpl_params_t;

typedef int (*seccpl_entry_fn)(const kernel_api_t* api, seccpl_params_t* params);

static seccpl_entry_fn g_seccpl_entry = NULL;
static uint8_t*        g_seccpl_buf   = NULL;

void app_seccpl_launch(void) {
    if (!g_seccpl_entry) {
        const char* paths[] = {
            "C:\\NewOS\\SystemLibrary\\SECCPL.DLL",
            "C:\\NewOS\\Boot\\SECCPL.DLL",
            "/NewOS/SystemLibrary/SECCPL.DLL",
            "SECCPL.DLL"
        };
        uint32_t sz = 0;
        for (int i = 0; i < 4 && !g_seccpl_entry; i++) {
            uint8_t* buf = (uint8_t*)vfs_read_entire_file(paths[i], &sz);
            if (buf && sz >= sizeof(module_header_t)) {
                if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY, "SECCPL")) {
                    const module_header_t* h = (const module_header_t*)buf;
                    uint32_t alloc_sz = sz + 32768;
                    if (h->code_size > sz) alloc_sz = h->code_size + 32768;
                    uint8_t* exec_buf = (uint8_t*)kmalloc(alloc_sz);
                    if (exec_buf) {
                        memcpy(exec_buf, buf, sz);
                        memset(exec_buf + sz, 0, alloc_sz - sz);
                        g_seccpl_entry = (seccpl_entry_fn)(exec_buf + h->entry_offset);
                        g_seccpl_buf   = exec_buf;
                    }
                }
            }
            if (buf) kfree(buf);
        }
    }
    if (!g_seccpl_entry) return;

    seccpl_params_t p;
    p.commit_kernel_policy = registry_commit_kernel_policy;
    g_seccpl_entry(screen_mgr_get_api(), &p);
}
