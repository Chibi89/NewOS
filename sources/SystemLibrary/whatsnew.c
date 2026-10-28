/**
 * SystemLibrary/whatsnew.c
 * Libreria Dinamica Out-of-Kernel per NewOS V1 (64-bit)
 * Visualizzatore interattivo delle novita' e del catalogo aggiornamenti (WHATSNEW.DLL)
 * (C) Newspoint Software - NewSoft Dynamic System Library
 */

#include <screen_modules.h>
#include <gui/wm.h>
#include <gui/graphics.h>
#include <gui/icons.h>

#ifndef BUILDING_KERNEL_LOADER
// Header di protezione del modulo (esattamente 64 byte all'inizio del file .DLL)
__attribute__((section(".header"), used))
module_header_t g_whatsnew_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size      = 0, // Popolato dal build system
    .entry_offset   = 64, // Posizione dell'entry point
    .checksum       = 0, // Popolato dal build system
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "WHATSNEW",
    .signature      = MODULE_AUTH_TAG
};
#endif

static const kernel_api_t* g_api = NULL;

#define SB_SIZE           16
#define MAX_CATALOG_ITEMS 48
#define MAX_ITEM_LEN      104

typedef struct {
    char items[MAX_CATALOG_ITEMS][MAX_ITEM_LEN];
    int  total_lines;
    int  scroll_offset;
    int  visible_lines;
    char pkg_name[80];   // [Update_Catalog] PackageName
    char rel_date[24];   // [Update_Catalog] ReleaseDate
    int  file_count;     // numero voci effettive in [Files]
} whatsnew_state_t;

static size_t local_strlen(const char* s) {
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

static int local_strncmp(const char* s1, const char* s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static char* local_strcat(char* dest, const char* src) {
    char* d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

static void wn_copy(char* dst, int dst_sz, const char* src, int len) {
    if (len > dst_sz - 1) len = dst_sz - 1;
    for (int i = 0; i < len; i++) dst[i] = src[i];
    dst[len] = '\0';
}

static void wn_add_line(whatsnew_state_t* s, const char* prefix, const char* val, int vlen) {
    if (s->total_lines >= MAX_CATALOG_ITEMS) return;
    char* out = s->items[s->total_lines];
    int cp = 0;
    while (prefix && *prefix && cp < MAX_ITEM_LEN - 1) out[cp++] = *prefix++;
    for (int i = 0; i < vlen && cp < MAX_ITEM_LEN - 1; i++) out[cp++] = val[i];
    out[cp] = '\0';
    s->total_lines++;
}

static void whatsnew_load_catalog(whatsnew_state_t* state) {
    state->total_lines = 0;
    state->scroll_offset = 0;
    state->file_count = 0;
    state->pkg_name[0] = '\0';
    state->rel_date[0] = '\0';

    if (!g_api || !g_api->vfs_read_entire_file) return;

    uint32_t sz = 0;
    char* buf = (char*)g_api->vfs_read_entire_file("C:\\NewOS\\Update\\CHANCECHATALOGY.ini", &sz);
    if (!buf) buf = (char*)g_api->vfs_read_entire_file("/NewOS/Update/CHANCECHATALOGY.ini", &sz);
    if (!buf) return;

    static const char* file_ptr[MAX_CATALOG_ITEMS];
    static int         file_len[MAX_CATALOG_ITEMS];
    int nfiles = 0;

    enum { SEC_NONE, SEC_CATALOG, SEC_NEWS, SEC_FILES } sec = SEC_NONE;
    const char* p = buf;
    const char* end = buf + sz;

    while (p < end && *p) {
        const char* ls = p;
        while (p < end && *p && *p != '\r' && *p != '\n') p++;
        int len = (int)(p - ls);
        while (p < end && (*p == '\r' || *p == '\n')) p++;

        while (len > 0 && (*ls == ' ' || *ls == '\t')) { ls++; len--; }
        while (len > 0 && (ls[len - 1] == ' ' || ls[len - 1] == '\t')) len--;
        if (len == 0) continue;

        if (ls[0] == '[') {
            if (len >= 16 && local_strncmp(ls, "[Update_Catalog]", 16) == 0) sec = SEC_CATALOG;
            else if (len >= 10 && local_strncmp(ls, "[WhatsNew]", 10) == 0)  sec = SEC_NEWS;
            else if (len >= 7  && local_strncmp(ls, "[Files]", 7) == 0)      sec = SEC_FILES;
            else sec = SEC_NONE;
            continue;
        }
        if (ls[0] == ';' || ls[0] == '#') continue;

        int eq = -1;
        for (int i = 0; i < len; i++) if (ls[i] == '=') { eq = i; break; }
        const char* val = (eq >= 0) ? ls + eq + 1 : ls;
        int vlen = (eq >= 0) ? len - eq - 1 : len;

        if (sec == SEC_CATALOG && eq > 0) {
            if (eq == 11 && local_strncmp(ls, "PackageName", 11) == 0)
                wn_copy(state->pkg_name, sizeof(state->pkg_name), val, vlen);
            else if (eq == 11 && local_strncmp(ls, "ReleaseDate", 11) == 0)
                wn_copy(state->rel_date, sizeof(state->rel_date), val, vlen);
        } else if (sec == SEC_NEWS) {
            if (val[0] == '-' || val[0] == '*') wn_add_line(state, "", val, vlen);
            else wn_add_line(state, "- ", val, vlen);
        } else if (sec == SEC_FILES && vlen > 0 && nfiles < MAX_CATALOG_ITEMS) {
            file_ptr[nfiles] = val;
            file_len[nfiles] = vlen;
            nfiles++;
        }
    }

    state->file_count = nfiles;
    if (nfiles > 0) {
        if (state->total_lines > 0) wn_add_line(state, "", "", 0);
        wn_add_line(state, "", "File aggiornati dal pacchetto:", 30);
        for (int i = 0; i < nfiles; i++) wn_add_line(state, "  + ", file_ptr[i], file_len[i]);
    }

    if (g_api->kfree) g_api->kfree(buf);
}

static void whatsnew_close_and_clear_token(window_t* win) {
    if (!g_api) return;
    const char* off_token = "[WhatsNew]\r\nShowOnFirstLogin=0\r\n";
    if (g_api->vfs_write_entire_file_inplace) {
        g_api->vfs_write_entire_file_inplace("C:\\NewOS\\Update\\pending_whatsnew.ini", off_token, local_strlen(off_token));
    }
    if (win->app_data && g_api->kfree) {
        g_api->kfree(win->app_data);
        win->app_data = NULL;
    }
    if (g_api->wm_destroy_window) {
        g_api->wm_destroy_window(win->id);
    }
}

static void draw_btn(int32_t x, int32_t y, int32_t w, int32_t h, const char* label, bool pressed) {
    if (!g_api) return;
    g_api->gfx_fill_rect(x, y, w, h, COLOR_BTNFACE);
    g_api->gfx_draw_bevel_rect(x, y, w, h, pressed);
    int len = (int)local_strlen(label);
    g_api->gfx_draw_string(x + (w - len * 8) / 2 + (pressed ? 1 : 0),
                           y + (h - 14) / 2 + (pressed ? 1 : 0), label, COLOR_WINDOWTEXT, 0, true);
}

static void draw_vscrollbar(int32_t x, int32_t y, int32_t h, int cur, int total, int visible) {
    if (!g_api) return;
    int32_t tlen = h - 2 * SB_SIZE;
    if (tlen <= 0) return;

    draw_btn(x, y, SB_SIZE, SB_SIZE, "^", false);
    draw_btn(x, y + h - SB_SIZE, SB_SIZE, SB_SIZE, "v", false);

    int32_t ty = y + SB_SIZE;
    g_api->gfx_fill_rect(x, ty, SB_SIZE, tlen, 0xE0DCC0);
    g_api->gfx_draw_bevel_rect(x, ty, SB_SIZE, tlen, true);

    int max_s = (total > visible) ? (total - visible) : 0;
    int thumb_sz = (total <= visible) ? tlen : (tlen * visible / (total ? total : 1));
    if (thumb_sz < 12) thumb_sz = 12;
    if (thumb_sz > tlen) thumb_sz = tlen;
    int thumb_pos = (max_s > 0) ? (cur * (tlen - thumb_sz) / max_s) : 0;

    g_api->gfx_fill_rect(x, ty + thumb_pos, SB_SIZE, thumb_sz, COLOR_BTNFACE);
    g_api->gfx_draw_bevel_rect(x, ty + thumb_pos, SB_SIZE, thumb_sz, false);
}

static void whatsnew_draw(window_t* win) {
    if (!g_api) return;
    whatsnew_state_t* state = (whatsnew_state_t*)win->app_data;
    if (!state) return;

    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    g_api->gfx_fill_rect(cx, cy, cw, ch, COLOR_BTNFACE);
    g_api->gfx_draw_gradient_h(cx + 4, cy + 4, cw - 8, 48, 0x0A246A, 0x3A6EA5);
    g_api->gfx_draw_icon_32(cx + 12, cy + 12, ICON_INFO);

    if (state->pkg_name[0]) {
        g_api->gfx_draw_string(cx + 54, cy + 10, state->pkg_name, 0xFFFFFF, 0, true);
        char sub[96] = "Data rilascio: ";
        local_strcat(sub, state->rel_date[0] ? state->rel_date : "n/d");
        g_api->gfx_draw_string(cx + 54, cy + 28, sub, 0xD0E0FF, 0, true);
    } else {
        g_api->gfx_draw_string(cx + 54, cy + 10, "Catalogo CHANCECHATALOGY.ini non trovato", 0xFFFFFF, 0, true);
        g_api->gfx_draw_string(cx + 54, cy + 28, "C:\\NewOS\\Update", 0xD0E0FF, 0, true);
    }

    g_api->gfx_draw_bevel_rect(cx + 4, cy + 56, cw - 8, 2, true);

    int32_t box_x = cx + 12;
    int32_t box_y = cy + 62;
    int32_t box_w = cw - 24;
    int32_t box_h = ch - 114;

    g_api->gfx_fill_rect(box_x, box_y, box_w, box_h, COLOR_WINDOW);
    g_api->gfx_draw_bevel_rect(box_x, box_y, box_w, box_h, true);

    {
        char hdr[80] = "Contenuto di UPDATE.PAK - file nel catalogo: ";
        char num[12];
        int n = state->file_count, k = 0;
        char tmp[12];
        do { tmp[k++] = (char)('0' + n % 10); n /= 10; } while (n > 0 && k < 11);
        for (int i = 0; i < k; i++) num[i] = tmp[k - 1 - i];
        num[k] = '\0';
        local_strcat(hdr, num);
        g_api->gfx_draw_string(box_x + 10, box_y + 8, hdr, 0x003366, 0, true);
    }
    g_api->gfx_draw_bevel_rect(box_x + 8, box_y + 25, box_w - 16, 2, true);

    int32_t text_y = box_y + 32;
    int32_t item_h = 20;
    int visible_lines = (box_h - 40) / item_h;
    if (visible_lines < 1) visible_lines = 1;
    state->visible_lines = visible_lines;

    int max_scroll = (state->total_lines > visible_lines) ? (state->total_lines - visible_lines) : 0;
    if (state->scroll_offset > max_scroll) state->scroll_offset = max_scroll;
    if (state->scroll_offset < 0) state->scroll_offset = 0;

    int start_idx = state->scroll_offset;
    for (int i = 0; i < visible_lines && (start_idx + i) < state->total_lines; i++) {
        g_api->gfx_draw_string(box_x + 10, text_y + (i * item_h), state->items[start_idx + i], COLOR_WINDOWTEXT, 0, true);
    }

    int32_t sb_x = box_x + box_w - SB_SIZE - 2;
    int32_t sb_y = box_y + 27;
    int32_t sb_h = box_h - 29;
    draw_vscrollbar(sb_x, sb_y, sb_h, state->scroll_offset, state->total_lines, visible_lines);

    int32_t btn_y = cy + ch - 38;
    int32_t btn1_x = cx + 12;
    int32_t btn1_w = 210;
    int32_t btn1_h = 26;
    draw_btn(btn1_x, btn_y, btn1_w, btn1_h, "Esplora Cartella Update", false);

    int32_t btn2_w = 100;
    int32_t btn2_x = cx + cw - 12 - btn2_w;
    int32_t btn2_h = 26;
    draw_btn(btn2_x, btn_y, btn2_w, btn2_h, "Chiudi", false);
}

static void whatsnew_mouse(window_t* win, int32_t lx, int32_t ly, bool clicked) {
    if (!clicked || !g_api) return;
    whatsnew_state_t* state = (whatsnew_state_t*)win->app_data;
    if (!state) return;

    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;
    int32_t btn_y = 24 + ch - 38;

    int32_t btn1_x = 4 + 12;
    int32_t btn1_w = 210;
    int32_t btn1_h = 26;
    if (lx >= btn1_x && lx <= (btn1_x + btn1_w) && ly >= btn_y && ly <= (btn_y + btn1_h)) {
        if (g_api->app_explorer_open_folder) {
            g_api->app_explorer_open_folder("NewOS/Update");
        }
        return;
    }

    int32_t btn2_w = 100;
    int32_t btn2_x = 4 + cw - 12 - btn2_w;
    int32_t btn2_h = 26;
    if (lx >= btn2_x && lx <= (btn2_x + btn2_w) && ly >= btn_y && ly <= (btn_y + btn2_h)) {
        whatsnew_close_and_clear_token(win);
        return;
    }

    int32_t box_x = 4 + 12;
    int32_t box_y = 24 + 62;
    int32_t box_w = cw - 24;
    int32_t box_h = ch - 114;

    int32_t sb_x = box_x + box_w - SB_SIZE - 2;
    int32_t sb_y = box_y + 27;
    int32_t sb_h = box_h - 29;

    int max_scroll = (state->total_lines > state->visible_lines) ? (state->total_lines - state->visible_lines) : 0;

    if (lx >= sb_x && lx <= (sb_x + SB_SIZE) && ly >= sb_y && ly <= (sb_y + SB_SIZE)) {
        if (state->scroll_offset > 0) state->scroll_offset--;
        return;
    }

    if (lx >= sb_x && lx <= (sb_x + SB_SIZE) && ly >= (sb_y + sb_h - SB_SIZE) && ly <= (sb_y + sb_h)) {
        if (state->scroll_offset < max_scroll) state->scroll_offset++;
        return;
    }

    if (lx >= sb_x && lx <= (sb_x + SB_SIZE) && ly > (sb_y + SB_SIZE) && ly < (sb_y + sb_h - SB_SIZE)) {
        int32_t tlen = sb_h - 2 * SB_SIZE;
        int thumb_sz = (state->total_lines <= state->visible_lines) ? tlen : (tlen * state->visible_lines / (state->total_lines ? state->total_lines : 1));
        if (thumb_sz < 12) thumb_sz = 12;
        int thumb_pos = (max_scroll > 0) ? (state->scroll_offset * (tlen - thumb_sz) / max_scroll) : 0;
        int click_ty = ly - (sb_y + SB_SIZE);

        if (click_ty < thumb_pos) {
            if (state->scroll_offset > 0) state->scroll_offset--;
        } else if (click_ty > (thumb_pos + thumb_sz)) {
            if (state->scroll_offset < max_scroll) state->scroll_offset++;
        }
        return;
    }
}

static void whatsnew_key(window_t* win, char c) {
    whatsnew_state_t* state = (whatsnew_state_t*)win->app_data;
    if (!state) return;

    int max_scroll = (state->total_lines > state->visible_lines) ? (state->total_lines - state->visible_lines) : 0;

    if (c == 'w' || c == 'k' || c == '-') {
        if (state->scroll_offset > 0) state->scroll_offset--;
        return;
    }
    if (c == 's' || c == 'j' || c == '+') {
        if (state->scroll_offset < max_scroll) state->scroll_offset++;
        return;
    }

    if (c == 27 || c == '\n' || c == '\r') {
        whatsnew_close_and_clear_token(win);
    }
}

// Entry Point Ufficiale della Libreria Dinamica Out-of-Kernel WHATSNEW.DLL
int whatsnew_dll_entry(const kernel_api_t* api, window_t* win) {
    if (!api || !win) return -1;
    g_api = api;

    whatsnew_state_t* state = (whatsnew_state_t*)g_api->kmalloc(sizeof(whatsnew_state_t));
    if (!state) return -1;

    whatsnew_load_catalog(state);
    state->visible_lines = 9;

    win->app_data = state;
    win->on_draw = whatsnew_draw;
    win->on_mouse = whatsnew_mouse;
    win->on_key = whatsnew_key;

    return 0;
}
