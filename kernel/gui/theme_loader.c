/**
 * kernel/gui/theme_loader.c
 * Loader client per la libreria dinamica out-of-kernel THEMEMGR.DLL
 * (C) Newspoint Software - NewSoft Dynamic System Subsystem
 */

#include "theme_mgr.h"
#include "wm.h"
#include <screen_modules.h>
#include <gui/screen_mgr.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <string.h>

static thememgr_entry_fn g_dll_entry = NULL;
static uint8_t* g_dll_module_buf = NULL;
static bool g_attempted_load = false;

static theme_info_t s_loaded_themes[MAX_SYSTEM_THEMES];
static int s_loaded_count = 0;
static char s_current_scanned_dir[128] = "C:/NewOS/Resource/Themes";

// Fallback di sicurezza incorporati (se THEMEMGR.DLL non è presente)
static void init_fallback_themes(void) {
    s_loaded_count = 2;
    memset(s_loaded_themes, 0, sizeof(s_loaded_themes));

    // 1. Classic 3D
    strncpy(s_loaded_themes[0].filename, "CLASSIC.THM", sizeof(s_loaded_themes[0].filename) - 1);
    strncpy(s_loaded_themes[0].name, "Windows 2000 Classic", sizeof(s_loaded_themes[0].name) - 1);
    strncpy(s_loaded_themes[0].description, "Stile classico Bevel 3D", sizeof(s_loaded_themes[0].description) - 1);
    strncpy(s_loaded_themes[0].author, "Microsoft / NewSoft", sizeof(s_loaded_themes[0].author) - 1);
    strncpy(s_loaded_themes[0].style_engine, "Classic3D", sizeof(s_loaded_themes[0].style_engine) - 1);
    s_loaded_themes[0].desktop_color = 0x008080;
    s_loaded_themes[0].surface_base = 0xD4D0C8;
    s_loaded_themes[0].active_title = 0x0A246A;
    s_loaded_themes[0].accent_color = 0x0A246A;
    s_loaded_themes[0].border_color = 0x808080;
    s_loaded_themes[0].surface_alpha = 255;
    s_loaded_themes[0].corner_radius = 0;
    s_loaded_themes[0].border_width = 2;
    s_loaded_themes[0].drop_shadow = false;
    s_loaded_themes[0].theme_id = 0;

    // 2. Modern Liquid Glass
    strncpy(s_loaded_themes[1].filename, "LIQUID_GLASS.THEMES", sizeof(s_loaded_themes[1].filename) - 1);
    strncpy(s_loaded_themes[1].name, "Liquid Glass", sizeof(s_loaded_themes[1].name) - 1);
    strncpy(s_loaded_themes[1].description, "Vetro traslucido Moderno", sizeof(s_loaded_themes[1].description) - 1);
    strncpy(s_loaded_themes[1].author, "Apple / NewSoft", sizeof(s_loaded_themes[1].author) - 1);
    strncpy(s_loaded_themes[1].style_engine, "Modern", sizeof(s_loaded_themes[1].style_engine) - 1);
    s_loaded_themes[1].desktop_color = 0x2A1B4E;
    s_loaded_themes[1].surface_base = 0x202025;
    s_loaded_themes[1].active_title = 0x0078D4;
    s_loaded_themes[1].accent_color = 0x0078D4;
    s_loaded_themes[1].border_color = 0x404040;
    s_loaded_themes[1].surface_alpha = 220;
    s_loaded_themes[1].corner_radius = 8;
    s_loaded_themes[1].border_width = 1;
    s_loaded_themes[1].drop_shadow = true;
    s_loaded_themes[1].theme_id = 1;
}

static void try_load_dll(void) {
    if (g_dll_entry || g_attempted_load) return;
    g_attempted_load = true;

    const char* paths[] = {
        "C:\\NewOS\\SystemLibrary\\THEMEMGR.DLL",
        "D:\\NewOS\\SystemLibrary\\THEMEMGR.DLL",
        "/NewOS/SystemLibrary/THEMEMGR.DLL",
        "C:\\NewOS\\Resource\\Themes\\THEMEMGR.DLL",
        "THEMEMGR.DLL"
    };

    uint8_t* mod_buf = NULL;
    uint32_t mod_size = 0;

    for (int i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); i++) {
        mod_buf = (uint8_t*)vfs_read_entire_file(paths[i], &mod_size);
        if (mod_buf && mod_size >= sizeof(module_header_t)) {
            break;
        }
    }

    if (!mod_buf || mod_size < sizeof(module_header_t)) {
        return;
    }

    // Verifica crittografica e integrità del modulo di libreria
    if (screen_mgr_verify_module(mod_buf, mod_size, MODULE_TYPE_SYSTEM_LIBRARY, "THEMEMGR.DLL")) {
        const module_header_t* h = (const module_header_t*)mod_buf;
        g_dll_entry = (thememgr_entry_fn)(mod_buf + h->entry_offset);
        g_dll_module_buf = mod_buf;
        return;
    }

    kfree(mod_buf);
}

int theme_mgr_scan(const char* dir_path) {
    const char* path_to_scan = dir_path ? dir_path : "C:/NewOS/Resource/Themes";
    strncpy(s_current_scanned_dir, path_to_scan, sizeof(s_current_scanned_dir) - 1);
    s_current_scanned_dir[sizeof(s_current_scanned_dir) - 1] = '\0';

    try_load_dll();

    if (g_dll_entry) {
        theme_mgr_cmd_params_t p;
        p.dir_path = path_to_scan;
        p.themes = s_loaded_themes;
        p.theme_count = 0;
        p.max_themes = MAX_SYSTEM_THEMES;
        p.sort_order = THEME_SORT_ALPHA;

        int count = g_dll_entry(screen_mgr_get_api(), THEME_CMD_SCAN, &p);
        if (count > 0) {
            s_loaded_count = count;
            return s_loaded_count;
        }
    }

    // Fallback di sicurezza incorporati se la DLL fallisce o non è presente
    if (s_loaded_count == 0) {
        init_fallback_themes();
    }
    return s_loaded_count;
}

int theme_mgr_init(const char* custom_dir) {
    return theme_mgr_scan(custom_dir ? custom_dir : "C:/NewOS/Resource/Themes");
}

void theme_mgr_sort(theme_sort_order_t order) {
    try_load_dll();
    if (g_dll_entry && s_loaded_count > 1) {
        theme_mgr_cmd_params_t p;
        p.dir_path = s_current_scanned_dir;
        p.themes = s_loaded_themes;
        p.theme_count = s_loaded_count;
        p.max_themes = MAX_SYSTEM_THEMES;
        p.sort_order = order;
        g_dll_entry(screen_mgr_get_api(), THEME_CMD_SORT, &p);
    }
}

int theme_mgr_get_count(void) {
    if (s_loaded_count == 0) {
        theme_mgr_init("C:/NewOS/Resource/Themes");
    }
    return s_loaded_count;
}

const theme_info_t* theme_mgr_get(int index) {
    if (s_loaded_count == 0) {
        theme_mgr_init("C:/NewOS/Resource/Themes");
    }
    if (index < 0 || index >= s_loaded_count) return NULL;
    return &s_loaded_themes[index];
}

int theme_mgr_find_by_filename(const char* filename) {
    if (!filename) return -1;
    if (s_loaded_count == 0) theme_mgr_init("C:/NewOS/Resource/Themes");
    for (int i = 0; i < s_loaded_count; i++) {
        if (strcasecmp(s_loaded_themes[i].filename, filename) == 0) return i;
    }
    return -1;
}

int theme_mgr_find_by_id(int theme_id) {
    if (s_loaded_count == 0) theme_mgr_init("C:/NewOS/Resource/Themes");
    for (int i = 0; i < s_loaded_count; i++) {
        if (s_loaded_themes[i].theme_id == theme_id) return i;
    }
    return -1;
}

bool theme_mgr_apply(int index) {
    if (s_loaded_count == 0) theme_mgr_init("C:/NewOS/Resource/Themes");
    if (index < 0 || index >= s_loaded_count) return false;
    wm_set_theme(s_loaded_themes[index].theme_id);
    return true;
}

const char* theme_mgr_get_scanned_dir(void) {
    return s_current_scanned_dir;
}
