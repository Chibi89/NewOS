/**
 * SystemLibrary/comctl_dll.c
 * Libreria Standardizzata Controlli GUI Out-of-Kernel (COMCTL.DLL)
 * Supporta tutte le varianti dei controlli moderni (Liquid Glass, Mica, Classic 2000).
 */
#include "cpl_dll_ui.h"
#include "comctl.h"

__attribute__((section(".header"), used)) module_header_t g_comctl_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size = 0,
    .entry_offset = 64,
    .checksum = 0,
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "COMCTL",
    .signature = MODULE_AUTH_TAG
};

static const kernel_api_t* g_comctl_kapi = NULL;
static comctl_api_t g_comctl_api;
static volatile uint32_t g_reloc_info[2] = { 0x52454C4Fu, 0xCAFEBABEu };

static void comctl_apply_relocs(void) {
    if (g_reloc_info[0] != 0x52454C4Fu || g_reloc_info[1] == 0xCAFEBABEu) return;
    uintptr_t base = (uintptr_t)&g_comctl_header;
    const uint32_t* tbl = (const uint32_t*)(base + g_reloc_info[1]);
    uint32_t count = tbl[0];
    for (uint32_t i = 0; i < count; i++) {
        uint32_t off = tbl[1 + i];
        *(uint64_t*)(base + off) += (uint64_t)base;
    }
    g_reloc_info[0] = 0;
}

static void c_strcpy(char* dst, const char* src, int max_len) {
    if (!dst || max_len <= 0) return;
    int i = 0;
    if (src) {
        while (src[i] && i < max_len - 1) { dst[i] = src[i]; i++; }
    }
    dst[i] = '\0';
}

static int c_strlen(const char* s) {
    int n = 0;
    if (s) while (s[n]) n++;
    return n;
}

static void c_itoa(int64_t v, char* buf, int max_len) {
    if (!buf || max_len < 2) return;
    char tmp[32];
    int p = 0;
    bool neg = v < 0;
    uint64_t uv = neg ? (uint64_t)(-v) : (uint64_t)v;
    if (uv == 0) tmp[p++] = '0';
    while (uv > 0 && p < 30) {
        tmp[p++] = (char)('0' + (uv % 10ULL));
        uv /= 10ULL;
    }
    int o = 0;
    if (neg && o < max_len - 1) buf[o++] = '-';
    while (p > 0 && o < max_len - 1) buf[o++] = tmp[--p];
    buf[o] = '\0';
}

static void c_format_hex(uint64_t v, char* buf, int max_len) {
    if (!buf || max_len < 4) return;
    const char* hx = "0123456789ABCDEF";
    buf[0] = '0'; buf[1] = 'x';
    char tmp[16];
    int p = 0;
    if (v == 0) tmp[p++] = '0';
    while (v > 0 && p < 16) { tmp[p++] = hx[v & 0xF]; v >>= 4; }
    int o = 2;
    while (p > 0 && o < max_len - 1) buf[o++] = tmp[--p];
    buf[o] = '\0';
}

static void c_format_bytes(uint64_t bytes, char* buf, int max_len) {
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    int u = 0;
    uint64_t val = bytes;
    while (val >= 1024ULL && u < 4) { val /= 1024ULL; u++; }
    c_itoa((int64_t)val, buf, max_len);
    int l = c_strlen(buf);
    if (l + 3 < max_len) {
        buf[l++] = ' ';
        buf[l++] = units[u][0];
        if (units[u][1]) buf[l++] = units[u][1];
        buf[l] = '\0';
    }
}

static void c_grid_compute_widths(const comctl_control_t* c, int32_t total_w, int32_t* out_w, int32_t ncols) {
    int32_t fixed = 0, autos = 0;
    for (int32_t col = 0; col < ncols; col++) {
        if (c->grid_cols[col].width > 0) {
            out_w[col] = c->grid_cols[col].width;
            fixed += out_w[col];
        } else {
            out_w[col] = 0;
            autos++;
        }
    }
    int32_t rem = (total_w > fixed) ? (total_w - fixed) : 40;
    int32_t auto_w = (autos > 0) ? (rem / autos) : 0;
    if (auto_w < 32) auto_w = 32;
    for (int32_t col = 0; col < ncols; col++) {
        if (out_w[col] == 0) out_w[col] = auto_w;
    }
}

static uint32_t c_blend(uint32_t c1, uint32_t c2, uint8_t alpha) {
    uint32_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    uint32_t r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    uint32_t a = alpha, ia = 255 - alpha;
    uint32_t r = (r1 * a + r2 * ia) / 255;
    uint32_t g = (g1 * a + g2 * ia) / 255;
    uint32_t b = (b1 * a + b2 * ia) / 255;
    return (r << 16) | (g << 8) | b;
}

static void c_draw_text(int32_t x, int32_t y, const char* s, uint32_t fg, uint32_t bg) {
    if (g_comctl_kapi && g_comctl_kapi->gfx_draw_string && s) {
        g_comctl_kapi->gfx_draw_string(x, y, s, fg, bg, true);
    }
}

static void c_fill_box(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t col,
                       int32_t r, uint8_t alpha, bool is_classic) {
    if (!g_comctl_kapi || w <= 0 || h <= 0) return;
    if (is_classic || r <= 0) {
        if (alpha < 255 && g_comctl_kapi->gfx_fill_rounded_alpha) {
            g_comctl_kapi->gfx_fill_rounded_alpha(x, y, w, h, 0, col, alpha);
        } else {
            g_comctl_kapi->gfx_fill_rect(x, y, w, h, col);
        }
    } else if (alpha < 255 && g_comctl_kapi->gfx_fill_rounded_alpha) {
        g_comctl_kapi->gfx_fill_rounded_alpha(x, y, w, h, r, col, alpha);
    } else if (g_comctl_kapi->gfx_fill_rounded_rect) {
        g_comctl_kapi->gfx_fill_rounded_rect(x, y, w, h, r, col);
    } else {
        g_comctl_kapi->gfx_fill_rect(x, y, w, h, col);
    }
}

static void c_draw_border(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t col, int32_t r, bool is_classic) {
    if (!g_comctl_kapi || w <= 0 || h <= 0) return;
    if (!is_classic && r > 0 && g_comctl_kapi->gfx_draw_rounded_rect) {
        g_comctl_kapi->gfx_draw_rounded_rect(x, y, w, h, r, col);
    } else {
        g_comctl_kapi->gfx_draw_rect(x, y, w, h, col);
    }
}

static void comctl_init_theme_from_system(comctl_theme_t* out_theme) {
    if (!out_theme) return;
    memset(out_theme, 0, sizeof(comctl_theme_t));
    if (g_comctl_kapi) {
        dll_theme_t dt;
        dll_theme_init(g_comctl_kapi, &dt);
        out_theme->is_classic = dt.is_classic;
        out_theme->is_dark = dt.is_dark;
        out_theme->bg_color = dt.bg_color;
        out_theme->card_bg = dt.card_bg;
        out_theme->card_border = dt.card_border;
        out_theme->text_primary = dt.text_primary;
        out_theme->text_secondary = dt.text_secondary;
        out_theme->accent = dt.accent;
        out_theme->accent_text = dt.accent_text;
        out_theme->input_bg = dt.input_bg;
        out_theme->hover_bg = dt.is_dark ? 0x383B44 : 0xE8EEF7;
        out_theme->danger_color = 0xC42B1C;
        out_theme->success_color = 0x107C10;
        out_theme->warning_color = 0xCA5010;
        out_theme->radius = dt.control_radius;
        uint8_t alpha = (dt.th && dt.th->surface_alpha) ? dt.th->surface_alpha : 255;
        if (dt.is_glass && alpha < 175) alpha = 175;
        out_theme->surface_alpha = dt.is_classic ? 255 : alpha;
        return;
    }
    out_theme->bg_color = 0xF0F4F9;
    out_theme->card_bg = 0xFFFFFF;
    out_theme->card_border = 0xD0D7E2;
    out_theme->text_primary = 0x1A1F28;
    out_theme->text_secondary = 0x5C6675;
    out_theme->accent = 0x0067C0;
    out_theme->accent_text = 0xFFFFFF;
    out_theme->input_bg = 0xFFFFFFFF;
    out_theme->hover_bg = 0xE6EEF8;
    out_theme->danger_color = 0xC42B1C;
    out_theme->success_color = 0x107C10;
    out_theme->warning_color = 0xCA5010;
    out_theme->radius = 6;
    out_theme->surface_alpha = 220;
}

static void comctl_form_init(comctl_form_t* form, const comctl_theme_t* theme) {
    if (!form) return;
    memset(form, 0, sizeof(comctl_form_t));
    form->focused_index = -1;
    form->open_popup_index = -1;
    if (theme) {
        form->theme = *theme;
    } else {
        comctl_init_theme_from_system(&form->theme);
    }
}

static int32_t comctl_form_add(comctl_form_t* form, const comctl_control_t* ctrl) {
    if (!form || !ctrl || form->count >= COMCTL_MAX_CONTROLS) return -1;
    int32_t idx = form->count++;
    form->controls[idx] = *ctrl;
    if ((form->controls[idx].flags & (COMCTL_F_VISIBLE | COMCTL_F_ENABLED)) == 0 && ctrl->flags == 0) {
        form->controls[idx].flags = COMCTL_F_VISIBLE | COMCTL_F_ENABLED;
    }
    return idx;
}

static comctl_control_t* comctl_form_get(comctl_form_t* form, uint32_t id) {
    if (!form) return NULL;
    for (int32_t i = 0; i < form->count; i++) {
        if (form->controls[i].id == id) return &form->controls[i];
    }
    return NULL;
}

static uint32_t variant_fill_color(const comctl_theme_t* th, const comctl_control_t* c) {
    bool hover = (c->flags & COMCTL_F_HOVERED) != 0;
    bool press = (c->flags & COMCTL_F_PRESSED) != 0;
    uint32_t base = th->card_bg;
    switch (c->variant) {
        case COMCTL_VAR_PRIMARY:   base = th->accent; break;
        case COMCTL_VAR_DANGER:    base = th->danger_color; break;
        case COMCTL_VAR_SUCCESS:   base = th->success_color; break;
        case COMCTL_VAR_WARNING:   base = th->warning_color; break;
        case COMCTL_VAR_INFO:      base = 0xFF0078D4; break;
        case COMCTL_VAR_SECONDARY: base = th->hover_bg; break;
        case COMCTL_VAR_OUTLINE:   base = th->input_bg; break;
        case COMCTL_VAR_GHOST:     base = hover ? th->hover_bg : th->bg_color; break;
        default:                   base = th->is_classic ? 0xFFD4D0C8 : th->card_bg; break;
    }
    if (press) return c_blend(base, 0xFF000000, 210);
    if (hover && c->variant != COMCTL_VAR_GHOST) return c_blend(base, 0xFFFFFFFF, 225);
    return base;
}

static void comctl_draw_control(const comctl_theme_t* th, const comctl_control_t* c, int32_t ox, int32_t oy) {
    if (!g_comctl_kapi || !th || !c || !(c->flags & COMCTL_F_VISIBLE)) return;
    int32_t x = ox + c->x, y = oy + c->y, w = c->w, h = c->h;
    int32_t rad = (c->radius >= 0) ? c->radius : th->radius;
    if (c->variant == COMCTL_VAR_PILL) rad = h / 2;
    if (th->is_classic) rad = 0;
    uint8_t alpha = (c->flags & COMCTL_F_TRANSLUCENT) ? th->surface_alpha : 255;
    bool enabled = (c->flags & COMCTL_F_ENABLED) != 0;
    uint32_t fg = enabled ? th->text_primary : th->text_secondary;

    switch (c->type) {
        case COMCTL_SEPARATOR: {
            if (c->variant == COMCTL_VAR_VERTICAL) {
                g_comctl_kapi->gfx_fill_rect(x + w / 2, y, 1, h, th->card_border);
            } else {
                g_comctl_kapi->gfx_fill_rect(x, y + h / 2, w, 1, th->card_border);
            }
            break;
        }
        case COMCTL_LABEL: {
            if (c->variant == COMCTL_VAR_BADGE) {
                uint32_t bcol = c->flags & COMCTL_F_CHECKED ? th->success_color : th->accent;
                c_fill_box(x, y, w, h, bcol, h / 2, 235, th->is_classic);
                c_draw_text(x + 8, y + (h - 14) / 2, c->text, th->accent_text, bcol);
            } else {
                uint32_t tcol = (c->variant == COMCTL_VAR_LINK) ? th->accent :
                                ((c->variant == COMCTL_VAR_SECONDARY) ? th->text_secondary : fg);
                int32_t tx = x;
                int tw = c_strlen(c->text) * 8;
                if (c->align == COMCTL_ALIGN_CENTER && w > tw) tx = x + (w - tw) / 2;
                else if (c->align == COMCTL_ALIGN_RIGHT && w > tw) tx = x + w - tw;
                c_draw_text(tx, y + (h - 14) / 2, c->text, tcol, th->bg_color);
                if (c->variant == COMCTL_VAR_LINK) {
                    g_comctl_kapi->gfx_fill_rect(tx, y + (h + 12) / 2, tw, 1, tcol);
                }
            }
            break;
        }
        case COMCTL_GROUPBOX: {
            c_fill_box(x, y + 6, w, h - 6, th->card_bg, rad, th->surface_alpha, th->is_classic);
            c_draw_border(x, y + 6, w, h - 6, th->card_border, rad, th->is_classic);
            if (c->text[0]) {
                int tw = c_strlen(c->text) * 8 + 12;
                c_fill_box(x + 10, y, tw, 16, th->card_bg, 4, 240, th->is_classic);
                const char* prefix = (c->variant == COMCTL_VAR_EXPANDER)
                                     ? ((c->flags & COMCTL_F_EXPANDED) ? "v " : "> ") : "";
                char title[132];
                c_strcpy(title, prefix, sizeof(title));
                int plen = c_strlen(title);
                c_strcpy(title + plen, c->text, (int)sizeof(title) - plen);
                c_draw_text(x + 16, y + 1, title, th->accent, th->card_bg);
            }
            break;
        }
        case COMCTL_BUTTON: {
            uint32_t bg = variant_fill_color(th, c);
            uint32_t bfg = (c->variant == COMCTL_VAR_PRIMARY || c->variant == COMCTL_VAR_DANGER ||
                            c->variant == COMCTL_VAR_SUCCESS || c->variant == COMCTL_VAR_WARNING)
                           ? th->accent_text : fg;
            if (c->variant != COMCTL_VAR_GHOST || (c->flags & COMCTL_F_HOVERED)) {
                c_fill_box(x, y, w, h, bg, rad, alpha, th->is_classic);
                if (!(c->flags & COMCTL_F_BORDERLESS)) {
                    uint32_t bc = (c->flags & COMCTL_F_FOCUSED) ? th->accent :
                                  (c->variant == COMCTL_VAR_OUTLINE ? th->accent : th->card_border);
                    c_draw_border(x, y, w, h, bc, rad, th->is_classic);
                }
            }
            int tw = c_strlen(c->text) * 8;
            int tx = x + (w - tw) / 2;
            if (tx < x + 6) tx = x + 6;
            c_draw_text(tx, y + (h - 14) / 2, c->text, bfg, bg);
            break;
        }
        case COMCTL_CHECKBOX: {
            int32_t by = y + (h - 16) / 2;
            bool chk = (c->flags & COMCTL_F_CHECKED) != 0;
            bool mix = (c->variant == COMCTL_VAR_TRISTATE) && (c->flags & COMCTL_F_INDETERMINATE);
            uint32_t box_bg = (chk || mix) ? th->accent : th->input_bg;
            c_fill_box(x, by, 16, 16, box_bg, th->is_classic ? 0 : 4, 255, th->is_classic);
            c_draw_border(x, by, 16, 16, (chk || mix) ? th->accent : th->card_border, th->is_classic ? 0 : 4, th->is_classic);
            if (mix) {
                g_comctl_kapi->gfx_fill_rect(x + 4, by + 7, 8, 2, th->accent_text);
            } else if (chk) {
                c_draw_text(x + 4, by + 1, "v", th->accent_text, th->accent);
            }
            c_draw_text(x + 24, y + (h - 14) / 2, c->text, fg, th->bg_color);
            break;
        }
        case COMCTL_RADIO: {
            if (c->variant == COMCTL_VAR_SEGMENTED || c->variant == COMCTL_VAR_CARD) {
                bool sel = (c->flags & COMCTL_F_CHECKED) != 0;
                uint32_t bg = sel ? th->accent : th->card_bg;
                uint32_t tf = sel ? th->accent_text : fg;
                c_fill_box(x, y, w, h, bg, rad, alpha, th->is_classic);
                c_draw_border(x, y, w, h, sel ? th->accent : th->card_border, rad, th->is_classic);
                int tw = c_strlen(c->text) * 8;
                c_draw_text(x + (w - tw) / 2, y + (h - 14) / 2, c->text, tf, bg);
            } else {
                int32_t ry = y + (h - 16) / 2;
                bool sel = (c->flags & COMCTL_F_CHECKED) != 0;
                c_fill_box(x, ry, 16, 16, th->input_bg, 8, 255, th->is_classic);
                c_draw_border(x, ry, 16, 16, sel ? th->accent : th->card_border, 8, th->is_classic);
                if (sel) c_fill_box(x + 4, ry + 4, 8, 8, th->accent, 4, 255, th->is_classic);
                c_draw_text(x + 24, y + (h - 14) / 2, c->text, fg, th->bg_color);
            }
            break;
        }
        case COMCTL_TOGGLE: {
            bool on = (c->flags & COMCTL_F_CHECKED) != 0;
            int32_t ty = y + (h - 20) / 2;
            uint32_t track = on ? th->accent : th->card_border;
            c_fill_box(x, ty, 38, 20, track, 10, 255, th->is_classic);
            int32_t knob_x = on ? (x + 20) : (x + 2);
            c_fill_box(knob_x, ty + 2, 16, 16, 0xFFFFFFFF, 8, 255, th->is_classic);
            if (c->text[0]) {
                c_draw_text(x + 46, y + (h - 14) / 2, c->text, fg, th->bg_color);
            }
            break;
        }
        case COMCTL_TEXTBOX: {
            bool foc = (c->flags & COMCTL_F_FOCUSED) != 0;
            c_fill_box(x, y, w, h, th->input_bg, rad, 255, th->is_classic);
            c_draw_border(x, y, w, h, foc ? th->accent : th->card_border, rad, th->is_classic);
            if (foc) g_comctl_kapi->gfx_fill_rect(x + 2, y + h - 2, w - 4, 2, th->accent);
            char disp[128];
            if (c->text[0]) {
                if (c->variant == COMCTL_VAR_PASSWORD) {
                    int n = c_strlen(c->text);
                    if (n > 120) n = 120;
                    for (int k = 0; k < n; k++) disp[k] = '*';
                    disp[n] = '\0';
                } else {
                    c_strcpy(disp, c->text, sizeof(disp));
                }
                c_draw_text(x + 8, y + (h - 14) / 2, disp, fg, th->input_bg);
                if (foc) {
                    int cx = x + 8 + c_strlen(disp) * 8;
                    if (cx < x + w - 6) g_comctl_kapi->gfx_fill_rect(cx, y + 5, 2, h - 10, th->accent);
                }
            } else {
                const char* ph = c->placeholder[0] ? c->placeholder : (c->variant == COMCTL_VAR_SEARCH ? "Cerca..." : "");
                c_draw_text(x + 8, y + (h - 14) / 2, ph, th->text_secondary, th->input_bg);
            }
            break;
        }
        case COMCTL_NUMBER_STEPPER: {
            c_fill_box(x, y, w, h, th->input_bg, rad, 255, th->is_classic);
            c_draw_border(x, y, w, h, th->card_border, rad, th->is_classic);
            char vbuf[32];
            c_itoa(c->value, vbuf, sizeof(vbuf));
            c_draw_text(x + 8, y + (h - 14) / 2, vbuf, fg, th->input_bg);
            int32_t bw = 22;
            c_fill_box(x + w - bw * 2, y + 1, bw - 1, h - 2, th->card_bg, 0, 255, true);
            c_fill_box(x + w - bw, y + 1, bw - 1, h - 2, th->card_bg, 0, 255, true);
            c_draw_text(x + w - bw * 2 + 7, y + (h - 14) / 2, "-", fg, th->card_bg);
            c_draw_text(x + w - bw + 7, y + (h - 14) / 2, "+", fg, th->card_bg);
            break;
        }
        case COMCTL_SLIDER: {
            int64_t span = (c->max_val > c->min_val) ? (c->max_val - c->min_val) : 100;
            int64_t rel = c->value - c->min_val;
            if (rel < 0) rel = 0;
            if (rel > span) rel = span;
            int32_t ty = y + h / 2 - 2;
            int32_t tw = w - 16;
            int32_t fill_w = (int32_t)((rel * tw) / span);
            c_fill_box(x + 8, ty, tw, 4, th->card_border, 2, 255, th->is_classic);
            c_fill_box(x + 8, ty, fill_w, 4, th->accent, 2, 255, th->is_classic);
            int32_t kx = x + 8 + fill_w - 7;
            c_fill_box(kx, y + h / 2 - 7, 14, 14, th->accent, 7, 255, th->is_classic);
            c_fill_box(kx + 3, y + h / 2 - 4, 8, 8, 0xFFFFFFFF, 4, 255, th->is_classic);
            break;
        }
        case COMCTL_PROGRESS: {
            c_fill_box(x, y, w, h, th->card_border, rad, 255, th->is_classic);
            int64_t span = (c->max_val > c->min_val) ? (c->max_val - c->min_val) : 100;
            int64_t rel = c->value - c->min_val;
            if (rel < 0) rel = 0;
            if (rel > span) rel = span;
            int32_t fw = (int32_t)((rel * w) / span);
            if (fw > 0) c_fill_box(x, y, fw, h, th->accent, rad, 255, th->is_classic);
            break;
        }
        case COMCTL_COMBOBOX: {
            c_fill_box(x, y, w, h, th->input_bg, rad, 255, th->is_classic);
            c_draw_border(x, y, w, h, (c->flags & COMCTL_F_DROPDOWN_OPEN) ? th->accent : th->card_border, rad, th->is_classic);
            const char* sel_txt = (c->items && c->value >= 0 && c->value < c->item_count)
                                  ? c->items[c->value].label : c->text;
            c_draw_text(x + 8, y + (h - 14) / 2, sel_txt, fg, th->input_bg);
            c_draw_text(x + w - 16, y + (h - 14) / 2, "v", th->text_secondary, th->input_bg);
            break;
        }
        case COMCTL_TABBAR: {
            if (!c->items || c->item_count <= 0) break;
            if (c->variant == COMCTL_VAR_SIDEBAR_NAV) {
                int32_t ih = 28;
                for (int32_t i = 0; i < c->item_count; i++) {
                    int32_t iy = y + i * ih;
                    bool act = (i == (int32_t)c->value);
                    if (act) {
                        c_fill_box(x, iy, w, ih - 2, th->hover_bg, rad, 235, th->is_classic);
                        g_comctl_kapi->gfx_fill_rect(x, iy + 5, 3, ih - 12, th->accent);
                    }
                    c_draw_text(x + 10, iy + 6, c->items[i].label, act ? th->accent : fg, th->bg_color);
                }
            } else {
                int32_t tw = w / c->item_count;
                for (int32_t i = 0; i < c->item_count; i++) {
                    int32_t tx = x + i * tw;
                    bool act = (i == (int32_t)c->value);
                    if (c->variant == COMCTL_VAR_SEGMENTED) {
                        if (act) c_fill_box(tx + 2, y + 2, tw - 4, h - 4, th->accent, rad, 255, th->is_classic);
                        c_draw_text(tx + 8, y + (h - 14) / 2, c->items[i].label,
                                                     act ? th->accent_text : fg, th->bg_color);
                    } else {
                        if (act) c_fill_box(tx, y, tw - 2, h, th->card_bg, rad, th->surface_alpha, th->is_classic);
                        c_draw_text(tx + 8, y + (h - 14) / 2, c->items[i].label,
                                                     act ? th->accent : th->text_secondary, th->bg_color);
                        if (act) g_comctl_kapi->gfx_fill_rect(tx, y + h - 2, tw - 2, 2, th->accent);
                    }
                }
            }
            break;
        }
        case COMCTL_LISTVIEW:
        case COMCTL_TREEVIEW: {
            c_fill_box(x, y, w, h, th->card_bg, rad, th->surface_alpha, th->is_classic);
            c_draw_border(x, y, w, h, th->card_border, rad, th->is_classic);
            if (!c->items || c->item_count <= 0) break;
            int32_t row_h = 22;
            int32_t start_y = y + 4;
            if (c->type == COMCTL_LISTVIEW && c->variant == COMCTL_VAR_REPORT) {
                c_fill_box(x + 1, y + 1, w - 2, 20, th->hover_bg, 0, 235, true);
                c_draw_text(x + 8, y + 4, c->text[0] ? c->text : "Nome", th->text_secondary, th->hover_bg);
                start_y += 20;
            }
            int32_t max_rows = (h - (start_y - y) - 4) / row_h;
            for (int32_t i = c->scroll_offset; i < c->item_count && (i - c->scroll_offset) < max_rows; i++) {
                int32_t ry = start_y + (i - c->scroll_offset) * row_h;
                bool sel = (i == (int32_t)c->value);
                if (sel) c_fill_box(x + 3, ry, w - 14, row_h - 2, th->accent, 4, 240, th->is_classic);
                int32_t indent = (c->type == COMCTL_TREEVIEW) ? (c->items[i].level * 14) : 0;
                uint32_t rfg = sel ? th->accent_text : fg;
                if (c->type == COMCTL_TREEVIEW && c->items[i].has_children) {
                    c_draw_text(x + 6 + indent, ry + 4, c->items[i].expanded ? "v" : ">", rfg, th->card_bg);
                    indent += 12;
                } else if (c->variant == COMCTL_VAR_CHECKLIST) {
                    c_draw_text(x + 6, ry + 4, c->items[i].checked ? "[x]" : "[ ]", rfg, th->card_bg);
                    indent += 28;
                }
                c_draw_text(x + 8 + indent, ry + 4, c->items[i].label, rfg, th->card_bg);
                if (c->items[i].badge[0]) {
                    int bw = c_strlen(c->items[i].badge) * 8;
                    c_draw_text(x + w - bw - 16, ry + 4, c->items[i].badge,
                                                 sel ? th->accent_text : th->text_secondary, th->card_bg);
                }
            }
            // Scrollbar integrata
            if (c->item_count > max_rows && max_rows > 0) {
                int32_t sb_h = h - 8;
                int32_t th_h = (sb_h * max_rows) / c->item_count;
                if (th_h < 18) th_h = 18;
                int32_t th_y = y + 4 + ((sb_h - th_h) * c->scroll_offset) / (c->item_count - max_rows);
                c_fill_box(x + w - 8, y + 4, 5, sb_h, th->bg_color, 2, 160, th->is_classic);
                c_fill_box(x + w - 8, th_y, 5, th_h, th->text_secondary, 2, 220, th->is_classic);
            }
            break;
        }
        case COMCTL_BANNER: {
            uint32_t bcol = th->accent;
            if (c->variant == COMCTL_VAR_SUCCESS) bcol = th->success_color;
            else if (c->variant == COMCTL_VAR_WARNING) bcol = th->warning_color;
            else if (c->variant == COMCTL_VAR_DANGER) bcol = th->danger_color;
            c_fill_box(x, y, w, h, bcol, rad, 235, th->is_classic);
            c_draw_text(x + 10, y + (h - 14) / 2, c->text, th->accent_text, bcol);
            break;
        }
        case COMCTL_STATUSBAR: {
            c_fill_box(x, y, w, h, th->card_bg, 0, th->surface_alpha, true);
            g_comctl_kapi->gfx_fill_rect(x, y, w, 1, th->card_border);
            c_draw_text(x + 8, y + (h - 14) / 2, c->text, th->text_secondary, th->card_bg);
            break;
        }
        case COMCTL_TOOLBAR: {
            c_fill_box(x, y, w, h, th->card_bg, rad, th->surface_alpha, th->is_classic);
            c_draw_border(x, y, w, h, th->card_border, rad, th->is_classic);
            if (c->items) {
                int32_t cx = x + 6;
                for (int32_t i = 0; i < c->item_count; i++) {
                    if (c->items[i].separator) {
                        g_comctl_kapi->gfx_fill_rect(cx + 2, y + 4, 1, h - 8, th->card_border);
                        cx += 7;
                        continue;
                    }
                    int32_t bw = c_strlen(c->items[i].label) * 8 + 14;
                    bool sel = (i == (int32_t)c->value);
                    if (sel) c_fill_box(cx, y + 3, bw, h - 6, th->hover_bg, 4, 240, th->is_classic);
                    c_draw_text(cx + 7, y + (h - 14) / 2, c->items[i].label,
                                                 sel ? th->accent : fg, th->card_bg);
                    cx += bw + 4;
                }
            }
            break;
        }
        case COMCTL_RIBBON_STYLE: {
            /* Controllo RibbonStyle moderno (Pulsante File + Tab strip superiore + fascia comandi raggruppati) */
            c_fill_box(x, y, w, h, th->card_bg, rad, th->surface_alpha, th->is_classic);
            c_draw_border(x, y, w, h, th->card_border, rad, th->is_classic);

            int32_t tab_h = 26;
            c_fill_box(x + 1, y + 1, w - 2, tab_h, th->bg_color, 0, 235, th->is_classic);
            g_comctl_kapi->gfx_fill_rect(x + 1, y + tab_h, w - 2, 1, th->card_border);

            if (c->items && c->item_count > 0) {
                int32_t tx = x + 6;
                for (int32_t i = 0; i < c->item_count; i++) {
                    if (c->items[i].level != 0) continue;
                    int32_t tw = c_strlen(c->items[i].label) * 8 + 14;
                    if (tx + tw > x + w - 4) break;
                    bool is_file = ((int32_t)c->items[i].tag < 0);
                    bool act = (!is_file && (int32_t)c->items[i].tag == (int32_t)c->value);
                    if (is_file) {
                        c_fill_box(tx, y + 3, tw, tab_h - 4, th->accent, 4, 250, th->is_classic);
                        c_draw_text(tx + 7, y + 7, c->items[i].label, th->accent_text, th->accent);
                    } else {
                        if (act) {
                            c_fill_box(tx, y + 3, tw, tab_h - 3, th->card_bg, 4, 248, th->is_classic);
                            g_comctl_kapi->gfx_fill_rect(tx + 4, y + tab_h - 2, tw - 8, 2, th->accent);
                        }
                        c_draw_text(tx + 7, y + 7, c->items[i].label, act ? th->accent : fg, th->bg_color);
                    }
                    tx += tw + 2;
                }

                int32_t bx = x + 8;
                int32_t by = y + tab_h + 6;
                int32_t bh = h - tab_h - 12;
                if (bh < 22) bh = 22;
                for (int32_t i = 0; i < c->item_count; i++) {
                    if (c->items[i].level != 1 || (int32_t)c->items[i].tag != (int32_t)c->value) continue;
                    if (c->items[i].separator) {
                        if (bx + 6 < x + w - 8) {
                            g_comctl_kapi->gfx_fill_rect(bx + 2, by + 2, 1, bh - 4, th->card_border);
                        }
                        bx += 7;
                        continue;
                    }
                    int32_t bw = c_strlen(c->items[i].label) * 8 + 14;
                    if (bx + bw > x + w - 6) break;
                    bool chk = c->items[i].checked;
                    c_fill_box(bx, by, bw, bh, chk ? th->accent : th->hover_bg, 5, chk ? 240 : 215, th->is_classic);
                    c_draw_border(bx, by, bw, bh, chk ? th->accent : th->card_border, 5, th->is_classic);
                    c_draw_text(bx + 7, by + (bh - 14) / 2, c->items[i].label,
                                chk ? th->accent_text : fg, th->card_bg);
                    bx += bw + 4;
                }
            }
            break;
        }
        case COMCTL_DATE_TIME_PICKER: {
            c_fill_box(x, y, w, h, th->input_bg, rad, 255, th->is_classic);
            c_draw_border(x, y, w, h, th->card_border, rad, th->is_classic);
            c_draw_text(x + 8, y + (h - 14) / 2, c->text[0] ? c->text : "2026-11-22", fg, th->input_bg);
            break;
        }
        case COMCTL_PROPERTY_GRID: {
            c_fill_box(x, y, w, h, th->card_bg, rad, th->surface_alpha, th->is_classic);
            c_draw_border(x, y, w, h, th->card_border, rad, th->is_classic);
            if (!c->grid_cols || c->grid_col_count <= 0) break;
            int32_t ncols = c->grid_col_count;
            if (ncols > COMCTL_GRID_MAX_COLS) ncols = COMCTL_GRID_MAX_COLS;
            int32_t cw[COMCTL_GRID_MAX_COLS];
            c_grid_compute_widths(c, w - 10, cw, ncols);

            // Header multi-colonna
            c_fill_box(x + 1, y + 1, w - 2, 22, th->hover_bg, 0, 235, true);
            int32_t hx = x + 2;
            for (int32_t col = 0; col < ncols; col++) {
                c_draw_text(hx + 6, y + 4, c->grid_cols[col].title, th->text_secondary, th->hover_bg);
                hx += cw[col];
                if (col + 1 < ncols) {
                    g_comctl_kapi->gfx_fill_rect(hx, y + 2, 1, h - 4, th->card_border);
                }
            }
            if (!c->grid_rows || c->grid_row_count <= 0) break;
            int32_t row_h = 22;
            int32_t max_rows = (h - 26) / row_h;
            for (int32_t r = c->scroll_offset; r < c->grid_row_count && (r - c->scroll_offset) < max_rows; r++) {
                int32_t ry = y + 24 + (r - c->scroll_offset) * row_h;
                bool rsel = (r == (int32_t)c->value);
                if (rsel) c_fill_box(x + 2, ry, w - 12, row_h - 1, th->hover_bg, 3, 200, th->is_classic);
                g_comctl_kapi->gfx_fill_rect(x + 2, ry + row_h - 1, w - 12, 1, th->card_border);

                int32_t cx = x + 2;
                for (int32_t col = 0; col < ncols; col++) {
                    const comctl_grid_cell_t* cell = &c->grid_rows[r].cells[col];
                    comctl_cell_type_t ctype = (cell->type != COMCTL_CELL_AUTO)
                                               ? cell->type : c->grid_cols[col].default_type;
                    bool csel = rsel && (col == c->selected_col);
                    if (csel) c_draw_border(cx + 1, ry + 1, cw[col] - 2, row_h - 3, th->accent, 2, th->is_classic);

                    if (ctype == COMCTL_CELL_BOOL) {
                        c_fill_box(cx + 8, ry + 4, 14, 14, cell->bool_val ? th->accent : th->input_bg, 3, 255, th->is_classic);
                        c_draw_border(cx + 8, ry + 4, 14, 14, cell->bool_val ? th->accent : th->card_border, 3, th->is_classic);
                        if (cell->bool_val) c_draw_text(cx + 11, ry + 4, "v", th->accent_text, th->accent);
                        if (cell->text[0]) c_draw_text(cx + 26, ry + 4, cell->text, fg, th->card_bg);
                    } else if (ctype == COMCTL_CELL_COLOR) {
                        c_fill_box(cx + 6, ry + 4, 16, 13, cell->color, 3, 255, th->is_classic);
                        c_draw_border(cx + 6, ry + 4, 16, 13, th->card_border, 3, th->is_classic);
                        char hbuf[16];
                        if (cell->text[0]) c_strcpy(hbuf, cell->text, sizeof(hbuf));
                        else c_format_hex(cell->color & 0xFFFFFFU, hbuf, sizeof(hbuf));
                        c_draw_text(cx + 26, ry + 4, hbuf, fg, th->card_bg);
                    } else if (ctype == COMCTL_CELL_PROGRESS) {
                        int64_t pct = cell->num_val;
                        if (pct < 0) pct = 0;
                        if (pct > 100) pct = 100;
                        int32_t pw = cw[col] - 14;
                        c_fill_box(cx + 6, ry + 6, pw, 9, th->input_bg, 3, 255, th->is_classic);
                        if (pct > 0) c_fill_box(cx + 6, ry + 6, (int32_t)((pw * pct) / 100), 9, th->accent, 3, 255, th->is_classic);
                    } else if (ctype == COMCTL_CELL_BADGE) {
                        uint32_t bcol = cell->color ? cell->color : th->accent;
                        c_fill_box(cx + 6, ry + 3, cw[col] - 14, 16, bcol, 8, 235, th->is_classic);
                        c_draw_text(cx + 12, ry + 4, cell->text, th->accent_text, bcol);
                    } else if (ctype == COMCTL_CELL_ACTION) {
                        c_fill_box(cx + 6, ry + 2, cw[col] - 14, 17, th->accent, 4, 240, th->is_classic);
                        c_draw_text(cx + 12, ry + 4, cell->text[0] ? cell->text : "Apri", th->accent_text, th->accent);
                    } else {
                        char tbuf[48];
                        if (ctype == COMCTL_CELL_INT) c_itoa(cell->num_val, tbuf, sizeof(tbuf));
                        else if (ctype == COMCTL_CELL_HEX) c_format_hex((uint64_t)cell->num_val, tbuf, sizeof(tbuf));
                        else if (ctype == COMCTL_CELL_SIZE_BYTES) c_format_bytes((uint64_t)cell->num_val, tbuf, sizeof(tbuf));
                        else c_strcpy(tbuf, cell->text, sizeof(tbuf));
                        c_draw_text(cx + 6, ry + 4, tbuf, fg, th->card_bg);
                    }
                    cx += cw[col];
                }
            }
            break;
        }
        case COMCTL_BREADCRUMB: {
            c_fill_box(x, y, w, h, th->input_bg, rad, th->surface_alpha, th->is_classic);
            c_draw_border(x, y, w, h, th->card_border, rad, th->is_classic);
            if (c->items) {
                int32_t bx = x + 8;
                for (int32_t i = 0; i < c->item_count && bx < x + w - 20; i++) {
                    bool last = (i == c->item_count - 1);
                    c_draw_text(bx, y + (h - 14) / 2, c->items[i].label, last ? th->accent : fg, th->input_bg);
                    bx += c_strlen(c->items[i].label) * 8 + 4;
                    if (!last && bx < x + w - 14) {
                        c_draw_text(bx, y + (h - 14) / 2, ">", th->text_secondary, th->input_bg);
                        bx += 12;
                    }
                }
            }
            break;
        }
        case COMCTL_SPLITTER: {
            c_fill_box(x, y, w, h, th->card_border, 2, 200, th->is_classic);
            if (c->variant == COMCTL_VAR_VERTICAL) {
                g_comctl_kapi->gfx_fill_rect(x + 1, y + h / 2 - 8, w - 2 > 0 ? w - 2 : 1, 16, th->text_secondary);
            } else {
                g_comctl_kapi->gfx_fill_rect(x + w / 2 - 8, y + 1, 16, h - 2 > 0 ? h - 2 : 1, th->text_secondary);
            }
            break;
        }
        case COMCTL_WIZARD_STEPS: {
            if (!c->items || c->item_count <= 0) break;
            int32_t sw = w / c->item_count;
            for (int32_t i = 0; i < c->item_count; i++) {
                int32_t sx = x + i * sw + 4;
                bool done = (i < (int32_t)c->value), cur = (i == (int32_t)c->value);
                uint32_t col = (done || cur) ? th->accent : th->card_border;
                c_fill_box(sx, y + (h - 20) / 2, 20, 20, col, 10, 255, th->is_classic);
                char nbuf[8];
                c_itoa(i + 1, nbuf, sizeof(nbuf));
                c_draw_text(sx + 6, y + (h - 14) / 2, done ? "v" : nbuf, th->accent_text, col);
                c_draw_text(sx + 25, y + (h - 14) / 2, c->items[i].label, cur ? th->accent : fg, th->bg_color);
            }
            break;
        }
        case COMCTL_CHART: {
            c_fill_box(x, y, w, h, th->card_bg, rad, th->surface_alpha, th->is_classic);
            c_draw_border(x, y, w, h, th->card_border, rad, th->is_classic);
            if (c->text[0]) c_draw_text(x + 8, y + 4, c->text, th->text_secondary, th->card_bg);
            if (c->items && c->item_count > 0) {
                int32_t bw = (w - 16) / c->item_count;
                if (bw < 4) bw = 4;
                int32_t ch = h - 28;
                for (int32_t i = 0; i < c->item_count; i++) {
                    int32_t v = (int32_t)(c->items[i].tag > 100 ? 100 : c->items[i].tag);
                    int32_t bh = (ch * v) / 100;
                    if (bh > 0) c_fill_box(x + 8 + i * bw, y + h - 6 - bh, bw - 3 > 1 ? bw - 3 : 1, bh, th->accent, 2, 235, th->is_classic);
                }
            }
            break;
        }
        case COMCTL_TILE: {
            uint32_t bg = (c->flags & COMCTL_F_HOVERED) ? th->hover_bg : th->card_bg;
            c_fill_box(x, y, w, h, bg, rad, th->surface_alpha, th->is_classic);
            c_draw_border(x, y, w, h, th->card_border, rad, th->is_classic);
            c_draw_text(x + 12, y + 8, c->text, fg, bg);
            if (c->placeholder[0]) c_draw_text(x + 12, y + 26, c->placeholder, th->text_secondary, bg);
            c_draw_text(x + w - 18, y + (h - 14) / 2, ">", th->text_secondary, bg);
            break;
        }
        case COMCTL_CHIP_GROUP: {
            if (c->items) {
                int32_t cx = x;
                for (int32_t i = 0; i < c->item_count && cx < x + w - 20; i++) {
                    int32_t cw = c_strlen(c->items[i].label) * 8 + 16;
                    bool sel = c->items[i].checked || (i == (int32_t)c->value);
                    c_fill_box(cx, y + 2, cw, h - 4, sel ? th->accent : th->hover_bg, (h - 4) / 2, 240, th->is_classic);
                    c_draw_text(cx + 8, y + (h - 14) / 2, c->items[i].label, sel ? th->accent_text : fg, th->bg_color);
                    cx += cw + 6;
                }
            }
            break;
        }
        case COMCTL_COLOR_PICKER: {
            c_fill_box(x, y, w, h, th->input_bg, rad, 255, th->is_classic);
            c_draw_border(x, y, w, h, th->card_border, rad, th->is_classic);
            c_fill_box(x + 4, y + 4, h - 8, h - 8, (uint32_t)c->value, 4, 255, th->is_classic);
            char hbuf[16];
            c_format_hex((uint64_t)(uint32_t)c->value, hbuf, sizeof(hbuf));
            c_draw_text(x + h + 4, y + (h - 14) / 2, hbuf, fg, th->input_bg);
            break;
        }
        case COMCTL_HOTKEY_BOX: {
            bool foc = (c->flags & COMCTL_F_FOCUSED) != 0;
            c_fill_box(x, y, w, h, th->input_bg, rad, 255, th->is_classic);
            c_draw_border(x, y, w, h, foc ? th->accent : th->card_border, rad, th->is_classic);
            c_draw_text(x + 8, y + (h - 14) / 2, c->text[0] ? c->text : "Premi tasti...", c->text[0] ? fg : th->text_secondary, th->input_bg);
            break;
        }
        case COMCTL_AVATAR: {
            c_fill_box(x, y, w, h, th->accent, w / 2, 245, th->is_classic);
            char init[3] = { c->text[0] ? c->text[0] : 'U', '\0', '\0' };
            c_draw_text(x + (w - 8) / 2, y + (h - 14) / 2, init, th->accent_text, th->accent);
            break;
        }
        case COMCTL_SPINNER: {
            const char* frames[4] = { "|", "/", "-", "\\" };
            c_draw_text(x + (w - 8) / 2, y + (h - 14) / 2, frames[c->value & 3], th->accent, th->bg_color);
            break;
        }
        case COMCTL_TOAST: {
            c_fill_box(x, y, w, h, th->card_bg, rad, 245, th->is_classic);
            c_draw_border(x, y, w, h, th->accent, rad, th->is_classic);
            g_comctl_kapi->gfx_fill_rect(x + 2, y + 4, 3, h - 8, th->accent);
            c_draw_text(x + 12, y + (h - 14) / 2, c->text, fg, th->card_bg);
            break;
        }
        default:
            break;
    }
}

static void comctl_form_draw(comctl_form_t* form, int32_t ox, int32_t oy) {
    if (!form) return;
    for (int32_t i = 0; i < form->count; i++) {
        const comctl_control_t* c = &form->controls[i];
        if (c->tab_page >= 0 && c->tab_page != form->active_tab) continue;
        comctl_draw_control(&form->theme, c, ox, oy);
    }
    // Render popup dropdown sopra a tutto se aperto
    if (form->open_popup_index >= 0 && form->open_popup_index < form->count) {
        const comctl_control_t* p = &form->controls[form->open_popup_index];
        if (p->items && p->item_count > 0) {
            int32_t px = ox + p->x, py = oy + p->y + p->h + 2;
            int32_t ph = p->item_count * 22 + 6;
            c_fill_box(px, py, p->w, ph, form->theme.input_bg, form->theme.radius, 250, form->theme.is_classic);
            c_draw_border(px, py, p->w, ph, form->theme.accent, form->theme.radius, form->theme.is_classic);
            for (int32_t i = 0; i < p->item_count; i++) {
                bool sel = (i == (int32_t)p->value);
                if (sel) c_fill_box(px + 3, py + 3 + i * 22, p->w - 6, 20, form->theme.accent, 4, 255, form->theme.is_classic);
                c_draw_text(px + 8, py + 6 + i * 22, p->items[i].label,
                                             sel ? form->theme.accent_text : form->theme.text_primary, form->theme.input_bg);
            }
        }
    }
}

static bool comctl_form_mouse(comctl_form_t* form, int32_t ox, int32_t oy, int32_t mx, int32_t my,
                              bool clicked, bool pressed, int32_t scroll_delta) {
    if (!form) return false;
    bool handled = false;

    // Gestione popup aperto (ComboBox)
    if (clicked && form->open_popup_index >= 0 && form->open_popup_index < form->count) {
        comctl_control_t* p = &form->controls[form->open_popup_index];
        int32_t px = ox + p->x, py = oy + p->y + p->h + 2;
        int32_t ph = p->item_count * 22 + 6;
        if (mx >= px && mx < px + p->w && my >= py && my < py + ph) {
            int32_t idx = (my - (py + 3)) / 22;
            if (idx >= 0 && idx < p->item_count) {
                p->value = idx;
                if (p->on_change) p->on_change(p, p->user_data);
            }
            p->flags &= ~COMCTL_F_DROPDOWN_OPEN;
            form->open_popup_index = -1;
            return true;
        }
        p->flags &= ~COMCTL_F_DROPDOWN_OPEN;
        form->open_popup_index = -1;
    }

    for (int32_t i = form->count - 1; i >= 0; i--) {
        comctl_control_t* c = &form->controls[i];
        if (!(c->flags & COMCTL_F_VISIBLE) || !(c->flags & COMCTL_F_ENABLED)) continue;
        if (c->tab_page >= 0 && c->tab_page != form->active_tab) continue;

        int32_t rx = ox + c->x, ry = oy + c->y;
        bool inside = (mx >= rx && mx < rx + c->w && my >= ry && my < ry + c->h);
        if (inside) c->flags |= COMCTL_F_HOVERED;
        else c->flags &= ~COMCTL_F_HOVERED;

        if (!inside) {
            if (clicked) c->flags &= ~COMCTL_F_FOCUSED;
            continue;
        }

        if (pressed && c->type == COMCTL_SLIDER) {
            int32_t tw = c->w - 16;
            int32_t rel_x = mx - (rx + 8);
            if (rel_x < 0) rel_x = 0;
            if (rel_x > tw) rel_x = tw;
            int64_t span = (c->max_val > c->min_val) ? (c->max_val - c->min_val) : 100;
            c->value = c->min_val + (rel_x * span) / (tw > 0 ? tw : 1);
            if (c->on_change) c->on_change(c, c->user_data);
            handled = true;
        }

        if (scroll_delta != 0 && (c->type == COMCTL_LISTVIEW || c->type == COMCTL_TREEVIEW) && c->items) {
            c->scroll_offset -= scroll_delta;
            if (c->scroll_offset < 0) c->scroll_offset = 0;
            if (c->scroll_offset >= c->item_count) c->scroll_offset = c->item_count > 0 ? c->item_count - 1 : 0;
            handled = true;
        }

        if (clicked) {
            form->focused_index = i;
            c->flags |= COMCTL_F_FOCUSED;
            handled = true;

            switch (c->type) {
                case COMCTL_BUTTON:
                    if (c->on_click) c->on_click(c, c->user_data);
                    break;
                case COMCTL_CHECKBOX:
                case COMCTL_TOGGLE:
                    if (c->variant == COMCTL_VAR_TRISTATE) {
                        if (!(c->flags & COMCTL_F_CHECKED) && !(c->flags & COMCTL_F_INDETERMINATE)) {
                            c->flags |= COMCTL_F_INDETERMINATE;
                        } else if (c->flags & COMCTL_F_INDETERMINATE) {
                            c->flags &= ~COMCTL_F_INDETERMINATE;
                            c->flags |= COMCTL_F_CHECKED;
                        } else {
                            c->flags &= ~(COMCTL_F_CHECKED | COMCTL_F_INDETERMINATE);
                        }
                    } else {
                        c->flags ^= COMCTL_F_CHECKED;
                    }
                    if (c->on_change) c->on_change(c, c->user_data);
                    break;
                case COMCTL_RADIO:
                    for (int32_t j = 0; j < form->count; j++) {
                        if (form->controls[j].type == COMCTL_RADIO && form->controls[j].group_id == c->group_id) {
                            form->controls[j].flags &= ~COMCTL_F_CHECKED;
                        }
                    }
                    c->flags |= COMCTL_F_CHECKED;
                    if (c->on_change) c->on_change(c, c->user_data);
                    break;
                case COMCTL_NUMBER_STEPPER: {
                    int64_t step = c->step > 0 ? c->step : 1;
                    if (mx >= rx + c->w - 22) c->value += step;
                    else if (mx >= rx + c->w - 44) c->value -= step;
                    if (c->max_val > c->min_val) {
                        if (c->value < c->min_val) c->value = c->min_val;
                        if (c->value > c->max_val) c->value = c->max_val;
                    }
                    if (c->on_change) c->on_change(c, c->user_data);
                    break;
                }
                case COMCTL_COMBOBOX:
                    c->flags ^= COMCTL_F_DROPDOWN_OPEN;
                    form->open_popup_index = (c->flags & COMCTL_F_DROPDOWN_OPEN) ? i : -1;
                    break;
                case COMCTL_TABBAR:
                    if (c->items && c->item_count > 0) {
                        int32_t idx = (c->variant == COMCTL_VAR_SIDEBAR_NAV)
                                      ? ((my - ry) / 28)
                                      : ((mx - rx) / (c->w / c->item_count > 0 ? c->w / c->item_count : 1));
                        if (idx >= 0 && idx < c->item_count) {
                            c->value = idx;
                            form->active_tab = idx;
                            if (c->on_change) c->on_change(c, c->user_data);
                        }
                    }
                    break;
                case COMCTL_RIBBON_STYLE:
                    if (c->items && c->item_count > 0) {
                        if (my < ry + 26) {
                            int32_t tx = rx + 6;
                            for (int32_t k = 0; k < c->item_count; k++) {
                                if (c->items[k].level != 0) continue;
                                int32_t tw = c_strlen(c->items[k].label) * 8 + 14;
                                if (tx + tw > rx + c->w - 4) break;
                                if (mx >= tx && mx < tx + tw) {
                                    if ((int32_t)c->items[k].tag < 0) {
                                        c->selected_col = k;
                                        if (c->on_click) c->on_click(c, c->user_data);
                                    } else {
                                        c->value = (int32_t)c->items[k].tag;
                                        form->active_tab = (int32_t)c->items[k].tag;
                                        if (c->on_change) c->on_change(c, c->user_data);
                                    }
                                    break;
                                }
                                tx += tw + 2;
                            }
                        } else {
                            int32_t bx = rx + 8;
                            for (int32_t k = 0; k < c->item_count; k++) {
                                if (c->items[k].level != 1 || (int32_t)c->items[k].tag != (int32_t)c->value) continue;
                                if (c->items[k].separator) { bx += 7; continue; }
                                int32_t bw = c_strlen(c->items[k].label) * 8 + 14;
                                if (bx + bw > rx + c->w - 6) break;
                                if (mx >= bx && mx < bx + bw) {
                                    c->selected_col = k;
                                    if (c->on_click) c->on_click(c, c->user_data);
                                    break;
                                }
                                bx += bw + 4;
                            }
                        }
                    }
                    break;
                case COMCTL_LISTVIEW:
                case COMCTL_TREEVIEW:
                    if (c->items && c->item_count > 0) {
                        int32_t hdr_off = (c->type == COMCTL_LISTVIEW && c->variant == COMCTL_VAR_REPORT) ? 24 : 4;
                        int32_t idx = c->scroll_offset + (my - (ry + hdr_off)) / 22;
                        if (idx >= 0 && idx < c->item_count) {
                            c->value = idx;
                            if (c->variant == COMCTL_VAR_CHECKLIST) c->items[idx].checked = !c->items[idx].checked;
                            if (c->type == COMCTL_TREEVIEW && c->items[idx].has_children) {
                                c->items[idx].expanded = !c->items[idx].expanded;
                            }
                            if (c->on_change) c->on_change(c, c->user_data);
                        }
                    }
                    break;
                case COMCTL_GROUPBOX:
                    if (c->variant == COMCTL_VAR_EXPANDER && my < ry + 20) {
                        c->flags ^= COMCTL_F_EXPANDED;
                        if (c->on_change) c->on_change(c, c->user_data);
                    }
                    break;
                case COMCTL_PROPERTY_GRID:
                    if (c->grid_cols && c->grid_col_count > 0 && c->grid_rows && c->grid_row_count > 0 && my >= ry + 24) {
                        int32_t ncols = c->grid_col_count > COMCTL_GRID_MAX_COLS ? COMCTL_GRID_MAX_COLS : c->grid_col_count;
                        int32_t cw[COMCTL_GRID_MAX_COLS];
                        c_grid_compute_widths(c, c->w - 10, cw, ncols);
                        int32_t row = c->scroll_offset + (my - (ry + 24)) / 22;
                        int32_t col = 0, cx = rx + 2;
                        for (int32_t k = 0; k < ncols; k++) {
                            if (mx >= cx && mx < cx + cw[k]) { col = k; break; }
                            cx += cw[k];
                        }
                        if (row >= 0 && row < c->grid_row_count) {
                            c->value = row;
                            c->selected_col = col;
                            comctl_grid_cell_t* cell = &c->grid_rows[row].cells[col];
                            comctl_cell_type_t ctype = (cell->type != COMCTL_CELL_AUTO)
                                                       ? cell->type : c->grid_cols[col].default_type;
                            if (ctype == COMCTL_CELL_BOOL && !cell->readonly) {
                                cell->bool_val = !cell->bool_val;
                                c->editing_row = -1;
                            } else if (c->grid_cols[col].editable && !cell->readonly &&
                                       (ctype == COMCTL_CELL_TEXT || ctype == COMCTL_CELL_INT || ctype == COMCTL_CELL_HEX)) {
                                c->editing_row = row;
                                c->editing_col = col;
                            } else {
                                c->editing_row = -1;
                            }
                            if (c->on_change) c->on_change(c, c->user_data);
                        }
                    }
                    break;
                case COMCTL_BREADCRUMB:
                case COMCTL_CHIP_GROUP:
                    if (c->items && c->item_count > 0) {
                        int32_t bx = rx + 8;
                        for (int32_t k = 0; k < c->item_count; k++) {
                            int32_t bw = c_strlen(c->items[k].label) * 8 + 16;
                            if (mx >= bx && mx < bx + bw) {
                                c->value = k;
                                if (c->type == COMCTL_CHIP_GROUP) c->items[k].checked = !c->items[k].checked;
                                if (c->on_change) c->on_change(c, c->user_data);
                                break;
                            }
                            bx += bw + 4;
                        }
                    }
                    break;
                default:
                    if (c->on_click) c->on_click(c, c->user_data);
                    break;
            }
            break;
        }
    }
    return handled;
}

static bool comctl_form_key(comctl_form_t* form, int key) {
    if (!form || form->count <= 0) return false;
    if (key == '\t') {
        if (form->focused_index >= 0 && form->focused_index < form->count) {
            form->controls[form->focused_index].flags &= ~COMCTL_F_FOCUSED;
        }
        form->focused_index = (form->focused_index + 1) % form->count;
        form->controls[form->focused_index].flags |= COMCTL_F_FOCUSED;
        return true;
    }
    if (form->focused_index < 0 || form->focused_index >= form->count) return false;
    comctl_control_t* c = &form->controls[form->focused_index];
    if (!(c->flags & COMCTL_F_ENABLED)) return false;

    if (c->type == COMCTL_TEXTBOX && !(c->flags & COMCTL_F_READONLY)) {
        int len = c_strlen(c->text);
        if (key == '\b' || key == 127) {
            if (len > 0) {
                c->text[len - 1] = '\0';
                if (c->on_change) c->on_change(c, c->user_data);
            }
            return true;
        }
        if (key >= 32 && key < 127 && len < (int)sizeof(c->text) - 1) {
            if (c->variant == COMCTL_VAR_NUMERIC && (key < '0' || key > '9') && key != '-') return false;
            c->text[len] = (char)key;
            c->text[len + 1] = '\0';
            if (c->on_change) c->on_change(c, c->user_data);
            return true;
        }
    }
    if (c->type == COMCTL_PROPERTY_GRID && c->grid_rows &&
        c->editing_row >= 0 && c->editing_row < c->grid_row_count &&
        c->editing_col >= 0 && c->editing_col < c->grid_col_count) {
        comctl_grid_cell_t* cell = &c->grid_rows[c->editing_row].cells[c->editing_col];
        int len = c_strlen(cell->text);
        if (key == '\b' || key == 127) {
            if (len > 0) {
                cell->text[len - 1] = '\0';
                if (c->on_change) c->on_change(c, c->user_data);
            }
            return true;
        }
        if (key >= 32 && key < 127 && len < (int)sizeof(cell->text) - 1) {
            cell->text[len] = (char)key;
            cell->text[len + 1] = '\0';
            if (c->on_change) c->on_change(c, c->user_data);
            return true;
        }
    }
    if ((key == '\n' || key == '\r' || key == ' ') && c->type == COMCTL_BUTTON) {
        if (c->on_click) c->on_click(c, c->user_data);
        return true;
    }
    return false;
}

typedef struct {
    const comctl_api_t* out_api;
} comctl_init_params_t;

__attribute__((section(".text.entry"), used, force_align_arg_pointer))
int module_main(const kernel_api_t* api, comctl_init_params_t* params) {
    comctl_apply_relocs();
    if (!api || !params) return -1;
    g_comctl_kapi = api;

    g_comctl_api.version = COMCTL_API_VERSION;
    g_comctl_api.init_theme_from_system = comctl_init_theme_from_system;
    g_comctl_api.form_init = comctl_form_init;
    g_comctl_api.form_add = comctl_form_add;
    g_comctl_api.form_get = comctl_form_get;
    g_comctl_api.form_draw = comctl_form_draw;
    g_comctl_api.form_mouse = comctl_form_mouse;
    g_comctl_api.form_key = comctl_form_key;
    g_comctl_api.draw_control = comctl_draw_control;

    params->out_api = &g_comctl_api;
    return 0;
}
