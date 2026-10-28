/**
 * SystemLibrary/rules.c
 * Libreria Dinamica Out-of-Kernel per NewOS V1 (64-bit)
 * Visualizzatore Ufficiale delle Regole di Sistema NewOS .NOR (RULES.DLL)
 * (C) Newspoint Software - NewSoft Dynamic System Library
 */

#include <screen_modules.h>
#include <rules_viewer.h>
#include <gui/wm.h>
#include <gui/graphics.h>
#include <gui/icons.h>

#ifndef BUILDING_KERNEL_LOADER
// Header di protezione del modulo (esattamente 64 byte all'inizio del file .DLL)
__attribute__((section(".header"), used))
module_header_t g_rules_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size      = 0, // Popolato dal build system
    .entry_offset   = 64, // Posizione dell'entry point
    .checksum       = 0, // Popolato dal build system
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "RULES",
    .signature      = MODULE_AUTH_TAG
};
#endif

static const kernel_api_t* g_api = NULL;

#define MAX_RULES 5

static const char* g_standard_rules[MAX_RULES] = {
    "C:\\NewOS\\Rules\\01_SYSTEM_HIERARCHY.NOR",
    "C:\\NewOS\\Rules\\02_REGISTRY_SECURITY.NOR",
    "C:\\NewOS\\Rules\\03_INSTALL_POLICY.NOR",
    "C:\\NewOS\\Rules\\04_USER_PRIVILEGES.NOR",
    "C:\\NewOS\\Rules\\05_NETWORK_AND_DEVICES.NOR"
};

static const char* g_standard_rules_fallback[MAX_RULES] = {
    "/NewOS/Rules/01_SYSTEM_HIERARCHY.NOR",
    "/NewOS/Rules/02_REGISTRY_SECURITY.NOR",
    "/NewOS/Rules/03_INSTALL_POLICY.NOR",
    "/NewOS/Rules/04_USER_PRIVILEGES.NOR",
    "/NewOS/Rules/05_NETWORK_AND_DEVICES.NOR"
};

static const char* g_rules_titles[MAX_RULES] = {
    "1. Gerarchia File System",
    "2. Sicurezza del Registro",
    "3. Policy Installatori App",
    "4. Privilegi e Sessione",
    "5. Rete e Diagnostica"
};

static const uint8_t NOR_CIPHER_KEY[32] = "NewSoft-NewOS-RuleSecurityKey-26";

typedef struct {
    int          selected_rule;
    nor_header_t header;
    char*        text_content;
    uint32_t     text_len;
    bool         is_loaded;
    bool         is_verified;
    int          scroll_line;
    int          total_lines;
    char         current_path[128];
} rules_viewer_state_t;

static size_t local_strlen(const char* s) {
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

static int local_strncmp(const char* s1, const char* s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static char* local_strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

static const char* local_strstr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char* h = haystack;
            const char* n = needle;
            while (*h && *n && *h == *n) { h++; n++; }
            if (!*n) return haystack;
        }
    }
    return NULL;
}

static uint32_t nor_calc_crc32(const uint8_t* buf, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
    }
    return crc ^ 0xFFFFFFFF;
}

static int count_text_lines(const char* s) {
    if (!s) return 0;
    int lines = 1;
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] == '\n') lines++;
    }
    return lines;
}

static void load_rule_from_buffer(rules_viewer_state_t* state, const uint8_t* raw, uint32_t sz) {
    if (state->text_content && g_api->kfree) {
        g_api->kfree(state->text_content);
        state->text_content = NULL;
    }
    state->is_loaded = false;
    state->is_verified = false;
    state->scroll_line = 0;
    state->total_lines = 0;

    if (!raw || sz < sizeof(nor_header_t)) {
        return;
    }

    const uint8_t* src_hdr = raw;
    uint8_t* dst_hdr = (uint8_t*)&state->header;
    for (size_t i = 0; i < sizeof(nor_header_t); i++) dst_hdr[i] = src_hdr[i];

    if (local_strncmp(state->header.magic, NOR_MAGIC, 4) != 0) {
        return;
    }

    uint32_t psize = state->header.payload_size;
    if (sz < sizeof(nor_header_t) + psize) {
        return;
    }

    const uint8_t* cipher_payload = raw + sizeof(nor_header_t);
    char* plain = (char*)g_api->kmalloc(psize + 1);
    if (!plain) return;

    for (uint32_t i = 0; i < psize; i++) {
        plain[i] = (char)(cipher_payload[i] ^ NOR_CIPHER_KEY[i % 32] ^ (uint8_t)((i * 11 + 0x6A) & 0xFF));
    }
    plain[psize] = '\0';

    uint32_t calc_crc = nor_calc_crc32((const uint8_t*)plain, psize);
    if (calc_crc == state->header.payload_crc32) {
        state->is_verified = true;
    }

    state->text_content = plain;
    state->text_len = psize;
    state->is_loaded = true;
    state->total_lines = count_text_lines(plain);
}

static void load_rule_by_index(rules_viewer_state_t* state, int idx) {
    if (idx < 0 || idx >= MAX_RULES || !g_api) return;
    state->selected_rule = idx;

    uint32_t sz = 0;
    uint8_t* raw = g_api->vfs_read_entire_file(g_standard_rules[idx], &sz);
    if (!raw || sz == 0) {
        raw = g_api->vfs_read_entire_file(g_standard_rules_fallback[idx], &sz);
    }

    if (raw && sz > 0) {
        local_strncpy(state->current_path, g_standard_rules[idx], sizeof(state->current_path) - 1);
        load_rule_from_buffer(state, raw, sz);
        if (g_api->kfree) g_api->kfree(raw);
    }
}

static void load_rule_by_path(rules_viewer_state_t* state, const char* path) {
    if (!path || !g_api) return;
    uint32_t sz = 0;
    uint8_t* raw = g_api->vfs_read_entire_file(path, &sz);
    if (!raw || sz == 0) return;

    local_strncpy(state->current_path, path, sizeof(state->current_path) - 1);
    load_rule_from_buffer(state, raw, sz);
    if (g_api->kfree) g_api->kfree(raw);

    for (int i = 0; i < MAX_RULES; i++) {
        if (local_strstr(path, g_standard_rules[i]) || local_strstr(path, g_standard_rules_fallback[i])) {
            state->selected_rule = i;
            break;
        }
    }
}

static void rules_viewer_draw(window_t* win) {
    if (!g_api) return;
    rules_viewer_state_t* state = (rules_viewer_state_t*)win->app_data;
    if (!state) return;

    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    g_api->gfx_fill_rect(cx, cy, cw, ch, COLOR_BTNFACE);

    int32_t side_w = 210;
    g_api->gfx_fill_rect(cx + 4, cy + 4, side_w, ch - 8, 0xFFFFFF);
    g_api->gfx_draw_bevel_rect(cx + 4, cy + 4, side_w, ch - 8, true);

    g_api->gfx_fill_rect(cx + 6, cy + 6, side_w - 4, 22, 0x0A246A);
    g_api->gfx_draw_icon_16(cx + 10, cy + 9, ICON_DOCUMENT);
    g_api->gfx_draw_string(cx + 32, cy + 10, "Regole NewOS (.NOR)", 0xFFFFFF, 0, true);

    for (int i = 0; i < MAX_RULES; i++) {
        int32_t iy = cy + 34 + i * 32;
        bool is_sel = (state->selected_rule == i);

        if (is_sel) {
            g_api->gfx_fill_rect(cx + 6, iy, side_w - 4, 28, 0x1D4594);
            g_api->gfx_draw_bevel_rect(cx + 6, iy, side_w - 4, 28, true);
            g_api->gfx_draw_icon_16(cx + 10, iy + 6, ICON_INFO);
            g_api->gfx_draw_string(cx + 32, iy + 7, g_rules_titles[i], 0xFFFF00, 0, true);
        } else {
            g_api->gfx_draw_icon_16(cx + 10, iy + 6, ICON_INFO);
            g_api->gfx_draw_string(cx + 32, iy + 7, g_rules_titles[i], 0x000000, 0, true);
        }
    }

    int32_t b_y = cy + ch - 54;
    g_api->gfx_fill_rect(cx + 8, b_y, side_w - 10, 42, 0xEBE9ED);
    g_api->gfx_draw_bevel_rect(cx + 8, b_y, side_w - 10, 42, false);
    g_api->gfx_draw_icon_16(cx + 14, b_y + 12, ICON_REGISTRY);
    g_api->gfx_draw_string(cx + 36, b_y + 6, "Formato: .NOR Protetto", 0x0A246A, 0, true);
    g_api->gfx_draw_string(cx + 36, b_y + 22, "Solo Ambiente Desktop", 0x008000, 0, true);

    int32_t main_x = cx + side_w + 10;
    int32_t main_w = cw - side_w - 14;

    int32_t card_h = 76;
    g_api->gfx_fill_rect(main_x, cy + 4, main_w, card_h, 0xFFFFFF);
    g_api->gfx_draw_bevel_rect(main_x, cy + 4, main_w, card_h, true);

    g_api->gfx_draw_icon_32(main_x + 8, cy + 12, ICON_DOCUMENT);

    if (state->is_loaded) {
        char title_buf[64] = "Regola: ";
        local_strncpy(title_buf + 8, state->header.rule_id, 32);
        g_api->gfx_draw_string(main_x + 48, cy + 10, title_buf, 0x0A246A, 0, true);

        char cat_buf[80] = "Categoria: ";
        local_strncpy(cat_buf + 11, state->header.category, 24);
        g_api->gfx_draw_string(main_x + 48, cy + 28, cat_buf, 0x444444, 0, true);

        char auth_buf[96] = "[STATO: REGOLA VERIFICATA E ATTIVA]";
        g_api->gfx_draw_string(main_x + 48, cy + 46, auth_buf, state->is_verified ? 0x008000 : 0xFF0000, 0, true);
    } else {
        g_api->gfx_draw_string(main_x + 48, cy + 16, "Selezionare un file di regola .NOR dal catalogo", 0x0A246A, 0, true);
        g_api->gfx_draw_string(main_x + 48, cy + 36, "Formato protetto riservato all'Ambiente Desktop NewOS", 0x888888, 0, true);
    }

    int32_t text_y = cy + 4 + card_h + 6;
    int32_t text_h = ch - card_h - 52;
    int32_t scroll_w = 20;
    int32_t text_w = main_w - scroll_w - 2;

    g_api->gfx_fill_rect(main_x, text_y, text_w, text_h, 0xFFFFFF);
    g_api->gfx_draw_bevel_rect(main_x, text_y, text_w, text_h, true);

    int32_t sb_x = main_x + text_w + 2;
    g_api->gfx_fill_rect(sb_x, text_y, scroll_w, text_h, COLOR_BTNFACE);
    g_api->gfx_draw_bevel_rect(sb_x, text_y, scroll_w, text_h, false);

    g_api->gfx_fill_rect(sb_x + 2, text_y + 2, scroll_w - 4, 18, COLOR_BTNFACE);
    g_api->gfx_draw_bevel_rect(sb_x + 2, text_y + 2, scroll_w - 4, 18, false);
    g_api->gfx_draw_string(sb_x + 6, text_y + 4, "^", 0x000000, 0, true);

    g_api->gfx_fill_rect(sb_x + 2, text_y + text_h - 20, scroll_w - 4, 18, COLOR_BTNFACE);
    g_api->gfx_draw_bevel_rect(sb_x + 2, text_y + text_h - 20, scroll_w - 4, 18, false);
    g_api->gfx_draw_string(sb_x + 6, text_y + text_h - 18, "v", 0x000000, 0, true);

    if (state->is_loaded && state->text_content) {
        int max_vis_lines = (text_h - 12) / 16;
        const char* p = state->text_content;

        int cur_l = 0;
        while (*p && cur_l < state->scroll_line) {
            if (*p == '\n') cur_l++;
            p++;
        }

        int line_idx = 0;
        while (*p && line_idx < max_vis_lines) {
            char line_buf[100];
            int c_cnt = 0;
            while (*p && *p != '\n' && *p != '\r' && c_cnt < 98) {
                line_buf[c_cnt++] = *p++;
            }
            line_buf[c_cnt] = '\0';

            uint32_t l_col = 0x000000;
            if (line_buf[0] == '=' || line_buf[0] == '-') l_col = 0x888888;
            else if (local_strstr(line_buf, "REGOLE") || local_strstr(line_buf, "RADICE") || local_strstr(line_buf, "STRUTTURA")) l_col = 0x0A246A;
            else if (local_strstr(line_buf, "CRITICA") || local_strstr(line_buf, "VIETATO")) l_col = 0xA00000;
            else if (local_strstr(line_buf, "->") || local_strstr(line_buf, "1.") || local_strstr(line_buf, "2.")) l_col = 0x005500;

            g_api->gfx_draw_string(main_x + 8, text_y + 8 + line_idx * 16, line_buf, l_col, 0, true);
            line_idx++;

            if (*p == '\r') p++;
            if (*p == '\n') p++;
        }
    }

    int32_t bar_y = cy + ch - 32;
    g_api->gfx_draw_bevel_rect(main_x, bar_y, main_w, 26, true);
    g_api->gfx_draw_string(main_x + 8, bar_y + 6, "Ambiente Desktop Autenticato - Licenza NewOS V1 (C) NewSoft", 0x555555, 0, true);

    int32_t cb_w = 80;
    int32_t cb_x = main_x + main_w - cb_w - 2;
    g_api->gfx_fill_rect(cb_x, bar_y + 2, cb_w, 22, COLOR_BTNFACE);
    g_api->gfx_draw_bevel_rect(cb_x, bar_y + 2, cb_w, 22, false);
    g_api->gfx_draw_string(cb_x + 18, bar_y + 5, "Chiudi", 0x000000, 0, true);
}

static void rules_viewer_mouse(window_t* win, int32_t mx, int32_t my, bool clicked) {
    if (!clicked || !g_api) return;
    rules_viewer_state_t* state = (rules_viewer_state_t*)win->app_data;
    if (!state) return;

    int32_t cx = 4;
    int32_t cy = 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;
    int32_t side_w = 210;

    for (int i = 0; i < MAX_RULES; i++) {
        int32_t iy = cy + 34 + i * 32;
        if (mx >= cx + 6 && mx <= cx + side_w + 2 && my >= iy && my <= iy + 28) {
            load_rule_by_index(state, i);
            return;
        }
    }

    int32_t main_x = cx + side_w + 10;
    int32_t main_w = cw - side_w - 14;
    int32_t card_h = 76;
    int32_t text_y = cy + 4 + card_h + 6;
    int32_t text_h = ch - card_h - 52;
    int32_t scroll_w = 20;
    int32_t text_w = main_w - scroll_w - 2;
    int32_t sb_x = main_x + text_w + 2;

    if (mx >= sb_x && mx <= sb_x + scroll_w && my >= text_y && my <= text_y + 24) {
        if (state->scroll_line > 0) state->scroll_line -= 3;
        if (state->scroll_line < 0) state->scroll_line = 0;
        return;
    }

    if (mx >= sb_x && mx <= sb_x + scroll_w && my >= text_y + text_h - 24 && my <= text_y + text_h) {
        int max_vis_lines = (text_h - 12) / 16;
        if (state->scroll_line + max_vis_lines < state->total_lines) {
            state->scroll_line += 3;
        }
        return;
    }

    int32_t bar_y = cy + ch - 32;
    int32_t cb_w = 80;
    int32_t cb_x = main_x + main_w - cb_w - 2;
    if (mx >= cb_x && mx <= cb_x + cb_w && my >= bar_y && my <= bar_y + 24) {
        if (g_api->wm_destroy_window) {
            g_api->wm_destroy_window(win->id);
        }
    }
}

static void rules_viewer_key(window_t* win, char c) {
    if (!g_api) return;
    rules_viewer_state_t* state = (rules_viewer_state_t*)win->app_data;
    if (!state) return;

    if (c == 27) {
        if (g_api->wm_destroy_window) {
            g_api->wm_destroy_window(win->id);
        }
        return;
    }

    int text_h = win->height - 28 - 76 - 52;
    int max_vis_lines = (text_h - 12) / 16;

    if (c == 'w' || c == 'W' || c == 0x11) {
        if (state->scroll_line > 0) state->scroll_line--;
    } else if (c == 's' || c == 'S' || c == 0x12) {
        if (state->scroll_line + max_vis_lines < state->total_lines) {
            state->scroll_line++;
        }
    }
}

// Entry Point Ufficiale della Libreria Dinamica Out-of-Kernel RULES.DLL
int rules_dll_entry(const kernel_api_t* api, window_t* win, const char* filepath) {
    if (!api || !win) return -1;
    g_api = api;

    if (g_api->registry_is_read_only && g_api->registry_is_read_only()) {
        if (g_api->serial_puts) {
            g_api->serial_puts("[SECURITY ERROR] I file di regola (.NOR) sono protetti e accessibili ESCLUSIVAMENTE in Ambiente Desktop!\n");
        }
        return -1;
    }

    rules_viewer_state_t* state = (rules_viewer_state_t*)g_api->kmalloc(sizeof(rules_viewer_state_t));
    if (!state) return -1;

    uint8_t* p = (uint8_t*)state;
    for (size_t i = 0; i < sizeof(rules_viewer_state_t); i++) p[i] = 0;
    state->selected_rule = 0;

    win->app_data = state;
    win->on_draw = rules_viewer_draw;
    win->on_mouse = rules_viewer_mouse;
    win->on_key = rules_viewer_key;

    if (filepath && filepath[0] != '\0') {
        load_rule_by_path(state, filepath);
    } else {
        load_rule_by_index(state, 0);
    }

    return 0;
}
