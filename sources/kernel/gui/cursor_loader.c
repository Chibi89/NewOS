/**
 * kernel/gui/cursor_loader.c
 * Loader client out-of-kernel per la libreria dinamica CURSORMGR.DLL
 * Gestione cursori personalizzati, gruppi/schemi e stati di sistema
 * (C) Newspoint Software - NewSoft Dynamic System Subsystem
 */

#include <gui/cursor_loader.h>
#include <gui/screen_mgr.h>
#include <screen_modules.h>
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

static cursormgr_entry_fn g_cursormgr_entry = NULL;
static uint8_t*           g_cursormgr_buf = NULL;
static bool               g_initialized = false;

static cursor_context_t g_cursor_ctx;

static const char* const g_state_names[CURSOR_STATE_COUNT] = {
    "Normal Select",
    "Help Select",
    "Working in Background",
    "Busy",
    "Precision Select",
    "Text Select",
    "Handwriting",
    "Unavailable",
    "Vertical Resize",
    "Horizontal Resize",
    "Diagonal Resize 1",
    "Diagonal Resize 2",
    "Move All",
    "Alternate Select",
    "Link Select",
    "Location Select",
    "Person Select"
};

const char* cursor_mgr_get_state_name(cursor_system_state_t state) {
    if (state < CURSOR_STATE_COUNT) {
        return g_state_names[state];
    }
    return "Unknown";
}

bool cursor_loader_init(void) {
    if (g_initialized) return (g_cursormgr_entry != NULL);
    g_initialized = true;

    serial_log("[CURSOR_LOADER] Ricerca libreria CURSORMGR.DLL...\n");

    const char* paths[] = {
        "C:\\NewOS\\SystemLibrary\\CURSORMGR.DLL",
        "C:\\NewOS\\Boot\\CURSORMGR.DLL",
        "D:\\NewOS\\SystemLibrary\\CURSORMGR.DLL",
        "/NewOS/SystemLibrary/CURSORMGR.DLL",
        "/NewOS/Boot/CURSORMGR.DLL",
        "CURSORMGR.DLL"
    };

    uint8_t* mod_buf = NULL;
    uint32_t mod_size = 0;

    for (int i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); i++) {
        serial_log("  [CURSOR_LOADER] Tentativo: ");
        serial_log(paths[i]);
        mod_buf = (uint8_t*)vfs_read_entire_file(paths[i], &mod_size);
        if (mod_buf && mod_size >= sizeof(module_header_t)) {
            serial_log(" -> TROVATO!\n");
            break;
        } else {
            serial_log(" -> NON TROVATO\n");
            if (mod_buf) { kfree(mod_buf); mod_buf = NULL; }
        }
    }

    if (!mod_buf || mod_size < sizeof(module_header_t)) {
        serial_log("[CURSOR_LOADER] Impossibile caricare CURSORMGR.DLL. Fallback su cursori minimali.\n");
        return false;
    }

    if (!screen_mgr_verify_module(mod_buf, mod_size, MODULE_TYPE_SYSTEM_LIBRARY, "CURSORMGR")) {
        serial_log("[CURSOR_LOADER] Verifica integrita modulo CURSORMGR fallita.\n");
        kfree(mod_buf);
        return false;
    }

    const module_header_t* h = (const module_header_t*)mod_buf;
    g_cursormgr_buf = mod_buf;
    g_cursormgr_entry = (cursormgr_entry_fn)(mod_buf + h->entry_offset);

    const kernel_api_t* api = screen_mgr_get_api();
    cursor_cmd_params_t params;
    memset(&params, 0, sizeof(params));
    params.ctx = &g_cursor_ctx;
    g_cursormgr_entry(api, CURSOR_CMD_INIT, &params);

    // Esegui scansione risorse e sottocartelle
    memset(&params, 0, sizeof(params));
    params.ctx = &g_cursor_ctx;
    params.root_dir = "C:/NewOS/Resource/cursor";
    int out_cnt = 0;
    params.out_count = &out_cnt;

    int scan_res = g_cursormgr_entry(api, CURSOR_CMD_SCAN, &params);
    if (!scan_res || out_cnt == 0) {
        params.root_dir = "C:/NewOS/Resources/cursor";
        g_cursormgr_entry(api, CURSOR_CMD_SCAN, &params);
    }

    serial_log("[CURSOR_LOADER] Inizializzazione completata. Schemi rilevati: ");
    char num_buf[32];
    mini_snprintf(num_buf, sizeof(num_buf), "%d, Singoli: %d\n", g_cursor_ctx.scheme_count, g_cursor_ctx.single_count);
    serial_log(num_buf);

    return true;
}

bool cursor_mgr_draw(int32_t x, int32_t y, cursor_system_state_t state) {
    if (!g_cursormgr_entry) {
        if (!cursor_loader_init()) return false;
    }

    cursor_cmd_params_t params;
    memset(&params, 0, sizeof(params));
    params.ctx = &g_cursor_ctx;
    params.draw_x = x;
    params.draw_y = y;
    params.state_role = state;

    int res = g_cursormgr_entry(screen_mgr_get_api(), CURSOR_CMD_DRAW, &params);
    return (res == 1);
}

int cursor_mgr_get_scheme_count(void) {
    if (!g_cursormgr_entry) cursor_loader_init();
    return g_cursor_ctx.scheme_count;
}

const cursor_scheme_info_t* cursor_mgr_get_scheme_info(int index) {
    if (!g_cursormgr_entry) cursor_loader_init();
    if (index >= 0 && index < g_cursor_ctx.scheme_count) {
        return &g_cursor_ctx.schemes[index];
    }
    return NULL;
}

bool cursor_mgr_set_active_scheme(int index) {
    if (!g_cursormgr_entry) {
        if (!cursor_loader_init()) return false;
    }
    if (index < 0 || index >= g_cursor_ctx.scheme_count) return false;

    cursor_cmd_params_t params;
    memset(&params, 0, sizeof(params));
    params.ctx = &g_cursor_ctx;
    params.scheme_index = index;

    int res = g_cursormgr_entry(screen_mgr_get_api(), CURSOR_CMD_SET_ACTIVE_SCHEME, &params);
    return (res == 1);
}

int cursor_mgr_get_active_scheme_index(void) {
    if (!g_cursormgr_entry) {
        if (!cursor_loader_init()) return 0;
    }
    return g_cursor_ctx.active_scheme_idx;
}

int cursor_mgr_get_single_count(void) {
    if (!g_cursormgr_entry) {
        if (!cursor_loader_init()) return 0;
    }
    return g_cursor_ctx.single_count;
}

const cursor_single_info_t* cursor_mgr_get_single_info(int index) {
    if (!g_cursormgr_entry) {
        if (!cursor_loader_init()) return NULL;
    }
    if (index >= 0 && index < g_cursor_ctx.single_count) {
        return &g_cursor_ctx.single_cursors[index];
    }
    return NULL;
}
