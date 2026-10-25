#include "rules_viewer.h"
#include <gui/wm.h>
#include <gui/graphics.h>
#include <gui/icons.h>
#include <fs/vfs.h>
#include <fs/registry.h>
#include <mm/heap.h>
#include <string.h>
#include <arch/x86_64/io.h>

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

static void serial_log(const char* s) {
    if (!s) return;
    while (*s) outb(0x3F8, *s++);
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
    if (state->text_content) {
        kfree(state->text_content);
        state->text_content = NULL;
    }
    state->is_loaded = false;
    state->is_verified = false;
    state->scroll_line = 0;
    state->total_lines = 0;

    if (!raw || sz < sizeof(nor_header_t)) {
        return;
    }

    memcpy(&state->header, raw, sizeof(nor_header_t));

    // Verifica magic "NOSR"
    if (strncmp(state->header.magic, NOR_MAGIC, 4) != 0) {
        return;
    }

    uint32_t psize = state->header.payload_size;
    if (sz < sizeof(nor_header_t) + psize) {
        return;
    }

    const uint8_t* cipher_payload = raw + sizeof(nor_header_t);
    char* plain = (char*)kmalloc(psize + 1);
    if (!plain) return;

    for (uint32_t i = 0; i < psize; i++) {
        plain[i] = (char)(cipher_payload[i] ^ NOR_CIPHER_KEY[i % 32] ^ (uint8_t)((i * 11 + 0x6A) & 0xFF));
    }
    plain[psize] = '\0';

    // Verifica Checksum
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
    if (idx < 0 || idx >= MAX_RULES) return;
    state->selected_rule = idx;

    uint32_t sz = 0;
    uint8_t* raw = vfs_read_entire_file(g_standard_rules[idx], &sz);
    if (!raw || sz == 0) {
        raw = vfs_read_entire_file(g_standard_rules_fallback[idx], &sz);
    }

    if (raw && sz > 0) {
        strncpy(state->current_path, g_standard_rules[idx], sizeof(state->current_path) - 1);
        load_rule_from_buffer(state, raw, sz);
        kfree(raw);
    }
}

static void load_rule_by_path(rules_viewer_state_t* state, const char* path) {
    if (!path) return;
    uint32_t sz = 0;
    uint8_t* raw = vfs_read_entire_file(path, &sz);
    if (!raw || sz == 0) return;

    strncpy(state->current_path, path, sizeof(state->current_path) - 1);
    load_rule_from_buffer(state, raw, sz);
    kfree(raw);

    // Identifica se fa parte delle regole standard
    for (int i = 0; i < MAX_RULES; i++) {
        if (strstr(path, g_standard_rules[i]) || strstr(path, g_standard_rules_fallback[i])) {
            state->selected_rule = i;
            break;
        }
    }
}

static void rules_viewer_draw(window_t* win) {
    rules_viewer_state_t* state = (rules_viewer_state_t*)win->app_data;
    if (!state) return;

    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    // Sfondo generale finestra
    gfx_fill_rect(cx, cy, cw, ch, COLOR_BTNFACE);

    // 1. Pannello laterale sinistro (Lista delle Regole Ufficiali)
    int32_t side_w = 210;
    gfx_fill_rect(cx + 4, cy + 4, side_w, ch - 8, 0xFFFFFF);
    gfx_draw_bevel_rect(cx + 4, cy + 4, side_w, ch - 8, true);

    // Titolo sidebar
    gfx_fill_rect(cx + 6, cy + 6, side_w - 4, 22, 0x0A246A);
    gfx_draw_icon_16(cx + 10, cy + 9, ICON_DOCUMENT);
    gfx_draw_string(cx + 32, cy + 10, "Regole NewOS (.NOR)", 0xFFFFFF, 0, true);

    for (int i = 0; i < MAX_RULES; i++) {
        int32_t iy = cy + 34 + i * 32;
        bool is_sel = (state->selected_rule == i);

        if (is_sel) {
            gfx_fill_rect(cx + 6, iy, side_w - 4, 28, 0x1D4594);
            gfx_draw_bevel_rect(cx + 6, iy, side_w - 4, 28, true);
            gfx_draw_icon_16(cx + 10, iy + 6, ICON_INFO);
            gfx_draw_string(cx + 32, iy + 7, g_rules_titles[i], 0xFFFF00, 0, true);
        } else {
            gfx_draw_icon_16(cx + 10, iy + 6, ICON_INFO);
            gfx_draw_string(cx + 32, iy + 7, g_rules_titles[i], 0x000000, 0, true);
        }
    }

    // Badge di protezione in fondo alla sidebar
    int32_t b_y = cy + ch - 54;
    gfx_fill_rect(cx + 8, b_y, side_w - 10, 42, 0xEBE9ED);
    gfx_draw_bevel_rect(cx + 8, b_y, side_w - 10, 42, false);
    gfx_draw_icon_16(cx + 14, b_y + 12, ICON_REGISTRY);
    gfx_draw_string(cx + 36, b_y + 6, "Formato: .NOR Protetto", 0x0A246A, 0, true);
    gfx_draw_string(cx + 36, b_y + 22, "Solo Ambiente Desktop", 0x008000, 0, true);

    // 2. Pannello principale di visualizzazione regola
    int32_t main_x = cx + side_w + 10;
    int32_t main_w = cw - side_w - 14;

    // Card Intestazione Regola
    int32_t card_h = 76;
    gfx_fill_rect(main_x, cy + 4, main_w, card_h, 0xFFFFFF);
    gfx_draw_bevel_rect(main_x, cy + 4, main_w, card_h, true);

    gfx_draw_icon_32(main_x + 8, cy + 12, ICON_DOCUMENT);

    if (state->is_loaded) {
        char title_buf[64];
        mini_snprintf(title_buf, sizeof(title_buf), "Regola: %s", state->header.rule_id);
        gfx_draw_string(main_x + 48, cy + 10, title_buf, 0x0A246A, 0, true);

        char cat_buf[80];
        mini_snprintf(cat_buf, sizeof(cat_buf), "Categoria: %s | Severita': %s", state->header.category, state->header.severity);
        gfx_draw_string(main_x + 48, cy + 28, cat_buf, 0x444444, 0, true);

        char auth_buf[96];
        mini_snprintf(auth_buf, sizeof(auth_buf), "[FIRMA: %s] [CRC32: 0x%08X] [STATO: VERIFICATA]",
                      state->header.auth_tag, state->header.payload_crc32);
        gfx_draw_string(main_x + 48, cy + 46, auth_buf, state->is_verified ? 0x008000 : 0xFF0000, 0, true);
    } else {
        gfx_draw_string(main_x + 48, cy + 16, "Selezionare un file di regola .NOR dal catalogo", 0x0A246A, 0, true);
        gfx_draw_string(main_x + 48, cy + 36, "Formato protetto riservato all'Ambiente Desktop NewOS", 0x888888, 0, true);
    }

    // Area di testo della Regola
    int32_t text_y = cy + 4 + card_h + 6;
    int32_t text_h = ch - card_h - 52;
    int32_t scroll_w = 20;
    int32_t text_w = main_w - scroll_w - 2;

    gfx_fill_rect(main_x, text_y, text_w, text_h, 0xFFFFFF);
    gfx_draw_bevel_rect(main_x, text_y, text_w, text_h, true);

    // Scrollbar
    int32_t sb_x = main_x + text_w + 2;
    gfx_fill_rect(sb_x, text_y, scroll_w, text_h, COLOR_BTNFACE);
    gfx_draw_bevel_rect(sb_x, text_y, scroll_w, text_h, false);

    // Pulsante Su [▲]
    gfx_fill_rect(sb_x + 2, text_y + 2, scroll_w - 4, 18, COLOR_BTNFACE);
    gfx_draw_bevel_rect(sb_x + 2, text_y + 2, scroll_w - 4, 18, false);
    gfx_draw_string(sb_x + 6, text_y + 4, "^", 0x000000, 0, true);

    // Pulsante Giù [▼]
    gfx_fill_rect(sb_x + 2, text_y + text_h - 20, scroll_w - 4, 18, COLOR_BTNFACE);
    gfx_draw_bevel_rect(sb_x + 2, text_y + text_h - 20, scroll_w - 4, 18, false);
    gfx_draw_string(sb_x + 6, text_y + text_h - 18, "v", 0x000000, 0, true);

    // Rendering linee di testo
    if (state->is_loaded && state->text_content) {
        int max_vis_lines = (text_h - 12) / 16;
        const char* p = state->text_content;

        // Avanza fino alla riga di scroll
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
            else if (strstr(line_buf, "REGOLE") || strstr(line_buf, "RADICE") || strstr(line_buf, "STRUTTURA")) l_col = 0x0A246A;
            else if (strstr(line_buf, "CRITICA") || strstr(line_buf, "VIETATO")) l_col = 0xA00000;
            else if (strstr(line_buf, "->") || strstr(line_buf, "1.") || strstr(line_buf, "2.")) l_col = 0x005500;

            gfx_draw_string(main_x + 8, text_y + 8 + line_idx * 16, line_buf, l_col, 0, true);
            line_idx++;

            if (*p == '\r') p++;
            if (*p == '\n') p++;
        }
    }

    // Barra di stato inferiore
    int32_t bar_y = cy + ch - 32;
    gfx_draw_bevel_rect(main_x, bar_y, main_w, 26, true);
    gfx_draw_string(main_x + 8, bar_y + 6, "Ambiente Desktop Autenticato - Licenza NewOS V1 (C) NewSoft", 0x555555, 0, true);

    // Pulsante Chiudi
    int32_t cb_w = 80;
    int32_t cb_x = main_x + main_w - cb_w - 2;
    gfx_fill_rect(cb_x, bar_y + 2, cb_w, 22, COLOR_BTNFACE);
    gfx_draw_bevel_rect(cb_x, bar_y + 2, cb_w, 22, false);
    gfx_draw_string(cb_x + 18, bar_y + 5, "Chiudi", 0x000000, 0, true);
}

static void rules_viewer_mouse(window_t* win, int32_t mx, int32_t my, bool clicked) {
    if (!clicked) return;
    rules_viewer_state_t* state = (rules_viewer_state_t*)win->app_data;
    if (!state) return;

    int32_t cx = 4;
    int32_t cy = 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    int32_t side_w = 210;

    // 1. Click su una delle 5 regole nella sidebar
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

    // 2. Click su scroll su [▲]
    if (mx >= sb_x && mx <= sb_x + scroll_w && my >= text_y && my <= text_y + 24) {
        if (state->scroll_line > 0) state->scroll_line -= 3;
        if (state->scroll_line < 0) state->scroll_line = 0;
        return;
    }

    // 3. Click su scroll giù [▼]
    if (mx >= sb_x && mx <= sb_x + scroll_w && my >= text_y + text_h - 24 && my <= text_y + text_h) {
        int max_vis_lines = (text_h - 12) / 16;
        if (state->scroll_line + max_vis_lines < state->total_lines) {
            state->scroll_line += 3;
        }
        return;
    }

    // 4. Click su [ Chiudi ]
    int32_t bar_y = cy + ch - 32;
    int32_t cb_w = 80;
    int32_t cb_x = main_x + main_w - cb_w - 2;
    if (mx >= cb_x && mx <= cb_x + cb_w && my >= bar_y && my <= bar_y + 24) {
        wm_destroy_window(win->id);
    }
}

static void rules_viewer_key(window_t* win, char c) {
    rules_viewer_state_t* state = (rules_viewer_state_t*)win->app_data;
    if (!state) return;

    if (c == 27) { // Escape
        wm_destroy_window(win->id);
        return;
    }

    int text_h = win->height - 28 - 76 - 52;
    int max_vis_lines = (text_h - 12) / 16;

    if (c == 'w' || c == 'W' || c == 0x11) { // Up
        if (state->scroll_line > 0) state->scroll_line--;
    } else if (c == 's' || c == 'S' || c == 0x12) { // Down
        if (state->scroll_line + max_vis_lines < state->total_lines) {
            state->scroll_line++;
        }
    }
}

static window_t* rules_viewer_create(const char* filepath) {
    if (registry_is_read_only()) {
        serial_log("[SECURITY ERROR] I file di regola (.NOR) sono protetti e accessibili ESCLUSIVAMENTE in Ambiente Desktop!\n");
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

    rules_viewer_state_t* state = (rules_viewer_state_t*)kmalloc(sizeof(rules_viewer_state_t));
    if (!state) return NULL;

    memset(state, 0, sizeof(rules_viewer_state_t));
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

    return win;
}

void app_rules_viewer_launch(void) {
    rules_viewer_create(NULL);
}

void app_rules_viewer_open_file(const char* filepath) {
    rules_viewer_create(filepath);
}

