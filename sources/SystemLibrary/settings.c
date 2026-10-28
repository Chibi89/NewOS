/**
 * SystemLibrary/settings.c
 * Libreria Dinamica Out-of-Kernel per NewOS V1 (64-bit)
 * Nuova modalità moderna del Pannello di Controllo (Impostazioni)
 * Ispirata a Windows 11 Impostazioni, con architettura scorporata
 * (C) Newspoint Software - NewSoft Dynamic System Library
 */

#include <screen_modules.h>
#include <settings.h>
#include <gui/icons.h>

#ifndef BUILDING_KERNEL_LOADER
// Header di protezione del modulo (esattamente 64 byte all'inizio del file .DLL)
__attribute__((section(".header"), used))
module_header_t g_settings_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size      = 0, // Popolato dal build system
    .entry_offset   = 64, // Posizione dell'entry point
    .checksum       = 0, // Popolato dal build system
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "SETTINGS",
    .signature      = MODULE_AUTH_TAG
};

// --- Funzioni di Memoria e Stringhe Freestanding ---

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

void* memmove(void* dest, const void* src, size_t n) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}
#endif


static size_t local_strlen(const char* s) {
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

static char* local_strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

static char* local_strncpy(char* dest, const char* src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i] != '\0'; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

static int local_tolower(int c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static bool local_str_contains_nocase(const char* haystack, const char* needle) {
    if (!needle || needle[0] == '\0') return true;
    if (!haystack) return false;
    size_t hlen = local_strlen(haystack);
    size_t nlen = local_strlen(needle);
    if (nlen > hlen) return false;

    for (size_t i = 0; i <= hlen - nlen; i++) {
        bool match = true;
        for (size_t j = 0; j < nlen; j++) {
            if (local_tolower((unsigned char)haystack[i + j]) != local_tolower((unsigned char)needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

// Struttura sezione della barra di navigazione
typedef struct {
    const char* title;
    icon_id_t   icon;
} nav_item_t;

static const nav_item_t s_nav_items[SETTINGS_NUM_SECTIONS] = {
    { "Home",                     ICON_THIS_PC },
    { "Sistema",                  ICON_INFO },
    { "Rete e Internet",          ICON_NET },
    { "Personalizzazione",        ICON_DESKTOP },
    { "Dispositivi e Audio",      ICON_SOUND },
    { "Opzioni Energia",          ICON_ENERGY },
    { "Sicurezza & Risorse",      ICON_MAINTENANCE }
};

// --- Funzioni Ausiliarie di Disegno Grafico ---

static void draw_card_bg(const kernel_api_t* api, int32_t x, int32_t y, int32_t w, int32_t h,
                         int32_t radius, uint32_t bg_col, uint32_t border_col, bool is_glass) {
    if (is_glass && api->gfx_fill_alpha) {
        api->gfx_fill_alpha(x, y, w, h, bg_col, 190);
    } else if (radius > 0 && api->gfx_fill_rounded_rect) {
        api->gfx_fill_rounded_rect(x, y, w, h, radius, bg_col);
    } else {
        api->gfx_fill_rect(x, y, w, h, bg_col);
    }

    if (radius > 0 && api->gfx_draw_rounded_rect) {
        api->gfx_draw_rounded_rect(x, y, w, h, radius, border_col);
    } else {
        api->gfx_draw_rect(x, y, w, h, border_col);
    }
}

static void draw_action_btn(const kernel_api_t* api, int32_t x, int32_t y, int32_t w, int32_t h,
                            const char* text, bool is_hover, bool is_primary) {
    uint32_t bg = is_primary ? (is_hover ? 0x0067C0 : 0x005FB8) : (is_hover ? 0xE4E8EC : 0xF3F4F6);
    uint32_t fg = is_primary ? 0xFFFFFF : 0x1A1A1A;
    uint32_t border = is_primary ? 0x004E98 : 0xC8CDD4;

    if (api->gfx_fill_rounded_rect) {
        api->gfx_fill_rounded_rect(x, y, w, h, 4, bg);
        api->gfx_draw_rounded_rect(x, y, w, h, 4, border);
    } else {
        api->gfx_fill_rect(x, y, w, h, bg);
        api->gfx_draw_rect(x, y, w, h, border);
    }

    size_t tlen = local_strlen(text);
    int32_t tx = x + (w - (int32_t)tlen * 8) / 2;
    int32_t ty = y + (h - 16) / 2;
    api->gfx_draw_string(tx, ty, text, fg, 0, true);
}

static void draw_badge(const kernel_api_t* api, int32_t x, int32_t y, const char* text,
                       uint32_t bg, uint32_t fg, uint32_t border) {
    size_t tlen = local_strlen(text);
    int32_t bw = (int32_t)tlen * 8 + 14;
    int32_t bh = 18;

    if (api->gfx_fill_rounded_rect) {
        api->gfx_fill_rounded_rect(x, y, bw, bh, 4, bg);
        api->gfx_draw_rounded_rect(x, y, bw, bh, 4, border);
    } else {
        api->gfx_fill_rect(x, y, bw, bh, bg);
        api->gfx_draw_rect(x, y, bw, bh, border);
    }
    api->gfx_draw_string(x + 7, y + 2, text, fg, 0, true);
}

// --- Funzioni di Calcolo Altezza e Disegno Scrollbar Moderna ---

static int32_t get_section_content_height(int32_t section, const char* search_text) {
    if (search_text && search_text[0] != '\0') {
        return 420;
    }
    switch (section) {
        case SETTINGS_SECTION_HOME:
            return 440;
        case SETTINGS_SECTION_SYSTEM:
            return 240;
        case SETTINGS_SECTION_NETWORK:
            return 200;
        case SETTINGS_SECTION_PERSONAL:
            return 250;
        case SETTINGS_SECTION_DEVICES:
            return 260;
        case SETTINGS_SECTION_POWER:
            return 200;
        case SETTINGS_SECTION_SECURITY:
            return 200;
        default:
            return 440;
    }
}

static void render_fluent_scrollbar(const kernel_api_t* api, int32_t sb_x, int32_t sb_y,
                                    int32_t sb_w, int32_t sb_h, int32_t total_content_h,
                                    int32_t visible_h, int32_t scroll_y,
                                    int32_t mouse_x, int32_t mouse_y) {
    if (total_content_h <= visible_h || visible_h <= 0) return;

    // 1. Sfondo corsia (Track) stile Fluent
    api->gfx_fill_rect(sb_x, sb_y, sb_w, sb_h, 0xF4F6F8);
    api->gfx_fill_rect(sb_x, sb_y, 1, sb_h, 0xDCE1E7); // Linea divisoria a sinistra

    int32_t btn_h = 14;

    // 2. Freccia Su
    bool hover_up = (mouse_x >= sb_x && mouse_x < sb_x + sb_w &&
                     mouse_y >= sb_y && mouse_y < sb_y + btn_h);
    if (hover_up) {
        api->gfx_fill_rect(sb_x + 1, sb_y, sb_w - 1, btn_h, 0xE2E7EC);
    }
    api->gfx_draw_string(sb_x + (sb_w - 8) / 2, sb_y + 1, "^", 0x5C646D, 0, true);

    // 3. Freccia Giù
    int32_t down_y = sb_y + sb_h - btn_h;
    bool hover_down = (mouse_x >= sb_x && mouse_x < sb_x + sb_w &&
                       mouse_y >= down_y && mouse_y < down_y + btn_h);
    if (hover_down) {
        api->gfx_fill_rect(sb_x + 1, down_y, sb_w - 1, btn_h, 0xE2E7EC);
    }
    api->gfx_draw_string(sb_x + (sb_w - 8) / 2, down_y + 1, "v", 0x5C646D, 0, true);

    // 4. Area del Thumb (cursore mobile)
    int32_t track_y = sb_y + btn_h;
    int32_t track_h = sb_h - (btn_h * 2);
    if (track_h < 16) return;

    int32_t max_scroll = total_content_h - visible_h;
    if (max_scroll <= 0) return;

    int32_t thumb_h = (track_h * visible_h) / total_content_h;
    if (thumb_h < 22) thumb_h = 22;
    if (thumb_h > track_h) thumb_h = track_h;

    int32_t thumb_y = track_y + (scroll_y * (track_h - thumb_h)) / max_scroll;
    if (thumb_y < track_y) thumb_y = track_y;
    if (thumb_y + thumb_h > track_y + track_h) thumb_y = track_y + track_h - thumb_h;

    int32_t thumb_x = sb_x + 2;
    int32_t thumb_w = sb_w - 4;

    bool hover_thumb = (mouse_x >= sb_x && mouse_x < sb_x + sb_w &&
                        mouse_y >= thumb_y && mouse_y < thumb_y + thumb_h);
    uint32_t thumb_col = hover_thumb ? 0x828A94 : 0xB8BFC7;

    if (api->gfx_fill_rounded_rect) {
        api->gfx_fill_rounded_rect(thumb_x, thumb_y, thumb_w, thumb_h, 3, thumb_col);
    } else {
        api->gfx_fill_rect(thumb_x, thumb_y, thumb_w, thumb_h, thumb_col);
    }
}

static void draw_clipped_card_bg(const kernel_api_t* api, int32_t x, int32_t y, int32_t w, int32_t h,
                                 int32_t radius, uint32_t bg_col, uint32_t border_col, bool is_glass,
                                 int32_t clip_top, int32_t clip_bot) {
    if (y + h <= clip_top || y >= clip_bot) return;

    int32_t cy = y;
    int32_t ch = h;
    if (cy < clip_top) {
        ch -= (clip_top - cy);
        cy = clip_top;
    }
    if (cy + ch > clip_bot) {
        ch = clip_bot - cy;
    }
    if (ch <= 0) return;

    draw_card_bg(api, x, cy, w, ch, (cy == y && ch == h) ? radius : 0, bg_col, border_col, is_glass);
}

static inline void draw_clipped_string(const kernel_api_t* api, int32_t x, int32_t y,
                                      const char* text, uint32_t fg, uint32_t bg, bool transparent,
                                      int32_t clip_top, int32_t clip_bot) {
    if (y >= clip_top && y + 14 <= clip_bot) {
        api->gfx_draw_string(x, y, text, fg, bg, transparent);
    }
}

static inline void draw_clipped_icon_16(const kernel_api_t* api, int32_t x, int32_t y,
                                       icon_id_t icon, int32_t clip_top, int32_t clip_bot) {
    if (y >= clip_top && y + 16 <= clip_bot) {
        api->gfx_draw_icon_16(x, y, icon);
    }
}

static inline void draw_clipped_icon_32(const kernel_api_t* api, int32_t x, int32_t y,
                                       icon_id_t icon, int32_t clip_top, int32_t clip_bot) {
    if (y >= clip_top && y + 32 <= clip_bot) {
        api->gfx_draw_icon_32(x, y, icon);
    }
}

static inline void draw_clipped_action_btn(const kernel_api_t* api, int32_t x, int32_t y,
                                          int32_t w, int32_t h, const char* text,
                                          bool is_hover, bool is_primary,
                                          int32_t clip_top, int32_t clip_bot) {
    if (y >= clip_top && y + h <= clip_bot) {
        draw_action_btn(api, x, y, w, h, text, is_hover, is_primary);
    }
}

static inline void draw_clipped_badge(const kernel_api_t* api, int32_t x, int32_t y,
                                     const char* text, uint32_t bg, uint32_t fg, uint32_t border,
                                     int32_t clip_top, int32_t clip_bot) {
    if (y >= clip_top && y + 18 <= clip_bot) {
        draw_badge(api, x, y, text, bg, fg, border);
    }
}

// --- Rendering Sezione HOME (Ispirata al layout Windows 11 Impostazioni) ---

static void render_home_section(const kernel_api_t* api, int32_t x, int32_t y, int32_t w, int32_t h,
                                const settings_render_params_t* p) {
    (void)h;
    bool is_glass = p->is_glass;
    int32_t clip_top = p->cy + 38;
    int32_t clip_bot = clip_top + (p->ch - 44);

    // 1. Scheda Eroe del Dispositivo (Top Hero Card)
    int32_t hero_h = 56;
    draw_clipped_card_bg(api, x, y, w, hero_h, 6, 0xF9FAFC, 0xDCE1E7, is_glass, clip_top, clip_bot);

    draw_clipped_icon_32(api, x + 12, y + 12, ICON_DESKTOP, clip_top, clip_bot);

    const char* pc_name = "Chibi89";
    const char* specs_str = "X870E AORUS ELITE X3D - AMD Ryzen 64-bit";
    if (api->user_get_current) {
        user_profile_t* u = api->user_get_current();
        if (u && u->computer_name[0] != '\0') {
            pc_name = u->computer_name;
        }
    }

    draw_clipped_string(api, x + 52, y + 10, pc_name, 0x1A1A1A, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x + 52, y + 28, specs_str, 0x5C646D, 0, true, clip_top, clip_bot);

    // Badge di Stato Rete e Sistema
    int32_t badge_y = y + 18;
    int32_t b2_x = x + w - 180;
    int32_t b1_x = b2_x - 170;
    if (b1_x > x + 300) {
        draw_clipped_badge(api, b1_x, badge_y, "Ethernet: Connesso", 0xE6F4EA, 0x137333, 0xCEEAD6, clip_top, clip_bot);
        draw_clipped_badge(api, b2_x, badge_y, "NewOS 64-bit Long Mode", 0xE8F0FE, 0x1A73E8, 0xD2E3FC, clip_top, clip_bot);
    }

    // 2. Gruppo "Impostazioni consigliate"
    int32_t cur_y = y + hero_h + 12;
    int32_t rec_box_h = 116;

    draw_clipped_string(api, x, cur_y, "Impostazioni consigliate", 0x1A1A1A, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x, cur_y + 14, "Impostazioni recenti e di uso comune", 0x6E7781, 0, true, clip_top, clip_bot);

    int32_t box_y = cur_y + 32;
    draw_clipped_card_bg(api, x, box_y, w, rec_box_h, 6, 0xFFFFFF, 0xDCE1E7, is_glass, clip_top, clip_bot);

    const struct {
        const char* name;
        const char* desc;
        icon_id_t   icon;
    } rec_items[] = {
        { "Schermo", "Personalizza sfondo, monitor e risoluzione video", ICON_DESKTOP },
        { "Suoni e periferiche audio", "Volume altoparlanti, mixer ed eventi audio AC'97", ICON_SOUND },
        { "Gestione periferiche", "Stato driver hardware e risorse di sistema (.SYS)", ICON_MAINTENANCE },
        { "Opzioni risparmio energia", "Standby monitor, spegnimento e profili ACPI", ICON_ENERGY }
    };

    for (int i = 0; i < 4; i++) {
        int32_t row_y = box_y + 2 + i * 28;
        if (i > 0 && row_y >= clip_top && row_y < clip_bot) {
            api->gfx_fill_rect(x + 40, row_y, w - 48, 1, 0xEEF1F4);
        }

        draw_clipped_icon_16(api, x + 12, row_y + 5, rec_items[i].icon, clip_top, clip_bot);
        draw_clipped_string(api, x + 36, row_y + 5, rec_items[i].name, 0x1A1A1A, 0, true, clip_top, clip_bot);

        int32_t desc_x = x + 240;
        if (w > 480) {
            draw_clipped_string(api, desc_x, row_y + 5, rec_items[i].desc, 0x6E7781, 0, true, clip_top, clip_bot);
        }

        int32_t btn_w = 64;
        int32_t btn_x = x + w - btn_w - 10;
        bool is_hov = (p->mouse_x >= btn_x && p->mouse_x <= btn_x + btn_w &&
                       p->mouse_y >= row_y + 3 && p->mouse_y <= row_y + 23);
        draw_clipped_action_btn(api, btn_x, row_y + 3, btn_w, 20, "Apri >", is_hov, false, clip_top, clip_bot);
    }

    // 3. Gruppo "Personalizzazione del dispositivo"
    cur_y += 32 + rec_box_h + 12;
    int32_t theme_box_h = 52;

    draw_clipped_string(api, x, cur_y, "Personalizzazione del dispositivo", 0x1A1A1A, 0, true, clip_top, clip_bot);

    box_y = cur_y + 18;
    draw_clipped_card_bg(api, x, box_y, w, theme_box_h, 6, 0xFFFFFF, 0xDCE1E7, is_glass, clip_top, clip_bot);

    const char* theme_names[] = { "Fluent Glass", "Mica Light", "Mica Dark", "Classic 3D" };
    uint32_t theme_swatches[] = { 0x0078D4, 0x6080A0, 0x202020, 0x0A246A };
    int32_t tbtn_w = (w - 20) / 4;
    if (tbtn_w > 120) tbtn_w = 120;

    for (int t = 0; t < 4; t++) {
        int32_t bx = x + 10 + t * (tbtn_w + 8);
        int32_t by = box_y + 12;
        bool is_hov = (p->mouse_x >= bx && p->mouse_x <= bx + tbtn_w &&
                       p->mouse_y >= by && p->mouse_y <= by + 26);

        draw_clipped_card_bg(api, bx, by, tbtn_w, 26, 4, is_hov ? 0xEDF2F7 : 0xF7F9FA, 0xCCD2DA, false, clip_top, clip_bot);
        if (by + 6 >= clip_top && by + 20 <= clip_bot) {
            api->gfx_fill_rect(bx + 6, by + 6, 12, 14, theme_swatches[t]);
            api->gfx_draw_rect(bx + 6, by + 6, 12, 14, 0x888888);
        }
        draw_clipped_string(api, bx + 24, by + 6, theme_names[t], 0x1A1A1A, 0, true, clip_top, clip_bot);
    }

    // 4. Gruppo "Archiviazione e Memoria" (Unità Disco Locale C:)
    cur_y += 18 + theme_box_h + 12;
    int32_t disk_box_h = 44;

    draw_clipped_card_bg(api, x, cur_y, w, disk_box_h, 6, 0xFFFFFF, 0xDCE1E7, is_glass, clip_top, clip_bot);
    draw_clipped_icon_16(api, x + 12, cur_y + 14, ICON_HD, clip_top, clip_bot);
    draw_clipped_string(api, x + 36, cur_y + 8, "Disco Locale (C:) - exFAT Volume", 0x1A1A1A, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x + 36, cur_y + 24, "62 MB usati su 124 MB disponibili", 0x6E7781, 0, true, clip_top, clip_bot);

    int32_t bar_x = x + 320;
    int32_t bar_w = w - 340;
    if (bar_w > 60) {
        int32_t bar_y = cur_y + 16;
        int32_t bar_h = 12;
        if (bar_y >= clip_top && bar_y + bar_h <= clip_bot) {
            api->gfx_fill_rect(bar_x, bar_y, bar_w, bar_h, 0xE9ECEF);
            api->gfx_draw_rect(bar_x, bar_y, bar_w, bar_h, 0xC8D0D8);
            int32_t fill_w = bar_w / 2;
            api->gfx_fill_rect(bar_x + 1, bar_y + 1, fill_w, bar_h - 2, 0x0078D4);
        }
    }

    // 5. Gruppo "Strumenti di Sistema e Manutenzione"
    cur_y += disk_box_h + 10;
    int32_t sys_box_h = 58;

    draw_clipped_card_bg(api, x, cur_y, w, sys_box_h, 6, 0xFFFFFF, 0xDCE1E7, is_glass, clip_top, clip_bot);

    draw_clipped_icon_16(api, x + 12, cur_y + 6, ICON_INFO, clip_top, clip_bot);
    draw_clipped_string(api, x + 36, cur_y + 6, "Informazioni su NewOS (WINVER)", 0x1A1A1A, 0, true, clip_top, clip_bot);
    int32_t b_winver_x = x + w - 74;
    bool is_hov_w = (p->mouse_x >= b_winver_x && p->mouse_x <= b_winver_x + 64 &&
                     p->mouse_y >= cur_y + 4 && p->mouse_y <= cur_y + 24);
    draw_clipped_action_btn(api, b_winver_x, cur_y + 4, 64, 20, "Apri >", is_hov_w, false, clip_top, clip_bot);

    if (cur_y + 28 >= clip_top && cur_y + 28 < clip_bot) {
        api->gfx_fill_rect(x + 36, cur_y + 28, w - 48, 1, 0xEEF1F4);
    }

    draw_clipped_icon_16(api, x + 12, cur_y + 34, ICON_REGISTRY, clip_top, clip_bot);
    draw_clipped_string(api, x + 36, cur_y + 34, "Editor del Registro (REGEDIT.EXE)", 0x1A1A1A, 0, true, clip_top, clip_bot);
    int32_t b_reg_x = x + w - 74;
    bool is_hov_r = (p->mouse_x >= b_reg_x && p->mouse_x <= b_reg_x + 64 &&
                     p->mouse_y >= cur_y + 32 && p->mouse_y <= cur_y + 52);
    draw_clipped_action_btn(api, b_reg_x, cur_y + 32, 64, 20, "Apri >", is_hov_r, false, clip_top, clip_bot);
}

// --- Rendering Sezione SISTEMA ---

static void render_system_section(const kernel_api_t* api, int32_t x, int32_t y, int32_t w, int32_t h,
                                  const settings_render_params_t* p) {
    (void)h;
    int32_t clip_top = p->cy + 38;
    int32_t clip_bot = clip_top + (p->ch - 44);

    draw_clipped_string(api, x, y, "Specifiche Hardware e Sistema", 0x1A1A1A, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x, y + 16, "Architettura processore, memoria e scheda madre", 0x6E7781, 0, true, clip_top, clip_bot);

    int32_t box_y = y + 36;
    draw_clipped_card_bg(api, x, box_y, w, 160, 6, 0xFFFFFF, 0xDCE1E7, p->is_glass, clip_top, clip_bot);

    draw_clipped_string(api, x + 16, box_y + 12, "Processore:", 0x005FB8, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x + 130, box_y + 12, "x86_64 Long Mode (AMD64 / Intel 64-bit Freestanding)", 0x1A1A1A, 0, true, clip_top, clip_bot);

    draw_clipped_string(api, x + 16, box_y + 34, "Scheda Madre:", 0x005FB8, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x + 130, box_y + 34, "X870E AORUS ELITE X3D (PCI Express / ACPI 2.0)", 0x1A1A1A, 0, true, clip_top, clip_bot);

    draw_clipped_string(api, x + 16, box_y + 56, "Memoria RAM:", 0x005FB8, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x + 130, box_y + 56, "128 MB Allocati (Paginazione PML4 a 4 livelli, No-Execute)", 0x1A1A1A, 0, true, clip_top, clip_bot);

    draw_clipped_string(api, x + 16, box_y + 78, "Multiprocessore:", 0x005FB8, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x + 130, box_y + 78, "SMP Hardware Attivo con Local APIC e I/O APIC", 0x1A1A1A, 0, true, clip_top, clip_bot);

    draw_clipped_string(api, x + 16, box_y + 100, "Kernel Budget:", 0x005FB8, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x + 130, box_y + 100, "Conforme (< 480 KB) con Moduli Out-of-Kernel (.MOD / .DLL)", 0x1A1A1A, 0, true, clip_top, clip_bot);

    draw_clipped_string(api, x + 16, box_y + 122, "File System:", 0x005FB8, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x + 130, box_y + 122, "Volume exFAT C: a 64 cluster con VFS dinamico", 0x1A1A1A, 0, true, clip_top, clip_bot);

    // Azioni rapide
    int32_t btn_y = box_y + 172;
    draw_clipped_action_btn(api, x, btn_y, 160, 24, "Gestione periferiche", false, true, clip_top, clip_bot);
    draw_clipped_action_btn(api, x + 170, btn_y, 160, 24, "Informazioni su NewOS", false, false, clip_top, clip_bot);
}

// --- Rendering Sezione RETE E INTERNET ---

static void render_network_section(const kernel_api_t* api, int32_t x, int32_t y, int32_t w, int32_t h,
                                   const settings_render_params_t* p) {
    (void)h;
    int32_t clip_top = p->cy + 38;
    int32_t clip_bot = clip_top + (p->ch - 44);

    draw_clipped_string(api, x, y, "Rete e Connessioni Internet", 0x1A1A1A, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x, y + 16, "Stato interfaccia Ethernet, indirizzo IP e DNS", 0x6E7781, 0, true, clip_top, clip_bot);

    int32_t box_y = y + 36;
    draw_clipped_card_bg(api, x, box_y, w, 140, 6, 0xFFFFFF, 0xDCE1E7, p->is_glass, clip_top, clip_bot);

    draw_clipped_icon_32(api, x + 16, box_y + 16, ICON_NET, clip_top, clip_bot);
    draw_clipped_string(api, x + 60, box_y + 14, "Scheda di Rete Ethernet (Intel E1000 / RTL8139)", 0x1A1A1A, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x + 60, box_y + 30, "Stato: Connesso alla rete locale (1000 Mbps Full Duplex)", 0x137333, 0, true, clip_top, clip_bot);

    if (box_y + 54 >= clip_top && box_y + 54 < clip_bot) {
        api->gfx_fill_rect(x + 16, box_y + 54, w - 32, 1, 0xEEF1F4);
    }

    draw_clipped_string(api, x + 20, box_y + 64, "Indirizzo IPv4: 10.0.2.15", 0x1A1A1A, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x + 240, box_y + 64, "Subnet Mask: 255.255.255.0", 0x5C646D, 0, true, clip_top, clip_bot);

    draw_clipped_string(api, x + 20, box_y + 84, "Gateway Predefinito: 10.0.2.2", 0x1A1A1A, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x + 240, box_y + 84, "Server DNS: 10.0.2.3", 0x5C646D, 0, true, clip_top, clip_bot);

    draw_clipped_string(api, x + 20, box_y + 104, "Protocolli Attivi: TCP/IP, UDP, DHCP, DNS, ARP, ICMP", 0x005FB8, 0, true, clip_top, clip_bot);
}

// --- Rendering Sezione PERSONALIZZAZIONE ---

static void render_personalization_section(const kernel_api_t* api, int32_t x, int32_t y, int32_t w, int32_t h,
                                          const settings_render_params_t* p) {
    (void)h;
    int32_t clip_top = p->cy + 38;
    int32_t clip_bot = clip_top + (p->ch - 44);

    draw_clipped_string(api, x, y, "Personalizzazione e Temi Grafici", 0x1A1A1A, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x, y + 16, "Seleziona uno stile visivo applicato in tempo reale da THEMEMGR.DLL", 0x6E7781, 0, true, clip_top, clip_bot);

    int32_t box_y = y + 36;
    draw_clipped_card_bg(api, x, box_y, w, 170, 6, 0xFFFFFF, 0xDCE1E7, p->is_glass, clip_top, clip_bot);

    const char* themes[] = {
        "1. Windows 11 Fluent Glass Light (Alta Trasparenza, Blur e Rim Speculare)",
        "2. Windows 11 Mica Light (Stile moderno opaco satinato)",
        "3. Windows 11 Mica Dark (Tema scuro per ambienti a basso contrasto)",
        "4. Windows 2000 Classic 3D (Stile tradizionale Bevel tridimensionale)"
    };

    for (int i = 0; i < 4; i++) {
        int32_t ry = box_y + 10 + i * 38;
        draw_clipped_string(api, x + 16, ry + 4, themes[i], 0x1A1A1A, 0, true, clip_top, clip_bot);

        int32_t btn_w = 90;
        int32_t btn_x = x + w - btn_w - 14;
        bool is_hov = (p->mouse_x >= btn_x && p->mouse_x <= btn_x + btn_w &&
                       p->mouse_y >= ry && p->mouse_y <= ry + 22);
        draw_clipped_action_btn(api, btn_x, ry, btn_w, 22, "Applica", is_hov, (i == 0), clip_top, clip_bot);
    }

    draw_clipped_action_btn(api, x, box_y + 180, 200, 24, "Proprieta Schermo Avanzate", false, false, clip_top, clip_bot);
}

// --- Rendering Sezione DISPOSITIVI E AUDIO ---

static void render_devices_section(const kernel_api_t* api, int32_t x, int32_t y, int32_t w, int32_t h,
                                   const settings_render_params_t* p) {
    (void)h;
    int32_t clip_top = p->cy + 38;
    int32_t clip_bot = clip_top + (p->ch - 44);

    draw_clipped_string(api, x, y, "Dispositivi Hardware e Audio", 0x1A1A1A, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x, y + 16, "Regolazione volume AC'97, mixer e periferiche di puntamento", 0x6E7781, 0, true, clip_top, clip_bot);

    int32_t box_y = y + 36;
    draw_clipped_card_bg(api, x, box_y, w, 130, 6, 0xFFFFFF, 0xDCE1E7, p->is_glass, clip_top, clip_bot);

    draw_clipped_icon_32(api, x + 16, box_y + 16, ICON_SOUND, clip_top, clip_bot);
    draw_clipped_string(api, x + 60, box_y + 14, "Altoparlanti AC'97 SoundMAX / Cirrus Logic", 0x1A1A1A, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x + 60, box_y + 30, "Livello Master Volume: 80% (Nessun muto attivo)", 0x5C646D, 0, true, clip_top, clip_bot);

    // Barra volume
    int32_t bar_y = box_y + 54;
    if (bar_y >= clip_top && bar_y + 10 <= clip_bot) {
        api->gfx_fill_rect(x + 60, bar_y, 220, 10, 0xE9ECEF);
        api->gfx_fill_rect(x + 60, bar_y, 176, 10, 0x0078D4);
        api->gfx_draw_rect(x + 60, bar_y, 220, 10, 0xC8D0D8);
    }

    draw_clipped_action_btn(api, x + 60, box_y + 74, 150, 22, "Proprieta Audio...", false, false, clip_top, clip_bot);

    // Sezione Mouse e Tastiera
    int32_t kbox_y = box_y + 142;
    draw_clipped_card_bg(api, x, kbox_y, w, 70, 6, 0xFFFFFF, 0xDCE1E7, p->is_glass, clip_top, clip_bot);
    draw_clipped_icon_16(api, x + 16, kbox_y + 14, ICON_MOUSE, clip_top, clip_bot);
    draw_clipped_string(api, x + 40, kbox_y + 14, "Mouse: PS/2 con schemi cursori personalizzati", 0x1A1A1A, 0, true, clip_top, clip_bot);
    draw_clipped_action_btn(api, x + w - 165, kbox_y + 10, 150, 22, "Proprieta' Mouse...", false, false, clip_top, clip_bot);
    draw_clipped_icon_16(api, x + 16, kbox_y + 38, ICON_KEYBOARD, clip_top, clip_bot);
    draw_clipped_string(api, x + 40, kbox_y + 38, "Tastiera: Layout Italiano (IT), Ripetizione 30 cps", 0x1A1A1A, 0, true, clip_top, clip_bot);
}

// --- Rendering Sezione OPZIONI ENERGIA ---

static void render_power_section(const kernel_api_t* api, int32_t x, int32_t y, int32_t w, int32_t h,
                                 const settings_render_params_t* p) {
    (void)h;
    int32_t clip_top = p->cy + 38;
    int32_t clip_bot = clip_top + (p->ch - 44);

    draw_clipped_string(api, x, y, "Opzioni Risparmio Energia e Spegnimento", 0x1A1A1A, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x, y + 16, "Profili energetici ACPI, sospensione monitor e spegnimento", 0x6E7781, 0, true, clip_top, clip_bot);

    int32_t box_y = y + 36;
    draw_clipped_card_bg(api, x, box_y, w, 110, 6, 0xFFFFFF, 0xDCE1E7, p->is_glass, clip_top, clip_bot);

    draw_clipped_string(api, x + 16, box_y + 12, "[x] Profilo Bilanciato (Ottimale per prestazioni e consumi)", 0x1A1A1A, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x + 16, box_y + 34, "[ ] Prestazioni Elevate (Nessun limite di throttling CPU)", 0x5C646D, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x + 16, box_y + 56, "[ ] Risparmio Energetico (Standby monitor dopo 10 minuti)", 0x5C646D, 0, true, clip_top, clip_bot);

    draw_clipped_action_btn(api, x + 16, box_y + 78, 170, 22, "Configura Profili...", false, false, clip_top, clip_bot);
}

// --- Rendering Sezione SICUREZZA E MANUTENZIONE ---

static void render_security_section(const kernel_api_t* api, int32_t x, int32_t y, int32_t w, int32_t h,
                                    const settings_render_params_t* p) {
    (void)h;
    int32_t clip_top = p->cy + 38;
    int32_t clip_bot = clip_top + (p->ch - 44);

    draw_clipped_string(api, x, y, "Sicurezza, Manutenzione e Risorse", 0x1A1A1A, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x, y + 16, "Integrità moduli firmati NOSM, CRC32 e database di registro", 0x6E7781, 0, true, clip_top, clip_bot);

    int32_t box_y = y + 36;
    draw_clipped_card_bg(api, x, box_y, w, 120, 6, 0xFFFFFF, 0xDCE1E7, p->is_glass, clip_top, clip_bot);

    draw_clipped_string(api, x + 16, box_y + 12, "Firma Moduli di Sistema: NEWSOFT Autenticato", 0x137333, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x + 16, box_y + 32, "Checksum Check: Convalida CRC32 attiva su ogni modulo .MOD / .DLL", 0x1A1A1A, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x + 16, box_y + 52, "Architettura Out-of-Kernel: SETTINGS.DLL e THEMEMGR.DLL protette", 0x005FB8, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x + 16, box_y + 72, "Registro di Sistema: C:\\NewOS\\Registry\\SYSTEM.DAT", 0x1A1A1A, 0, true, clip_top, clip_bot);

    draw_clipped_action_btn(api, x + 16, box_y + 92, 190, 22, "Editor del Registro (REGEDIT)", false, true, clip_top, clip_bot);
}

// --- Rendering Risultati Ricerca ("Trova un'impostazione") ---

static void render_search_results(const kernel_api_t* api, int32_t x, int32_t y, int32_t w, int32_t h,
                                  const settings_render_params_t* p) {
    (void)h;
    int32_t clip_top = p->cy + 38;
    int32_t clip_bot = clip_top + (p->ch - 44);

    draw_clipped_string(api, x, y, "Risultati per la ricerca:", 0x1A1A1A, 0, true, clip_top, clip_bot);
    draw_clipped_string(api, x + 175, y, p->state->search_text, 0x005FB8, 0, true, clip_top, clip_bot);

    const struct {
        const char* name;
        const char* category;
        icon_id_t   icon;
    } all_settings[] = {
        { "Schermo e Risoluzione video", "Sistema > Schermo", ICON_DESKTOP },
        { "Suoni e periferiche audio", "Dispositivi > Audio AC'97", ICON_SOUND },
        { "Gestione periferiche hardware", "Sistema > Driver .SYS", ICON_MAINTENANCE },
        { "Opzioni risparmio energia", "Sistema > Alimentazione", ICON_ENERGY },
        { "Connessioni di rete Ethernet", "Rete e Internet > Scheda", ICON_NET },
        { "Personalizzazione temi e colori", "Personalizzazione > Temi", ICON_DESKTOP },
        { "Informazioni di sistema (WinVer)", "Sistema > Info", ICON_INFO },
        { "Editor del Registro di sistema", "Sicurezza > REGEDIT", ICON_REGISTRY },
        { "Mouse e puntamento", "Dispositivi > Input", ICON_MOUSE },
        { "Tastiera e layout italiano", "Dispositivi > Input", ICON_KEYBOARD }
    };

    int32_t cur_y = y + 26;
    int count = 0;

    for (int i = 0; i < 10; i++) {
        if (!local_str_contains_nocase(all_settings[i].name, p->state->search_text) &&
            !local_str_contains_nocase(all_settings[i].category, p->state->search_text)) {
            continue;
        }

        draw_clipped_card_bg(api, x, cur_y, w, 32, 4, 0xFFFFFF, 0xDCE1E7, p->is_glass, clip_top, clip_bot);
        draw_clipped_icon_16(api, x + 10, cur_y + 8, all_settings[i].icon, clip_top, clip_bot);
        draw_clipped_string(api, x + 34, cur_y + 8, all_settings[i].name, 0x1A1A1A, 0, true, clip_top, clip_bot);
        draw_clipped_string(api, x + 280, cur_y + 8, all_settings[i].category, 0x6E7781, 0, true, clip_top, clip_bot);

        int32_t btn_w = 64;
        int32_t btn_x = x + w - btn_w - 8;
        bool is_hov = (p->mouse_x >= btn_x && p->mouse_x <= btn_x + btn_w &&
                       p->mouse_y >= cur_y + 5 && p->mouse_y <= cur_y + 25);
        draw_clipped_action_btn(api, btn_x, cur_y + 5, btn_w, 20, "Apri >", is_hov, false, clip_top, clip_bot);

        cur_y += 36;
        count++;
    }

    if (count == 0) {
        draw_clipped_string(api, x, cur_y + 10, "Nessuna impostazione trovata corrispondente al criterio.", 0x888888, 0, true, clip_top, clip_bot);
    }
}

// --- Funzione Principale di Rendering della Nuova Modalità Impostazioni ---

static void render_settings_view(const kernel_api_t* api, const settings_render_params_t* p) {
    int32_t cx = p->cx;
    int32_t cy = p->cy;
    int32_t cw = p->cw;
    int32_t ch = p->ch;
    bool is_glass = p->is_glass;

    // Sfondo generale chiaro
    if (is_glass && api->gfx_fill_alpha) {
        api->gfx_fill_alpha(cx, cy, cw, ch, 0xF5F7FA, 220);
    } else {
        api->gfx_fill_rect(cx, cy, cw, ch, 0xF5F7FA);
    }

    int32_t side_w = 175;

    // 1. Sidebar Sinistra (Fluent Navigation Bar)
    if (is_glass && api->gfx_fill_alpha) {
        api->gfx_fill_alpha(cx, cy, side_w, ch, 0xEFF2F6, 180);
    } else {
        api->gfx_fill_rect(cx, cy, side_w, ch, 0xEFF2F6);
    }
    api->gfx_fill_rect(cx + side_w - 1, cy, 1, ch, 0xDCE1E7);

    // Profilo Utente in cima alla Sidebar
    int32_t prof_y = cy + 8;
    api->gfx_fill_rect(cx + 10, prof_y + 2, 28, 28, 0x005FB8);
    api->gfx_draw_rect(cx + 10, prof_y + 2, 28, 28, 0x004588);
    api->gfx_draw_string(cx + 17, prof_y + 8, "GM", 0xFFFFFF, 0, true);

    api->gfx_draw_string(cx + 44, prof_y + 2, "Giuseppe Morgana", 0x1A1A1A, 0, true);
    api->gfx_draw_string(cx + 44, prof_y + 18, "morganagiuseppe@..", 0x707070, 0, true);

    api->gfx_fill_rect(cx + 10, prof_y + 36, side_w - 20, 1, 0xDCE1E7);

    // Voci del menu di navigazione
    int32_t nav_y = prof_y + 44;
    for (int i = 0; i < SETTINGS_NUM_SECTIONS; i++) {
        int32_t iy = nav_y + i * 26;
        bool is_active = (p->state->active_section == i);
        bool is_hover = (!is_active && p->mouse_x >= cx + 6 && p->mouse_x <= cx + side_w - 6 &&
                         p->mouse_y >= iy && p->mouse_y <= iy + 24);

        if (is_active) {
            draw_card_bg(api, cx + 6, iy, side_w - 12, 24, 4, 0xDCE6F3, 0xB8CEE8, false);
            api->gfx_fill_rect(cx + 8, iy + 4, 3, 16, 0x005FB8);
        } else if (is_hover) {
            draw_card_bg(api, cx + 6, iy, side_w - 12, 24, 4, 0xE5EAF0, 0xDCE1E7, false);
        }

        api->gfx_draw_icon_16(cx + 14, iy + 4, s_nav_items[i].icon);
        uint32_t text_col = is_active ? 0x005FB8 : (is_hover ? 0x1A1A1A : 0x333333);
        api->gfx_draw_string(cx + 36, iy + 4, s_nav_items[i].title, text_col, 0, true);
    }

    // In fondo alla sidebar: Link per tornare alla modalità classica
    int32_t classic_btn_y = cy + ch - 32;
    bool is_hov_classic = (p->mouse_x >= cx + 8 && p->mouse_x <= cx + side_w - 8 &&
                           p->mouse_y >= classic_btn_y && p->mouse_y <= classic_btn_y + 24);
    draw_card_bg(api, cx + 8, classic_btn_y, side_w - 16, 24, 4,
                 is_hov_classic ? 0xE4E8EE : 0xFFFFFF, 0xC0C8D0, false);
    api->gfx_draw_string(cx + 14, classic_btn_y + 4, "< Pannello classico", 0x0A246A, 0, true);

    // 2. Geometria Area Contenuti Principale e Scrollbar
    int32_t top_bar_x = cx + side_w + 12;
    int32_t top_bar_y = cy + 6;
    int32_t top_bar_w = cw - side_w - 24;
    int32_t top_bar_h = 24;

    int32_t main_x = top_bar_x;
    int32_t main_y = cy + 38;
    int32_t main_w = top_bar_w;
    int32_t main_h = ch - 44;

    int32_t total_content_h = get_section_content_height(p->state->active_section, p->state->search_text);
    bool has_scrollbar = (total_content_h > main_h);

    int32_t sb_w = 14;
    int32_t sb_x = main_x + main_w - sb_w;
    int32_t content_w = has_scrollbar ? (main_w - sb_w - 8) : main_w;

    // Clamp scroll_y
    int32_t max_scroll = has_scrollbar ? (total_content_h - main_h) : 0;
    if (p->state->scroll_y > max_scroll) p->state->scroll_y = max_scroll;
    if (p->state->scroll_y < 0) p->state->scroll_y = 0;

    int32_t content_y = main_y - p->state->scroll_y;

    // Pulizia preventiva dell'area contenuti
    api->gfx_fill_rect(main_x, main_y, main_w, main_h, 0xF5F7FA);

    // 3. Rendering Sezione Attiva (con coordinate scorse e clipping rigoroso)
    if (p->state->search_text[0] != '\0') {
        render_search_results(api, main_x, content_y, content_w, main_h, p);
    } else {
        switch (p->state->active_section) {
            case SETTINGS_SECTION_HOME:
                render_home_section(api, main_x, content_y, content_w, main_h, p);
                break;
            case SETTINGS_SECTION_SYSTEM:
                render_system_section(api, main_x, content_y, content_w, main_h, p);
                break;
            case SETTINGS_SECTION_NETWORK:
                render_network_section(api, main_x, content_y, content_w, main_h, p);
                break;
            case SETTINGS_SECTION_PERSONAL:
                render_personalization_section(api, main_x, content_y, content_w, main_h, p);
                break;
            case SETTINGS_SECTION_DEVICES:
                render_devices_section(api, main_x, content_y, content_w, main_h, p);
                break;
            case SETTINGS_SECTION_POWER:
                render_power_section(api, main_x, content_y, content_w, main_h, p);
                break;
            case SETTINGS_SECTION_SECURITY:
                render_security_section(api, main_x, content_y, content_w, main_h, p);
                break;
            default:
                render_home_section(api, main_x, content_y, content_w, main_h, p);
                break;
        }
    }

    // 4. Copertura / Sticky Header (pulisce perfettamente l'area superiore per non mostrare elementi sopra la search bar)
    api->gfx_fill_rect(main_x, cy, main_w, 37, 0xF5F7FA);

    // Barra di Ricerca Superiore ("Trova un'impostazione")
    uint32_t search_border = p->state->search_focus ? 0x005FB8 : 0xC8CDD4;
    draw_card_bg(api, top_bar_x, top_bar_y, top_bar_w, top_bar_h, 4, 0xFFFFFF, search_border, false);

    api->gfx_draw_icon_16(top_bar_x + 6, top_bar_y + 4, ICON_SEARCH);
    if (p->state->search_text[0] != '\0') {
        api->gfx_draw_string(top_bar_x + 28, top_bar_y + 4, p->state->search_text, 0x1A1A1A, 0, true);
        api->gfx_draw_string(top_bar_x + top_bar_w - 16, top_bar_y + 4, "x", 0x888888, 0, true);
    } else {
        api->gfx_draw_string(top_bar_x + 28, top_bar_y + 4, "Trova un'impostazione...", 0x888888, 0, true);
    }

    // 5. Scrollbar Moderna a Destra
    if (has_scrollbar) {
        render_fluent_scrollbar(api, sb_x, main_y, sb_w, main_h,
                                total_content_h, main_h, p->state->scroll_y,
                                p->mouse_x, p->mouse_y);
    }
}

// --- Gestione Eventi Mouse nella Nuova Modalità ---

static settings_action_t handle_mouse_events(const kernel_api_t* api, const settings_mouse_params_t* p) {
    (void)api;
    if (!p->clicked) return SETTINGS_ACTION_NONE;

    int32_t cx = p->cx;
    int32_t cy = p->cy;
    int32_t cw = p->cw;
    int32_t ch = p->ch;
    int32_t mx = p->mx;
    int32_t my = p->my;

    int32_t side_w = 175;

    // 1. Clic sul tasto "< Pannello classico" (torna alla modalità classica)
    int32_t classic_btn_y = cy + ch - 32;
    if (mx >= cx + 8 && mx <= cx + side_w - 8 &&
        my >= classic_btn_y && my <= classic_btn_y + 24) {
        return SETTINGS_ACTION_CLASSIC_MODE;
    }

    // 2. Clic sulle schede della barra di navigazione
    int32_t nav_y = cy + 52;
    for (int i = 0; i < SETTINGS_NUM_SECTIONS; i++) {
        int32_t iy = nav_y + i * 26;
        if (mx >= cx + 6 && mx <= cx + side_w - 6 && my >= iy && my <= iy + 24) {
            p->state->active_section = i;
            p->state->search_text[0] = '\0';
            p->state->search_focus = false;
            p->state->scroll_y = 0; // Reset scorrimento all'apertura della sezione
            return SETTINGS_ACTION_NONE;
        }
    }

    // 3. Clic sulla barra di ricerca
    int32_t top_bar_x = cx + side_w + 12;
    int32_t top_bar_y = cy + 6;
    int32_t top_bar_w = cw - side_w - 24;
    int32_t top_bar_h = 24;

    if (mx >= top_bar_x && mx <= top_bar_x + top_bar_w &&
        my >= top_bar_y && my <= top_bar_y + top_bar_h) {
        if (p->state->search_text[0] != '\0' && mx >= top_bar_x + top_bar_w - 20) {
            p->state->search_text[0] = '\0';
            p->state->scroll_y = 0;
        } else {
            p->state->search_focus = true;
        }
        return SETTINGS_ACTION_NONE;
    } else {
        p->state->search_focus = false;
    }

    // 4. Geometria e Interazione con la Scrollbar Moderna
    int32_t main_x = top_bar_x;
    int32_t main_y = cy + 38;
    int32_t main_w = top_bar_w;
    int32_t main_h = ch - 44;

    int32_t total_content_h = get_section_content_height(p->state->active_section, p->state->search_text);
    bool has_scrollbar = (total_content_h > main_h);
    int32_t sb_w = 14;
    int32_t sb_x = main_x + main_w - sb_w;

    if (has_scrollbar && mx >= sb_x && mx <= sb_x + sb_w && my >= main_y && my <= main_y + main_h) {
        int32_t max_scroll = total_content_h - main_h;
        int32_t btn_h = 14;
        int32_t track_y = main_y + btn_h;
        int32_t track_h = main_h - (btn_h * 2);

        if (my < main_y + btn_h) {
            // Freccia Su: scorrimento di una riga
            p->state->scroll_y -= 32;
        } else if (my > main_y + main_h - btn_h) {
            // Freccia Giù: scorrimento di una riga
            p->state->scroll_y += 32;
        } else if (track_h > 16) {
            int32_t thumb_h = (track_h * main_h) / total_content_h;
            if (thumb_h < 22) thumb_h = 22;
            if (thumb_h > track_h) thumb_h = track_h;

            int32_t thumb_y = track_y + (p->state->scroll_y * (track_h - thumb_h)) / (max_scroll > 0 ? max_scroll : 1);

            if (my < thumb_y) {
                // Pagina Su
                p->state->scroll_y -= (main_h * 3) / 4;
            } else if (my > thumb_y + thumb_h) {
                // Pagina Giù
                p->state->scroll_y += (main_h * 3) / 4;
            } else {
                // Clic sul thumb / posizionamento proporzionale
                int32_t rel = my - track_y - (thumb_h / 2);
                p->state->scroll_y = (rel * max_scroll) / (track_h - thumb_h > 0 ? (track_h - thumb_h) : 1);
            }
        }

        if (p->state->scroll_y > max_scroll) p->state->scroll_y = max_scroll;
        if (p->state->scroll_y < 0) p->state->scroll_y = 0;
        return SETTINGS_ACTION_NONE;
    }

    // 5. Clic nell'area principale di contenuto (tenendo conto dello scorrimento)
    int32_t clip_top = main_y;
    int32_t clip_bot = main_y + main_h;
    int32_t content_y = main_y - p->state->scroll_y;

    if (p->state->search_text[0] != '\0') {
        int32_t cur_y = content_y + 26;
        const settings_action_t search_actions[] = {
            SETTINGS_ACTION_LAUNCH_DISPLAY,
            SETTINGS_ACTION_LAUNCH_SOUND,
            SETTINGS_ACTION_LAUNCH_DEVMGMT,
            SETTINGS_ACTION_LAUNCH_POWER,
            SETTINGS_ACTION_NONE,
            SETTINGS_ACTION_LAUNCH_DISPLAY,
            SETTINGS_ACTION_LAUNCH_ABOUT,
            SETTINGS_ACTION_LAUNCH_REGEDIT,
            SETTINGS_ACTION_LAUNCH_MOUSE,
            SETTINGS_ACTION_NONE
        };

        for (int i = 0; i < 10; i++) {
            if (my >= cur_y && my <= cur_y + 32 && my >= clip_top && my <= clip_bot) {
                return search_actions[i];
            }
            cur_y += 36;
        }
        return SETTINGS_ACTION_NONE;
    }

    // Azioni specifiche per sezione con offset scroll_y
    if (p->state->active_section == SETTINGS_SECTION_HOME) {
        // Clic su riga "Impostazioni consigliate"
        int32_t cur_y = content_y + 56 + 12 + 32;
        if (my >= cur_y && my <= cur_y + 116 && my >= clip_top && my <= clip_bot) {
            int row = (my - cur_y) / 28;
            if (row == 0) return SETTINGS_ACTION_LAUNCH_DISPLAY;
            if (row == 1) return SETTINGS_ACTION_LAUNCH_SOUND;
            if (row == 2) return SETTINGS_ACTION_LAUNCH_DEVMGMT;
            if (row == 3) return SETTINGS_ACTION_LAUNCH_POWER;
        }

        // Clic sui bottoni tema rapido
        int32_t theme_y = cur_y + 116 + 12 + 18;
        if (my >= theme_y + 12 && my <= theme_y + 38 && my >= clip_top && my <= clip_bot) {
            int32_t tbtn_w = (main_w - 20) / 4;
            if (tbtn_w > 120) tbtn_w = 120;
            for (int t = 0; t < 4; t++) {
                int32_t bx = main_x + 10 + t * (tbtn_w + 8);
                if (mx >= bx && mx <= bx + tbtn_w) {
                    if (t == 0) return SETTINGS_ACTION_APPLY_THEME_1; // Fluent Glass Light
                    if (t == 1) return SETTINGS_ACTION_APPLY_THEME_2; // Mica Light
                    if (t == 2) return SETTINGS_ACTION_APPLY_THEME_3; // Mica Dark
                    if (t == 3) return SETTINGS_ACTION_APPLY_THEME_0; // Classic
                }
            }
        }

        // Clic su "Strumenti di Sistema"
        int32_t sys_y = theme_y + 52 + 12 + 44 + 10;
        if (my >= clip_top && my <= clip_bot) {
            if (my >= sys_y && my <= sys_y + 28) return SETTINGS_ACTION_LAUNCH_ABOUT;
            if (my >= sys_y + 28 && my <= sys_y + 58) return SETTINGS_ACTION_LAUNCH_REGEDIT;
        }
    } else if (p->state->active_section == SETTINGS_SECTION_SYSTEM) {
        int32_t btn_y = content_y + 36 + 172;
        if (my >= btn_y && my <= btn_y + 24 && my >= clip_top && my <= clip_bot) {
            if (mx >= main_x && mx <= main_x + 160) return SETTINGS_ACTION_LAUNCH_DEVMGMT;
            if (mx >= main_x + 170 && mx <= main_x + 330) return SETTINGS_ACTION_LAUNCH_ABOUT;
        }
    } else if (p->state->active_section == SETTINGS_SECTION_PERSONAL) {
        int32_t box_y = content_y + 36;
        for (int i = 0; i < 4; i++) {
            int32_t ry = box_y + 10 + i * 38;
            if (my >= ry && my <= ry + 24 && my >= clip_top && my <= clip_bot) {
                if (i == 0) return SETTINGS_ACTION_APPLY_THEME_1;
                if (i == 1) return SETTINGS_ACTION_APPLY_THEME_2;
                if (i == 2) return SETTINGS_ACTION_APPLY_THEME_3;
                if (i == 3) return SETTINGS_ACTION_APPLY_THEME_0;
            }
        }
        if (my >= box_y + 180 && my <= box_y + 204 && my >= clip_top && my <= clip_bot) {
            return SETTINGS_ACTION_LAUNCH_DISPLAY;
        }
    } else if (p->state->active_section == SETTINGS_SECTION_DEVICES) {
        int32_t box_y = content_y + 36;
        if (my >= box_y + 74 && my <= box_y + 96 && my >= clip_top && my <= clip_bot) {
            return SETTINGS_ACTION_LAUNCH_SOUND;
        }
        int32_t kbox_y = box_y + 142;
        if (my >= kbox_y + 10 && my <= kbox_y + 32 && mx >= main_x + main_w - 175 && mx <= main_x + main_w && my >= clip_top && my <= clip_bot) {
            return SETTINGS_ACTION_LAUNCH_MOUSE;
        }
    } else if (p->state->active_section == SETTINGS_SECTION_POWER) {
        int32_t box_y = content_y + 36;
        if (my >= box_y + 78 && my <= box_y + 100 && my >= clip_top && my <= clip_bot) {
            return SETTINGS_ACTION_LAUNCH_POWER;
        }
    } else if (p->state->active_section == SETTINGS_SECTION_SECURITY) {
        int32_t box_y = content_y + 36;
        if (my >= box_y + 92 && my <= box_y + 114 && my >= clip_top && my <= clip_bot) {
            return SETTINGS_ACTION_LAUNCH_REGEDIT;
        }
    }

    return SETTINGS_ACTION_NONE;
}

// --- Gestione Eventi Tastiera (Digitazione nel campo di ricerca e Scorrimento) ---

static bool handle_key_events(const kernel_api_t* api, const settings_key_params_t* p) {
    (void)api;
    if (!p->state) return false;

    char c = p->key;

    // Se la barra di ricerca non ha il focus, supporta lo scorrimento da tastiera
    if (!p->state->search_focus) {
        int32_t total_content_h = get_section_content_height(p->state->active_section, p->state->search_text);
        int32_t max_scroll = (total_content_h > 260) ? (total_content_h - 260) : 0;

        if (c == 'j' || c == 'J' || c == 0x1F) { // Scorrimento giù (freccia giù o tasto 'j')
            p->state->scroll_y += 32;
            if (p->state->scroll_y > max_scroll) p->state->scroll_y = max_scroll;
            return true;
        }
        if (c == 'k' || c == 'K' || c == 0x1E) { // Scorrimento su (freccia su o tasto 'k')
            p->state->scroll_y -= 32;
            if (p->state->scroll_y < 0) p->state->scroll_y = 0;
            return true;
        }
        if (c == ' ' || c == 0x12) { // Pagina giù (Barra spaziatrice o PageDown)
            p->state->scroll_y += 120;
            if (p->state->scroll_y > max_scroll) p->state->scroll_y = max_scroll;
            return true;
        }
        if (c == 'b' || c == 'B' || c == 0x11) { // Pagina su (Tasto 'b' o PageUp)
            p->state->scroll_y -= 120;
            if (p->state->scroll_y < 0) p->state->scroll_y = 0;
            return true;
        }
    }

    if (c == 27) { // Escape
        p->state->search_focus = false;
        p->state->search_text[0] = '\0';
        p->state->scroll_y = 0;
        return true;
    }

    if (c == 8) { // Backspace
        if (!p->state->search_focus) return false;
        size_t len = local_strlen(p->state->search_text);
        if (len > 0) {
            p->state->search_text[len - 1] = '\0';
            p->state->scroll_y = 0;
            return true;
        }
    }

    if (c >= 32 && c <= 126 && c != '\r' && c != '\n') {
        if (!p->state->search_focus) return false;
        size_t len = local_strlen(p->state->search_text);
        if (len < sizeof(p->state->search_text) - 1) {
            p->state->search_text[len] = c;
            p->state->search_text[len + 1] = '\0';
            p->state->scroll_y = 0;
            return true;
        }
    }

    return false;
}

// --- Entry Point Unico del Modulo all'offset 64 (.text.entry) ---

__attribute__((section(".text.entry"), used))
int settings_dll_entry(const kernel_api_t* api, int cmd, void* params) {
    if (!api || !params) return 0;

    if (cmd == SETTINGS_CMD_INIT) {
        settings_state_t* st = (settings_state_t*)params;
        st->active_section = SETTINGS_SECTION_HOME;
        st->search_text[0] = '\0';
        st->search_focus = false;
        st->hovered_item = -1;
        st->scroll_y = 0;
        return 1;
    }

    if (cmd == SETTINGS_CMD_RENDER) {
        render_settings_view(api, (const settings_render_params_t*)params);
        return 1;
    }

    if (cmd == SETTINGS_CMD_MOUSE) {
        return (int)handle_mouse_events(api, (const settings_mouse_params_t*)params);
    }

    if (cmd == SETTINGS_CMD_KEY) {
        return handle_key_events(api, (const settings_key_params_t*)params) ? 1 : 0;
    }

    return 0;
}
