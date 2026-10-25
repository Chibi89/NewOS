#include "control_panel.h"
#include <gui/wm.h>
#include <gui/graphics.h>
#include <gui/icons.h>
#include <drivers/vga.h>
#include <mm/heap.h>
#include <gui/theme_mgr.h>
#include <string.h>

typedef struct {
    int selected_bg;
    int orig_bg;
    int selected_theme;
    int orig_theme;
    int active_tab; // 0 = Sfondo, 1 = Tema, 2 = Risoluzione
    int selected_mode;
    int orig_mode;
    int selected_theme_idx;
    int theme_scroll_offset;
} display_state_t;

static const char* bg_names[NUM_BACKGROUNDS] = {
    "Teal 2000",
    "Blu Notte NT",
    "Bliss XP",
    "Grigio Ardesia",
    "Mattoni Retro",
    "Circuito Matrix",
    "Windows Hero",
    "Theme Flow",
    "Personalizzato"
};

typedef struct {
    const char* label;
    int32_t width;
} display_tab_def_t;

static const display_tab_def_t g_display_tabs[3] = {
    { "Sfondo",      92 },
    { "Tema",        84 },
    { "Risoluzione", 120 }
};

static void draw_tabs(int32_t cx, int32_t cy, int32_t cw, int active_tab) {
    int32_t tx = cx + 8;
    int32_t ty = cy + 4;
    int32_t th = 22;
    int32_t cur_x = tx;

    for (int i = 0; i < 3; i++) {
        int32_t tw = g_display_tabs[i].width;
        bool sel = (i == active_tab);
        gfx_fill_rect(cur_x, ty, tw, th + (sel ? 2 : 0), sel ? COLOR_BTNFACE : COLOR_BTNSHADOW);
        gfx_draw_bevel_rect(cur_x, ty, tw, th, sel);

        int32_t text_len = (int32_t)strlen(g_display_tabs[i].label);
        int32_t text_w = text_len * 8;
        int32_t text_x = cur_x + (tw - text_w) / 2;
        gfx_draw_string(text_x, ty + 3, g_display_tabs[i].label,
                        sel ? COLOR_WINDOWTEXT : COLOR_BTNHIGHLIGHT, 0, true);
        cur_x += tw + 4;
    }
    gfx_fill_rect(cx + 4, ty + th, cw - 8, 2, COLOR_BTNSHADOW);
    gfx_fill_rect(cx + 4, ty + th + 2, cw - 8, 1, COLOR_BTNHIGHLIGHT);
}

static void draw_bg_tab(window_t* win, display_state_t* state) {
    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    gfx_draw_mini_monitor(cx + (cw - 120) / 2, cy + 30, state->selected_bg);
    int32_t box_x = cx + 14;
    int32_t box_y = cy + 138;
    int32_t box_w = cw - 28;
    int32_t box_h = 175;
    gfx_draw_bevel_rect(box_x, box_y, box_w, box_h, true);
    gfx_fill_rect(box_x + 10, box_y - 6, 120, 12, COLOR_BTNFACE);
    gfx_draw_string(box_x + 12, box_y - 6, " Sfondo Desktop ", COLOR_WINDOWTEXT, 0, true);
    for (int i = 0; i < NUM_BACKGROUNDS; i++) {
        int col = i / 5;
        int row = i % 5;
        int32_t bx = box_x + 10 + col * (box_w / 2);
        int32_t by = box_y + 12 + row * 32;
        int32_t bw = (box_w / 2) - 16;
        int32_t bh = 28;
        bool is_sel = (state->selected_bg == i);
        gfx_fill_rect(bx, by, bw, bh, is_sel ? 0xC0D2EC : COLOR_BTNFACE);
        gfx_draw_bevel_rect(bx, by, bw, bh, is_sel);
        int32_t icon_x = bx + 6;
        int32_t icon_y = by + 6;
        switch (i) {
            case BG_BLUE_NAVY:   gfx_fill_rect(icon_x, icon_y, 16, 16, 0x000080); break;
            case BG_SLATE_GRAY:  gfx_fill_rect(icon_x, icon_y, 16, 16, 0x3A4B53); break;
            case BG_BLISS_XP:
                gfx_draw_gradient_v(icon_x, icon_y, 16, 10, 0x1B58B8, 0x82B2F4);
                gfx_draw_gradient_v(icon_x, icon_y + 10, 16, 6, 0x489C32, 0x245C1A); break;
            case BG_BRICKS:
                gfx_fill_rect(icon_x, icon_y, 16, 16, 0x8A2A18);
                gfx_fill_rect(icon_x, icon_y + 8, 16, 1, 0xC4C0B8); break;
            case BG_MATRIX:
                gfx_fill_rect(icon_x, icon_y, 16, 16, 0x060E08);
                gfx_fill_rect(icon_x + 6, icon_y + 6, 4, 4, 0x1FC044); break;
            case BG_PHOTO_HERO:
                gfx_fill_rect(icon_x, icon_y, 16, 16, 0x001B3A);
                gfx_fill_rect(icon_x + 4, icon_y + 4, 8, 8, 0x00A4EF); break;
            case BG_PHOTO_FLOW:
                gfx_draw_gradient_v(icon_x, icon_y, 16, 8, 0x2A1B4E, 0x7E3A6E);
                gfx_draw_gradient_v(icon_x, icon_y + 8, 16, 8, 0xC45B4A, 0x301B2E); break;
            case BG_CUSTOM:
                gfx_draw_icon_16(icon_x, icon_y, ICON_IMAGE); break;
            default: gfx_fill_rect(icon_x, icon_y, 16, 16, COLOR_DESKTOP); break;
        }
        gfx_draw_bevel_rect(icon_x, icon_y, 16, 16, true);
        gfx_draw_string(bx + 28, by + 6, bg_names[i],
                        is_sel ? 0x0A246A : COLOR_WINDOWTEXT, 0, true);
    }
}

static void draw_bg_pattern_rect(int32_t px, int32_t py, int32_t pw, int32_t ph, int bg_type, uint32_t fallback_color) {
    switch (bg_type) {
        case BG_BLUE_NAVY:
            gfx_fill_rect(px, py, pw, ph, 0x000080);
            break;
        case BG_SLATE_GRAY:
            gfx_fill_rect(px, py, pw, ph, 0x3A4B53);
            break;
        case BG_BLISS_XP: {
            int32_t sky_h = (ph * 65) / 100;
            gfx_draw_gradient_v(px, py, pw, sky_h, 0x1B58B8, 0x82B2F4);
            gfx_draw_gradient_v(px, py + sky_h, pw, ph - sky_h, 0x489C32, 0x245C1A);
            break;
        }
        case BG_BRICKS: {
            gfx_fill_rect(px, py, pw, ph, 0x8A2A18);
            for (int32_t by = py; by < py + ph; by += 4) {
                gfx_fill_rect(px, by, pw, 1, 0xC4C0B8);
            }
            break;
        }
        case BG_MATRIX: {
            gfx_fill_rect(px, py, pw, ph, 0x060E08);
            for (int32_t by = py; by < py + ph; by += 8) {
                gfx_fill_rect(px, by, pw, 1, 0x0D3814);
            }
            break;
        }
        case BG_PHOTO_HERO:
            gfx_draw_gradient_v(px, py, pw, ph, 0x001B3A, 0x003366);
            gfx_fill_rect(px + pw / 4, py + ph / 4, pw / 2, ph / 2, 0x00A4EF);
            break;
        case BG_PHOTO_FLOW: {
            int32_t h1 = (ph * 60) / 100;
            gfx_draw_gradient_v(px, py, pw, h1, 0x1F0D3D, 0x7E2E6E);
            gfx_draw_gradient_v(px, py + h1, pw, ph - h1, 0xC95B4A, 0x200B2A);
            // Arco all'orizzonte
            if (pw > 40 && ph > 30) {
                gfx_fill_rect(px + pw / 2 - 8, py + h1 - 3, 16, 2, 0xFFA500);
            }
            break;
        }
        default:
            gfx_fill_rect(px, py, pw, ph, fallback_color ? fallback_color : 0x008080);
            break;
    }
}

static int get_theme_bg_type(const theme_info_t* th, int fallback_bg) {
    if (!th) return fallback_bg;
    if (strstr(th->filename, "NAVY") || strstr(th->name, "Navy")) return BG_BLUE_NAVY;
    if (strstr(th->filename, "LIQUID") || strstr(th->name, "Liquid")) return BG_PHOTO_FLOW;
    if (strstr(th->filename, "GLASS") || strstr(th->name, "Glass")) return BG_BLISS_XP;
    if (strstr(th->filename, "MICA_DARK") || strstr(th->name, "Mica Dark")) return BG_PHOTO_HERO;
    if (strstr(th->filename, "MICA") || strstr(th->name, "Mica")) return BG_PHOTO_FLOW;
    if (strstr(th->filename, "CLASSIC") || strstr(th->name, "Classic")) return BG_TEAL_2000;
    return fallback_bg;
}

static void draw_hscrollbar(int32_t x, int32_t y, int32_t w, int cur, int total, int visible) {
    int32_t sb_size = 16;
    int32_t tlen = w - 2 * sb_size;
    if (tlen <= 0) return;

    // Freccia Sinistra <
    gfx_fill_rect(x, y, sb_size, sb_size, COLOR_BTNFACE);
    gfx_draw_bevel_rect(x, y, sb_size, sb_size, false);
    gfx_draw_string(x + 4, y + 1, "<", COLOR_WINDOWTEXT, 0, true);

    // Freccia Destra >
    gfx_fill_rect(x + w - sb_size, y, sb_size, sb_size, COLOR_BTNFACE);
    gfx_draw_bevel_rect(x + w - sb_size, y, sb_size, sb_size, false);
    gfx_draw_string(x + w - sb_size + 4, y + 1, ">", COLOR_WINDOWTEXT, 0, true);

    // Traccia scrollbar orizzontale
    int32_t tx = x + sb_size;
    gfx_fill_rect(tx, y, tlen, sb_size, 0xE0DCC0);
    gfx_draw_bevel_rect(tx, y, tlen, sb_size, true);

    // Calcolo thumb
    int max_s = (total > visible) ? (total - visible) : 0;
    int thumb_sz = (total <= visible) ? tlen : (tlen * visible / (total ? total : 1));
    if (thumb_sz < 24) thumb_sz = 24;
    if (thumb_sz > tlen) thumb_sz = tlen;
    int thumb_pos = (max_s > 0) ? (cur * (tlen - thumb_sz) / max_s) : 0;

    // Thumb pulsante 3D
    gfx_fill_rect(tx + thumb_pos, y, thumb_sz, sb_size, COLOR_BTNFACE);
    gfx_draw_bevel_rect(tx + thumb_pos, y, thumb_sz, sb_size, false);

    // Trattini di presa verticali al centro del thumb
    if (thumb_sz >= 28) {
        int32_t grip_x = tx + thumb_pos + (thumb_sz / 2) - 3;
        gfx_fill_rect(grip_x,     y + 4, 1, sb_size - 8, COLOR_BTNSHADOW);
        gfx_fill_rect(grip_x + 2, y + 4, 1, sb_size - 8, COLOR_BTNSHADOW);
        gfx_fill_rect(grip_x + 4, y + 4, 1, sb_size - 8, COLOR_BTNSHADOW);
    }
}

static void draw_palette_icon(int32_t x, int32_t y) {
    // Tavolozza stile Windows 11 (contorno azzurro 0x0078D4 con macchie di colore)
    gfx_draw_rect(x + 2, y + 2, 12, 11, 0x0078D4);
    gfx_draw_pixel(x + 2, y + 2, COLOR_BTNFACE);
    gfx_draw_pixel(x + 13, y + 2, COLOR_BTNFACE);
    gfx_draw_pixel(x + 2, y + 12, COLOR_BTNFACE);
    gfx_draw_pixel(x + 13, y + 12, COLOR_BTNFACE);
    // Macchie di colore all'interno
    gfx_fill_rect(x + 4, y + 4, 3, 3, 0x0078D4); // Blu
    gfx_fill_rect(x + 9, y + 4, 3, 3, 0xE81123); // Rosso
    gfx_fill_rect(x + 4, y + 8, 3, 3, 0xFFB900); // Giallo
    gfx_fill_rect(x + 8, y + 8, 3, 3, 0x107C10); // Verde
}

static void draw_theme_tab(window_t* win, display_state_t* state) {
    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;

    const theme_info_t* sel_th = theme_mgr_get(state->selected_theme_idx);

    // =========================================================================
    // PARTE SUPERIORE: Grande Anteprima Widescreen (Sinistra) + Proprietà (Destra)
    // =========================================================================
    int32_t prev_x = cx + 14;
    int32_t prev_y = cy + 28;
    int32_t prev_w = 184;
    int32_t prev_h = 92;

    // Cornice esterna scura
    gfx_fill_rect(prev_x, prev_y, prev_w, prev_h, 0x111111);
    gfx_draw_rect(prev_x, prev_y, prev_w, prev_h, 0x000000);

    // Sfondo interno panoramico del tema
    int th_bg = get_theme_bg_type(sel_th, state->selected_bg);
    draw_bg_pattern_rect(prev_x + 2, prev_y + 2, prev_w - 4, prev_h - 4,
                         th_bg, sel_th ? sel_th->desktop_color : 0);

    // Finestrella vuota dimostrativa sovrapposta a destra
    if (sel_th) {
        int32_t fw_w = 78;
        int32_t fw_h = 52;
        int32_t fw_x = prev_x + prev_w - fw_w - 14;
        int32_t fw_y = prev_y + 18;

        // Ombra diffusa
        gfx_fill_rect(fw_x + 2, fw_y + 2, fw_w, fw_h, 0x080808);

        // Bordo e corpo finestra
        uint32_t f_border = sel_th->border_color ? sel_th->border_color : 0x606060;
        gfx_fill_rect(fw_x, fw_y, fw_w, fw_h, f_border);
        gfx_fill_rect(fw_x + 1, fw_y + 1, fw_w - 2, fw_h - 2, sel_th->surface_base);

        // Barra del titolo superiore sottile
        uint32_t t_right = sel_th->accent_color ? sel_th->accent_color : sel_th->active_title;
        if (sel_th->active_title != t_right) {
            gfx_draw_gradient_h(fw_x + 2, fw_y + 2, fw_w - 4, 5, sel_th->active_title, t_right);
        } else {
            gfx_fill_rect(fw_x + 2, fw_y + 2, fw_w - 4, 5, sel_th->active_title);
        }

        // Rettangolino d'accento in basso a destra (come nello screenshot di Windows 11)
        int32_t bw = 24;
        int32_t bh = 10;
        int32_t bx = fw_x + fw_w - bw - 8;
        int32_t by = fw_y + fw_h - bh - 8;
        uint32_t acc_col = sel_th->accent_color ? sel_th->accent_color : 0x0078D4;
        gfx_fill_rect(bx, by, bw, bh, acc_col);
        gfx_draw_rect(bx, by, bw, bh, 0x004080);
    }

    // --- Proprietà a Destra (Griglia 2x2 come nello screenshot) ---
    int32_t p_col1 = cx + 214;
    int32_t p_col2 = cx + 374;

    // 1. Sfondo
    gfx_draw_icon_16(p_col1, cy + 30, ICON_IMAGE);
    gfx_draw_string(p_col1 + 24, cy + 28, "Sfondo", 0x000000, 0, true);
    const char* bg_desc = "Contenuti NewOS";
    if (sel_th && strstr(sel_th->filename, "NAVY")) bg_desc = "Blu Notte NT";
    else if (sel_th && strstr(sel_th->filename, "LIQUID")) bg_desc = "Theme Flow";
    else if (sel_th && strstr(sel_th->filename, "GLASS")) bg_desc = "Bliss Glass";
    else if (sel_th && strstr(sel_th->filename, "CLASSIC")) bg_desc = "Teal 2000";
    else if (sel_th && strstr(sel_th->filename, "MICA")) bg_desc = "Windows Mica";
    gfx_draw_string(p_col1 + 24, cy + 44, bg_desc, 0x555555, 0, true);

    // 2. Suoni
    gfx_draw_icon_16(p_col1, cy + 64, ICON_SOUND);
    gfx_draw_string(p_col1 + 24, cy + 62, "Suoni", 0x000000, 0, true);
    gfx_draw_string(p_col1 + 24, cy + 78, "Predefiniti NewOS", 0x555555, 0, true);

    // 3. Colore
    draw_palette_icon(p_col2, cy + 30);
    gfx_draw_string(p_col2 + 24, cy + 28, "Colore", 0x000000, 0, true);
    const char* col_desc = "Blu";
    if (sel_th && strstr(sel_th->filename, "TEAL")) col_desc = "Teal Classico";
    else if (sel_th && strstr(sel_th->filename, "DARK")) col_desc = "Dark Navy";
    else if (sel_th && strstr(sel_th->filename, "LIGHT")) col_desc = "Mica Light";
    else if (sel_th && strstr(sel_th->filename, "CLASSIC")) col_desc = "Teal Classico";
    gfx_draw_string(p_col2 + 24, cy + 44, col_desc, 0x555555, 0, true);

    // 4. Cursore del mouse
    gfx_draw_icon_16(p_col2, cy + 64, ICON_MOUSE);
    gfx_draw_string(p_col2 + 24, cy + 62, "Cursore mouse", 0x000000, 0, true);
    gfx_draw_string(p_col2 + 24, cy + 78, "Breeze", 0x555555, 0, true);

    // Pulsante [ Salva ]
    int32_t save_w = 86;
    int32_t save_h = 24;
    gfx_fill_rect(p_col1, cy + 96, save_w, save_h, 0xFFFFFF);
    gfx_draw_rect(p_col1, cy + 96, save_w, save_h, 0xCCCCCC);
    gfx_draw_string(p_col1 + 24, cy + 100, "Salva", 0x222222, 0, true);

    // =========================================================================
    // PARTE INFERIORE: Box Pannello "Tema corrente" & Galleria Card Orizzontali
    // =========================================================================
    int32_t panel_x = cx + 12;
    int32_t panel_y = cy + 128;
    int32_t panel_w = cw - 24;
    int32_t panel_h = 168;

    // Pannello contenitore bianco pulito stile card Windows 11
    gfx_fill_rect(panel_x, panel_y, panel_w, panel_h, 0xFFFFFF);
    gfx_draw_rect(panel_x, panel_y, panel_w, panel_h, 0xD4D0C8);

    // Intestazione sezione
    gfx_draw_string(panel_x + 14, panel_y + 10, "Tema corrente", 0x000000, 0, true);
    gfx_draw_string(panel_x + panel_w - 142, panel_y + 10, "Personalizzato ^", 0x666666, 0, true);
    gfx_draw_string(panel_x + 14, panel_y + 24,
                    "Scegli una combinazione di sfondi, suoni e colori.",
                    0x777777, 0, true);

    int total_themes = theme_mgr_get_count();
    int visible_cards = 6;
    int max_scroll = (total_themes > visible_cards) ? (total_themes - visible_cards) : 0;
    if (state->theme_scroll_offset > max_scroll) state->theme_scroll_offset = max_scroll;
    if (state->theme_scroll_offset < 0) state->theme_scroll_offset = 0;

    int32_t card_w = 76;
    int32_t card_h = 56;
    int32_t card_gap = 10;
    int32_t total_w = visible_cards * card_w + (visible_cards - 1) * card_gap; // 506px
    int32_t start_x = cx + (cw - total_w) / 2;
    int32_t card_y = panel_y + 42; // cy + 170

    for (int v = 0; v < visible_cards; v++) {
        int t = state->theme_scroll_offset + v;
        if (t >= total_themes) break;
        const theme_info_t* th = theme_mgr_get(t);
        if (!th) continue;

        int32_t kx = start_x + v * (card_w + card_gap);
        bool sel = (state->selected_theme_idx == t);

        // Bordo esterno card (focus ring stile Windows 11 se selezionato)
        if (sel) {
            gfx_draw_rect(kx - 2, card_y - 2, card_w + 4, card_h + 4, 0x0078D4);
            gfx_draw_rect(kx - 1, card_y - 1, card_w + 2, card_h + 2, 0xFFFFFF);
            gfx_draw_rect(kx, card_y, card_w, card_h, 0x0078D4);
        } else {
            gfx_draw_rect(kx, card_y, card_w, card_h, 0xB0B0B0);
        }

        // Sfondo interno della card
        int k_bg = get_theme_bg_type(th, state->selected_bg);
        draw_bg_pattern_rect(kx + 1, card_y + 1, card_w - 2, card_h - 2,
                             k_bg, th->desktop_color);

        // Mini finestrella in sovrimpressione in basso a destra
        int32_t mw_w = 26;
        int32_t mw_h = 20;
        int32_t mw_x = kx + card_w - mw_w - 4;
        int32_t mw_y = card_y + card_h - mw_h - 4;

        gfx_fill_rect(mw_x, mw_y, mw_w, mw_h, th->surface_base);
        gfx_draw_rect(mw_x, mw_y, mw_w, mw_h, 0x404040);

        // Trattino d'accento colorato al centro in basso
        int32_t tw2 = 12;
        int32_t th2 = 3;
        int32_t tx2 = mw_x + (mw_w - tw2) / 2;
        int32_t ty2 = mw_y + mw_h - th2 - 4;
        gfx_fill_rect(tx2, ty2, tw2, th2, th->accent_color ? th->accent_color : th->active_title);
    }

    // Scrollbar orizzontale sotto le card all'interno del pannello
    int32_t sb_x = start_x;
    int32_t sb_y = panel_y + 104; // cy + 232
    int32_t sb_w = total_w;
    draw_hscrollbar(sb_x, sb_y, sb_w, state->theme_scroll_offset, total_themes, visible_cards);

    // Dettaglio tema selezionato all'interno del box
    if (sel_th) {
        char sel_info[128];
        mini_snprintf(sel_info, sizeof(sel_info), "Tema attivo: %s  (%d/%d)",
                      sel_th->name, state->selected_theme_idx + 1, total_themes);
        gfx_draw_string(panel_x + 14, panel_y + 124, sel_info, 0x005A9E, 0, true);
    }

    // Footer informativo all'esterno del box
    char footer_msg[128];
    mini_snprintf(footer_msg, sizeof(footer_msg),
                  "Libreria: %d temi rilevati in NewOS\\Resource\\Themes",
                  total_themes);
    gfx_draw_string(cx + 14, cy + 300, footer_msg, COLOR_BTNSHADOW, 0, true);
}

static void draw_res_tab(window_t* win, display_state_t* state) {
    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;

    // Mini monitor preview
    gfx_draw_mini_monitor(cx + (cw - 120) / 2, cy + 30, wm_get_background_type());

    // Box Risoluzioni
    int32_t box_x = cx + 14;
    int32_t box_y = cy + 138;
    int32_t box_w = cw - 28;
    int32_t box_h = 175;

    gfx_draw_bevel_rect(box_x, box_y, box_w, box_h, true);
    gfx_fill_rect(box_x + 10, box_y - 6, 176, 12, COLOR_BTNFACE);
    gfx_draw_string(box_x + 12, box_y - 6, " Risoluzione dello Schermo ", COLOR_WINDOWTEXT, 0, true);

    bool bga_ok = vga_is_bga_supported();
    if (!bga_ok) {
        vga_init();
        bga_ok = vga_is_bga_supported();
    }

    char info_str[80];
    mini_snprintf(info_str, sizeof(info_str), "Driver Video: %s",
                  bga_ok ? "VGA.SYS (Accelerazione VBE BGA)" : "VESA Framebuffer Legacy");
    gfx_draw_string(box_x + 10, box_y + 10, info_str, 0x0A246A, 0, true);

    int mode_count = vga_get_mode_count();
    for (int i = 0; i < mode_count; i++) {
        const vga_mode_info_t* m = vga_get_mode_info(i);
        if (!m) continue;

        int col = i / 3;
        int row = i % 3;
        int32_t bx = box_x + 10 + col * (box_w / 2);
        int32_t by = box_y + 30 + row * 44;
        int32_t bw = (box_w / 2) - 16;
        int32_t bh = 40;

        bool is_sel = (state->selected_mode == i);
        gfx_fill_rect(bx, by, bw, bh, is_sel ? 0xC0D2EC : COLOR_BTNFACE);
        gfx_draw_bevel_rect(bx, by, bw, bh, is_sel);

        // Radio button
        int32_t rx = bx + 8;
        int32_t ry = by + 13;
        gfx_fill_rect(rx, ry, 14, 14, is_sel ? COLOR_BTNFACE : 0xFFFFFF);
        gfx_draw_bevel_rect(rx, ry, 14, 14, true);
        if (is_sel) {
            gfx_fill_rect(rx + 3, ry + 3, 8, 8, 0x0A246A);
        }

        // Testo risoluzione e formato
        gfx_draw_string(bx + 28, by + 6, m->label, is_sel ? 0x0A246A : COLOR_WINDOWTEXT, 0, true);
        gfx_draw_string(bx + 28, by + 22, m->aspect_ratio, 0x555555, 0, true);
    }
}

static void display_draw(window_t* win) {
    display_state_t* state = (display_state_t*)win->app_data;
    if (!state) return;
    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;
    gfx_fill_rect(cx, cy, cw, ch, COLOR_BTNFACE);
    draw_tabs(cx, cy, cw, state->active_tab);

    if (state->active_tab == 0)      draw_bg_tab(win, state);
    else if (state->active_tab == 1) draw_theme_tab(win, state);
    else                             draw_res_tab(win, state);

    int32_t btn_y = cy + ch - 34;
    if (state->active_tab == 0) {
        // [ Sfoglia... ]
        gfx_fill_rect(cx + 14, btn_y, 76, 24, COLOR_BTNFACE);
        gfx_draw_bevel_rect(cx + 14, btn_y, 76, 24, false);
        gfx_draw_string(cx + 20, btn_y + 4, "Sfoglia...", 0x0A246A, 0, true);

        // [ Suoni... ]
        gfx_fill_rect(cx + 96, btn_y, 68, 24, COLOR_BTNFACE);
        gfx_draw_bevel_rect(cx + 96, btn_y, 68, 24, false);
        gfx_draw_string(cx + 106, btn_y + 4, "Suoni...", COLOR_WINDOWTEXT, 0, true);
    }
    gfx_fill_rect(cx + cw - 230, btn_y, 68, 24, COLOR_BTNFACE);
    gfx_draw_bevel_rect(cx + cw - 230, btn_y, 68, 24, false);
    gfx_draw_string(cx + cw - 208, btn_y + 4, "OK", COLOR_WINDOWTEXT, 0, true);
    gfx_fill_rect(cx + cw - 154, btn_y, 68, 24, COLOR_BTNFACE);
    gfx_draw_bevel_rect(cx + cw - 154, btn_y, 68, 24, false);
    gfx_draw_string(cx + cw - 146, btn_y + 4, "Annulla", COLOR_WINDOWTEXT, 0, true);
    gfx_fill_rect(cx + cw - 78, btn_y, 68, 24, COLOR_BTNFACE);
    gfx_draw_bevel_rect(cx + cw - 78, btn_y, 68, 24, false);
    gfx_draw_string(cx + cw - 70, btn_y + 4, "Applica", COLOR_WINDOWTEXT, 0, true);
}

static void display_mouse(window_t* win, int32_t mx, int32_t my, bool clicked) {
    if (!clicked) return;
    display_state_t* state = (display_state_t*)win->app_data;
    if (!state) return;
    int32_t cx = 4;
    int32_t cy = 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;
    int32_t tx = cx + 8;
    int32_t ty = cy + 4;
    int32_t cur_bx = tx;
    for (int i = 0; i < 3; i++) {
        int32_t tw = g_display_tabs[i].width;
        if (mx >= cur_bx && mx <= cur_bx + tw && my >= ty && my <= ty + 24) {
            state->active_tab = i;
            return;
        }
        cur_bx += tw + 4;
    }

    if (state->active_tab == 0) {
        int32_t box_x = cx + 14;
        int32_t box_y = cy + 138;
        int32_t box_w = cw - 28;
        for (int i = 0; i < NUM_BACKGROUNDS; i++) {
            int col = i / 5;
            int row = i % 5;
            int32_t bx = box_x + 10 + col * (box_w / 2);
            int32_t by = box_y + 12 + row * 32;
            int32_t bw = (box_w / 2) - 16;
            int32_t bh = 28;
            if (mx >= bx && mx <= bx + bw && my >= by && my <= by + bh) {
                state->selected_bg = i;
                return;
            }
        }
        int32_t btn_y2 = cy + ch - 34;
        // Click su Sfoglia... (apre la cartella sfondi)
        if (mx >= cx + 14 && mx <= cx + 90 && my >= btn_y2 && my <= btn_y2 + 24) {
            app_explorer_open_folder("SYSTEM/WALLPAPERS");
            return;
        }
        // Click su Suoni...
        if (mx >= cx + 96 && mx <= cx + 164 && my >= btn_y2 && my <= btn_y2 + 24) {
            app_sound_props_launch();
            return;
        }
    }
    else if (state->active_tab == 1) {
        int total = theme_mgr_get_count();
        int visible_cards = 6;
        int max_scroll = (total > visible_cards) ? (total - visible_cards) : 0;

        int32_t card_w = 76;
        int32_t card_h = 56;
        int32_t card_gap = 10;
        int32_t total_w = visible_cards * card_w + (visible_cards - 1) * card_gap; // 506px
        int32_t start_x = cx + (cw - total_w) / 2;
        int32_t card_y = cy + 170;

        // Click su una card tema nella galleria orizzontale
        for (int v = 0; v < visible_cards; v++) {
            int t = state->theme_scroll_offset + v;
            if (t >= total) break;
            int32_t kx = start_x + v * (card_w + card_gap);
            if (mx >= kx && mx <= kx + card_w && my >= card_y && my <= card_y + card_h) {
                state->selected_theme_idx = t;
                const theme_info_t* th = theme_mgr_get(t);
                if (th) {
                    state->selected_theme = th->theme_id;
                }
                return;
            }
        }

        // Coordinate scrollbar orizzontale
        int32_t sb_x = start_x;
        int32_t sb_y = cy + 232;
        int32_t sb_w = total_w;
        int32_t sb_h = 16;

        // Click su Freccia SINISTRA scrollbar
        if (mx >= sb_x && mx <= sb_x + 16 && my >= sb_y && my <= sb_y + sb_h) {
            if (state->theme_scroll_offset > 0) state->theme_scroll_offset--;
            return;
        }

        // Click su Freccia DESTRA scrollbar
        if (mx >= sb_x + sb_w - 16 && mx <= sb_x + sb_w && my >= sb_y && my <= sb_y + sb_h) {
            if (state->theme_scroll_offset < max_scroll) state->theme_scroll_offset++;
            return;
        }

        // Click sulla traccia scrollbar orizzontale
        if (mx > sb_x + 16 && mx < sb_x + sb_w - 16 && my >= sb_y && my <= sb_y + sb_h) {
            int32_t tlen = sb_w - 32;
            int thumb_sz = (total <= visible_cards) ? tlen : (tlen * visible_cards / (total ? total : 1));
            if (thumb_sz < 24) thumb_sz = 24;
            if (thumb_sz > tlen) thumb_sz = tlen;
            int thumb_pos = (max_scroll > 0) ? (state->theme_scroll_offset * (tlen - thumb_sz) / max_scroll) : 0;
            int click_tx = mx - (sb_x + 16);

            if (click_tx < thumb_pos) {
                if (state->theme_scroll_offset > 0) state->theme_scroll_offset--;
            } else if (click_tx > (thumb_pos + thumb_sz)) {
                if (state->theme_scroll_offset < max_scroll) state->theme_scroll_offset++;
            }
            return;
        }

        // Click sul pulsante [ Salva ] a cx + 214, cy + 96
        if (mx >= cx + 214 && mx <= cx + 300 && my >= cy + 96 && my <= cy + 120) {
            wm_set_theme(state->selected_theme);
            return;
        }
    }
    else if (state->active_tab == 2) {
        int32_t box_x = cx + 14;
        int32_t box_y = cy + 138;
        int32_t box_w = cw - 28;
        int mode_count = vga_get_mode_count();
        for (int i = 0; i < mode_count; i++) {
            int col = i / 3;
            int row = i % 3;
            int32_t bx = box_x + 10 + col * (box_w / 2);
            int32_t by = box_y + 30 + row * 44;
            int32_t bw = (box_w / 2) - 16;
            int32_t bh = 40;
            if (mx >= bx && mx <= bx + bw && my >= by && my <= by + bh) {
                state->selected_mode = i;
                return;
            }
        }
    }

    int32_t btn_y = cy + ch - 34;
    // Tasto [ OK ]
    if (mx >= cx + cw - 230 && mx <= cx + cw - 162 && my >= btn_y && my <= btn_y + 24) {
        wm_set_background_type(state->selected_bg);
        wm_set_theme(state->selected_theme);
        if (state->selected_mode != state->orig_mode) {
            vga_set_mode_index(state->selected_mode);
            state->orig_mode = state->selected_mode;
        }
        wm_destroy_window(win->id);
        return;
    }
    // Tasto [ Annulla ]
    if (mx >= cx + cw - 154 && mx <= cx + cw - 86 && my >= btn_y && my <= btn_y + 24) {
        if (state->orig_mode != vga_get_current_mode_index()) {
            vga_set_mode_index(state->orig_mode);
        }
        wm_set_background_type(state->orig_bg);
        wm_set_theme(state->orig_theme);
        wm_destroy_window(win->id);
        return;
    }
    // Tasto [ Applica ]
    if (mx >= cx + cw - 78 && mx <= cx + cw - 10 && my >= btn_y && my <= btn_y + 24) {
        wm_set_background_type(state->selected_bg);
        wm_set_theme(state->selected_theme);
        if (state->selected_mode != state->orig_mode) {
            vga_set_mode_index(state->selected_mode);
            state->orig_mode = state->selected_mode;
            // Ricentra la finestra Proprietà Schermo nella nuova risoluzione
            win->x = ((int32_t)gfx_get_width() - win->width) / 2;
            win->y = ((int32_t)gfx_get_height() - 28 - win->height) / 2;
            if (win->y < 10) win->y = 10;
        }
        return;
    }
}

static void display_key(window_t* win, char c) {
    display_state_t* state = (display_state_t*)win->app_data;
    if (!state) return;

    if (c == '\t') {
        state->active_tab = (state->active_tab + 1) % 3;
    } else if (state->active_tab == 1) {
        int total = theme_mgr_get_count();
        int visible_cards = 6;
        if (c == 'w' || c == 'W' || c == 'k' || c == 'K' || c == '-' || c == 'a' || c == 'A') {
            if (state->selected_theme_idx > 0) {
                state->selected_theme_idx--;
                const theme_info_t* th = theme_mgr_get(state->selected_theme_idx);
                if (th) state->selected_theme = th->theme_id;
                if (state->selected_theme_idx < state->theme_scroll_offset) {
                    state->theme_scroll_offset = state->selected_theme_idx;
                }
            }
        } else if (c == 's' || c == 'S' || c == 'j' || c == 'J' || c == '+' || c == 'd' || c == 'D' || c == ' ') {
            if (state->selected_theme_idx < total - 1) {
                state->selected_theme_idx++;
                const theme_info_t* th = theme_mgr_get(state->selected_theme_idx);
                if (th) state->selected_theme = th->theme_id;
                if (state->selected_theme_idx >= state->theme_scroll_offset + visible_cards) {
                    state->theme_scroll_offset = state->selected_theme_idx - visible_cards + 1;
                }
            }
        }
    } else if (state->active_tab == 2 && c >= '0' && c <= '5') {
        state->selected_mode = c - '0';
    } else if (state->active_tab == 2 && (c == 'w' || c == 'W' || c == 'a' || c == 'A')) {
        if (state->selected_mode > 0) state->selected_mode--;
    } else if (state->active_tab == 2 && (c == 's' || c == 'S' || c == 'd' || c == 'D')) {
        if (state->selected_mode < VGA_MODE_COUNT - 1) state->selected_mode++;
    } else if (c == '1') {
        state->active_tab = 0;
    } else if (c == '2') {
        state->active_tab = 1;
    } else if (c == '3' || c == 'r' || c == 'R') {
        state->active_tab = 2; // Passa a Risoluzione
    } else if (c == '\n' || c == '\r') {
        // Applica modifiche
        wm_set_background_type(state->selected_bg);
        wm_set_theme(state->selected_theme);
        if (state->selected_mode != state->orig_mode) {
            vga_set_mode_index(state->selected_mode);
            state->orig_mode = state->selected_mode;
            win->x = ((int32_t)gfx_get_width() - win->width) / 2;
            win->y = ((int32_t)gfx_get_height() - 28 - win->height) / 2;
            if (win->y < 10) win->y = 10;
        }
    } else if (c == 27) { // Escape: Annulla
        if (state->orig_mode != vga_get_current_mode_index()) {
            vga_set_mode_index(state->orig_mode);
        }
        wm_set_background_type(state->orig_bg);
        wm_set_theme(state->orig_theme);
        wm_destroy_window(win->id);
    }
}

void app_display_props_launch(void) {
    uint32_t sw = gfx_get_width();
    uint32_t sh = gfx_get_height();
    int32_t win_w = 560;
    int32_t win_h = 460;
    if ((uint32_t)win_w > sw - 20) win_w = (int32_t)(sw - 20);
    if ((uint32_t)win_h > sh - 40) win_h = (int32_t)(sh - 40);
    int32_t win_x = (sw > (uint32_t)win_w) ? (int32_t)(sw - win_w) / 2 : 10;
    int32_t win_y = (sh > (uint32_t)win_h) ? (int32_t)(sh - 28 - win_h) / 2 : 10;
    if (win_y < 10) win_y = 10;

    window_t* win = wm_create_window("Proprieta - Schermo", win_x, win_y, win_w, win_h, APP_DISPLAY);
    if (!win) return;
    display_state_t* state = (display_state_t*)kmalloc(sizeof(display_state_t));
    if (state) {
        state->selected_bg    = wm_get_background_type();
        state->orig_bg        = state->selected_bg;
        state->selected_theme = wm_get_theme();
        state->orig_theme     = state->selected_theme;
        state->active_tab     = 0;
        state->selected_mode  = vga_get_current_mode_index();
        state->orig_mode      = state->selected_mode;
        state->theme_scroll_offset = 0;
        theme_mgr_init("C:/NewOS/Resource/Themes");
        state->selected_theme_idx = theme_mgr_find_by_id(state->selected_theme);
        if (state->selected_theme_idx < 0) state->selected_theme_idx = 0;
        if (state->selected_theme_idx >= 6) {
            state->theme_scroll_offset = state->selected_theme_idx - 5;
        }
        win->app_data = state;
        win->on_draw  = display_draw;
        win->on_mouse = display_mouse;
        win->on_key   = display_key;
    }
}
