/**
 * kernel/gui/theme_loader.c
 * Loader client per la libreria dinamica out-of-kernel THEMEMGR.DLL
 * (C) Newspoint Software - NewSoft Dynamic System Subsystem
 */

#include "theme_mgr.h"
#include "wm.h"
#include "graphics.h"
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
    s_loaded_count = 4;
    memset(s_loaded_themes, 0, sizeof(s_loaded_themes));

    // 0. Windows 2000 Classic
    strncpy(s_loaded_themes[0].filename, "CLASSIC.THM", sizeof(s_loaded_themes[0].filename) - 1);
    strncpy(s_loaded_themes[0].name, "Windows 2000 Classic", sizeof(s_loaded_themes[0].name) - 1);
    strncpy(s_loaded_themes[0].description, "Stile classico Bevel 3D", sizeof(s_loaded_themes[0].description) - 1);
    strncpy(s_loaded_themes[0].author, "Microsoft / NewSoft", sizeof(s_loaded_themes[0].author) - 1);
    strncpy(s_loaded_themes[0].style_engine, "Classic3D", sizeof(s_loaded_themes[0].style_engine) - 1);
    s_loaded_themes[0].desktop_color = 0x008080;
    s_loaded_themes[0].surface_base = 0xD4D0C8;
    s_loaded_themes[0].active_title = 0x0A246A;
    s_loaded_themes[0].active_title_text = 0xFFFFFF;
    s_loaded_themes[0].inactive_title = 0x808080;
    s_loaded_themes[0].inactive_title_text = 0xD4D0C8;
    s_loaded_themes[0].accent_color = 0x0A246A;
    s_loaded_themes[0].border_color = 0x808080;
    s_loaded_themes[0].border_inactive = 0x808080;
    s_loaded_themes[0].btn_face = 0xD4D0C8;
    s_loaded_themes[0].btn_text = 0x000000;
    s_loaded_themes[0].window_bg = 0xFFFFFF;
    s_loaded_themes[0].window_text = 0x000000;
    s_loaded_themes[0].surface_alpha = 255;
    s_loaded_themes[0].taskbar_alpha = 255;
    s_loaded_themes[0].taskbar_color = 0xD4D0C8;
    s_loaded_themes[0].corner_radius = 0;
    s_loaded_themes[0].title_height = 18;
    s_loaded_themes[0].border_width = 2;
    s_loaded_themes[0].drop_shadow = false;
    s_loaded_themes[0].shadow_spread = 0;
    s_loaded_themes[0].specular_rim = false;
    s_loaded_themes[0].modern_controls = false;
    s_loaded_themes[0].control_radius = 0;
    s_loaded_themes[0].button_radius = 0;
    s_loaded_themes[0].tab_radius = 0;
    s_loaded_themes[0].input_radius = 0;
    s_loaded_themes[0].panel_radius = 0;
    s_loaded_themes[0].taskbar_btn_radius = 0;
    s_loaded_themes[0].start_btn_radius = 0;
    s_loaded_themes[0].tray_radius = 0;
    s_loaded_themes[0].border_follows_corner = false;
    s_loaded_themes[0].border_radius = 0;
    s_loaded_themes[0].control_border_follows_radius = false;
    s_loaded_themes[0].button_border_radius = 0;
    s_loaded_themes[0].tab_border_radius = 0;
    s_loaded_themes[0].input_border_radius = 0;
    s_loaded_themes[0].panel_border_radius = 0;
    s_loaded_themes[0].taskbar_border_radius = 0;
    s_loaded_themes[0].start_btn_border_radius = 0;
    s_loaded_themes[0].tray_border_radius = 0;
    s_loaded_themes[0].theme_id = 0;

    // 1. Windows 11 Fluent Glass Light (Alta Trasparenza)
    strncpy(s_loaded_themes[1].filename, "GLASS_LIGHT.THEMES", sizeof(s_loaded_themes[1].filename) - 1);
    strncpy(s_loaded_themes[1].name, "Windows 11 Fluent Glass Light", sizeof(s_loaded_themes[1].name) - 1);
    strncpy(s_loaded_themes[1].description, "Vetro satinato frosted glass ad alta trasparenza (60%)", sizeof(s_loaded_themes[1].description) - 1);
    strncpy(s_loaded_themes[1].author, "Morgan / NewSoft", sizeof(s_loaded_themes[1].author) - 1);
    strncpy(s_loaded_themes[1].style_engine, "FluentGlassLight", sizeof(s_loaded_themes[1].style_engine) - 1);
    s_loaded_themes[1].desktop_color = 0x1E2A38;
    s_loaded_themes[1].surface_base = 0xFFFFFF;
    s_loaded_themes[1].surface_alpha = 155;
    s_loaded_themes[1].active_title = 0xFFFFFF;
    s_loaded_themes[1].active_title_text = 0x1A1A1A;
    s_loaded_themes[1].inactive_title = 0xEFEFEF;
    s_loaded_themes[1].inactive_title_text = 0x707070;
    s_loaded_themes[1].window_bg = 0xFFFFFF;
    s_loaded_themes[1].window_text = 0x1A1A1A;
    s_loaded_themes[1].btn_face = 0xFFFFFF;
    s_loaded_themes[1].btn_text = 0x1A1A1A;
    s_loaded_themes[1].btn_hover = 0xE2E8F0;
    s_loaded_themes[1].border_color = 0xFFFFFF;
    s_loaded_themes[1].border_inactive = 0xD0D0D0;
    s_loaded_themes[1].control_border_color = 0xC8D4E2;
    s_loaded_themes[1].accent_color = 0x0078D4;
    s_loaded_themes[1].accent_text = 0xFFFFFF;
    s_loaded_themes[1].selection_color = 0xC8E0F8;
    s_loaded_themes[1].selection_text = 0x003366;
    s_loaded_themes[1].close_hover_bg = 0xE81123;
    s_loaded_themes[1].taskbar_color = 0xEDEFF5;
    s_loaded_themes[1].taskbar_alpha = 175;
    s_loaded_themes[1].taskbar_height = 28;
    s_loaded_themes[1].taskbar_border = 0xFFFFFF;
    s_loaded_themes[1].start_btn_color = 0xFFFFFF;
    s_loaded_themes[1].start_btn_text = 0x1E1E1E;
    s_loaded_themes[1].active_tab_indicator = 0x0078D4;
    s_loaded_themes[1].tray_pill_color = 0xE4E8F0;
    s_loaded_themes[1].corner_radius = 10;
    s_loaded_themes[1].title_height = 24;
    s_loaded_themes[1].border_width = 1;
    s_loaded_themes[1].drop_shadow = true;
    s_loaded_themes[1].shadow_spread = 8;
    s_loaded_themes[1].specular_rim = false;
    s_loaded_themes[1].modern_controls = true;
    s_loaded_themes[1].control_radius = 5;
    s_loaded_themes[1].button_radius = 5;
    s_loaded_themes[1].tab_radius = 5;
    s_loaded_themes[1].input_radius = 5;
    s_loaded_themes[1].panel_radius = 8;
    s_loaded_themes[1].taskbar_btn_radius = 6;
    s_loaded_themes[1].start_btn_radius = 8;
    s_loaded_themes[1].tray_radius = 6;
    s_loaded_themes[1].border_follows_corner = true;
    s_loaded_themes[1].border_radius = 10;
    s_loaded_themes[1].control_border_follows_radius = true;
    s_loaded_themes[1].button_border_radius = 5;
    s_loaded_themes[1].tab_border_radius = 5;
    s_loaded_themes[1].input_border_radius = 5;
    s_loaded_themes[1].panel_border_radius = 8;
    s_loaded_themes[1].taskbar_border_radius = 6;
    s_loaded_themes[1].start_btn_border_radius = 8;
    s_loaded_themes[1].tray_border_radius = 6;
    s_loaded_themes[1].control_border = true;
    s_loaded_themes[1].theme_id = 1;
    strncpy(s_loaded_themes[1].wallpaper_file, "C:\\NewOS\\Resource\\wallpapers\\FLOW.BMP", sizeof(s_loaded_themes[1].wallpaper_file) - 1);

    // 2. Windows 11 Mica Light
    strncpy(s_loaded_themes[2].filename, "MICA_LIGHT.THEMES", sizeof(s_loaded_themes[2].filename) - 1);
    strncpy(s_loaded_themes[2].name, "Windows 11 Mica Light", sizeof(s_loaded_themes[2].name) - 1);
    strncpy(s_loaded_themes[2].description, "Materiale Mica chiaro semi-opaco (92%)", sizeof(s_loaded_themes[2].description) - 1);
    strncpy(s_loaded_themes[2].author, "Morgan / NewSoft", sizeof(s_loaded_themes[2].author) - 1);
    strncpy(s_loaded_themes[2].style_engine, "MicaLight", sizeof(s_loaded_themes[2].style_engine) - 1);
    s_loaded_themes[2].desktop_color = 0x1E2A38;
    s_loaded_themes[2].surface_base = 0xF8F9FA;
    s_loaded_themes[2].surface_alpha = 235;
    s_loaded_themes[2].active_title = 0xF8F9FA;
    s_loaded_themes[2].active_title_text = 0x1E1E1E;
    s_loaded_themes[2].inactive_title = 0xEAECEF;
    s_loaded_themes[2].inactive_title_text = 0x757575;
    s_loaded_themes[2].window_bg = 0xFFFFFF;
    s_loaded_themes[2].window_text = 0x1E1E1E;
    s_loaded_themes[2].btn_face = 0xF0F2F5;
    s_loaded_themes[2].btn_text = 0x1E1E1E;
    s_loaded_themes[2].btn_hover = 0xE4E8EC;
    s_loaded_themes[2].border_color = 0xE2E2E2;
    s_loaded_themes[2].border_inactive = 0xD5D5D5;
    s_loaded_themes[2].control_border_color = 0xD0D0D5;
    s_loaded_themes[2].accent_color = 0x0078D4;
    s_loaded_themes[2].accent_text = 0xFFFFFF;
    s_loaded_themes[2].selection_color = 0xCCE8FF;
    s_loaded_themes[2].selection_text = 0x004578;
    s_loaded_themes[2].close_hover_bg = 0xE81123;
    s_loaded_themes[2].taskbar_color = 0xF3F3F7;
    s_loaded_themes[2].taskbar_alpha = 225;
    s_loaded_themes[2].taskbar_height = 28;
    s_loaded_themes[2].taskbar_border = 0xE5E5E5;
    s_loaded_themes[2].start_btn_color = 0xEAEAEA;
    s_loaded_themes[2].start_btn_text = 0x1E1E1E;
    s_loaded_themes[2].active_tab_indicator = 0x0078D4;
    s_loaded_themes[2].tray_pill_color = 0xEAEAEA;
    s_loaded_themes[2].corner_radius = 8;
    s_loaded_themes[2].title_height = 22;
    s_loaded_themes[2].border_width = 1;
    s_loaded_themes[2].drop_shadow = true;
    s_loaded_themes[2].shadow_spread = 8;
    s_loaded_themes[2].specular_rim = false;
    s_loaded_themes[2].modern_controls = true;
    s_loaded_themes[2].control_radius = 4;
    s_loaded_themes[2].button_radius = 4;
    s_loaded_themes[2].tab_radius = 4;
    s_loaded_themes[2].input_radius = 4;
    s_loaded_themes[2].panel_radius = 6;
    s_loaded_themes[2].taskbar_btn_radius = 5;
    s_loaded_themes[2].start_btn_radius = 6;
    s_loaded_themes[2].tray_radius = 5;
    s_loaded_themes[2].border_follows_corner = true;
    s_loaded_themes[2].border_radius = 8;
    s_loaded_themes[2].control_border_follows_radius = true;
    s_loaded_themes[2].button_border_radius = 4;
    s_loaded_themes[2].tab_border_radius = 4;
    s_loaded_themes[2].input_border_radius = 4;
    s_loaded_themes[2].panel_border_radius = 6;
    s_loaded_themes[2].taskbar_border_radius = 5;
    s_loaded_themes[2].start_btn_border_radius = 6;
    s_loaded_themes[2].tray_border_radius = 5;
    s_loaded_themes[2].theme_id = 2;
    strncpy(s_loaded_themes[2].wallpaper_file, "C:\\NewOS\\Resource\\wallpapers\\FLOW.BMP", sizeof(s_loaded_themes[2].wallpaper_file) - 1);

    // 3. Windows 11 Mica Dark
    strncpy(s_loaded_themes[3].filename, "MICA_DARK.THEMES", sizeof(s_loaded_themes[3].filename) - 1);
    strncpy(s_loaded_themes[3].name, "Windows 11 Mica Dark", sizeof(s_loaded_themes[3].name) - 1);
    strncpy(s_loaded_themes[3].description, "Materiale Mica scuro per ambienti notturni", sizeof(s_loaded_themes[3].description) - 1);
    strncpy(s_loaded_themes[3].author, "Morgan / NewSoft", sizeof(s_loaded_themes[3].author) - 1);
    strncpy(s_loaded_themes[3].style_engine, "MicaDark", sizeof(s_loaded_themes[3].style_engine) - 1);
    s_loaded_themes[3].desktop_color = 0x0E0E12;
    s_loaded_themes[3].surface_base = 0x202025;
    s_loaded_themes[3].surface_alpha = 238;
    s_loaded_themes[3].active_title = 0x202025;
    s_loaded_themes[3].active_title_text = 0xFFFFFF;
    s_loaded_themes[3].inactive_title = 0x18181C;
    s_loaded_themes[3].inactive_title_text = 0x8A8A8E;
    s_loaded_themes[3].window_bg = 0x25252B;
    s_loaded_themes[3].window_text = 0xEAEAEA;
    s_loaded_themes[3].btn_face = 0x2E2E35;
    s_loaded_themes[3].btn_text = 0xFFFFFF;
    s_loaded_themes[3].btn_hover = 0x3A3A42;
    s_loaded_themes[3].border_color = 0x383840;
    s_loaded_themes[3].border_inactive = 0x28282E;
    s_loaded_themes[3].control_border_color = 0x404048;
    s_loaded_themes[3].accent_color = 0x0078D4;
    s_loaded_themes[3].accent_text = 0xFFFFFF;
    s_loaded_themes[3].selection_color = 0x004578;
    s_loaded_themes[3].selection_text = 0xFFFFFF;
    s_loaded_themes[3].close_hover_bg = 0xE81123;
    s_loaded_themes[3].taskbar_color = 0x1C1C20;
    s_loaded_themes[3].taskbar_alpha = 230;
    s_loaded_themes[3].taskbar_height = 28;
    s_loaded_themes[3].taskbar_border = 0x2A2A30;
    s_loaded_themes[3].start_btn_color = 0x2A2A30;
    s_loaded_themes[3].start_btn_text = 0xFFFFFF;
    s_loaded_themes[3].active_tab_indicator = 0x0078D4;
    s_loaded_themes[3].tray_pill_color = 0x2A2A30;
    s_loaded_themes[3].corner_radius = 8;
    s_loaded_themes[3].title_height = 22;
    s_loaded_themes[3].border_width = 1;
    s_loaded_themes[3].drop_shadow = true;
    s_loaded_themes[3].shadow_spread = 8;
    s_loaded_themes[3].specular_rim = false;
    s_loaded_themes[3].modern_controls = true;
    s_loaded_themes[3].control_radius = 4;
    s_loaded_themes[3].button_radius = 4;
    s_loaded_themes[3].tab_radius = 4;
    s_loaded_themes[3].input_radius = 4;
    s_loaded_themes[3].panel_radius = 6;
    s_loaded_themes[3].taskbar_btn_radius = 5;
    s_loaded_themes[3].start_btn_radius = 6;
    s_loaded_themes[3].tray_radius = 5;
    s_loaded_themes[3].border_follows_corner = true;
    s_loaded_themes[3].border_radius = 8;
    s_loaded_themes[3].control_border_follows_radius = true;
    s_loaded_themes[3].button_border_radius = 4;
    s_loaded_themes[3].tab_border_radius = 4;
    s_loaded_themes[3].input_border_radius = 4;
    s_loaded_themes[3].panel_border_radius = 6;
    s_loaded_themes[3].taskbar_border_radius = 5;
    s_loaded_themes[3].start_btn_border_radius = 6;
    s_loaded_themes[3].tray_border_radius = 5;
    s_loaded_themes[3].theme_id = 3;
    strncpy(s_loaded_themes[3].wallpaper_file, "C:\\NewOS\\Resource\\wallpapers\\FLOW.BMP", sizeof(s_loaded_themes[3].wallpaper_file) - 1);
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
    if (strstr(s_loaded_themes[index].wallpaper_file, "FLOW") || strstr(s_loaded_themes[index].wallpaper_file, "flow")) {
        wm_set_background_type(BG_PHOTO_FLOW);
    } else if (strstr(s_loaded_themes[index].wallpaper_file, "HERO") || strstr(s_loaded_themes[index].wallpaper_file, "hero")) {
        wm_set_background_type(BG_PHOTO_HERO);
    } else if (strstr(s_loaded_themes[index].wallpaper_file, "BLISS") || strstr(s_loaded_themes[index].wallpaper_file, "bliss")) {
        wm_set_background_type(BG_BLISS_XP);
    }
    wm_apply_theme_props(&s_loaded_themes[index]);
    wm_set_theme(s_loaded_themes[index].theme_id);
    return true;
}

void theme_mgr_apply_props(const theme_info_t* theme) {
    if (!theme) return;
    if (strstr(theme->wallpaper_file, "FLOW") || strstr(theme->wallpaper_file, "flow")) {
        wm_set_background_type(BG_PHOTO_FLOW);
    } else if (strstr(theme->wallpaper_file, "HERO") || strstr(theme->wallpaper_file, "hero")) {
        wm_set_background_type(BG_PHOTO_HERO);
    } else if (strstr(theme->wallpaper_file, "BLISS") || strstr(theme->wallpaper_file, "bliss")) {
        wm_set_background_type(BG_BLISS_XP);
    }
    wm_apply_theme_props(theme);
    wm_set_theme(theme->theme_id);
}

const theme_info_t* theme_mgr_get_active(void) {
    return wm_get_current_theme_props();
}

const char* theme_mgr_get_scanned_dir(void) {
    return s_current_scanned_dir;
}

bool theme_mgr_render_script(const char* script, int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint32_t flags) {
    if (!script || !script[0]) return false;
    try_load_dll();
    if (!g_dll_entry) return false;

    theme_mgr_cmd_params_t p;
    memset(&p, 0, sizeof(p));
    p.render_script = script;
    p.render_x = x;
    p.render_y = y;
    p.render_w = w;
    p.render_h = h;
    p.render_radius = radius;
    p.render_flags = flags;

    return (g_dll_entry(screen_mgr_get_api(), THEME_CMD_EXEC_SCRIPT, &p) > 0);
}
