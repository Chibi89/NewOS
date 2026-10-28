/**
 * control_panel/cpl_theme.h
 * Helper grafico per l'adattamento tematico delle Applet del Pannello di Controllo (.CPL)
 * Fornisce supporto coordinato per temi Classic, Liquid Glass, Mica Light e Mica Dark
 * (C) Newspoint Software - NewSoft - NewOS V1
 */

#ifndef CONTROL_PANEL_CPL_THEME_H
#define CONTROL_PANEL_CPL_THEME_H

#include <gui/wm.h>
#include <gui/graphics.h>
#include <gui/theme_mgr.h>
#include <string.h>

typedef struct {
    const theme_info_t* th;
    bool     is_classic;
    bool     is_glass;
    bool     is_dark;
    uint32_t bg_color;          // Sfondo client area
    uint32_t card_bg;           // Sfondo pannelli/groupbox
    uint32_t card_border;       // Bordo pannelli/groupbox
    uint32_t text_primary;      // Testo principale
    uint32_t text_secondary;    // Testo secondario/descrizioni
    uint32_t accent;            // Accento primario
    uint32_t accent_text;       // Testo su accento
    uint32_t input_bg;          // Sfondo caselle input, liste e combobox
    uint32_t input_border;      // Bordo caselle input e liste
    uint32_t sel_bg;            // Sfondo selezione attiva
    uint32_t sel_text;          // Testo selezione attiva
    uint16_t panel_radius;      // Raggio angoli pannelli
    uint16_t control_radius;    // Raggio angoli controlli
} cpl_theme_t;

static inline void cpl_theme_init(cpl_theme_t* t) {
    if (!t) return;
    memset(t, 0, sizeof(cpl_theme_t));
    t->th = wm_get_current_theme_props();
    int theme_id = wm_get_theme();

    if (!t->th || (!t->th->modern_controls && theme_id == THEME_CLASSIC)) {
        // ─── TEMA CLASSIC (Windows 2000) ───
        t->is_classic = true;
        t->is_glass = false;
        t->is_dark = false;
        t->bg_color = COLOR_BTNFACE;
        t->card_bg = COLOR_BTNFACE;
        t->card_border = COLOR_BTNSHADOW;
        t->text_primary = COLOR_WINDOWTEXT;
        t->text_secondary = 0x5C646D;
        t->accent = 0x0A246A;
        t->accent_text = 0xFFFFFF;
        t->input_bg = 0xFFFFFF;
        t->input_border = 0x808080;
        t->sel_bg = 0x0A246A;
        t->sel_text = 0xFFFFFF;
        t->panel_radius = 0;
        t->control_radius = 0;
    } else {
        // ─── TEMI MODERNI (Liquid Glass, Mica Light, Mica Dark) ───
        t->is_classic = false;
        t->is_glass = (t->th->surface_alpha < 200 || theme_id == THEME_LIQUID_GLASS);
        t->is_dark = (theme_id == THEME_MICA_DARK || (t->th->surface_base & 0xFF) < 0x60);

        t->bg_color = t->th->surface_base ? t->th->surface_base : (t->is_dark ? 0x202025 : 0xF8F9FA);
        t->text_primary = t->th->window_text ? t->th->window_text : (t->is_dark ? 0xEAEAEA : 0x1A1A1A);
        t->text_secondary = t->is_dark ? 0x9A9AA0 : 0x5C646D;
        t->accent = t->th->accent_color ? t->th->accent_color : 0x0078D4;
        t->accent_text = t->th->accent_text ? t->th->accent_text : 0xFFFFFF;
        t->card_border = t->th->control_border_color ? t->th->control_border_color : (t->is_dark ? 0x3E3E48 : 0xD4D8E0);

        if (t->is_dark) {
            t->card_bg = 0x2A2A33;
            t->input_bg = 0x1E1E24;
            t->input_border = 0x444452;
            t->sel_bg = t->accent;
            t->sel_text = 0xFFFFFF;
        } else if (t->is_glass) {
            t->card_bg = 0xFFFFFF;
            t->input_bg = 0xFFFFFF;
            t->input_border = t->card_border;
            t->sel_bg = t->th->selection_color ? t->th->selection_color : 0xC8E0F8;
            t->sel_text = t->th->selection_text ? t->th->selection_text : 0x003366;
        } else {
            // Mica Light
            t->card_bg = 0xFFFFFF;
            t->input_bg = 0xFFFFFF;
            t->input_border = t->card_border;
            t->sel_bg = t->th->selection_color ? t->th->selection_color : 0xCCE8FF;
            t->sel_text = t->th->selection_text ? t->th->selection_text : 0x004578;
        }

        t->panel_radius = t->th->panel_radius > 0 ? t->th->panel_radius : 8;
        t->control_radius = t->th->control_radius > 0 ? t->th->control_radius : 5;
    }
}

// Disegna lo sfondo della client area adattato al tema
static inline void cpl_draw_dialog_bg(window_t* win, const cpl_theme_t* t, int32_t cx, int32_t cy, int32_t cw, int32_t ch) {
    (void)win;
    if (t->is_classic) {
        gfx_fill_rect(cx, cy, cw, ch, COLOR_BTNFACE);
    } else if (t->is_glass) {
        gfx_fill_rounded_alpha(cx, cy, cw, ch, t->panel_radius, t->bg_color, t->th->surface_alpha > 0 ? t->th->surface_alpha : 180);
    } else {
        gfx_fill_rect(cx, cy, cw, ch, t->bg_color);
    }
}

// Disegna la linea divisoria sotto le schede Tabs
static inline void cpl_draw_tabs_divider(int32_t cx, int32_t ty, int32_t th, int32_t cw, const cpl_theme_t* t) {
    if (t->is_classic) {
        gfx_fill_rect(cx + 4, ty + th, cw - 8, 1, COLOR_BTNSHADOW);
        gfx_fill_rect(cx + 4, ty + th + 1, cw - 8, 1, COLOR_BTNHIGHLIGHT);
    } else {
        gfx_fill_rect(cx + 4, ty + th + 1, cw - 8, 1, t->card_border);
    }
}

// Disegna un GroupBox/Pannello temizzato
static inline void cpl_draw_groupbox(int32_t x, int32_t y, int32_t w, int32_t h, const char* title, const cpl_theme_t* t) {
    int32_t tlen = title ? (int32_t)strlen(title) * 8 : 0;
    if (t->is_classic) {
        gfx_draw_bevel_rect(x, y, w, h, true);
        if (title && title[0]) {
            gfx_fill_rect(x + 10, y - 6, tlen + 8, 12, COLOR_BTNFACE);
            gfx_draw_string(x + 12, y - 6, title, COLOR_WINDOWTEXT, 0, true);
        }
    } else {
        // Stile moderno Fluent / Mica / Glass con card ad alto contrasto
        if (t->is_glass) {
            gfx_fill_rounded_alpha(x, y, w, h, t->panel_radius, t->card_bg, 225);
        } else {
            gfx_fill_rounded_rect(x, y, w, h, t->panel_radius, t->card_bg);
        }
        gfx_draw_rounded_rect(x, y, w, h, t->panel_radius, t->card_border);

        if (title && title[0]) {
            gfx_fill_rounded_rect(x + 10, y - 7, tlen + 12, 14, 3, t->card_bg);
            gfx_draw_rounded_rect(x + 10, y - 7, tlen + 12, 14, 3, t->card_border);
            gfx_draw_string(x + 16, y - 6, title, t->accent, 0, true);
        }
    }
}

#endif // CONTROL_PANEL_CPL_THEME_H
