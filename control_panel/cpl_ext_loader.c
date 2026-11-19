/**
 * control_panel/cpl_ext_loader.c
 * Loader dinamico unificato out-of-kernel per le Applet del Pannello di Controllo (.DLL)
 * Carica, verifica con firma crittografica CRC32 ed esegue da C:\NewOS\SystemLibrary\:
 *   - USERACCT.DLL (Account Utente - USERACCT.CPL)
 *   - TIMEDATE.DLL (Data, Ora e Lingua - TIMEDATE.CPL)
 *   - NETCPL.DLL   (Connessioni di Rete - NCPA.CPL)
 *   - KEYBCPL.DLL  (Proprietà Tastiera - KEYBOARD.CPL)
 * (C) Newspoint Software - NewSoft Dynamic System Subsystem
 */

#include "control_panel.h"
#include <gui/wm.h>
#include <gui/graphics.h>
#include <gui/screen_mgr.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <string.h>

typedef int (*cpl_dll_entry_fn)(const kernel_api_t* api, window_t* win);

typedef struct {
    const char*      dll_filename;
    const char*      module_tag;
    const char*      win_title;
    int32_t          win_w;
    int32_t          win_h;
    app_type_t       app_type;
    cpl_dll_entry_fn cached_entry;
    uint8_t*         cached_buf;
} cpl_dll_slot_t;

static cpl_dll_slot_t g_cpl_slots[4] = {
    { "USERACCT.DLL", "USERACCT", "Account Utente e Profilo (USERACCT.CPL)",       470, 400, APP_USERACCT, NULL, NULL },
    { "TIMEDATE.DLL", "TIMEDATE", "Data, Ora e Lingua (TIMEDATE.CPL)",             470, 400, APP_TIMEDATE, NULL, NULL },
    { "NETCPL.DLL",   "NETCPL",   "Connessioni di Rete e Diagnostica (NCPA.CPL)",  480, 410, APP_NETCPL,   NULL, NULL },
    { "KEYBCPL.DLL",  "KEYBCPL",  "Proprieta' Tastiera (KEYBOARD.CPL)",            460, 400, APP_KEYBCPL,  NULL, NULL }
};

static bool load_cpl_dll_slot(cpl_dll_slot_t* slot) {
    if (!slot) return false;
    if (slot->cached_entry) return true;

    const char* prefixes[] = {
        "C:\\NewOS\\SystemLibrary\\",
        "C:\\NewOS\\Boot\\",
        "D:\\NewOS\\SystemLibrary\\",
        "/NewOS/SystemLibrary/",
        "/NewOS/Boot/",
        ""
    };

    char full_path[96];
    uint32_t sz = 0;

    for (int i = 0; i < (int)(sizeof(prefixes) / sizeof(prefixes[0])); i++) {
        mini_snprintf(full_path, sizeof(full_path), "%s%s", prefixes[i], slot->dll_filename);
        uint8_t* buf = (uint8_t*)vfs_read_entire_file(full_path, &sz);
        if (buf && sz >= sizeof(module_header_t)) {
            if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY, slot->module_tag)) {
                const module_header_t* h = (const module_header_t*)buf;
                slot->cached_entry = (cpl_dll_entry_fn)(buf + h->entry_offset);
                slot->cached_buf = buf;
                return true;
            }
            kfree(buf);
        }
    }
    return false;
}

static void launch_cpl_slot(int idx) {
    if (idx < 0 || idx >= 4) return;
    cpl_dll_slot_t* slot = &g_cpl_slots[idx];

    if (!load_cpl_dll_slot(slot)) {
        return;
    }

    uint32_t sw = gfx_get_width();
    uint32_t sh = gfx_get_height();

    int32_t dw = slot->win_w;
    int32_t dh = slot->win_h;
    int32_t dx = ((int32_t)sw - dw) / 2 + (idx * 14);
    int32_t dy = ((int32_t)sh - dh) / 2 + (idx * 10);
    if (dx < 10) dx = 10;
    if (dy < 10) dy = 10;

    window_t* win = wm_create_window(slot->win_title, dx, dy, dw, dh, slot->app_type);
    if (!win) return;

    if (slot->cached_entry(screen_mgr_get_api(), win) != 0) {
        wm_destroy_window(win->id);
    }
}

void app_user_accounts_launch(void) {
    launch_cpl_slot(0);
}

void app_timedate_props_launch(void) {
    launch_cpl_slot(1);
}

void app_net_props_launch(void) {
    launch_cpl_slot(2);
}

void app_keyboard_props_launch(void) {
    launch_cpl_slot(3);
}

typedef struct {
    uint32_t cmd;
} cplext2_params_t;

typedef int (*cplext2_entry_fn)(const kernel_api_t *api, cplext2_params_t *p);
static cplext2_entry_fn g_cplext2_entry = NULL;

static void launch_cplext2(uint32_t cmd) {
    if (!g_cplext2_entry) {
        const char *paths[] = {
            "C:\\NewOS\\SystemLibrary\\CPLEXT2.DLL",
            "C:\\NewOS\\Boot\\CPLEXT2.DLL",
            "/NewOS/SystemLibrary/CPLEXT2.DLL",
            "CPLEXT2.DLL"
        };
        uint32_t sz = 0;
        for (int i = 0; i < 4 && !g_cplext2_entry; i++) {
            uint8_t *buf = (uint8_t *)vfs_read_entire_file(paths[i], &sz);
            if (buf && sz >= sizeof(module_header_t)) {
                if (screen_mgr_verify_module(buf, sz, MODULE_TYPE_SYSTEM_LIBRARY, "CPLEXT2")) {
                    uint8_t *exec_buf = (uint8_t *)kmalloc(sz + 32768);
                    if (exec_buf) {
                        memcpy(exec_buf, buf, sz);
                        memset(exec_buf + sz, 0, 32768);
                        const module_header_t *h = (const module_header_t *)exec_buf;
                        g_cplext2_entry = (cplext2_entry_fn)(exec_buf + h->entry_offset);
                    }
                }
            }
            if (buf) kfree(buf);
        }
    }
    if (!g_cplext2_entry) return;
    cplext2_params_t p = { cmd };
    g_cplext2_entry(screen_mgr_get_api(), &p);
}

void app_about_launch(void)           { launch_cplext2(1); }
void app_display_props_launch(void)   { launch_cplext2(2); }
void app_sound_props_launch(void)     { launch_cplext2(3); }
void app_power_props_launch(void)     { launch_cplext2(4); }
void app_shutdown_dialog_launch(void) { launch_cplext2(5); }
void app_mouse_props_launch(void)     { launch_cplext2(6); }
void app_devmgmt_launch(void)         { launch_cplext2(7); }

