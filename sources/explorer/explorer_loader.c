/**
 * explorer/explorer_loader.c
 * Loader out-of-kernel per NewOS Explorer (C:\NewOS\SystemLibrary\EXPLORER.DLL)
 */
#include "explorer.h"
#include <fs/vfs.h>
#include <gui/screen_mgr.h>
#include <mm/heap.h>
#include <string.h>

typedef struct {
    uint32_t cmd;
    const char *arg;
} explorer_dll_params_t;

typedef int (*explorer_entry_fn)(const kernel_api_t *api, explorer_dll_params_t *p);
static explorer_entry_fn g_exp_entry = NULL;

static bool load_explorer_dll(void) {
    if (g_exp_entry) return true;
    const char *paths[] = {
        "C:\\NewOS\\SystemLibrary\\EXPLORER.DLL",
        "C:\\NewOS\\Boot\\EXPLORER.DLL",
        "/NewOS/SystemLibrary/EXPLORER.DLL",
        "EXPLORER.DLL"
    };
    uint32_t sz = 0;
    for (int i = 0; i < 4 && !g_exp_entry; i++) {
        uint8_t *buf = (uint8_t *)vfs_read_entire_file(paths[i], &sz);
        if (buf && sz >= sizeof(module_header_t)) {
            if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY, "EXPLORER")) {
                uint8_t *exec_buf = (uint8_t *)kmalloc(sz + 32768);
                if (exec_buf) {
                    memcpy(exec_buf, buf, sz);
                    memset(exec_buf + sz, 0, 32768);
                    const module_header_t *h = (const module_header_t *)exec_buf;
                    g_exp_entry = (explorer_entry_fn)(exec_buf + h->entry_offset);
                }
            }
        }
        if (buf) kfree(buf);
    }
    return (g_exp_entry != NULL);
}

static void call_exp(uint32_t cmd, const char *arg) {
    if (!load_explorer_dll()) return;
    explorer_dll_params_t p = { cmd, arg };
    g_exp_entry(screen_mgr_get_api(), &p);
}

void app_explorer_open_this_pc(void)             { call_exp(1, NULL); }
void app_explorer_open_drive_c(void)             { call_exp(2, NULL); }
void app_explorer_open_drive_d(void)             { call_exp(3, NULL); }
void app_explorer_open_drive_a(void)             { call_exp(4, NULL); }
void app_explorer_open_drive_u(void)             { call_exp(5, NULL); }
void app_explorer_open_folder(const char *f)     { call_exp(6, f); }
void app_explorer_open_recycle_bin(void)         { call_exp(7, NULL); }
void app_control_panel_open(void)                { call_exp(8, NULL); }
void app_settings_open(void)                     { call_exp(9, NULL); }
