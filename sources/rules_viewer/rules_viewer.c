/**
 * rules_viewer/rules_viewer.c
 * Loader dinamico client per la libreria out-of-kernel RULES.DLL
 * (C) Newspoint Software - NewSoft Dynamic System Subsystem
 */

#include "rules_viewer.h"
#include <gui/wm.h>
#include <gui/graphics.h>
#include <gui/screen_mgr.h>
#include <fs/vfs.h>
#include <fs/registry.h>
#include <mm/heap.h>

typedef int (*rules_entry_fn)(const kernel_api_t* api, window_t* win, const char* filepath);

static rules_entry_fn g_rules_entry = NULL;
static uint8_t* g_rules_buf = NULL;

static bool load_rules_dll(void) {
    if (g_rules_entry) return true;

    const char* paths[] = {
        "C:\\NewOS\\SystemLibrary\\RULES.DLL",
        "D:\\NewOS\\SystemLibrary\\RULES.DLL",
        "/NewOS/SystemLibrary/RULES.DLL",
        "RULES.DLL"
    };

    uint32_t sz = 0;
    for (int i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); i++) {
        g_rules_buf = (uint8_t*)vfs_read_entire_file(paths[i], &sz);
        if (g_rules_buf && sz >= sizeof(module_header_t)) {
            if (screen_mgr_verify_module(g_rules_buf, sz, MODULE_TYPE_SYSTEM_LIBRARY, "RULES")) {
                const module_header_t* h = (const module_header_t*)g_rules_buf;
                g_rules_entry = (rules_entry_fn)(g_rules_buf + h->entry_offset);
                return true;
            }
            kfree(g_rules_buf);
            g_rules_buf = NULL;
        }
    }
    return false;
}

static window_t* rules_viewer_create(const char* filepath) {
    if (registry_is_read_only()) {
        return NULL;
    }

    if (!load_rules_dll()) {
        return NULL;
    }

    uint32_t sw = gfx_get_width();
    uint32_t sh = gfx_get_height();

    int32_t dw = 750;
    int32_t dh = 500;
    if (dw > (int32_t)sw - 20) dw = sw - 20;
    if (dh > (int32_t)sh - 40) dh = sh - 40;

    int32_t dx = (sw - dw) / 2;
    int32_t dy = (sh - dh) / 2;

    window_t* win = wm_create_window("Regole Ufficiali di Sistema NewOS (.NOR)", dx, dy, dw, dh, APP_RULES);
    if (!win) return NULL;

    if (g_rules_entry(screen_mgr_get_api(), win, filepath) != 0) {
        wm_destroy_window(win->id);
        return NULL;
    }

    return win;
}

void app_rules_viewer_launch(void) {
    rules_viewer_create(NULL);
}

void app_rules_viewer_open_file(const char* filepath) {
    rules_viewer_create(filepath);
}
