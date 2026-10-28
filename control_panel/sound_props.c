#include "control_panel.h"
#include "cpl_theme.h"
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

    cpl_theme_t theme;
    cpl_theme_init(&theme);

    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    // Sfondo client area adattato al tema attivo
    cpl_draw_dialog_bg(win, &theme, cx, cy, cw, ch);

    // 1. Riquadro Periferica Audio
    int32_t box1_x = cx + 12;
    int32_t box1_y = cy + 14;
    int32_t box1_w = cw - 24;
    int32_t box1_h = 60;
    cpl_draw_groupbox(box1_x, box1_y, box1_w, box1_h, " Periferica Audio ", &theme);

    // Icona 32x32 altoparlante
    gfx_draw_icon_32(box1_x + 14, box1_y + 14, ICON_SOUND);
    gfx_draw_string(box1_x + 54, box1_y + 14, sound_get_driver_name(), theme.accent, 0, true);
    if (sound_is_ac97()) {
        uint32_t dma_green = theme.is_dark ? 0x2ECC71 : 0x205020;
        gfx_draw_string(box1_x + 54, box1_y + 32, "Controller Hardware: Intel ICH AC'97 (DMA Attivo)", dma_green, 0, true);
    } else {
        gfx_draw_string(box1_x + 54, box1_y + 32, "Modalita: Fallback Altoparlante PC Speaker", theme.text_secondary, 0, true);
    }

    // 2. Riquadro Eventi Sonori
    int32_t box2_x = cx + 12;
    int32_t box2_y = cy + 86;
    int32_t box2_w = cw - 24;
    int32_t box2_h = 135;
    cpl_draw_groupbox(box2_x, box2_y, box2_w, box2_h, " Eventi Sonori di NewOS V1 ", &theme);

    // Lista eventi
    for (int i = 0; i < 3; i++) {
        int32_t iy = box2_y + 16 + i * 36;
        bool is_sel = (state->selected_sound == i);
        int32_t item_w = box2_w - 110;

        if (is_sel) {
            if (theme.is_classic) {
                gfx_fill_rect(box2_x + 12, iy, item_w, 26, theme.sel_bg);
            } else {
                gfx_fill_rounded_rect(box2_x + 12, iy, item_w, 26, theme.control_radius, theme.sel_bg);
            }
            gfx_draw_string(box2_x + 18, iy + 5, sound_names[i], theme.sel_text, 0, true);
        } else {
            gfx_fill_rect(box2_x + 12, iy, item_w, 26, theme.input_bg);
            if (theme.is_classic) {
                gfx_draw_bevel_rect(box2_x + 12, iy, item_w, 26, true);
            } else {
                gfx_draw_rounded_rect(box2_x + 12, iy, item_w, 26, theme.control_radius, theme.input_border);
            }
            gfx_draw_string(box2_x + 18, iy + 5, sound_names[i], theme.text_primary, 0, true);
        }
    }

    // Pulsante [ > Prova ]
    int32_t play_x = box2_x + box2_w - 90;
    int32_t play_y = box2_y + 36;
    wm_draw_themed_button(play_x, play_y, 76, 26, "> Prova", false);

    // 3. Riquadro Volume Master
    int32_t box3_x = cx + 12;
    int32_t box3_y = cy + 232;
    int32_t box3_w = cw - 24;
    int32_t box3_h = 58;
    cpl_draw_groupbox(box3_x, box3_y, box3_w, box3_h, " Volume Master ", &theme);

    // Bottone [ - ]
    int32_t btn_minus_x = box3_x + 14;
    int32_t btn_vol_y   = box3_y + 18;
    wm_draw_themed_button(btn_minus_x, btn_vol_y, 24, 24, "-", false);

    // Barra del volume
    int32_t bar_x = btn_minus_x + 32;
    int32_t bar_w = box3_w - 110;
    gfx_fill_rect(bar_x, btn_vol_y + 4, bar_w, 16, theme.input_bg);
    if (theme.is_classic) {
        gfx_draw_bevel_rect(bar_x, btn_vol_y + 4, bar_w, 16, true);
    } else {
        gfx_draw_rounded_rect(bar_x, btn_vol_y + 4, bar_w, 16, 2, theme.input_border);
    }

    int32_t fill_w = (bar_w - 4) * state->volume / 100;
    if (fill_w > 0) {
        gfx_fill_rect(bar_x + 2, btn_vol_y + 6, fill_w, 12, theme.accent);
    }

    // Bottone [ + ]
    int32_t btn_plus_x = bar_x + bar_w + 8;
    wm_draw_themed_button(btn_plus_x, btn_vol_y, 24, 24, "+", false);

    // 4. Pulsante OK centrato in basso
    int32_t ok_w = 78;
    int32_t ok_h = 24;
    int32_t ok_x = cx + (cw - ok_w) / 2;
    int32_t ok_y = cy + ch - 32;
    wm_draw_themed_button(ok_x, ok_y, ok_w, ok_h, "OK", false);
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

    // Click sul pulsante [ > Prova ]
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
