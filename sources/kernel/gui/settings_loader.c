/**
 * kernel/gui/settings_loader.c
 * Loader client snello per la libreria dinamica out-of-kernel SETTINGS.DLL
 * (C) Newspoint Software - NewSoft Dynamic System Subsystem
 */

#include <settings.h>
#include <screen_modules.h>
#include <gui/screen_mgr.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <string.h>

#include <arch/x86_64/io.h>

static void serial_log(const char* s) {
    if (!s) return;
    while (*s) {
        outb(0x3F8, (uint8_t)(*s++));
    }
}

static settings_entry_fn g_settings_entry = NULL;
static uint8_t* g_settings_module_buf = NULL;

static void try_load_settings_dll(void) {
    if (g_settings_entry) return;
    serial_log("[SETTINGS_LOADER] Inizio ricerca SETTINGS.DLL...\n");

    const char* paths[] = {
        "C:\\NewOS\\SystemLibrary\\SETTINGS.DLL",
        "C:\\NewOS\\Boot\\SETTINGS.DLL",
        "D:\\NewOS\\SystemLibrary\\SETTINGS.DLL",
        "/NewOS/SystemLibrary/SETTINGS.DLL",
        "/NewOS/Boot/SETTINGS.DLL",
        "SETTINGS.DLL"
    };

    uint8_t* mod_buf = NULL;
    uint32_t mod_size = 0;

    for (int i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); i++) {
        serial_log("  [SETTINGS_LOADER] Tentativo: ");
        serial_log(paths[i]);
        mod_buf = (uint8_t*)vfs_read_entire_file(paths[i], &mod_size);
        if (mod_buf && mod_size >= sizeof(module_header_t)) {
            serial_log(" -> TROVATO! Size: ");
            char sbuf[16];
            mini_snprintf(sbuf, sizeof(sbuf), "%u", (unsigned int)mod_size);
            serial_log(sbuf);
            serial_log("\n");
            break;
        } else {
            serial_log(" -> NULL\n");
            if (mod_buf) { kfree(mod_buf); mod_buf = NULL; }
        }
    }

    if (!mod_buf || mod_size < sizeof(module_header_t)) {
        return;
    }

    // Verifica crittografica e integrità del modulo di libreria dinamica
    if (screen_mgr_verify_module(mod_buf, mod_size, MODULE_TYPE_SYSTEM_LIBRARY, "SETTINGS.DLL")) {
        const module_header_t* h = (const module_header_t*)mod_buf;
        g_settings_entry = (settings_entry_fn)(mod_buf + h->entry_offset);
        g_settings_module_buf = mod_buf;
        serial_log("[SETTINGS_LOADER OK] SETTINGS.DLL caricata da VFS, verificata ed eseguita!\n");
        return;
    } else {
        serial_log("[SETTINGS_LOADER ERROR] screen_mgr_verify_module FALLITA per SETTINGS.DLL!\n");
    }

    kfree(mod_buf);
}

void settings_loader_init(void) {
    try_load_settings_dll();
}

bool settings_is_available(void) {
    try_load_settings_dll();
    return (g_settings_entry != NULL);
}

void settings_render(const kernel_api_t* api, int32_t cx, int32_t cy, int32_t cw, int32_t ch,
                     int32_t mx, int32_t my, bool is_glass, uint32_t bg_color,
                     settings_state_t* state, const theme_info_t* cur_theme) {
    try_load_settings_dll();

    if (g_settings_entry) {
        settings_render_params_t p;
        p.cx = cx;
        p.cy = cy;
        p.cw = cw;
        p.ch = ch;
        p.mouse_x = mx;
        p.mouse_y = my;
        p.is_glass = is_glass;
        p.bg_color = bg_color;
        p.state = state;
        p.cur_theme = cur_theme;
        g_settings_entry(api, SETTINGS_CMD_RENDER, &p);
    } else {
        // Fallback minimale se la libreria non è presente
        if (api && api->gfx_fill_rect) {
            api->gfx_fill_rect(cx, cy, cw, ch, 0xF5F7FA);
            api->gfx_draw_string(cx + 20, cy + 30, "Impostazioni di Sistema (NewOS V1)", 0x1A1A1A, 0, true);
            api->gfx_draw_string(cx + 20, cy + 50, "Caricamento libreria dinamica C:\\NewOS\\SystemLibrary\\SETTINGS.DLL in corso...", 0x666666, 0, true);
        }
    }
}

settings_action_t settings_handle_mouse(const kernel_api_t* api, int32_t cx, int32_t cy, int32_t cw, int32_t ch,
                                        int32_t mx, int32_t my, bool clicked, settings_state_t* state) {
    try_load_settings_dll();

    if (g_settings_entry) {
        settings_mouse_params_t p;
        p.cx = cx;
        p.cy = cy;
        p.cw = cw;
        p.ch = ch;
        p.mx = mx;
        p.my = my;
        p.clicked = clicked;
        p.state = state;
        return (settings_action_t)g_settings_entry(api, SETTINGS_CMD_MOUSE, &p);
    }

    return SETTINGS_ACTION_NONE;
}

bool settings_handle_key(const kernel_api_t* api, char key, settings_state_t* state) {
    try_load_settings_dll();

    if (g_settings_entry) {
        settings_key_params_t p;
        p.key = key;
        p.state = state;
        return (g_settings_entry(api, SETTINGS_CMD_KEY, &p) != 0);
    }

    return false;
}
