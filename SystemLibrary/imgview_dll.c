/**
 * SystemLibrary/imgview_dll.c
 * Visualizzatore Immagini Out-of-Kernel (IMGVIEW.DLL)
 * Supporta zoom, rotazione 90/180/270 gradi, adattamento e navigazione libreria.
 * (C) Newspoint Software - NewSoft Dynamic System Library
 */

#include "cpl_dll_ui.h"
#include <kernel_ext_api.h>

#ifndef BUILDING_KERNEL_LOADER
__attribute__((section(".header"), used))
module_header_t g_imgview_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size      = 0,
    .entry_offset   = 64,
    .checksum       = 0,
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "IMGVIEW",
    .signature      = MODULE_AUTH_TAG
};
#endif

static const kernel_api_t* g_api = NULL;

#define IV_THUMB_W 320
#define IV_THUMB_H 240

typedef struct {
    uint32_t thumb[IV_THUMB_W * IV_THUMB_H];
    int      orig_w;
    int      orig_h;
    int      zoom_pct;   // 50, 75, 100, 125, 150
    int      rot_deg;    // 0, 90, 180, 270
    int      cur_idx;
    char     cur_path[96];
    bool     loaded;
} imgview_state_t;

static void iv_u32_str(char* out, uint32_t v) {
    char tmp[16];
    int i = 0;
    if (v == 0) { out[0] = '0'; out[1] = '\0'; return; }
    while (v > 0 && i < 14) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    int j = 0;
    while (i > 0) out[j++] = tmp[--i];
    out[j] = '\0';
}

static void iv_get_sample_path(int idx, char* out, size_t max_len) {
    if (idx == 0) dll_strcpy(out, "C:\\NewOS\\Resources\\Wallpapers\\FLOW.BMP", max_len);
    else if (idx == 1) dll_strcpy(out, "C:\\NewOS\\Resources\\Wallpapers\\BLISS.BMP", max_len);
    else if (idx == 2) dll_strcpy(out, "C:\\NewOS\\Resources\\Wallpapers\\HERO.BMP", max_len);
    else dll_strcpy(out, "C:\\NewOS\\Resources\\Wallpapers\\THEME_A.BMP", max_len);
}

static bool iv_load_bmp(imgview_state_t* st, int idx) {
    if (!g_api || !g_api->vfs_read_entire_file) return false;
    char path[96];
    iv_get_sample_path(idx, path, sizeof(path));
    uint32_t sz = 0;
    uint8_t* raw = (uint8_t*)g_api->vfs_read_entire_file(path, &sz);
    if (!raw || sz < 54 || raw[0] != 'B' || raw[1] != 'M') {
        if (raw) g_api->kfree(raw);
        return false;
    }

    uint32_t data_off = *(uint32_t*)(raw + 10);
    int32_t w = *(int32_t*)(raw + 18);
    int32_t h = *(int32_t*)(raw + 22);
    uint16_t bpp = *(uint16_t*)(raw + 28);
    bool top_down = (h < 0);
    if (h < 0) h = -h;
    if (w <= 0 || h <= 0 || (bpp != 24 && bpp != 32)) {
        g_api->kfree(raw);
        return false;
    }

    uint32_t bytes_pp = bpp / 8;
    uint32_t stride = ((uint32_t)w * bytes_pp + 3) & ~3U;

    for (int ty = 0; ty < IV_THUMB_H; ty++) {
        int sy = (ty * h) / IV_THUMB_H;
        int bmp_row = top_down ? sy : (h - 1 - sy);
        uint32_t row_off = data_off + (uint32_t)bmp_row * stride;
        for (int tx = 0; tx < IV_THUMB_W; tx++) {
            int sx = (tx * w) / IV_THUMB_W;
            uint32_t p_off = row_off + (uint32_t)sx * bytes_pp;
            if (p_off + 2 < sz) {
                uint8_t b = raw[p_off];
                uint8_t g = raw[p_off + 1];
                uint8_t r = raw[p_off + 2];
                st->thumb[ty * IV_THUMB_W + tx] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        }
    }

    g_api->kfree(raw);
    st->orig_w = w;
    st->orig_h = h;
    st->cur_idx = idx;
    dll_strcpy(st->cur_path, path, sizeof(st->cur_path));
    st->loaded = true;
    return true;
}

static void imgview_draw(window_t* win) {
    if (!g_api || !win || !win->app_data) return;
    imgview_state_t* st = (imgview_state_t*)win->app_data;
    dll_theme_t t;
    dll_theme_init(g_api, &t);

    int32_t cx = win->x + 6, cy = win->y + 26;
    int32_t cw = win->width - 12, ch = win->height - 32;

    // Toolbar superiore
    g_api->wm_draw_themed_button(cx,      cy, 76, 22, "Zoom -", false);
    g_api->wm_draw_themed_button(cx + 80, cy, 76, 22, "Zoom +", false);
    g_api->wm_draw_themed_button(cx + 160, cy, 86, 22, "Ruota 90", false);
    g_api->wm_draw_themed_button(cx + 250, cy, 76, 22, "Adatta", false);
    g_api->wm_draw_themed_button(cx + 330, cy, 90, 22, "Prossima", false);

    // Viewport scuro per immagine
    int32_t vx = cx, vy = cy + 28;
    int32_t vw = cw, vh = ch - 52;
    g_api->gfx_fill_rect(vx, vy, vw, vh, 0x0F172A);
    g_api->gfx_draw_rect(vx, vy, vw, vh, 0x334155);

    if (st->loaded) {
        int disp_w = (IV_THUMB_W * st->zoom_pct) / 100;
        int disp_h = (IV_THUMB_H * st->zoom_pct) / 100;
        if (st->rot_deg == 90 || st->rot_deg == 270) {
            int tmp = disp_w; disp_w = disp_h; disp_h = tmp;
        }
        if (disp_w > vw - 8) disp_w = vw - 8;
        if (disp_h > vh - 8) disp_h = vh - 8;
        int32_t ox = vx + (vw - disp_w) / 2;
        int32_t oy = vy + (vh - disp_h) / 2;

        for (int dy = 0; dy < disp_h; dy++) {
            for (int dx = 0; dx < disp_w; dx++) {
                int sx = 0, sy = 0;
                if (st->rot_deg == 0) {
                    sx = (dx * IV_THUMB_W) / disp_w;
                    sy = (dy * IV_THUMB_H) / disp_h;
                } else if (st->rot_deg == 90) {
                    sx = (dy * IV_THUMB_W) / disp_h;
                    sy = IV_THUMB_H - 1 - ((dx * IV_THUMB_H) / disp_w);
                } else if (st->rot_deg == 180) {
                    sx = IV_THUMB_W - 1 - ((dx * IV_THUMB_W) / disp_w);
                    sy = IV_THUMB_H - 1 - ((dy * IV_THUMB_H) / disp_h);
                } else {
                    sx = IV_THUMB_W - 1 - ((dy * IV_THUMB_W) / disp_h);
                    sy = (dx * IV_THUMB_H) / disp_w;
                }
                if (sx >= 0 && sx < IV_THUMB_W && sy >= 0 && sy < IV_THUMB_H) {
                    g_api->gfx_draw_pixel(ox + dx, oy + dy, st->thumb[sy * IV_THUMB_W + sx]);
                }
            }
        }
    }

    // Barra di stato inferiore
    char status[96], n1[16], n2[16], nz[16], nr[16];
    iv_u32_str(n1, (uint32_t)st->orig_w);
    iv_u32_str(n2, (uint32_t)st->orig_h);
    iv_u32_str(nz, (uint32_t)st->zoom_pct);
    iv_u32_str(nr, (uint32_t)st->rot_deg);
    dll_strcpy(status, st->cur_path, sizeof(status));
    int sl = (int)dll_strlen(status);
    dll_strcpy(status + sl, " | ", sizeof(status) - sl); sl = (int)dll_strlen(status);
    dll_strcpy(status + sl, n1, sizeof(status) - sl); sl = (int)dll_strlen(status);
    dll_strcpy(status + sl, "x", sizeof(status) - sl); sl = (int)dll_strlen(status);
    dll_strcpy(status + sl, n2, sizeof(status) - sl); sl = (int)dll_strlen(status);
    dll_strcpy(status + sl, " | Zoom ", sizeof(status) - sl); sl = (int)dll_strlen(status);
    dll_strcpy(status + sl, nz, sizeof(status) - sl); sl = (int)dll_strlen(status);
    dll_strcpy(status + sl, "% | Rot ", sizeof(status) - sl); sl = (int)dll_strlen(status);
    dll_strcpy(status + sl, nr, sizeof(status) - sl); sl = (int)dll_strlen(status);
    dll_strcpy(status + sl, " deg", sizeof(status) - sl);
    g_api->gfx_draw_string(cx, win->y + win->height - 20, status, t.text_primary, 0, true);
}

static void imgview_mouse(window_t* win, int32_t lx, int32_t ly, bool clicked) {
    if (!clicked || !g_api || !win || !win->app_data) return;
    imgview_state_t* st = (imgview_state_t*)win->app_data;
    if (ly >= 26 && ly <= 48) {
        if (lx >= 6 && lx <= 82) {
            if (st->zoom_pct > 50) st->zoom_pct -= 25;
        } else if (lx >= 86 && lx <= 162) {
            if (st->zoom_pct < 150) st->zoom_pct += 25;
        } else if (lx >= 166 && lx <= 252) {
            st->rot_deg = (st->rot_deg + 90) % 360;
        } else if (lx >= 256 && lx <= 332) {
            st->zoom_pct = 100;
            st->rot_deg = 0;
        } else if (lx >= 336 && lx <= 426) {
            iv_load_bmp(st, (st->cur_idx + 1) % 4);
        }
    }
}

__attribute__((section(".text.entry")))
int dll_entry(const kernel_api_t* api, void* params) {
    g_api = api;
    window_t* win = (window_t*)params;
    if (!win) win = api->wm_create_window("Visualizzatore Immagini (IMGVIEW)", 140, 70, 560, 400, APP_NOTEPAD);
    if (!win) return -1;
    imgview_state_t* st = (imgview_state_t*)api->kmalloc(sizeof(imgview_state_t));
    if (!st) return -1;
    for (size_t i = 0; i < sizeof(imgview_state_t); i++) ((uint8_t*)st)[i] = 0;
    st->zoom_pct = 100;
    st->rot_deg = 0;
    iv_load_bmp(st, 0);
    win->app_data = st;
    win->on_draw = imgview_draw;
    win->on_mouse = imgview_mouse;
    return 0;
}
