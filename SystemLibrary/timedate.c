/**
 * SystemLibrary/timedate.c
 * Libreria Dinamica Out-of-Kernel TIMEDATE.DLL (Applet Data, Ora e Lingua - TIMEDATE.CPL)
 * Lettura in tempo reale dell'orologio hardware CMOS RTC, calendario mensile,
 * configurazione Fuso Orario e Layout di Tastiera / Lingua di Sistema.
 * (C) Newspoint Software - NewSoft Dynamic System Library
 */

#include "cpl_dll_ui.h"
#include <drivers/rtc.h>

#ifndef BUILDING_KERNEL_LOADER
__attribute__((section(".header"), used))
module_header_t g_timedate_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size      = 0,
    .entry_offset   = 64,
    .checksum       = 0,
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "TIMEDATE",
    .signature      = MODULE_AUTH_TAG
};
#endif

static const kernel_api_t* g_api = NULL;

typedef struct {
    int  active_tab;      // 0: Data e Ora, 1: Lingua e Tastiera
    int  tz_index;        // 0..4
    int  hour_offset;     // Correzione manuale ore (-12..+12)
    bool dst_enabled;     // Ora legale automatica (+1h)
    int  kbd_layout_idx;  // 0: Italiano (IT), 1: Inglese (US), 2: Internazionale
    int  date_fmt_idx;    // 0: GG/MM/AAAA, 1: AAAA-MM-GG
    char status_msg[64];
} timedate_state_t;

static void get_tz_label(int idx, char* out, size_t max_len) {
    if (idx == 0) dll_strcpy(out, "(UTC+01:00) Roma, Berlino, Berna, Parigi", max_len);
    else if (idx == 1) dll_strcpy(out, "(UTC+00:00) Londra, Dublino, Lisbona (UTC)", max_len);
    else if (idx == 2) dll_strcpy(out, "(UTC+02:00) Atene, Bucarest, Helsinki", max_len);
    else if (idx == 3) dll_strcpy(out, "(UTC-05:00) Eastern Time (Stati Uniti)", max_len);
    else dll_strcpy(out, "(UTC+09:00) Osaka, Sapporo, Tokyo (JST)", max_len);
}

static void get_kbd_label(int idx, char* out, size_t max_len) {
    if (idx == 0) dll_strcpy(out, "Italiano (Italia) - QWERTY [00000410]", max_len);
    else if (idx == 1) dll_strcpy(out, "Inglese (Stati Uniti) - US [00000409]", max_len);
    else dll_strcpy(out, "Europeo Internazionale - EUR [00020409]", max_len);
}

static void save_timedate_settings(timedate_state_t* st) {
    if (!g_api || !st) return;
    if (g_api->registry_set_value_sz) {
        char tz_buf[64];
        get_tz_label(st->tz_index, tz_buf, sizeof(tz_buf));
        g_api->registry_set_value_sz("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\TimeZoneInformation", "TimeZoneKeyName", tz_buf);

        char kbd_buf[64];
        get_kbd_label(st->kbd_layout_idx, kbd_buf, sizeof(kbd_buf));
        g_api->registry_set_value_sz("HKEY_CURRENT_USER\\Keyboard Layout", "ActiveLayout", kbd_buf);
        g_api->registry_set_value_sz("HKEY_CURRENT_USER\\NEWOS\\PREFERENCES", "Language", (st->kbd_layout_idx == 1) ? "en-US" : "it-IT");

        if (g_api->registry_save_to_disk) g_api->registry_save_to_disk();
    }
    dll_strcpy(st->status_msg, "Impostazioni Data, Ora e Lingua salvate nel Registro!", sizeof(st->status_msg));
}

static void timedate_draw(window_t* win) {
    timedate_state_t* st = (timedate_state_t*)win->app_data;
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
    g_api->wm_draw_themed_tab(cx + 8,   tab_y, 125, tab_h, "Data e Ora", st->active_tab == 0);
    g_api->wm_draw_themed_tab(cx + 135, tab_y, 165, tab_h, "Lingua e Tastiera", st->active_tab == 1);
    dll_draw_tabs_divider(g_api, cx, tab_y, tab_h, cw, &t);

    rtc_time_t rtc;
    rtc.hour = 12; rtc.minute = 0; rtc.second = 0;
    rtc.day = 28; rtc.month = 10; rtc.year = 2026;
    if (g_api->rtc_get_time) {
        g_api->rtc_get_time(&rtc);
    }

    int adj_hour = ((int)rtc.hour + st->hour_offset + (st->dst_enabled ? 1 : 0) + 24) % 24;

    if (st->active_tab == 0) {
        // Box 1: Orologio CMOS e Calendario
        int32_t b1_x = cx + 10;
        int32_t b1_y = cy + 36;
        int32_t b1_w = cw - 20;
        int32_t b1_h = 148;
        dll_draw_groupbox(g_api, b1_x, b1_y, b1_w, b1_h, " Orologio Hardware CMOS (RTC) e Data Corrente ", &t);

        // Display digitale grande a sinistra
        int32_t clk_x = b1_x + 14;
        int32_t clk_y = b1_y + 22;
        int32_t clk_w = 178;
        int32_t clk_h = 82;
        g_api->gfx_fill_rect(clk_x, clk_y, clk_w, clk_h, t.input_bg);
        g_api->gfx_draw_rect(clk_x, clk_y, clk_w, clk_h, t.input_border);

        char hh[4], mm[4], ss[4], dd[4], mo[4], yy[8];
        dll_uint_to_str((uint32_t)adj_hour, hh, 1);
        dll_uint_to_str(rtc.minute, mm, 1);
        dll_uint_to_str(rtc.second, ss, 1);
        dll_uint_to_str(rtc.day, dd, 1);
        dll_uint_to_str(rtc.month, mo, 1);
        dll_uint_to_str(rtc.year, yy, 0);

        char time_str[24];
        time_str[0] = 'O'; time_str[1] = 'r'; time_str[2] = 'a'; time_str[3] = ':'; time_str[4] = ' ';
        time_str[5] = hh[0]; time_str[6] = hh[1]; time_str[7] = ':';
        time_str[8] = mm[0]; time_str[9] = mm[1]; time_str[10] = ':';
        time_str[11] = ss[0]; time_str[12] = ss[1]; time_str[13] = '\0';

        char date_str[32];
        if (st->date_fmt_idx == 0) {
            date_str[0] = 'D'; date_str[1] = 'a'; date_str[2] = 't'; date_str[3] = 'a'; date_str[4] = ':'; date_str[5] = ' ';
            date_str[6] = dd[0]; date_str[7] = dd[1]; date_str[8] = '/';
            date_str[9] = mo[0]; date_str[10] = mo[1]; date_str[11] = '/';
            date_str[12] = yy[0]; date_str[13] = yy[1]; date_str[14] = yy[2]; date_str[15] = yy[3]; date_str[16] = '\0';
        } else {
            date_str[0] = 'I'; date_str[1] = 'S'; date_str[2] = 'O'; date_str[3] = ':'; date_str[4] = ' ';
            date_str[5] = yy[0]; date_str[6] = yy[1]; date_str[7] = yy[2]; date_str[8] = yy[3]; date_str[9] = '-';
            date_str[10] = mo[0]; date_str[11] = mo[1]; date_str[12] = '-';
            date_str[13] = dd[0]; date_str[14] = dd[1]; date_str[15] = '\0';
        }

        g_api->gfx_draw_string(clk_x + 12, clk_y + 14, time_str, t.accent, 0, true);
        g_api->gfx_draw_string(clk_x + 12, clk_y + 38, date_str, t.text_primary, 0, true);
        g_api->gfx_draw_string(clk_x + 12, clk_y + 58, "Chip CMOS RTC 0x70", t.text_secondary, 0, true);

        // Bottoni correzione rapida ora
        g_api->wm_draw_themed_button(clk_x,       clk_y + clk_h + 6, 56, 22, "-1 Ora", false);
        g_api->wm_draw_themed_button(clk_x + 60,  clk_y + clk_h + 6, 56, 22, "+1 Ora", false);
        g_api->wm_draw_themed_button(clk_x + 120, clk_y + clk_h + 6, 58, 22, "Reset", false);

        // Mini-Calendario a destra
        int32_t cal_x = b1_x + 202;
        int32_t cal_y = b1_y + 22;
        int32_t cal_w = b1_w - 214;
        int32_t cal_h = 110;
        g_api->gfx_fill_rect(cal_x, cal_y, cal_w, cal_h, t.input_bg);
        g_api->gfx_draw_rect(cal_x, cal_y, cal_w, cal_h, t.input_border);
        g_api->gfx_fill_rect(cal_x + 1, cal_y + 1, cal_w - 2, 18, t.accent);
        g_api->gfx_draw_string(cal_x + 10, cal_y + 3, "LU MA ME GI VE SA DO", t.accent_text, 0, true);

        for (int d = 1; d <= 31; d++) {
            int row = (d - 1) / 7;
            int col = (d - 1) % 7;
            int32_t dx = cal_x + 10 + col * 24;
            int32_t dy = cal_y + 22 + row * 17;
            char dbuf[4];
            dll_uint_to_str((uint32_t)d, dbuf, 1);
            if (d == (int)rtc.day) {
                g_api->gfx_fill_rect(dx - 2, dy - 1, 20, 15, t.sel_bg);
                g_api->gfx_draw_string(dx, dy, dbuf, t.sel_text, 0, true);
            } else {
                g_api->gfx_draw_string(dx, dy, dbuf, t.text_primary, 0, true);
            }
        }

        // Box 2: Fuso Orario
        int32_t b2_x = cx + 10;
        int32_t b2_y = b1_y + b1_h + 10;
        int32_t b2_w = cw - 20;
        int32_t b2_h = 104;
        dll_draw_groupbox(g_api, b2_x, b2_y, b2_w, b2_h, " Fuso Orario e Ora Legale ", &t);

        char tz_str[64];
        get_tz_label(st->tz_index, tz_str, sizeof(tz_str));
        g_api->gfx_fill_rect(b2_x + 12, b2_y + 20, b2_w - 84, 24, t.input_bg);
        g_api->gfx_draw_rect(b2_x + 12, b2_y + 20, b2_w - 84, 24, t.input_border);
        g_api->gfx_draw_string(b2_x + 18, b2_y + 25, tz_str, t.text_primary, 0, true);

        g_api->wm_draw_themed_button(b2_x + b2_w - 66, b2_y + 20, 24, 24, "<", false);
        g_api->wm_draw_themed_button(b2_x + b2_w - 38, b2_y + 20, 24, 24, ">", false);

        const char* dst_lbl = st->dst_enabled ? "[x] Passa automaticamente all'ora legale (DST +1h)"
                                              : "[ ] Passa automaticamente all'ora legale (DST +1h)";
        g_api->gfx_draw_string(b2_x + 14, b2_y + 56, dst_lbl, t.text_primary, 0, true);
    } else {
        // Scheda 1: Lingua e Tastiera
        int32_t b1_x = cx + 10;
        int32_t b1_y = cy + 36;
        int32_t b1_w = cw - 20;
        int32_t b1_h = 130;
        dll_draw_groupbox(g_api, b1_x, b1_y, b1_w, b1_h, " Lingua di Sistema e Layout di Tastiera ", &t);

        g_api->gfx_draw_icon_32(b1_x + 16, b1_y + 22, ICON_KEYBOARD);
        g_api->gfx_draw_string(b1_x + 58, b1_y + 22, "Layout di input attivo per PS/2 e USB HID:", t.text_primary, 0, true);

        char kbd_str[64];
        get_kbd_label(st->kbd_layout_idx, kbd_str, sizeof(kbd_str));
        g_api->gfx_fill_rect(b1_x + 14, b1_y + 58, b1_w - 86, 24, t.input_bg);
        g_api->gfx_draw_rect(b1_x + 14, b1_y + 58, b1_w - 86, 24, t.input_border);
        g_api->gfx_draw_string(b1_x + 20, b1_y + 63, kbd_str, t.text_primary, 0, true);

        g_api->wm_draw_themed_button(b1_x + b1_w - 66, b1_y + 58, 24, 24, "<", false);
        g_api->wm_draw_themed_button(b1_x + b1_w - 38, b1_y + 58, 24, 24, ">", false);

        g_api->gfx_draw_string(b1_x + 14, b1_y + 94, "Codifica caratteri: UTF-8 / CP850 Estesa", t.text_secondary, 0, true);

        // Box 2: Formato Data e Numeri
        int32_t b2_x = cx + 10;
        int32_t b2_y = b1_y + b1_h + 12;
        int32_t b2_w = cw - 20;
        int32_t b2_h = 120;
        dll_draw_groupbox(g_api, b2_x, b2_y, b2_w, b2_h, " Formati Internazionali ", &t);

        const char* fmt_lbl = (st->date_fmt_idx == 0) ? "Formato Data Breve: GG/MM/AAAA (Europeo)"
                                                      : "Formato Data Breve: AAAA-MM-GG (ISO 8601)";
        g_api->gfx_draw_string(b2_x + 14, b2_y + 24, fmt_lbl, t.text_primary, 0, true);
        g_api->wm_draw_themed_button(b2_x + 14, b2_y + 48, 180, 24, "Cambia Formato Data", false);
        g_api->gfx_draw_string(b2_x + 14, b2_y + 84, "Valuta predefinita: Euro (EUR) - Separatore decimale: ','", t.text_secondary, 0, true);
    }

    int32_t foot_y = cy + ch - 32;
    if (st->status_msg[0]) {
        g_api->gfx_draw_string(cx + 12, foot_y - 18, st->status_msg, t.is_dark ? 0x4EC9B0 : 0x107C41, 0, true);
    }
    g_api->wm_draw_themed_button(cx + cw - 264, foot_y, 80, 24, "Applica", false);
    g_api->wm_draw_themed_button(cx + cw - 176, foot_y, 80, 24, "OK", false);
    g_api->wm_draw_themed_button(cx + cw - 88,  foot_y, 80, 24, "Chiudi", false);
}

static void timedate_mouse(window_t* win, int32_t lx, int32_t ly, bool clicked) {
    if (!clicked) return;
    timedate_state_t* st = (timedate_state_t*)win->app_data;
    if (!st || !g_api) return;

    int32_t cx = 6;
    int32_t cy = 28;
    int32_t cw = win->width - 12;
    int32_t ch = win->height - 34;

    if (ly >= cy + 4 && ly <= cy + 26) {
        if (lx >= cx + 8 && lx < cx + 133) { st->active_tab = 0; return; }
        if (lx >= cx + 135 && lx < cx + 300) { st->active_tab = 1; return; }
    }

    if (st->active_tab == 0) {
        int32_t b1_x = cx + 10;
        int32_t b1_y = cy + 36;
        int32_t clk_x = b1_x + 14;
        int32_t btn_y = b1_y + 22 + 82 + 6;
        if (ly >= btn_y && ly <= btn_y + 22) {
            if (lx >= clk_x && lx <= clk_x + 56) { st->hour_offset--; return; }
            if (lx >= clk_x + 62 && lx <= clk_x + 118) { st->hour_offset++; return; }
            if (lx >= clk_x + 124 && lx <= clk_x + 185) { st->hour_offset = 0; return; }
        }

        int32_t b2_x = cx + 10;
        int32_t b2_y = b1_y + 148 + 10;
        int32_t b2_w = cw - 20;
        if (ly >= b2_y + 20 && ly <= b2_y + 44) {
            if (lx >= b2_x + b2_w - 66 && lx <= b2_x + b2_w - 42) {
                st->tz_index = (st->tz_index + 4) % 5;
                return;
            }
            if (lx >= b2_x + b2_w - 38 && lx <= b2_x + b2_w - 14) {
                st->tz_index = (st->tz_index + 1) % 5;
                return;
            }
        }
        if (ly >= b2_y + 52 && ly <= b2_y + 72 && lx >= b2_x + 12 && lx <= b2_x + 360) {
            st->dst_enabled = !st->dst_enabled;
            return;
        }
    } else {
        int32_t b1_x = cx + 10;
        int32_t b1_y = cy + 36;
        int32_t b1_w = cw - 20;
        if (ly >= b1_y + 58 && ly <= b1_y + 82) {
            if (lx >= b1_x + b1_w - 66 && lx <= b1_x + b1_w - 42) {
                st->kbd_layout_idx = (st->kbd_layout_idx + 2) % 3;
                return;
            }
            if (lx >= b1_x + b1_w - 38 && lx <= b1_x + b1_w - 14) {
                st->kbd_layout_idx = (st->kbd_layout_idx + 1) % 3;
                return;
            }
        }
        int32_t b2_x = cx + 10;
        int32_t b2_y = b1_y + 130 + 12;
        if (lx >= b2_x + 14 && lx <= b2_x + 194 && ly >= b2_y + 48 && ly <= b2_y + 72) {
            st->date_fmt_idx = (st->date_fmt_idx + 1) % 2;
            return;
        }
    }

    int32_t foot_y = cy + ch - 32;
    if (ly >= foot_y && ly <= foot_y + 24) {
        if (lx >= cx + cw - 264 && lx <= cx + cw - 184) {
            save_timedate_settings(st);
            return;
        }
        if (lx >= cx + cw - 176 && lx <= cx + cw - 96) {
            save_timedate_settings(st);
            g_api->wm_destroy_window(win->id);
            return;
        }
        if (lx >= cx + cw - 88 && lx <= cx + cw - 8) {
            g_api->wm_destroy_window(win->id);
            return;
        }
    }
}

static void timedate_key(window_t* win, char c) {
    if (c == 27 && g_api) {
        g_api->wm_destroy_window(win->id);
    }
}

__attribute__((section(".text.entry"), used))
int timedate_dll_entry(const kernel_api_t* api, window_t* win) {
    if (!api || !win) return -1;
    g_api = api;

    timedate_state_t* st = (timedate_state_t*)g_api->kmalloc(sizeof(timedate_state_t));
    if (!st) return -1;
    for (size_t i = 0; i < sizeof(timedate_state_t); i++) ((uint8_t*)st)[i] = 0;

    st->tz_index = 0;
    st->dst_enabled = true;
    st->kbd_layout_idx = 0;
    st->date_fmt_idx = 0;

    win->app_data = st;
    win->on_draw  = timedate_draw;
    win->on_mouse = timedate_mouse;
    win->on_key   = timedate_key;
    return 0;
}

