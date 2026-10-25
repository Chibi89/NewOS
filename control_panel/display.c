#include "control_panel.h"
#include <gui/wm.h>
#include <gui/graphics.h>
#include <gui/icons.h>
#include <drivers/vga.h>
#include <mm/heap.h>
#include <string.h>

typedef struct {
    int selected_bg;
    int orig_bg;
    int selected_theme;
    int orig_theme;
    int active_tab; // 0 = Sfondo, 1 = Tema, 2 = Risoluzione
    int selected_mode;
    int orig_mode;
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

static void draw_tabs(int32_t cx, int32_t cy, int32_t cw, int active_tab) {
    const char* tab_labels[] = { "  Sfondo  ", "  Tema  ", " Risoluzione " };
    int32_t tx = cx + 8;
    int32_t ty = cy + 4;
    int32_t tw = 88;
    int32_t th = 20;
    for (int i = 0; i < 3; i++) {
        int32_t bx = tx + i * (tw + 4);
        bool sel = (i == active_tab);
        gfx_fill_rect(bx, ty, tw, th + (sel ? 2 : 0), sel ? COLOR_BTNFACE : COLOR_BTNSHADOW);
        gfx_draw_bevel_rect(bx, ty, tw, th, sel);
        gfx_draw_string(bx + 4, ty + 2, tab_labels[i],
                        sel ? COLOR_WINDOWTEXT : COLOR_BTNHIGHLIGHT, 0, true);
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

static void draw_theme_tab(window_t* win, display_state_t* state) {
    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    gfx_draw_string(cx + 18, cy + 36, "Seleziona lo stile visivo:", COLOR_WINDOWTEXT, 0, true);
    const char* theme_names[2] = { "Classic", "Liquid Glass" };
    const char* theme_line1[2] = { "Interfaccia Windows 2000", "Ispirato ad Apple iOS 26" };
    const char* theme_line2[2] = { "Bevel 3D classici", "Vetro frosted, angoli tondati" };
    for (int t = 0; t < 2; t++) {
        int32_t bx = cx + 18;
        int32_t by = cy + 60 + t * 100;
        int32_t bw = cw - 36;
        int32_t bh = 86;
        bool sel = (state->selected_theme == t);
        uint32_t bg_c = (t == 0) ? (sel ? 0xC0D2EC : COLOR_BTNFACE)
                                 : (sel ? 0x1A3060 : 0x0A1830);
        gfx_fill_rect(bx, by, bw, bh, bg_c);
        gfx_draw_bevel_rect(bx, by, bw, bh, sel);
        int32_t px = bx + 8;
        int32_t py = by + 8;
        int32_t pw = 60;
        int32_t ph = 70;
        if (t == 0) {
            gfx_fill_rect(px, py, pw, ph, COLOR_BTNFACE);
            gfx_draw_bevel_rect(px, py, pw, ph, false);
            gfx_draw_gradient_h(px + 2, py + 2, pw - 4, 10, 0x0A246A, 0xA6CAF0);
            gfx_draw_string(px + 4, py + 3, "Classic", 0xFFFFFF, 0, true);
            gfx_fill_rect(px + 2, py + 14, pw - 4, ph - 16, 0xD4D0C8);
        } else {
            gfx_draw_gradient_v(px, py, pw, ph, 0x1A2E5A, 0x3A1A5A);
            gfx_fill_alpha(px, py, pw, 12, 0x4080FF, 120);
            gfx_draw_string(px + 4, py + 2, "Glass", 0xFFFFFF, 0, true);
            gfx_fill_alpha(px, py + 14, pw, ph - 16, 0xFFFFFF, 45);
            gfx_fill_alpha(px, py, pw, 2, 0xFFFFFF, 160);
        }
        int32_t rx = bx + pw + 20;
        int32_t ry = by + 30;
        gfx_fill_rect(rx, ry, 14, 14, sel ? COLOR_BTNFACE : 0xB0B0B0);
        gfx_draw_bevel_rect(rx, ry, 14, 14, true);
        if (sel) gfx_fill_rect(rx + 4, ry + 4, 6, 6, 0x0A246A);
        int32_t ttx = bx + pw + 40;
        uint32_t tc = (t == 1) ? 0xCCE0FF : COLOR_WINDOWTEXT;
        if (sel && t == 0) tc = 0x0A246A;
        gfx_draw_string(ttx, by + 22, theme_names[t], tc, 0, true);
        gfx_draw_string(ttx, by + 38, theme_line1[t], tc, 0, true);
        gfx_draw_string(ttx, by + 52, theme_line2[t], tc, 0, true);
    }
    gfx_draw_string(cx + 18, cy + 270,
        "Nota: Glass funziona meglio con", COLOR_BTNSHADOW, 0, true);
    gfx_draw_string(cx + 18, cy + 284,
        "wallpaper foto (Hero o Flow).", COLOR_BTNSHADOW, 0, true);
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
                  bga_ok ? "VGA.SYS (Bochs/VBox VBE BGA Dynamic Acceleration)" : "VESA Linear Framebuffer Legacy");
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
    int32_t tw = 88;
    int32_t th = 20;

    // Click sui tab
    for (int i = 0; i < 3; i++) {
        int32_t bx = tx + i * (tw + 4);
        if (mx >= bx && mx <= bx + tw && my >= ty && my <= ty + th) {
            state->active_tab = i;
            return;
        }
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
        for (int t = 0; t < 2; t++) {
            int32_t bx = cx + 18;
            int32_t by = cy + 60 + t * 100;
            int32_t bw = cw - 36;
            int32_t bh = 86;
            if (mx >= bx && mx <= bx + bw && my >= by && my <= by + bh) {
                state->selected_theme = t;
                return;
            }
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
    window_t* win = wm_create_window("Proprieta - Schermo", 90, 50, 440, 420, APP_DISPLAY);
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
        win->app_data = state;
        win->on_draw  = display_draw;
        win->on_mouse = display_mouse;
        win->on_key   = display_key;
    }
}
