/**
 * calculator/calculator.c
 * Applicazione Calcolatrice standard per NewOS V1 (64-bit).
 * Componente autonomo scorporato dal kernel core.
 * (C) Newspoint Software - NewSoft - NewOS V1
 */

#include "calculator.h"
#include <gui/wm.h>
#include <gui/graphics.h>
#include <mm/heap.h>
#include <string.h>

typedef struct {
    char display[32];
    int64_t current_val;
    int64_t prev_val;
    char pending_op;
    bool clear_on_next;
} calc_state_t;

static const char* btn_labels[4][4] = {
    {"7", "8", "9", "/"},
    {"4", "5", "6", "*"},
    {"1", "2", "3", "-"},
    {"C", "0", "=", "+"}
};

static void calc_draw(window_t* win) {
    calc_state_t* state = (calc_state_t*)win->app_data;
    if (!state) return;

    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    // Sfondo calcolatrice
    gfx_fill_rect(cx, cy, cw, ch, COLOR_BTNFACE);

    // Display LCD scavato
    int32_t dx = cx + 8;
    int32_t dy = cy + 8;
    int32_t dw = cw - 16;
    int32_t dh = 26;

    gfx_fill_rect(dx, dy, dw, dh, 0xFFFFFF);
    gfx_draw_bevel_rect(dx, dy, dw, dh, true);

    // Allinea il testo a destra sul display
    int text_len = (int)strlen(state->display);
    int32_t tx = dx + dw - 8 - (text_len * 8);
    gfx_draw_string(tx, dy + 5, state->display, 0x000000, 0, true);

    // Griglia pulsanti 4x4
    int32_t grid_x = cx + 8;
    int32_t grid_y = dy + dh + 10;
    int32_t bw = 36;
    int32_t bh = 26;
    int32_t spacing_x = (dw - (bw * 4)) / 3;
    int32_t spacing_y = 6;

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int32_t bx = grid_x + c * (bw + spacing_x);
            int32_t by = grid_y + r * (bh + spacing_y);

            gfx_fill_rect(bx, by, bw, bh, COLOR_BTNFACE);
            gfx_draw_bevel_rect(bx, by, bw, bh, false);

            const char* label = btn_labels[r][c];
            uint32_t label_col = (label[0] >= '0' && label[0] <= '9') ? 0x000080 : 0x800000;
            gfx_draw_string(bx + 14, by + 5, label, label_col, 0, true);
        }
    }
}

static void calc_click(window_t* win, int32_t local_x, int32_t local_y, bool clicked) {
    if (!clicked) return;
    calc_state_t* state = (calc_state_t*)win->app_data;
    if (!state) return;

    int32_t cw = win->width - 8;
    int32_t dw = cw - 16;
    int32_t dh = 26;
    int32_t grid_x = 4 + 8;
    int32_t grid_y = 24 + 8 + dh + 10;
    int32_t bw = 36;
    int32_t bh = 26;
    int32_t spacing_x = (dw - (bw * 4)) / 3;
    int32_t spacing_y = 6;

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int32_t bx = grid_x + c * (bw + spacing_x);
            int32_t by = grid_y + r * (bh + spacing_y);

            if (local_x >= bx && local_x < bx + bw && local_y >= by && local_y < by + bh) {
                const char* btn = btn_labels[r][c];

                if (btn[0] >= '0' && btn[0] <= '9') {
                    if (state->clear_on_next || strcmp(state->display, "0") == 0) {
                        state->display[0] = btn[0];
                        state->display[1] = '\0';
                        state->clear_on_next = false;
                    } else if (strlen(state->display) < 14) {
                        strcat(state->display, btn);
                    }
                } else if (btn[0] == 'C') {
                    strcpy(state->display, "0");
                    state->current_val = 0;
                    state->prev_val = 0;
                    state->pending_op = 0;
                    state->clear_on_next = false;
                } else if (btn[0] == '+' || btn[0] == '-' || btn[0] == '*' || btn[0] == '/') {
                    int64_t val = 0;
                    for (int k = 0; state->display[k]; k++) {
                        val = val * 10 + (state->display[k] - '0');
                    }
                    state->prev_val = val;
                    state->pending_op = btn[0];
                    state->clear_on_next = true;
                } else if (btn[0] == '=') {
                    if (state->pending_op != 0) {
                        int64_t val = 0;
                        for (int k = 0; state->display[k]; k++) {
                            val = val * 10 + (state->display[k] - '0');
                        }
                        int64_t res = 0;
                        if (state->pending_op == '+') res = state->prev_val + val;
                        else if (state->pending_op == '-') res = state->prev_val - val;
                        else if (state->pending_op == '*') res = state->prev_val * val;
                        else if (state->pending_op == '/') res = (val != 0) ? (state->prev_val / val) : 0;

                        itoa(res, state->display, 10);
                        state->pending_op = 0;
                        state->clear_on_next = true;
                    }
                }
                return;
            }
        }
    }
}

void app_calc_launch(void) {
    window_t* win = wm_create_window("Calcolatrice", 200, 120, 210, 215, APP_CALC);
    if (!win) return;

    calc_state_t* state = (calc_state_t*)kmalloc(sizeof(calc_state_t));
    if (state) {
        memset(state, 0, sizeof(calc_state_t));
        strcpy(state->display, "0");

        win->app_data = state;
        win->on_draw = calc_draw;
        win->on_mouse = calc_click;
    }
}
