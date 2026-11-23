/**
 * SystemLibrary/keybcpl.c
 * Libreria Dinamica Out-of-Kernel KEYBCPL.DLL (Applet Proprietà Tastiera - KEYBOARD.CPL)
 * Configurazione ritardo e velocità di ripetizione dei tasti, frequenza di lampeggio
 * del cursore, casella di test interattiva e diagnostica controller PS/2 8042 e USB HID.
 * (C) Newspoint Software - NewSoft Dynamic System Library
 */

#include "cpl_dll_ui.h"
#include <kernel_ext_api.h>

#ifndef BUILDING_KERNEL_LOADER
__attribute__((section(".header"), used))
module_header_t g_keybcpl_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size      = 0,
    .entry_offset   = 64,
    .checksum       = 0,
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "KEYBCPL",
    .signature      = MODULE_AUTH_TAG
};
#endif

static const kernel_api_t* g_api = NULL;

typedef struct {
    int  active_tab;       // 0: Velocita', 1: Hardware, 2: Layout
    int  repeat_delay;     // 1..4 (1=Lungo .. 4=Breve)
    int  repeat_speed;     // 1..10 (1=Lenta .. 10=Veloce)
    int  blink_rate;       // 1..10
    char test_buffer[48];
    bool test_focused;
    int  selected_hw_row;  // 0: PS/2 8042, 1: USB HID
    int  layout_id;        // 0: US, 1: IT, 2: UK, 3: DE, 4: FR
    char status_msg[64];
} keybcpl_state_t;

static void draw_slider(int32_t x, int32_t y, int32_t w, int val, int max_val, const dll_theme_t* t) {
    if (!g_api) return;
    g_api->gfx_fill_rect(x, y + 7, w, 4, t->card_border);
    int32_t pos = (max_val > 1) ? ((val - 1) * (w - 14)) / (max_val - 1) : 0;
    g_api->gfx_fill_rect(x, y + 7, pos, 4, t->accent);
    g_api->gfx_fill_rect(x + pos, y + 1, 14, 16, t->accent);
    g_api->gfx_draw_rect(x + pos, y + 1, 14, 16, t->accent_text);
}

static void save_keyboard_settings(keybcpl_state_t* st) {
    if (!g_api || !st) return;
    const kernel_ext_api_t* ext = (const kernel_ext_api_t*)g_api->ext_api;
    if (ext && ext->keyboard_set_layout) {
        ext->keyboard_set_layout(st->layout_id);
    }
    if (g_api->registry_set_value_dword) {
        g_api->registry_set_value_dword("HKEY_CURRENT_USER\\Control Panel\\Keyboard", "KeyboardDelay", (uint32_t)st->repeat_delay);
        g_api->registry_set_value_dword("HKEY_CURRENT_USER\\Control Panel\\Keyboard", "KeyboardSpeed", (uint32_t)st->repeat_speed);
        g_api->registry_set_value_dword("HKEY_CURRENT_USER\\Control Panel\\Keyboard", "CursorBlinkRate", (uint32_t)st->blink_rate);
        g_api->registry_set_value_dword("HKEY_CURRENT_USER\\Control Panel\\Keyboard", "Layout", (uint32_t)st->layout_id);
        if (g_api->registry_save_to_disk) g_api->registry_save_to_disk();
    }
    dll_strcpy(st->status_msg, "Parametri e layout tastiera salvati!", sizeof(st->status_msg));
}

static void keybcpl_draw(window_t* win) {
    keybcpl_state_t* st = (keybcpl_state_t*)win->app_data;
    if (!st || !g_api) return;

    dll_theme_t t;
    dll_theme_init(g_api, &t);

    int32_t cx = win->x + 6;
    int32_t cy = win->y + 28;
    int32_t cw = win->width - 12;
    int32_t ch = win->height - 34;

    dll_draw_dialog_bg(g_api, &t, cx, cy, cw, ch);

    int32_t tab_y = cy + 4;
    int32_t tab_h = 22;
    g_api->wm_draw_themed_tab(cx + 8,   tab_y, 110, tab_h, "Velocita'", st->active_tab == 0);
    g_api->wm_draw_themed_tab(cx + 120, tab_y, 110, tab_h, "Hardware",  st->active_tab == 1);
    g_api->wm_draw_themed_tab(cx + 232, tab_y, 100, tab_h, "Layout",    st->active_tab == 2);
    dll_draw_tabs_divider(g_api, cx, tab_y, tab_h, cw, &t);

    if (st->active_tab == 0) {
        // Box 1: Ripetizione caratteri
        int32_t b1_x = cx + 10;
        int32_t b1_y = cy + 36;
        int32_t b1_w = cw - 20;
        int32_t b1_h = 178;
        dll_draw_groupbox(g_api, b1_x, b1_y, b1_w, b1_h, " Ripetizione Caratteri ", &t);

        g_api->gfx_draw_icon_32(b1_x + 14, b1_y + 22, ICON_KEYBOARD);

        g_api->gfx_draw_string(b1_x + 56, b1_y + 18, "Ritardo di ripetizione:", t.text_primary, 0, true);
        g_api->gfx_draw_string(b1_x + 56, b1_y + 38, "Lungo", t.text_secondary, 0, true);
        draw_slider(b1_x + 108, b1_y + 36, 190, st->repeat_delay, 4, &t);
        g_api->gfx_draw_string(b1_x + 308, b1_y + 38, "Breve", t.text_secondary, 0, true);

        g_api->gfx_draw_string(b1_x + 56, b1_y + 66, "Velocita' di ripetizione:", t.text_primary, 0, true);
        g_api->gfx_draw_string(b1_x + 56, b1_y + 86, "Lenta", t.text_secondary, 0, true);
        draw_slider(b1_x + 108, b1_y + 84, 190, st->repeat_speed, 10, &t);
        g_api->gfx_draw_string(b1_x + 308, b1_y + 86, "Veloce", t.text_secondary, 0, true);

        g_api->gfx_draw_string(b1_x + 14, b1_y + 116, "Clicca qui e tieni premuto un tasto di prova:", t.text_primary, 0, true);
        dll_draw_input_box(g_api, b1_x + 14, b1_y + 136, b1_w - 28, 24, st->test_buffer, st->test_focused, false, &t);

        // Box 2: Intermittenza del cursore
        int32_t b2_x = cx + 10;
        int32_t b2_y = b1_y + b1_h + 10;
        int32_t b2_w = cw - 20;
        int32_t b2_h = 84;
        dll_draw_groupbox(g_api, b2_x, b2_y, b2_w, b2_h, " Velocita' di Intermittenza del Cursore ", &t);

        // Indicatore cursore lampeggiante
        uint64_t ticks = g_api->timer_get_ticks ? g_api->timer_get_ticks() : 0;
        uint64_t period = (uint64_t)(120 - st->blink_rate * 10);
        if (period < 15) period = 15;
        bool blink_on = ((ticks / period) % 2) == 0;
        g_api->gfx_fill_rect(b2_x + 22, b2_y + 26, 28, 32, t.input_bg);
        g_api->gfx_draw_rect(b2_x + 22, b2_y + 26, 28, 32, t.input_border);
        if (blink_on) {
            g_api->gfx_fill_rect(b2_x + 34, b2_y + 32, 3, 20, t.accent);
        }

        g_api->gfx_draw_string(b2_x + 62, b2_y + 34, "Lenta", t.text_secondary, 0, true);
        draw_slider(b2_x + 112, b2_y + 32, 190, st->blink_rate, 10, &t);
        g_api->gfx_draw_string(b2_x + 312, b2_y + 34, "Veloce", t.text_secondary, 0, true);
    } else if (st->active_tab == 1) {
        // Scheda 1: Hardware
        int32_t b1_x = cx + 10;
        int32_t b1_y = cy + 36;
        int32_t b1_w = cw - 20;
        int32_t b1_h = 135;
        dll_draw_groupbox(g_api, b1_x, b1_y, b1_w, b1_h, " Dispositivi Tastiera Installati ", &t);

        int32_t lx = b1_x + 12;
        int32_t ly = b1_y + 20;
        int32_t lw = b1_w - 24;
        g_api->gfx_fill_rect(lx, ly, lw, 96, t.input_bg);
        g_api->gfx_draw_rect(lx, ly, lw, 96, t.input_border);

        for (int r = 0; r < 2; r++) {
            int32_t ry = ly + 4 + r * 32;
            bool sel = (st->selected_hw_row == r);
            if (sel) {
                g_api->gfx_fill_rect(lx + 2, ry, lw - 4, 28, t.sel_bg);
            }
            g_api->gfx_draw_icon_16(lx + 8, ry + 6, ICON_KEYBOARD);
            const char* dev_name = (r == 0) ? "Tastiera Standard PS/2 101/102 Tasti (Intel 8042)"
                                            : "Tastiera USB HID Universale (Boot Protocol)";
            g_api->gfx_draw_string(lx + 32, ry + 7, dev_name, sel ? t.sel_text : t.text_primary, 0, true);
        }

        int32_t b2_x = cx + 10;
        int32_t b2_y = b1_y + b1_h + 10;
        int32_t b2_w = cw - 20;
        int32_t b2_h = 126;
        dll_draw_groupbox(g_api, b2_x, b2_y, b2_w, b2_h, " Proprieta' del Dispositivo ", &t);

        if (st->selected_hw_row == 0) {
            g_api->gfx_draw_string(b2_x + 14, b2_y + 22, "Produttore:     (Tastiere standard x86_64)", t.text_primary, 0, true);
            g_api->gfx_draw_string(b2_x + 14, b2_y + 44, "Ubicazione:     Controller i8042 (Porte I/O 0x60, 0x64 - IRQ 1)", t.text_primary, 0, true);
        } else {
            g_api->gfx_draw_string(b2_x + 14, b2_y + 22, "Produttore:     USB Implementers Forum (HID Class)", t.text_primary, 0, true);
            g_api->gfx_draw_string(b2_x + 14, b2_y + 44, "Ubicazione:     Root Hub USB (UHCI / OHCI / xHCI)", t.text_primary, 0, true);
        }
        g_api->gfx_draw_string(b2_x + 14, b2_y + 68, "Stato periferica: Il dispositivo funziona correttamente.", 0x107C41, 0, true);
        g_api->gfx_draw_string(b2_x + 14, b2_y + 92, "Driver attivo:    C:\\NewOS\\Driver\\KEYBOARD.SYS", t.text_secondary, 0, true);
    } else if (st->active_tab == 2) {
        int32_t box_x = cx + 10, box_y = cy + 36, box_w = cw - 20, box_h = ch - 82;
        dll_draw_groupbox(g_api, box_x, box_y, box_w, box_h, "Layout Tastiera e Mappatura Tasti", &t);
        static const char* k_layout_names[5] = {
            "US  - Inglese (Stati Uniti / QWERTY Standard)",
            "IT  - Italiano (QWERTY, tasti accentati e simboli)",
            "UK  - Inglese (Regno Unito / GBP e simboli UK)",
            "DE  - Tedesco (QWERTZ / Y-Z invertiti)",
            "FR  - Francese (AZERTY / A-Q e W-Z invertiti)"
        };
        for (int i = 0; i < 5; i++) {
            int32_t ry = box_y + 24 + i * 32;
            bool sel = (st->layout_id == i);
            g_api->gfx_fill_rounded_rect(box_x + 12, ry, box_w - 24, 26, 4, sel ? t.sel_bg : t.input_bg);
            g_api->gfx_draw_rounded_rect(box_x + 12, ry, box_w - 24, 26, 4, sel ? t.accent : t.card_border);
            g_api->gfx_draw_string(box_x + 22, ry + 7, k_layout_names[i], sel ? t.sel_text : t.text_primary, 0, true);
        }
        g_api->gfx_draw_string(box_x + 14, box_y + box_h - 24,
                               "Suggerimento: puoi cambiare layout anche dalla barra in basso a destra.",
                               t.text_secondary, 0, true);
    }

    int32_t foot_y = cy + ch - 32;
    if (st->status_msg[0]) {
        g_api->gfx_draw_string(cx + 12, foot_y - 18, st->status_msg, t.is_dark ? 0x4EC9B0 : 0x107C41, 0, true);
    }
    g_api->wm_draw_themed_button(cx + cw - 264, foot_y, 80, 24, "Applica", false);
    g_api->wm_draw_themed_button(cx + cw - 176, foot_y, 80, 24, "OK", false);
    g_api->wm_draw_themed_button(cx + cw - 88,  foot_y, 80, 24, "Chiudi", false);
}

static void keybcpl_mouse(window_t* win, int32_t lx, int32_t ly, bool clicked) {
    if (!clicked) return;
    keybcpl_state_t* st = (keybcpl_state_t*)win->app_data;
    if (!st || !g_api) return;

    int32_t cx = 6;
    int32_t cy = 28;
    int32_t cw = win->width - 12;
    int32_t ch = win->height - 34;

    if (ly >= cy + 4 && ly <= cy + 26) {
        if (lx >= cx + 8 && lx < cx + 118)   { st->active_tab = 0; return; }
        if (lx >= cx + 120 && lx < cx + 230) { st->active_tab = 1; return; }
        if (lx >= cx + 232 && lx < cx + 332) { st->active_tab = 2; return; }
    }

    if (st->active_tab == 0) {
        int32_t b1_x = cx + 10;
        int32_t b1_y = cy + 36;
        // Slider 1: Ritardo (1..4)
        if (lx >= b1_x + 108 && lx <= b1_x + 298 && ly >= b1_y + 30 && ly <= b1_y + 56) {
            st->repeat_delay = 1 + ((lx - (b1_x + 108)) * 3) / 190;
            if (st->repeat_delay < 1) st->repeat_delay = 1;
            if (st->repeat_delay > 4) st->repeat_delay = 4;
            return;
        }
        // Slider 2: Velocita' (1..10)
        if (lx >= b1_x + 108 && lx <= b1_x + 298 && ly >= b1_y + 78 && ly <= b1_y + 104) {
            st->repeat_speed = 1 + ((lx - (b1_x + 108)) * 9) / 190;
            if (st->repeat_speed < 1) st->repeat_speed = 1;
            if (st->repeat_speed > 10) st->repeat_speed = 10;
            return;
        }
        // Casella test
        if (lx >= b1_x + 14 && lx <= b1_x + cw - 34 && ly >= b1_y + 136 && ly <= b1_y + 160) {
            st->test_focused = true;
            return;
        }
        // Slider 3: Blink rate (1..10)
        int32_t b2_x = cx + 10;
        int32_t b2_y = b1_y + 178 + 10;
        if (lx >= b2_x + 112 && lx <= b2_x + 302 && ly >= b2_y + 26 && ly <= b2_y + 52) {
            st->blink_rate = 1 + ((lx - (b2_x + 112)) * 9) / 190;
            if (st->blink_rate < 1) st->blink_rate = 1;
            if (st->blink_rate > 10) st->blink_rate = 10;
            return;
        }
    } else if (st->active_tab == 1) {
        int32_t ly_list = cy + 36 + 20;
        if (lx >= cx + 22 && lx <= cx + cw - 22 && ly >= ly_list && ly <= ly_list + 68) {
            st->selected_hw_row = (ly - ly_list >= 34) ? 1 : 0;
            return;
        }
    } else if (st->active_tab == 2) {
        int32_t box_x = cx + 10, box_y = cy + 36, box_w = cw - 20;
        for (int i = 0; i < 5; i++) {
            int32_t ry = box_y + 24 + i * 32;
            if (lx >= box_x + 12 && lx <= box_x + box_w - 12 && ly >= ry && ly <= ry + 26) {
                st->layout_id = i;
                const kernel_ext_api_t* ext = (const kernel_ext_api_t*)g_api->ext_api;
                if (ext && ext->keyboard_set_layout) {
                    ext->keyboard_set_layout(i);
                }
                dll_strcpy(st->status_msg, "Layout tastiera applicato (premi OK/Applica per salvare)", sizeof(st->status_msg));
                return;
            }
        }
    }

    int32_t foot_y = cy + ch - 32;
    if (ly >= foot_y && ly <= foot_y + 24) {
        if (lx >= cx + cw - 264 && lx <= cx + cw - 184) {
            save_keyboard_settings(st);
            return;
        }
        if (lx >= cx + cw - 176 && lx <= cx + cw - 96) {
            save_keyboard_settings(st);
            g_api->wm_destroy_window(win->id);
            return;
        }
        if (lx >= cx + cw - 88 && lx <= cx + cw - 8) {
            g_api->wm_destroy_window(win->id);
            return;
        }
    }
}

static void keybcpl_key(window_t* win, char c) {
    keybcpl_state_t* st = (keybcpl_state_t*)win->app_data;
    if (!st || !g_api) return;

    if (c == 27) {
        g_api->wm_destroy_window(win->id);
        return;
    }
    if (st->active_tab == 0 && st->test_focused) {
        size_t len = dll_strlen(st->test_buffer);
        if (c == '\b') {
            if (len > 0) st->test_buffer[len - 1] = '\0';
            return;
        }
        if (c >= 32 && c <= 126 && len < 42) {
            st->test_buffer[len] = c;
            st->test_buffer[len + 1] = '\0';
        }
    }
}

__attribute__((section(".text.entry"), used))
int keybcpl_dll_entry(const kernel_api_t* api, window_t* win) {
    if (!api || !win) return -1;
    g_api = api;

    keybcpl_state_t* st = (keybcpl_state_t*)g_api->kmalloc(sizeof(keybcpl_state_t));
    if (!st) return -1;
    for (size_t i = 0; i < sizeof(keybcpl_state_t); i++) ((uint8_t*)st)[i] = 0;

    st->repeat_delay = 3;
    st->repeat_speed = 8;
    st->blink_rate   = 6;
    st->test_focused = true;
    {
        const kernel_ext_api_t* ext = (const kernel_ext_api_t*)g_api->ext_api;
        if (ext && ext->keyboard_get_layout) {
            st->layout_id = ext->keyboard_get_layout();
        }
    }

    win->app_data = st;
    win->on_draw  = keybcpl_draw;
    win->on_mouse = keybcpl_mouse;
    win->on_key   = keybcpl_key;
    return 0;
}

