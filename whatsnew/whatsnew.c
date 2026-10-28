/**
 * whatsnew/whatsnew.c
 * Loader dinamico client per la libreria out-of-kernel WHATSNEW.DLL
 * (C) Newspoint Software - NewSoft Dynamic System Subsystem
 */

#include "whatsnew.h"
#include <gui/wm.h>
#include <gui/graphics.h>
#include <gui/screen_mgr.h>
#include <fs/vfs.h>
#include <mm/heap.h>

typedef int (*whatsnew_entry_fn)(const kernel_api_t* api, window_t* win);

static whatsnew_entry_fn g_whatsnew_entry = NULL;
static uint8_t* g_whatsnew_buf = NULL;

static bool load_whatsnew_dll(void) {
    if (g_whatsnew_entry) return true;

    const char* paths[] = {
        "C:\\NewOS\\SystemLibrary\\WHATSNEW.DLL",
        "D:\\NewOS\\SystemLibrary\\WHATSNEW.DLL",
        "/NewOS/SystemLibrary/WHATSNEW.DLL",
        "WHATSNEW.DLL"
    };

    uint32_t sz = 0;
    for (int i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); i++) {
        g_whatsnew_buf = (uint8_t*)vfs_read_entire_file(paths[i], &sz);
        if (g_whatsnew_buf && sz >= sizeof(module_header_t)) {
            if (screen_mgr_verify_module(g_whatsnew_buf, sz, MODULE_TYPE_SYSTEM_LIBRARY, "WHATSNEW")) {
                const module_header_t* h = (const module_header_t*)g_whatsnew_buf;
                g_whatsnew_entry = (whatsnew_entry_fn)(g_whatsnew_buf + h->entry_offset);
                return true;
            }
            kfree(g_whatsnew_buf);
            g_whatsnew_buf = NULL;
        }
    }
    return false;
}

void app_whatsnew_launch(void) {
    if (!load_whatsnew_dll()) {
        return;
    }

    uint32_t sw = gfx_get_width();
    uint32_t sh = gfx_get_height();

    int32_t dw = 660;
    int32_t dh = 390;
    int32_t dx = (sw - dw) / 2;
    int32_t dy = (sh - dh) / 2;

    window_t* win = wm_create_window("Novita' di NewOS V1 (Aggiornamento)", dx, dy, dw, dh, APP_WHATSNEW);
    if (!win) return;

    g_whatsnew_entry(screen_mgr_get_api(), win);
}
