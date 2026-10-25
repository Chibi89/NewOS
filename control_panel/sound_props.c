#include "control_panel.h"
#include <gui/wm.h>
#include <gui/graphics.h>
#include <gui/icons.h>
#include <drivers/sound.h>
#include <mm/heap.h>
#include <string.h>

typedef struct {
    int selected_sound; // 0: Startup, 1: Ding, 2: Chime
    int volume;
} sound_state_t;

static const char* sound_names[] = {
    "Avvio di NewOS V1 (Windows Startup)",
    "Notifica ed Eventi (ding.wav)",
    "Melodia Chime di Sistema"
};

static void sound_props_draw(window_t* win) {
    sound_state_t* state = (sound_state_t*)win->app_data;
    if (!state) return;

    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    // Sfondo finestra standard
    gfx_fill_rect(cx, cy, cw, ch, COLOR_BTNFACE);

    // 1. Riquadro Periferica Audio
    int32_t box1_x = cx + 12;
    int32_t box1_y = cy + 14;
    int32_t box1_w = cw - 24;
    int32_t box1_h = 60;
    gfx_draw_bevel_rect(box1_x, box1_y, box1_w, box1_h, true);
    gfx_fill_rect(box1_x + 10, box1_y - 6, 120, 12, COLOR_BTNFACE);
    gfx_draw_string(box1_x + 12, box1_y - 6, " Periferica Audio ", COLOR_WINDOWTEXT, 0, true);

    // Icona autentica 32x32 altoparlante
    gfx_draw_icon_32(box1_x + 14, box1_y + 14, ICON_SOUND);
    gfx_draw_string(box1_x + 54, box1_y + 14, sound_get_driver_name(), 0x000080, 0, true);
    if (sound_is_ac97()) {
        gfx_draw_string(box1_x + 54, box1_y + 32, "Controller Hardware: Intel ICH AC'97 (DMA Attivo)", 0x205020, 0, true);
    } else {
        gfx_draw_string(box1_x + 54, box1_y + 32, "Modalita: Fallback Altoparlante PC Speaker", 0x805000, 0, true);
    }

    // 2. Riquadro Eventi Sonori
    int32_t box2_x = cx + 12;
    int32_t box2_y = cy + 86;
    int32_t box2_w = cw - 24;
    int32_t box2_h = 135;
    gfx_draw_bevel_rect(box2_x, box2_y, box2_w, box2_h, true);
    gfx_fill_rect(box2_x + 10, box2_y - 6, 170, 12, COLOR_BTNFACE);
    gfx_draw_string(box2_x + 12, box2_y - 6, " Eventi Sonori di NewOS V1 ", COLOR_WINDOWTEXT, 0, true);

    // Lista eventi
    for (int i = 0; i < 3; i++) {
        int32_t iy = box2_y + 16 + i * 36;
        bool is_sel = (state->selected_sound == i);

        if (is_sel) {
            gfx_fill_rect(box2_x + 12, iy, box2_w - 110, 26, 0x0A246A);
            gfx_draw_string(box2_x + 18, iy + 5, sound_names[i], 0xFFFFFF, 0, true);
        } else {
            gfx_fill_rect(box2_x + 12, iy, box2_w - 110, 26, 0xFFFFFF);
            gfx_draw_bevel_rect(box2_x + 12, iy, box2_w - 110, 26, true);
            gfx_draw_string(box2_x + 18, iy + 5, sound_names[i], COLOR_WINDOWTEXT, 0, true);
        }
    }

    // Pulsante [ ▶ Prova ]
    int32_t play_x = box2_x + box2_w - 90;
    int32_t play_y = box2_y + 36;
    gfx_fill_rect(play_x, play_y, 76, 26, COLOR_BTNFACE);
    gfx_draw_bevel_rect(play_x, play_y, 76, 26, false);
    gfx_draw_string(play_x + 12, play_y + 5, "> Prova", 0x0A246A, 0, true);

    // 3. Riquadro Volume Master
    int32_t box3_x = cx + 12;
    int32_t box3_y = cy + 232;
    int32_t box3_w = cw - 24;
    int32_t box3_h = 58;
    gfx_draw_bevel_rect(box3_x, box3_y, box3_w, box3_h, true);
    gfx_fill_rect(box3_x + 10, box3_y - 6, 120, 12, COLOR_BTNFACE);
    gfx_draw_string(box3_x + 12, box3_y - 6, " Volume Master ", COLOR_WINDOWTEXT, 0, true);

    // Bottone [ - ]
    int32_t btn_minus_x = box3_x + 14;
    int32_t btn_vol_y   = box3_y + 18;
    gfx_fill_rect(btn_minus_x, btn_vol_y, 24, 24, COLOR_BTNFACE);
    gfx_draw_bevel_rect(btn_minus_x, btn_vol_y, 24, 24, false);
    gfx_draw_string(btn_minus_x + 8, btn_vol_y + 4, "-", COLOR_WINDOWTEXT, 0, true);

    // Barra del volume (160 px)
    int32_t bar_x = btn_minus_x + 32;
    int32_t bar_w = box3_w - 110;
    gfx_fill_rect(bar_x, btn_vol_y + 4, bar_w, 16, 0xFFFFFF);
    gfx_draw_bevel_rect(bar_x, btn_vol_y + 4, bar_w, 16, true);

    int32_t fill_w = (bar_w - 4) * state->volume / 100;
    if (fill_w > 0) {
        gfx_fill_rect(bar_x + 2, btn_vol_y + 6, fill_w, 12, 0x0A246A);
    }

    // Bottone [ + ]
    int32_t btn_plus_x = bar_x + bar_w + 8;
    gfx_fill_rect(btn_plus_x, btn_vol_y, 24, 24, COLOR_BTNFACE);
    gfx_draw_bevel_rect(btn_plus_x, btn_vol_y, 24, 24, false);
    gfx_draw_string(btn_plus_x + 8, btn_vol_y + 4, "+", COLOR_WINDOWTEXT, 0, true);

    // 4. Pulsante OK centrato in basso
    int32_t ok_w = 78;
    int32_t ok_h = 24;
    int32_t ok_x = cx + (cw - ok_w) / 2;
    int32_t ok_y = cy + ch - 32;
    gfx_fill_rect(ok_x, ok_y, ok_w, ok_h, COLOR_BTNFACE);
    gfx_draw_bevel_rect(ok_x, ok_y, ok_w, ok_h, false);
    gfx_draw_string(ok_x + (ok_w - 16) / 2, ok_y + 4, "OK", COLOR_WINDOWTEXT, 0, true);
}

static void sound_props_click(window_t* win, int32_t mx, int32_t my, bool clicked) {
    if (!clicked) return;
    sound_state_t* state = (sound_state_t*)win->app_data;
    if (!state) return;

    int32_t cx = 4;
    int32_t cy = 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    int32_t box2_x = cx + 12;
    int32_t box2_y = cy + 86;
    int32_t box2_w = cw - 24;

    // Click sugli eventi sonori
    for (int i = 0; i < 3; i++) {
        int32_t iy = box2_y + 16 + i * 36;
        if (mx >= box2_x + 12 && mx <= box2_x + box2_w - 100 && my >= iy && my <= iy + 26) {
            state->selected_sound = i;
            return;
        }
    }

    // Click sul pulsante [ ▶ Prova ]
    int32_t play_x = box2_x + box2_w - 90;
    int32_t play_y = box2_y + 36;
    if (mx >= play_x && mx <= play_x + 76 && my >= play_y && my <= play_y + 26) {
        if (state->selected_sound == 0) sound_play(SOUND_STARTUP);
        else if (state->selected_sound == 1) sound_play(SOUND_DING);
        else if (state->selected_sound == 2) sound_play(SOUND_CHIME);
        return;
    }

    // Click su [ - ]
    int32_t box3_x = cx + 12;
    int32_t box3_y = cy + 232;
    int32_t box3_w = cw - 24;
    int32_t btn_minus_x = box3_x + 14;
    int32_t btn_vol_y   = box3_y + 18;
    if (mx >= btn_minus_x && mx <= btn_minus_x + 24 && my >= btn_vol_y && my <= btn_vol_y + 24) {
        if (state->volume >= 10) state->volume -= 10;
        else state->volume = 0;
        sound_set_volume((uint8_t)state->volume);
        return;
    }

    // Click su [ + ]
    int32_t bar_x = btn_minus_x + 32;
    int32_t bar_w = box3_w - 110;
    int32_t btn_plus_x = bar_x + bar_w + 8;
    if (mx >= btn_plus_x && mx <= btn_plus_x + 24 && my >= btn_vol_y && my <= btn_vol_y + 24) {
        if (state->volume <= 90) state->volume += 10;
        else state->volume = 100;
        sound_set_volume((uint8_t)state->volume);
        return;
    }

    // Click su OK
    int32_t ok_w = 78;
    int32_t ok_h = 24;
    int32_t ok_x = cx + (cw - ok_w) / 2;
    int32_t ok_y = cy + ch - 32;
    if (mx >= ok_x && mx <= ok_x + ok_w && my >= ok_y && my <= ok_y + ok_h) {
        wm_destroy_window(win->id);
        return;
    }
}

void app_sound_props_launch(void) {
    window_t* win = wm_create_window("Proprieta - Suoni e periferiche audio", 140, 90, 440, 360, APP_SOUND);
    if (!win) return;

    sound_state_t* state = (sound_state_t*)kmalloc(sizeof(sound_state_t));
    if (state) {
        state->selected_sound = 0;
        state->volume = sound_get_volume();

        win->app_data = state;
        win->on_draw = sound_props_draw;
        win->on_mouse = sound_props_click;
    }
}
