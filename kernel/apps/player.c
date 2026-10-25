/**
 * kernel/apps/player.c
 * NewOS Media Player (PLAYER.EXE) - Riproduttore Multimediale Ufficiale di NewOS V1 (64-bit)
 * Architettura compatta e ottimizzata per rimanere rigorosamente entro il budget di memoria
 * (C) Copyright Newspoint Software - NewSoft Media Subsystem
 */

#include "player.h"
#include <gui/wm.h>
#include <gui/graphics.h>
#include <gui/icons.h>
#include <drivers/ac97.h>
#include <drivers/sound_wav.h>
#include <drivers/timer.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <string.h>

#define PLAYER_WIN_W 550
#define PLAYER_WIN_H 370
#define MAX_PLAYLIST 8
#define SB_SIZE      14

typedef struct {
    char     path[64];
    char     title[24];
    uint16_t duration_sec;
    bool     is_video;
} player_track_t;

typedef struct {
    window_t*      win;
    bool           is_playing;
    bool           is_paused;
    uint32_t       current_time_ms;
    uint32_t       total_duration_ms;
    uint64_t       play_start_tick;
    uint32_t       paused_ms;

    player_track_t playlist[MAX_PLAYLIST];
    int            playlist_count;
    int            current_track;
    bool           show_playlist;
    bool           repeat_mode;
    uint8_t        volume;
    bool           is_muted;

    int            pl_scroll_y;
    int            pl_scroll_x;

    uint8_t        bars[16];
    uint8_t        peaks[16];
    uint8_t        vu_l;
    uint8_t        vu_r;
    uint32_t       anim_tick;
    bool           is_video;
    uint32_t       video_frame;
} player_state_t;

static player_state_t* g_player_state = NULL;

static const player_track_t g_default_tracks[4] = {
    { "C:\\NewOS\\Resources\\Miusic\\WELCOME.WAV", "Welcome Symphony", 134, false },
    { "C:\\NewOS\\Resources\\Miusic\\SAMPLE.MP3",  "Digital Groove", 225, false },
    { "C:\\NewOS\\Resources\\Videos\\NEWOS_INTRO.AVI", "Presentazione NewOS", 48, true },
    { "C:\\NewOS\\Resources\\Videos\\SAMPLE.MP4",  "Dimostrazione AV1", 80, true }
};

static __attribute__((noinline)) void player_play_track(player_state_t* s, int track_idx) {
    if (!s || track_idx < 0 || track_idx >= s->playlist_count) return;

    s->current_track = track_idx;
    player_track_t* t = &s->playlist[track_idx];

    s->is_playing = true;
    s->is_paused = false;
    s->current_time_ms = 0;
    s->paused_ms = 0;
    s->play_start_tick = timer_get_ticks();
    s->total_duration_ms = (uint32_t)t->duration_sec * 1000;
    s->is_video = t->is_video;
    s->video_frame = 0;

    if (s->win) {
        char buf[48];
        mini_snprintf(buf, sizeof(buf), "NewOS Player - %s", t->title);
        strncpy(s->win->title, buf, sizeof(s->win->title) - 1);
    }

    if (!t->is_video && strstr(t->path, ".WAV")) {
        sound_play_wav_file(t->path);
    } else if (ac97_is_available()) {
        ac97_set_volume(s->is_muted ? 0 : s->volume);
    }
}

static __attribute__((noinline)) void player_stop(player_state_t* s) {
    if (!s) return;
    s->is_playing = false;
    s->is_paused = false;
    s->current_time_ms = 0;
    s->video_frame = 0;
    for (int i = 0; i < 16; i++) s->bars[i] = s->peaks[i] = 0;
    s->vu_l = s->vu_r = 0;
    if (ac97_is_available()) ac97_stop();
}

static __attribute__((noinline)) void player_toggle_pause(player_state_t* s) {
    if (!s) return;
    if (!s->is_playing) { player_play_track(s, s->current_track); return; }
    s->is_paused = !s->is_paused;
    if (s->is_paused) s->paused_ms = (uint32_t)((timer_get_ticks() - s->play_start_tick) * 10);
    else s->play_start_tick = timer_get_ticks() - (s->paused_ms * 100 / 1000);
}

static __attribute__((noinline)) void player_seek(player_state_t* s, uint32_t target_ms) {
    if (!s || s->total_duration_ms == 0) return;
    if (target_ms > s->total_duration_ms) target_ms = s->total_duration_ms;
    s->current_time_ms = target_ms;
    s->play_start_tick = timer_get_ticks() - ((uint64_t)target_ms * 100 / 1000);
    s->paused_ms = target_ms;
    s->video_frame = (target_ms * 30) / 1000;
}

void app_player_update_tick(void) {
    player_state_t* s = g_player_state;
    if (!s || !s->is_playing || s->is_paused) return;

    uint64_t elapsed_ms = (timer_get_ticks() - s->play_start_tick) * 10;
    s->current_time_ms = (uint32_t)elapsed_ms;

    if (s->current_time_ms >= s->total_duration_ms) {
        if (s->repeat_mode) player_play_track(s, s->current_track);
        else if (s->current_track + 1 < s->playlist_count) player_play_track(s, s->current_track + 1);
        else player_stop(s);
        return;
    }

    s->anim_tick++;
    for (int i = 0; i < 16; i++) {
        int h = ((s->anim_tick * 7 + i * 11) % 64);
        int v = (h > 40) ? h : (h + (i * 3) % 20);
        if (v > 64) v = 64;
        s->bars[i] = (uint8_t)v;
        if (s->bars[i] > s->peaks[i]) s->peaks[i] = s->bars[i];
        else if (s->peaks[i] > 0 && (s->anim_tick % 3 == 0)) s->peaks[i]--;
    }
    s->vu_l = (uint8_t)((s->bars[2] + s->bars[4]) / 2);
    s->vu_r = (uint8_t)((s->bars[8] + s->bars[12]) / 2);
    if (s->is_video) s->video_frame++;
}

static __attribute__((noinline)) void draw_btn(int32_t x, int32_t y, int32_t w, int32_t h, const char* label, bool pressed) {
    gfx_fill_rect(x, y, w, h, COLOR_BTNFACE);
    gfx_draw_bevel_rect(x, y, w, h, pressed);
    int len = (int)strlen(label);
    gfx_draw_string(x + (w - len * 8) / 2 + (pressed ? 1 : 0),
                    y + (h - 14) / 2 + (pressed ? 1 : 0), label, COLOR_WINDOWTEXT, 0, true);
}

static __attribute__((noinline)) void draw_scrollbar(int32_t x, int32_t y, int32_t len, int is_vert, int cur, int total, int visible) {
    int32_t tlen = len - 2 * SB_SIZE;
    if (tlen <= 0) return;
    int32_t tx = is_vert ? x : (x + SB_SIZE);
    int32_t ty = is_vert ? (y + SB_SIZE) : y;
    int32_t tw = is_vert ? SB_SIZE : tlen;
    int32_t th = is_vert ? tlen : SB_SIZE;

    draw_btn(x, y, SB_SIZE, SB_SIZE, is_vert ? "^" : "<", false);
    draw_btn(is_vert ? x : (x + len - SB_SIZE), is_vert ? (y + len - SB_SIZE) : y, SB_SIZE, SB_SIZE, is_vert ? "v" : ">", false);

    gfx_fill_rect(tx, ty, tw, th, 0xE0DCC0);
    gfx_draw_bevel_rect(tx, ty, tw, th, true);

    int max_s = (total > visible) ? (total - visible) : 0;
    int thumb_sz = (total <= visible) ? tlen : (tlen * visible / (total ? total : 1));
    if (thumb_sz < 12) thumb_sz = 12;
    if (thumb_sz > tlen) thumb_sz = tlen;
    int thumb_pos = (max_s > 0) ? (cur * (tlen - thumb_sz) / max_s) : 0;

    int32_t th_x = is_vert ? (tx + 1) : (tx + thumb_pos);
    int32_t th_y = is_vert ? (ty + thumb_pos) : (ty + 1);
    int32_t th_w = is_vert ? (SB_SIZE - 2) : thumb_sz;
    int32_t th_h = is_vert ? thumb_sz : (SB_SIZE - 2);

    gfx_fill_rect(th_x, th_y, th_w, th_h, COLOR_BTNFACE);
    gfx_draw_bevel_rect(th_x, th_y, th_w, th_h, false);
}

static __attribute__((noinline)) int player_clamp_scroll(player_state_t* s, int visible_items, int max_chars) {
    int max_len = 0;
    for (int i = 0; i < s->playlist_count; i++) {
        int l = 4 + (int)strlen(s->playlist[i].title);
        if (l > max_len) max_len = l;
    }
    int max_x = (max_len > max_chars) ? (max_len - max_chars) : 0;
    int max_y = (s->playlist_count > visible_items) ? (s->playlist_count - visible_items) : 0;
    if (s->pl_scroll_x > max_x) s->pl_scroll_x = max_x;
    if (s->pl_scroll_x < 0) s->pl_scroll_x = 0;
    if (s->pl_scroll_y > max_y) s->pl_scroll_y = max_y;
    if (s->pl_scroll_y < 0) s->pl_scroll_y = 0;
    return max_len;
}

static void player_draw(window_t* win) {
    player_state_t* s = (player_state_t*)win->app_data;
    if (!s) return;

    int32_t cx = win->x + 4, cy = win->y + 24;
    int32_t cw = win->width - 8, ch = win->height - 28;

    gfx_fill_rect(cx, cy, cw, ch, COLOR_BTNFACE);
    gfx_draw_string(cx + 6, cy + 2, "File   Riproduzione   Audio   Video   Playlist   Aiuto", COLOR_WINDOWTEXT, 0, true);

    int32_t pl_w = s->show_playlist ? 164 : 0;
    int32_t vx = cx + 6, vy = cy + 20;
    int32_t vw = cw - 12 - (s->show_playlist ? (pl_w + 6) : 0);
    int32_t vh = ch - 114;

    gfx_fill_rect(vx, vy, vw, vh, 0x000000);
    gfx_draw_bevel_rect(vx, vy, vw, vh, true);

    player_track_t* trk = (s->current_track >= 0 && s->current_track < s->playlist_count) ?
                          &s->playlist[s->current_track] : NULL;

    if (s->is_video && trk && trk->is_video) {
        int32_t fx = vx + 4, fy = vy + 4, fw = vw - 8, fh = vh - 8;
        uint32_t c1 = 0x0A192F + ((s->video_frame * 3) & 0x001F2F);
        uint32_t c2 = 0x1E3A8A + ((s->video_frame * 5) & 0x002F1F);
        gfx_draw_gradient_v(fx, fy, fw, fh, c1, c2);

        int32_t sy = fy + (int32_t)((s->video_frame * 4) % (fh ? fh : 1));
        gfx_fill_rect(fx, sy, fw, 2, 0x38BDF8);

        gfx_fill_rect(fx + 4, fy + 4, 210, 16, 0x000000);
        gfx_draw_string(fx + 6, fy + 6, "[VIDEO] AV1 / IVF 30 FPS", 0x38BDF8, 0, true);
        gfx_draw_string(fx + 8, fy + fh - 18, trk->title, 0xFFFFFF, 0, true);
    } else {
        gfx_fill_rect(vx + 2, vy + 2, vw - 4, 18, 0x0A246A);
        char hdr[48];
        mini_snprintf(hdr, sizeof(hdr), "> %s", trk ? trk->title : "Nessun brano");
        gfx_draw_string(vx + 6, vy + 4, hdr, 0xFFFFFF, 0, true);

        int32_t vu_x = vx + 8, vu_y = vy + 26, vu_h = vh - 38;
        gfx_draw_string(vu_x, vu_y, "L", 0x94A3B8, 0, true);
        gfx_draw_string(vu_x + 10, vu_y, "R", 0x94A3B8, 0, true);
        int32_t vuh_l = (s->vu_l * (vu_h - 16)) / 64;
        int32_t vuh_r = (s->vu_r * (vu_h - 16)) / 64;
        gfx_fill_rect(vu_x, vu_y + vu_h - vuh_l, 6, vuh_l, 0x22C55E);
        gfx_fill_rect(vu_x + 10, vu_y + vu_h - vuh_r, 6, vuh_r, 0x22C55E);

        int32_t sx = vx + 32, sy = vy + 26, sw = vw - 44, sh = vh - 38;
        int32_t bw = (sw - 45) / 16;
        if (bw < 3) bw = 3;

        for (int i = 0; i < 16; i++) {
            int32_t bx = sx + i * (bw + 3);
            int32_t bh = (s->bars[i] * sh) / 64;
            int32_t by = sy + sh - bh;

            for (int32_t y = sy + sh; y >= by; y -= 4) {
                uint32_t col = (sy + sh - y > (sh * 7 / 10)) ? 0xEF4444 :
                               (sy + sh - y > (sh * 4 / 10)) ? 0xFACC15 : 0x22C55E;
                gfx_fill_rect(bx, y - 3, bw, 2, col);
            }
            int32_t py = sy + sh - (s->peaks[i] * sh) / 64;
            if (py < sy) py = sy;
            gfx_fill_rect(bx, py - 2, bw, 2, 0xFFFFFF);
        }
    }

    // Playlist laterale con Scrollbar Verticale e Orizzontale
    if (s->show_playlist) {
        int32_t px = vx + vw + 6, py = vy;
        int32_t pw = pl_w, ph = vh;

        gfx_fill_rect(px, py, pw, ph, COLOR_WINDOW);
        gfx_draw_bevel_rect(px, py, pw, ph, true);

        gfx_fill_rect(px + 1, py + 1, pw - 2, 16, COLOR_BTNFACE);
        gfx_draw_string(px + 4, py + 2, "Playlist", COLOR_WINDOWTEXT, 0, true);
        gfx_draw_bevel_rect(px + 1, py + 17, pw - 2, 2, true);

        int32_t cx_list = px + 2;
        int32_t cy_list = py + 19;
        int32_t cw_list = pw - 4 - SB_SIZE;
        int32_t ch_list = ph - 19 - SB_SIZE - 2;

        int visible_items = ch_list / 18;
        int max_chars = (cw_list - 4) / 8;
        if (max_chars < 1) max_chars = 1;

        int max_len = player_clamp_scroll(s, visible_items, max_chars);

        gfx_fill_rect(cx_list, cy_list, cw_list, ch_list, COLOR_WINDOW);

        for (int i = 0; i < visible_items; i++) {
            int idx = i + s->pl_scroll_y;
            if (idx >= s->playlist_count) break;
            bool sel = (idx == s->current_track);
            int32_t iy = cy_list + i * 18;

            gfx_fill_rect(cx_list, iy, cw_list, 18, sel ? 0x0A246A : COLOR_WINDOW);

            char full_txt[36];
            mini_snprintf(full_txt, sizeof(full_txt), "%s %s", s->playlist[idx].is_video ? "[V]" : "[A]", s->playlist[idx].title);

            char vis_txt[24];
            int flen = (int)strlen(full_txt);
            int src_pos = s->pl_scroll_x;
            int c = 0;
            while (c < max_chars && (src_pos + c) < flen) {
                vis_txt[c] = full_txt[src_pos + c];
                c++;
            }
            vis_txt[c] = '\0';
            gfx_draw_string(cx_list + 2, iy + 2, vis_txt, sel ? 0xFFFFFF : COLOR_WINDOWTEXT, 0, true);
        }

        // Scrollbar Verticale
        int32_t sb_vx = px + pw - SB_SIZE - 2;
        int32_t sb_vy = py + 19;
        draw_scrollbar(sb_vx, sb_vy, ch_list, 1, s->pl_scroll_y, s->playlist_count, visible_items);

        // Scrollbar Orizzontale
        int32_t sb_hx = cx_list;
        int32_t sb_hy = py + ph - SB_SIZE - 2;
        draw_scrollbar(sb_hx, sb_hy, cw_list, 0, s->pl_scroll_x, max_len, max_chars);

        gfx_fill_rect(sb_vx, sb_hy, SB_SIZE, SB_SIZE, COLOR_BTNFACE);
    }

    int32_t seek_y = vy + vh + 6;
    uint32_t cs = s->current_time_ms / 1000, ts = s->total_duration_ms / 1000;
    char tstr[32];
    mini_snprintf(tstr, sizeof(tstr), "%02d:%02d/%02d:%02d", cs / 60, cs % 60, ts / 60, ts % 60);
    gfx_draw_string(cx + 6, seek_y + 2, tstr, COLOR_WINDOWTEXT, 0, true);

    int32_t bx = cx + 110, bw = cw - 122;
    gfx_fill_rect(bx, seek_y + 3, bw, 10, 0xFFFFFF);
    gfx_draw_bevel_rect(bx, seek_y + 3, bw, 10, true);

    if (ts > 0) {
        int32_t pw = (int32_t)((uint64_t)cs * bw / ts);
        if (pw > bw) pw = bw;
        if (pw > 0) gfx_fill_rect(bx + 1, seek_y + 4, pw, 8, 0x2563EB);
        int32_t tx = bx + pw - 4;
        if (tx < bx) tx = bx;
        if (tx + 8 > bx + bw) tx = bx + bw - 8;
        gfx_fill_rect(tx, seek_y + 1, 8, 14, COLOR_BTNFACE);
        gfx_draw_bevel_rect(tx, seek_y + 1, 8, 14, false);
    }

    int32_t cy_btn = seek_y + 18, kx = cx + 6;

    static const int16_t b_pos[5] = { 0, 34, 110, 144, 178 };
    static const char* const b_lbl[5] = { "|<", "<<", "[]", ">>", ">|" };
    for (int b = 0; b < 5; b++) {
        draw_btn(kx + b_pos[b], cy_btn, 30, 22, b_lbl[b], false);
    }
    draw_btn(kx + 68,  cy_btn, 38, 22, (s->is_playing && !s->is_paused) ? "||" : ">", (s->is_playing && !s->is_paused));
    draw_btn(kx + 216, cy_btn, 28, 22, s->repeat_mode ? "[R]" : "R", s->repeat_mode);
    draw_btn(kx + 248, cy_btn, 28, 22, s->show_playlist ? "[P]" : "P", s->show_playlist);
    draw_btn(kx + 288, cy_btn, 26, 22, s->is_muted ? "X" : "V", s->is_muted);

    int32_t vx_bar = kx + 318, vw_bar = 80;
    gfx_fill_rect(vx_bar, cy_btn + 6, vw_bar, 10, 0xFFFFFF);
    gfx_draw_bevel_rect(vx_bar, cy_btn + 6, vw_bar, 10, true);

    uint8_t cur_vol = s->is_muted ? 0 : s->volume;
    int32_t vpw = (int32_t)cur_vol * vw_bar / 100;
    if (vpw > 0) gfx_fill_rect(vx_bar + 1, cy_btn + 7, vpw, 8, 0x16A34A);

    char vstr[8];
    mini_snprintf(vstr, sizeof(vstr), "%d%%", (int)cur_vol);
    gfx_draw_string(vx_bar + vw_bar + 6, cy_btn + 5, vstr, COLOR_WINDOWTEXT, 0, true);
}

static __attribute__((noinline)) void player_mouse(window_t* win, int32_t lx, int32_t ly, bool clicked) {
    player_state_t* s = (player_state_t*)win->app_data;
    if (!s || !clicked) return;

    int32_t cx = 4, cy = 24, cw = win->width - 8, ch = win->height - 28;
    int32_t pl_w = s->show_playlist ? 164 : 0;
    int32_t vx = cx + 6, vy = cy + 20;
    int32_t vw = cw - 12 - (s->show_playlist ? (pl_w + 6) : 0);
    int32_t vh = ch - 114;

    if (ly >= cy && ly <= cy + 18) {
        if (lx >= cx + 6 && lx <= cx + 40) {
            app_explorer_open_folder("C:/NewOS/Resources/Miusic");
            return;
        } else if (lx >= cx + 44 && lx <= cx + 140) {
            player_toggle_pause(s);
            return;
        } else if (lx >= cx + 144 && lx <= cx + 185) {
            app_sound_props_launch();
            return;
        } else if (lx >= cx + 240 && lx <= cx + 300) {
            s->show_playlist = !s->show_playlist;
            return;
        } else if (lx >= cx + 305 && lx <= cx + 350) {
            app_notepad_open_file("NewOS Player", "NewOS Media Player (PLAYER.EXE)\n");
            return;
        }
    }

    if (s->show_playlist) {
        int32_t px = vx + vw + 6, py = vy;
        int32_t pw = pl_w, ph = vh;
        int32_t cx_list = px + 2;
        int32_t cy_list = py + 19;
        int32_t cw_list = pw - 4 - SB_SIZE;
        int32_t ch_list = ph - 19 - SB_SIZE - 2;

        int visible_items = ch_list / 18;
        int max_chars = (cw_list - 4) / 8;
        if (max_chars < 1) max_chars = 1;

        if (lx >= cx_list && lx < cx_list + cw_list && ly >= cy_list && ly < cy_list + ch_list) {
            int idx = s->pl_scroll_y + (ly - cy_list) / 18;
            if (idx >= 0 && idx < s->playlist_count) {
                player_play_track(s, idx);
                return;
            }
        }

        int32_t sb_vx = px + pw - SB_SIZE - 2;
        int32_t sb_vy = py + 19;
        if (lx >= sb_vx && lx <= sb_vx + SB_SIZE && ly >= sb_vy && ly <= sb_vy + ch_list) {
            if (ly < sb_vy + SB_SIZE) s->pl_scroll_y--;
            else if (ly > sb_vy + ch_list - SB_SIZE) s->pl_scroll_y++;
            else s->pl_scroll_y += (ly < sb_vy + ch_list / 2) ? -2 : 2;
            player_clamp_scroll(s, visible_items, max_chars);
            return;
        }

        int32_t sb_hx = cx_list;
        int32_t sb_hy = py + ph - SB_SIZE - 2;
        if (lx >= sb_hx && lx <= sb_hx + cw_list && ly >= sb_hy && ly <= sb_hy + SB_SIZE) {
            if (lx < sb_hx + SB_SIZE) s->pl_scroll_x--;
            else if (lx > sb_hx + cw_list - SB_SIZE) s->pl_scroll_x++;
            else s->pl_scroll_x += (lx < sb_hx + cw_list / 2) ? -3 : 3;
            player_clamp_scroll(s, visible_items, max_chars);
            return;
        }
    }

    int32_t seek_y = vy + vh + 6;
    int32_t bx = cx + 110, bw = cw - 122;
    if (lx >= bx && lx <= bx + bw && ly >= seek_y && ly <= seek_y + 14) {
        uint32_t ms = (uint32_t)((uint64_t)(lx - bx) * s->total_duration_ms / bw);
        player_seek(s, ms);
        return;
    }

    int32_t cy_btn = seek_y + 18, kx = cx + 6;
    if (ly >= cy_btn && ly <= cy_btn + 22) {
        if (lx >= kx + 0 && lx < kx + 30) {
            if (s->current_track > 0) player_play_track(s, s->current_track - 1);
        } else if (lx >= kx + 34 && lx < kx + 64) {
            player_seek(s, (s->current_time_ms > 5000) ? (s->current_time_ms - 5000) : 0);
        } else if (lx >= kx + 68 && lx < kx + 106) {
            player_toggle_pause(s);
        } else if (lx >= kx + 110 && lx < kx + 140) {
            player_stop(s);
        } else if (lx >= kx + 144 && lx < kx + 174) {
            player_seek(s, s->current_time_ms + 5000);
        } else if (lx >= kx + 178 && lx < kx + 208) {
            if (s->current_track + 1 < s->playlist_count) player_play_track(s, s->current_track + 1);
        } else if (lx >= kx + 216 && lx < kx + 244) {
            s->repeat_mode = !s->repeat_mode;
        } else if (lx >= kx + 248 && lx < kx + 276) {
            s->show_playlist = !s->show_playlist;
        } else if (lx >= kx + 288 && lx < kx + 314) {
            s->is_muted = !s->is_muted;
            if (ac97_is_available()) ac97_set_volume(s->is_muted ? 0 : s->volume);
        } else if (lx >= kx + 318 && lx <= kx + 398) {
            s->volume = (uint8_t)((lx - (kx + 318)) * 100 / 80);
            s->is_muted = false;
            if (ac97_is_available()) ac97_set_volume(s->volume);
        }
    }
}

static __attribute__((noinline)) void player_key(window_t* win, char c) {
    player_state_t* s = (player_state_t*)win->app_data;
    if (!s) return;
    if (c >= 'A' && c <= 'Z') c += 32;

    if (c == ' ') player_toggle_pause(s);
    else if (c == 's') player_stop(s);
    else if (c == 'n') { if (s->current_track + 1 < s->playlist_count) player_play_track(s, s->current_track + 1); }
    else if (c == 'p') { if (s->current_track > 0) player_play_track(s, s->current_track - 1); }
    else if (c == 'r') s->repeat_mode = !s->repeat_mode;
    else if (c == 'l') s->show_playlist = !s->show_playlist;
    else if (c == 'm') {
        s->is_muted = !s->is_muted;
        if (ac97_is_available()) ac97_set_volume(s->is_muted ? 0 : s->volume);
    } else if (c == '+' || c == '=') {
        s->volume = (s->volume <= 95) ? (s->volume + 5) : 100;
        s->is_muted = false;
        if (ac97_is_available()) ac97_set_volume(s->volume);
    } else if (c == '-' || c == '_') {
        s->volume = (s->volume >= 5) ? (s->volume - 5) : 0;
        if (ac97_is_available()) ac97_set_volume(s->is_muted ? 0 : s->volume);
    }
}

void app_player_launch(void) {
    if (g_player_state && g_player_state->win) {
        wm_focus_window(g_player_state->win->id);
        return;
    }

    window_t* win = wm_create_window("NewOS Player - WELCOME.WAV", 120, 75, PLAYER_WIN_W, PLAYER_WIN_H, APP_PLAYER);
    if (!win) return;

    player_state_t* s = (player_state_t*)kmalloc(sizeof(player_state_t));
    if (!s) { wm_destroy_window(win->id); return; }

    memset(s, 0, sizeof(player_state_t));
    s->win           = win;
    s->volume        = 85;
    s->show_playlist = true;
    s->playlist_count = 4;
    for (int i = 0; i < 4; i++) {
        strncpy(s->playlist[i].path, g_default_tracks[i].path, sizeof(s->playlist[i].path) - 1);
        strncpy(s->playlist[i].title, g_default_tracks[i].title, sizeof(s->playlist[i].title) - 1);
        s->playlist[i].duration_sec = g_default_tracks[i].duration_sec;
        s->playlist[i].is_video = g_default_tracks[i].is_video;
    }

    win->app_data = s;
    win->on_draw  = player_draw;
    win->on_mouse = player_mouse;
    win->on_key   = player_key;

    g_player_state = s;
    s->current_track = 0;
    s->total_duration_ms = (uint32_t)s->playlist[0].duration_sec * 1000;
    s->is_video = s->playlist[0].is_video;
}

void app_player_open_file(const char* filepath) {
    if (!filepath || !filepath[0]) { app_player_launch(); return; }
    app_player_launch();
    if (!g_player_state) return;
    player_state_t* s = g_player_state;

    for (int i = 0; i < s->playlist_count; i++) {
        if (strcasecmp(s->playlist[i].path, filepath) == 0) {
            player_play_track(s, i);
            return;
        }
    }
    if (s->playlist_count < MAX_PLAYLIST) {
        int idx = s->playlist_count++;
        strncpy(s->playlist[idx].path, filepath, sizeof(s->playlist[idx].path) - 1);
        const char* p = filepath;
        for (const char* c = filepath; *c; c++) if (*c == '\\' || *c == '/') p = c + 1;
        strncpy(s->playlist[idx].title, p, sizeof(s->playlist[idx].title) - 1);
        s->playlist[idx].duration_sec = 180;
        s->playlist[idx].is_video = (strstr(filepath, ".AVI") || strstr(filepath, ".avi") ||
                                     strstr(filepath, ".MP4") || strstr(filepath, ".mp4") ||
                                     strstr(filepath, ".AV1") || strstr(filepath, ".av1") ||
                                     strstr(filepath, ".WEBM") || strstr(filepath, ".webm"));
        player_play_track(s, idx);
    }
}

void app_player_on_close(void) {
    if (g_player_state) {
        player_stop(g_player_state);
        g_player_state = NULL;
    }
}
