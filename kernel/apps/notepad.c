#include <gui/wm.h>
#include <gui/graphics.h>
#include <mm/heap.h>
#include <string.h>

#define NP_MAX_TEXT 2048

typedef struct {
    char text[NP_MAX_TEXT];
    int len;
} notepad_state_t;

static void notepad_draw(window_t* win) {
    notepad_state_t* state = (notepad_state_t*)win->app_data;
    if (!state) return;

    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    // Barra dei menu classica Win2000 (File, Modifica, Cerca, Aiuto)
    gfx_fill_rect(cx, cy, cw, 18, COLOR_BTNFACE);
    gfx_draw_string(cx + 6, cy + 2, "File   Modifica   Formato   Guida", COLOR_WINDOWTEXT, 0, true);

    // Area di testo bianca con bordo scavato 3D
    int32_t tx = cx;
    int32_t ty = cy + 20;
    int32_t tw = cw;
    int32_t th = ch - 20;

    gfx_fill_rect(tx, ty, tw, th, COLOR_WINDOW);
    gfx_draw_bevel_rect(tx, ty, tw, th, true);

    // Renderizza il testo inserito
    int32_t px = tx + 4;
    int32_t py = ty + 4;

    for (int i = 0; i < state->len; i++) {
        char c = state->text[i];
        if (c == '\n') {
            py += 16;
            px = tx + 4;
            if (py + 16 > ty + th) break;
        } else {
            gfx_draw_char(px, py, c, COLOR_WINDOWTEXT, COLOR_WINDOW, false);
            px += 8;
            if (px + 8 > tx + tw - 8) {
                py += 16;
                px = tx + 4;
                if (py + 16 > ty + th) break;
            }
        }
    }

    // Cursore lampeggiante / solido
    gfx_fill_rect(px, py, 2, 14, 0x000000);
}

static void notepad_key(window_t* win, char c) {
    notepad_state_t* state = (notepad_state_t*)win->app_data;
    if (!state) return;

    if (c == '\b') {
        if (state->len > 0) {
            state->len--;
            state->text[state->len] = '\0';
        }
    } else if (c == '\n' || (c >= 32 && c <= 126)) {
        if (state->len < NP_MAX_TEXT - 2) {
            state->text[state->len++] = c;
            state->text[state->len] = '\0';
        }
    }
}

void app_notepad_open_file(const char* title, const char* content) {
    char win_title[64];
    if (title) {
        mini_snprintf(win_title, sizeof(win_title), "%s - Blocco note", title);
    } else {
        strcpy(win_title, "Senza nome - Blocco note");
    }

    window_t* win = wm_create_window(win_title, 160, 110, 420, 280, APP_NOTEPAD);
    if (!win) return;

    notepad_state_t* state = (notepad_state_t*)kmalloc(sizeof(notepad_state_t));
    if (state) {
        memset(state, 0, sizeof(notepad_state_t));
        if (content && content[0] != '\0') {
            strncpy(state->text, content, NP_MAX_TEXT - 1);
            state->len = (int)strlen(state->text);
        }
        win->app_data = state;
        win->on_draw = notepad_draw;
        win->on_key = notepad_key;
    }
}

void app_notepad_launch(void) {
    app_notepad_open_file("Senza nome", "Benvenuto in NewOS V1 Notepad!\nPuoi digitare testo direttamente dalla tua tastiera.");
}
