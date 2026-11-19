/**
 * SystemLibrary/cpl_dll_ui.h
 * Helper grafico freestanding e position-independent per le Applet .DLL in SystemLibrary
 * Adatta automaticamente pannelli, schede, campi input e bottoni al tema attivo
 * (Classic, Liquid Glass, Mica Light, Mica Dark) tramite kernel_api_t.
 * (C) Newspoint Software - NewSoft Dynamic System Library
 */

#ifndef SYSTEM_LIBRARY_CPL_DLL_UI_H
#define SYSTEM_LIBRARY_CPL_DLL_UI_H

#include <screen_modules.h>
#include <gui/wm.h>
#include <gui/graphics.h>
#include <gui/icons.h>
#include <gui/theme_mgr.h>

#ifndef BUILDING_KERNEL_LOADER
void* memset(void* s, int c, size_t n) {
    unsigned char* p = (unsigned char*)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

void* memcpy(void* dest, const void* src, size_t n) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    while (n--) *d++ = *s++;
    return dest;
}
#endif

static inline size_t dll_strlen(const char* s) {
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

static inline void dll_strcpy(char* dst, const char* src, size_t max_len) {
    if (!dst || max_len == 0) return;
    size_t i = 0;
    if (src) {
        while (src[i] && i + 1 < max_len) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

static inline int dll_strcmp(const char* a, const char* b) {
    if (!a || !b) return (a == b) ? 0 : 1;
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

static inline void dll_uint_to_str(uint32_t val, char* buf, int pad2) {
    if (pad2) {
        buf[0] = (char)('0' + ((val / 10) % 10));
        buf[1] = (char)('0' + (val % 10));
        buf[2] = '\0';
        return;
    }
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    char tmp[16];
    int i = 0;
    while (val > 0 && i < 15) {
        tmp[i++] = (char)('0' + (val % 10));
        val /= 10;
    }
    int j = 0;
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
}

typedef struct {
    const theme_info_t* th;
    bool     is_classic;
    bool     is_glass;
    bool     is_dark;
    uint32_t bg_color;
    uint32_t card_bg;
    uint32_t card_border;
    uint32_t text_primary;
    uint32_t text_secondary;
    uint32_t accent;
    uint32_t accent_text;
    uint32_t input_bg;
    uint32_t input_border;
    uint32_t sel_bg;
    uint32_t sel_text;
    int32_t  panel_radius;
    int32_t  control_radius;
} dll_theme_t;

static inline void dll_theme_init(const kernel_api_t* api, dll_theme_t* t) {
    if (!t) return;
    for (size_t i = 0; i < sizeof(dll_theme_t); i++) ((uint8_t*)t)[i] = 0;

    t->th = (api && api->wm_get_current_theme_props) ? (const theme_info_t*)api->wm_get_current_theme_props() : NULL;
    int theme_id = (api && api->wm_get_theme) ? api->wm_get_theme() : THEME_CLASSIC;

    if (!t->th || (!t->th->modern_controls && theme_id == THEME_CLASSIC)) {
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
        } else {
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

static inline void dll_draw_dialog_bg(const kernel_api_t* api, const dll_theme_t* t, int32_t cx, int32_t cy, int32_t cw, int32_t ch) {
    if (t->is_classic) {
        api->gfx_fill_rect(cx, cy, cw, ch, COLOR_BTNFACE);
    } else if (t->is_glass && api->gfx_fill_rounded_alpha) {
        api->gfx_fill_rounded_alpha(cx, cy, cw, ch, t->panel_radius, t->bg_color, t->th->surface_alpha > 0 ? t->th->surface_alpha : 180);
    } else {
        api->gfx_fill_rect(cx, cy, cw, ch, t->bg_color);
    }
}

static inline void dll_draw_tabs_divider(const kernel_api_t* api, int32_t cx, int32_t ty, int32_t th, int32_t cw, const dll_theme_t* t) {
    if (t->is_classic) {
        api->gfx_fill_rect(cx + 4, ty + th, cw - 8, 1, COLOR_BTNSHADOW);
        api->gfx_fill_rect(cx + 4, ty + th + 1, cw - 8, 1, COLOR_BTNHIGHLIGHT);
    } else {
        api->gfx_fill_rect(cx + 4, ty + th + 1, cw - 8, 1, t->card_border);
    }
}

static inline void dll_draw_groupbox(const kernel_api_t* api, int32_t x, int32_t y, int32_t w, int32_t h, const char* title, const dll_theme_t* t) {
    int32_t tlen = title ? (int32_t)dll_strlen(title) * 8 : 0;
    if (t->is_classic) {
        api->gfx_draw_bevel_rect(x, y, w, h, true);
        if (title && title[0]) {
            api->gfx_fill_rect(x + 10, y - 6, tlen + 8, 12, COLOR_BTNFACE);
            api->gfx_draw_string(x + 12, y - 6, title, COLOR_WINDOWTEXT, 0, true);
        }
    } else {
        if (t->is_glass && api->gfx_fill_rounded_alpha) {
            api->gfx_fill_rounded_alpha(x, y, w, h, t->panel_radius, t->card_bg, 195);
        } else {
            api->gfx_fill_rounded_rect(x, y, w, h, t->panel_radius, t->card_bg);
        }
        api->gfx_draw_rounded_rect(x, y, w, h, t->panel_radius, t->card_border);

        if (title && title[0]) {
            if (t->is_glass && api->gfx_fill_rounded_alpha) {
                api->gfx_fill_rounded_alpha(x + 10, y - 7, tlen + 12, 14, 3, t->card_bg, 220);
            } else {
                api->gfx_fill_rounded_rect(x + 10, y - 7, tlen + 12, 14, 3, t->card_bg);
            }
            api->gfx_draw_rounded_rect(x + 10, y - 7, tlen + 12, 14, 3, t->card_border);
            api->gfx_draw_string(x + 16, y - 6, title, t->accent, 0, true);
        }
    }
}

static inline void dll_draw_input_box(const kernel_api_t* api, int32_t x, int32_t y, int32_t w, int32_t h, const char* text, bool focused, bool masked, const dll_theme_t* t) {
    if (t->is_classic) {
        api->gfx_fill_rect(x, y, w, h, t->input_bg);
        api->gfx_draw_bevel_rect(x, y, w, h, true);
        if (focused) {
            api->gfx_draw_rect(x + 1, y + 1, w - 2, h - 2, t->accent);
        }
    } else {
        if (t->is_glass && api->gfx_fill_rounded_alpha) {
            api->gfx_fill_rounded_alpha(x, y, w, h, t->control_radius, t->input_bg, 195);
        } else {
            api->gfx_fill_rounded_rect(x, y, w, h, t->control_radius, t->input_bg);
        }
        uint32_t brd = focused ? t->accent : t->input_border;
        api->gfx_draw_rounded_rect(x, y, w, h, t->control_radius, brd);
    }

    char disp[64];
    size_t len = dll_strlen(text);
    if (len > 58) len = 58;
    if (masked) {
        for (size_t i = 0; i < len; i++) disp[i] = '*';
    } else {
        for (size_t i = 0; i < len; i++) disp[i] = text[i];
    }
    if (focused) {
        disp[len++] = '_';
    }
    disp[len] = '\0';

    api->gfx_draw_string(x + 8, y + (h - 14) / 2, disp, t->text_primary, 0, true);
}

#endif // SYSTEM_LIBRARY_CPL_DLL_UI_H

