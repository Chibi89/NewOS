/**
 * SystemLibrary/fontscpl.c
 * Libreria Dinamica Out-of-Kernel FONTSCPL.DLL (Applet Caratteri e Font TrueType - FONTS.CPL)
 * Scansione dinamica C:\NewOS\Resources\Fonts\, anteprima TrueType live tramite FONTMGR.DLL,
 * selezione font di sistema, modalita' rendering (Bitmap / Anti-Aliasing / ClearType) e Registro.
 * (C) Newspoint Software - NewSoft Dynamic System Library
 */

#include "cpl_dll_ui.h"
#include <kernel_ext_api.h>
#include <fs/vfs.h>

#ifndef BUILDING_KERNEL_LOADER
__attribute__((section(".header"), used))
module_header_t g_fontscpl_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size      = 0,
    .entry_offset   = 64,
    .checksum       = 0,
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "FONTSCPL",
    .signature      = MODULE_AUTH_TAG
};
#endif

#define MAX_FONT_FILES 24

static const kernel_api_t* g_api = NULL;

typedef struct {
    char fonts[MAX_FONT_FILES][40];
    int  num_fonts;
    int  selected_idx;
    int  scroll_off;
    int  render_mode; // 0: Bitmap 8x16, 1: TrueType AA, 2: ClearType Subpixel
    int  base_size;   // 12, 14, 16, 18
    char active_font[40];
    char status_msg[64];
} fontscpl_state_t;

static bool ends_with_ttf(const char* name) {
    size_t len = dll_strlen(name);
    if (len < 5) return false;
    const char* ext = name + len - 4;
    return (ext[0] == '.' &&
            (ext[1] == 't' || ext[1] == 'T') &&
            (ext[2] == 't' || ext[2] == 'T') &&
            (ext[3] == 'f' || ext[3] == 'F'));
}

static void fontscpl_scan_fonts(fontscpl_state_t* st) {
    st->num_fonts = 0;
    if (g_api && g_api->vfs_readdir) {
        vfs_dirent_t entries[48];
        int32_t cnt = g_api->vfs_readdir("C:/NewOS/Resources/Fonts", entries, 48);
        for (int32_t i = 0; i < cnt && st->num_fonts < MAX_FONT_FILES; i++) {
            if (!entries[i].is_directory && ends_with_ttf(entries[i].name)) {
                dll_strcpy(st->fonts[st->num_fonts], entries[i].name, sizeof(st->fonts[0]));
                st->num_fonts++;
            }
        }
    }
    if (st->num_fonts == 0) {
        dll_strcpy(st->fonts[st->num_fonts++], "SANS.TTF", 40);
        dll_strcpy(st->fonts[st->num_fonts++], "segoeui.ttf", 40);
        dll_strcpy(st->fonts[st->num_fonts++], "arial.ttf", 40);
        dll_strcpy(st->fonts[st->num_fonts++], "tahoma.ttf", 40);
        dll_strcpy(st->fonts[st->num_fonts++], "verdana.ttf", 40);
    }
    st->selected_idx = 0;
    for (int i = 0; i < st->num_fonts; i++) {
        if (st->fonts[i][0] == 'S' && st->fonts[i][1] == 'A' &&
            st->fonts[i][2] == 'N' && st->fonts[i][3] == 'S') {
            st->selected_idx = i;
            break;
        }
    }
}

static void draw_ttf_preview_line(int32_t x, int32_t y, int32_t max_x,
                                  int px_h, const char* text, uint32_t col) {
    if (!g_api || !g_api->fontmgr_get_ttf_glyph || !g_api->gfx_fill_alpha) {
        if (g_api && g_api->gfx_draw_string)
            g_api->gfx_draw_string(x, y, text, col, 0, true);
        return;
    }
    uint8_t bmp[48 * 48];
    int32_t cx = x;
    int32_t baseline = y + px_h;
    for (int i = 0; text[i]; i++) {
        uint32_t cp = (uint8_t)text[i];
        int w = 0, h = 0, xoff = 0, yoff = 0, adv = 0;
        if (g_api->fontmgr_get_ttf_glyph(cp, px_h, bmp, sizeof(bmp),
                                         &w, &h, &xoff, &yoff, &adv)) {
            if (cx + adv > max_x) break;
            for (int row = 0; row < h; row++) {
                for (int c = 0; c < w; c++) {
                    uint8_t a = bmp[row * w + c];
                    if (a > 12) {
                        g_api->gfx_fill_alpha(cx + xoff + c, baseline + yoff + row, 1, 1, col, a);
                    }
                }
            }
            cx += adv;
        } else {
            cx += px_h / 2;
        }
    }
}

static void fontscpl_apply_selected(fontscpl_state_t* st) {
    if (!g_api || !st || st->selected_idx < 0 || st->selected_idx >= st->num_fonts)
        return;

    const char* fname = st->fonts[st->selected_idx];
    char full_path[96];
    dll_strcpy(full_path, "C:\\NewOS\\Resources\\Fonts\\", sizeof(full_path));
    size_t base_len = dll_strlen(full_path);
    dll_strcpy(full_path + base_len, fname, sizeof(full_path) - base_len);

    bool ok = false;
    const kernel_ext_api_t* ext = (const kernel_ext_api_t*)g_api->ext_api;
    if (ext && ext->fontmgr_set_ttf) {
        ok = ext->fontmgr_set_ttf(full_path);
    }
    if (ok) {
        dll_strcpy(st->active_font, fname, sizeof(st->active_font));
    }

    if (g_api->registry_set_value_sz) {
        g_api->registry_set_value_sz("HKEY_CURRENT_USER\\Control Panel\\Desktop",
                                     "SystemFontFile", full_path);
    }
    if (g_api->registry_set_value_dword) {
        g_api->registry_set_value_dword("HKEY_CURRENT_USER\\Control Panel\\Desktop",
                                        "FontRenderMode", (uint32_t)st->render_mode);
        g_api->registry_set_value_dword("HKEY_CURRENT_USER\\Control Panel\\Desktop",
                                        "FontBaseSize", (uint32_t)st->base_size);
    }
    if (g_api->registry_save_to_disk) {
        g_api->registry_save_to_disk();
    }

    if (ok) {
        dll_strcpy(st->status_msg, "Font TrueType applicato e salvato!", sizeof(st->status_msg));
    } else {
        dll_strcpy(st->status_msg, "Preferenze font salvate nel Registro.", sizeof(st->status_msg));
    }
}

static void fc_draw_card(int32_t x, int32_t y, int32_t w, int32_t h, const dll_theme_t* t) {
    if (!g_api) return;
    if (t->is_glass) {
        g_api->gfx_fill_rounded_alpha(x, y, w, h, 8, t->card_bg, 210);
        g_api->gfx_draw_rounded_rect(x, y, w, h, 8, t->card_border);
    } else {
        g_api->gfx_fill_rect(x, y, w, h, t->card_bg);
        g_api->gfx_draw_bevel_rect(x, y, w, h, false);
    }
}

static void fc_draw_button(int32_t x, int32_t y, int32_t w, int32_t h, const char* lbl, bool primary, const dll_theme_t* t) {
    if (!g_api) return;
    if (t->is_glass) {
        g_api->gfx_fill_rounded_alpha(x, y, w, h, 6, primary ? t->accent : t->card_bg, primary ? 235 : 215);
        g_api->gfx_draw_rounded_rect(x, y, w, h, 6, primary ? t->accent : t->card_border);
    } else {
        g_api->gfx_fill_rect(x, y, w, h, primary ? t->accent : t->card_bg);
        g_api->gfx_draw_bevel_rect(x, y, w, h, false);
    }
    int32_t tx = x + (w - (int32_t)dll_strlen(lbl) * 8) / 2;
    if (tx < x + 4) tx = x + 4;
    g_api->gfx_draw_string(tx, y + (h - 14) / 2, lbl, primary ? t->accent_text : t->text_primary, 0, true);
}

static void fontscpl_draw(window_t* win) {
    fontscpl_state_t* st = (fontscpl_state_t*)win->app_data;
    if (!st || !g_api) return;

    dll_theme_t t;
    dll_theme_init(g_api, &t);

    int32_t cx = win->x + 12;
    int32_t cy = win->y + 32;
    int32_t cw = win->width - 24;
    int32_t ch = win->height - 44;

    fc_draw_card(cx, cy, cw, 44, &t);
    g_api->gfx_draw_icon_32(cx + 8, cy + 6, ICON_FONT);
    g_api->gfx_draw_string(cx + 48, cy + 8, "Gestione Caratteri e Font TrueType di Sistema", t.text_primary, 0, true);
    char sub[72];
    dll_strcpy(sub, "Font attivo: ", sizeof(sub));
    size_t sl = dll_strlen(sub);
    dll_strcpy(sub + sl, st->active_font[0] ? st->active_font : "SANS.TTF", sizeof(sub) - sl);
    g_api->gfx_draw_string(cx + 48, cy + 24, sub, t.accent, 0, true);

    // Pannello sinistro: Elenco Font TrueType
    int32_t list_x = cx;
    int32_t list_y = cy + 52;
    int32_t list_w = 175;
    int32_t list_h = ch - 96;
    fc_draw_card(list_x, list_y, list_w, list_h, &t);
    g_api->gfx_draw_string(list_x + 8, list_y + 6, "Caratteri (.TTF):", t.text_secondary, 0, true);

    int vis_rows = (list_h - 30) / 20;
    if (vis_rows < 1) vis_rows = 1;
    for (int r = 0; r < vis_rows; r++) {
        int idx = st->scroll_off + r;
        if (idx >= st->num_fonts) break;
        int32_t ry = list_y + 24 + r * 20;
        bool sel = (idx == st->selected_idx);
        if (sel) {
            g_api->gfx_fill_rect(list_x + 6, ry, list_w - 12, 18, t.accent);
        }
        char short_n[22];
        dll_strcpy(short_n, st->fonts[idx], sizeof(short_n));
        g_api->gfx_draw_string(list_x + 12, ry + 2, short_n,
                               sel ? t.accent_text : t.text_primary, 0, true);
    }

    // Pannello destro: Anteprima Live TrueType + Opzioni
    int32_t prev_x = list_x + list_w + 8;
    int32_t prev_w = cw - list_w - 8;
    int32_t prev_h = list_h;
    fc_draw_card(prev_x, list_y, prev_w, prev_h, &t);
    g_api->gfx_draw_string(prev_x + 10, list_y + 6, "Anteprima TrueType Live:", t.text_secondary, 0, true);

    int32_t pv_box_y = list_y + 26;
    g_api->gfx_fill_rect(prev_x + 10, pv_box_y, prev_w - 20, 118, 0xFFFFFF);
    g_api->gfx_draw_rect(prev_x + 10, pv_box_y, prev_w - 20, 118, t.card_border);

    int32_t pv_max_x = prev_x + prev_w - 16;
    draw_ttf_preview_line(prev_x + 16, pv_box_y + 4,  pv_max_x, 14, "NewOS V1 64-bit TrueType (14px)", 0x1E293B);
    draw_ttf_preview_line(prev_x + 16, pv_box_y + 28, pv_max_x, 20, "Abcdefghilmnopq 012345 (20px)", 0x0F172A);
    draw_ttf_preview_line(prev_x + 16, pv_box_y + 62, pv_max_x, 28, "NewOS Font Demo (28px)", 0x1D4ED8);

    // Selettore Modalita' Rendering
    int32_t opt_y = pv_box_y + 128;
    g_api->gfx_draw_string(prev_x + 10, opt_y, "Modalita' Rendering:", t.text_primary, 0, true);
    const char* modes[3] = { "Bitmap 8x16", "TrueType AA", "ClearType" };
    for (int m = 0; m < 3; m++) {
        int32_t bx = prev_x + 10 + m * 106;
        int32_t by = opt_y + 18;
        bool act = (st->render_mode == m);
        fc_draw_button(bx, by, 100, 22, modes[m], act, &t);
    }

    // Selettore Dimensione Base
    int32_t sz_y = opt_y + 48;
    g_api->gfx_draw_string(prev_x + 10, sz_y, "Dimensione Base:", t.text_primary, 0, true);
    const int sizes[4] = { 12, 14, 16, 18 };
    const char* size_lbls[4] = { "12 px", "14 px", "16 px", "18 px" };
    for (int s = 0; s < 4; s++) {
        int32_t bx = prev_x + 10 + s * 78;
        int32_t by = sz_y + 18;
        fc_draw_button(bx, by, 72, 22, size_lbls[s], (st->base_size == sizes[s]), &t);
    }

    // Messaggio di stato + Bottoni azione in basso
    int32_t foot_y = cy + ch - 32;
    if (st->status_msg[0] != '\0') {
        g_api->gfx_draw_string(cx + 4, foot_y + 6, st->status_msg, t.accent, 0, true);
    }
    fc_draw_button(cx + cw - 204, foot_y, 114, 24, "Applica Font", true, &t);
    fc_draw_button(cx + cw - 84,  foot_y, 84,  24, "Chiudi", false, &t);
}

static void fontscpl_mouse(window_t* win, int32_t lx, int32_t ly, bool clicked) {
    fontscpl_state_t* st = (fontscpl_state_t*)win->app_data;
    if (!st || !g_api || !clicked) return;

    int32_t cx = 12;
    int32_t cy = 32;
    int32_t cw = win->width - 24;
    int32_t ch = win->height - 44;
    int32_t list_x = cx;
    int32_t list_y = cy + 52;
    int32_t list_w = 175;
    int32_t list_h = ch - 96;

    // Click sulla lista font
    if (lx >= list_x + 6 && lx <= list_x + list_w - 6 &&
        ly >= list_y + 24 && ly <= list_y + list_h - 6) {
        int r = (ly - (list_y + 24)) / 20;
        int idx = st->scroll_off + r;
        if (idx >= 0 && idx < st->num_fonts) {
            st->selected_idx = idx;
            fontscpl_apply_selected(st);
        }
        return;
    }

    // Click sui bottoni modalita' rendering & dimensione
    int32_t prev_x = list_x + list_w + 8;
    int32_t opt_y = list_y + 26 + 128;
    if (ly >= opt_y + 18 && ly <= opt_y + 40) {
        for (int m = 0; m < 3; m++) {
            int32_t bx = prev_x + 10 + m * 106;
            if (lx >= bx && lx <= bx + 100) {
                st->render_mode = m;
                return;
            }
        }
    }

    int32_t sz_y = opt_y + 48;
    const int sizes[4] = { 12, 14, 16, 18 };
    if (ly >= sz_y + 18 && ly <= sz_y + 40) {
        for (int s = 0; s < 4; s++) {
            int32_t bx = prev_x + 10 + s * 78;
            if (lx >= bx && lx <= bx + 72) {
                st->base_size = sizes[s];
                return;
            }
        }
    }

    int32_t foot_y = cy + ch - 32;
    if (ly >= foot_y && ly <= foot_y + 24) {
        if (lx >= cx + cw - 204 && lx <= cx + cw - 90) {
            fontscpl_apply_selected(st);
            return;
        }
        if (lx >= cx + cw - 84 && lx <= cx + cw) {
            g_api->wm_destroy_window(win->id);
            return;
        }
    }
}

static void fontscpl_key(window_t* win, char c) {
    fontscpl_state_t* st = (fontscpl_state_t*)win->app_data;
    if (!st || !g_api) return;
    if (c == 27) {
        g_api->wm_destroy_window(win->id);
        return;
    }
    if ((uint8_t)c == 0xF2 || c == '\n') { // Wheel down
        if (st->selected_idx < st->num_fonts - 1) st->selected_idx++;
    } else if ((uint8_t)c == 0xF1) { // Wheel up
        if (st->selected_idx > 0) st->selected_idx--;
    }
}

__attribute__((section(".text.entry"), used))
int fontscpl_dll_entry(const kernel_api_t* api, window_t* win) {
    if (!api || !win) return -1;
    g_api = api;

    fontscpl_state_t* st = (fontscpl_state_t*)g_api->kmalloc(sizeof(fontscpl_state_t));
    if (!st) return -1;
    for (size_t i = 0; i < sizeof(fontscpl_state_t); i++) ((uint8_t*)st)[i] = 0;

    st->render_mode = 1; // TrueType Anti-Aliased
    st->base_size = 14;
    dll_strcpy(st->active_font, "SANS.TTF", sizeof(st->active_font));
    fontscpl_scan_fonts(st);

    win->app_data = st;
    win->on_draw  = fontscpl_draw;
    win->on_mouse = fontscpl_mouse;
    win->on_key   = fontscpl_key;
    return 0;
}
