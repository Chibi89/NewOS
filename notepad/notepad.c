/**
 * notepad/notepad.c
 * Editor di testo Blocco Note (NOTEPAD.EXE) per NewOS V1 (64-bit)
 * Integrato con COMDLG.DLL (Apri/Salva con nome) e Appunti Globali (Ctrl+C/V/X/S/O/N).
 * (C) Newspoint Software - NewSoft - NewOS V1
 */

#include "notepad.h"
#include <gui/wm.h>
#include <gui/graphics.h>
#include <drivers/keyboard.h>
#include <fs/vfs.h>
#include <include/comdlg.h>
#include <include/clipboard.h>
#include <mm/heap.h>
#include <string.h>

#define NP_MAX_TEXT 2048

typedef struct {
    char text[NP_MAX_TEXT];
    int  len;
    char filepath[128];
    char status[64];
} notepad_state_t;

static void notepad_on_open_file_cb(const char* selected_path, void* user_data) {
    window_t* win = (window_t*)user_data;
    if (!win || !win->app_data || !selected_path || !selected_path[0]) return;
    notepad_state_t* state = (notepad_state_t*)win->app_data;

    uint32_t sz = 0;
    char* buf = (char*)vfs_read_entire_file(selected_path, &sz);
    if (buf) {
        if (sz >= NP_MAX_TEXT) sz = NP_MAX_TEXT - 1;
        memcpy(state->text, buf, sz);
        state->text[sz] = '\0';
        state->len = (int)sz;
        kfree(buf);
        strncpy(state->filepath, selected_path, sizeof(state->filepath) - 1);
        mini_snprintf(win->title, sizeof(win->title), "%s - Blocco note", selected_path);
        strcpy(state->status, "File caricato da disco con successo.");
    } else {
        strcpy(state->status, "Impossibile aprire il file selezionato.");
    }
}

static void notepad_on_save_file_cb(const char* selected_path, void* user_data) {
    window_t* win = (window_t*)user_data;
    if (!win || !win->app_data || !selected_path || !selected_path[0]) return;
    notepad_state_t* state = (notepad_state_t*)win->app_data;

    if (!vfs_exists(selected_path)) {
        vfs_create(selected_path);
    }
    if (vfs_write_entire_file_inplace(selected_path, state->text, (uint32_t)state->len) >= 0) {
        strncpy(state->filepath, selected_path, sizeof(state->filepath) - 1);
        mini_snprintf(win->title, sizeof(win->title), "%s - Blocco note", selected_path);
        strcpy(state->status, "File salvato su disco con successo.");
    } else {
        strcpy(state->status, "Errore durante il salvataggio del file.");
    }
}

static void notepad_draw(window_t* win) {
    notepad_state_t* state = (notepad_state_t*)win->app_data;
    if (!state) return;

    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    // Barra strumenti / menu interattiva (Nuovo, Apri, Salva, Copia, Incolla, Taglia)
    gfx_fill_rect(cx, cy, cw, 24, COLOR_BTNFACE);
    wm_draw_themed_button(cx + 2,   cy + 2, 54, 20, "Nuovo", false);
    wm_draw_themed_button(cx + 58,  cy + 2, 62, 20, "Apri...", false);
    wm_draw_themed_button(cx + 122, cy + 2, 62, 20, "Salva...", false);
    wm_draw_themed_button(cx + 190, cy + 2, 56, 20, "Copia", false);
    wm_draw_themed_button(cx + 248, cy + 2, 64, 20, "Incolla", false);
    wm_draw_themed_button(cx + 314, cy + 2, 58, 20, "Taglia", false);

    // Area di testo bianca con bordo scavato 3D
    int32_t tx = cx;
    int32_t ty = cy + 26;
    int32_t tw = cw;
    int32_t th = ch - 46;

    gfx_fill_rect(tx, ty, tw, th, COLOR_WINDOW);
    gfx_draw_bevel_rect(tx, ty, tw, th, true);

    // Renderizza il testo inserito
    int32_t px = tx + 6;
    int32_t py = ty + 6;

    for (int i = 0; i < state->len; i++) {
        char c = state->text[i];
        if (c == '\r') continue;
        if (c == '\n') {
            py += 16;
            px = tx + 6;
            if (py + 16 > ty + th) break;
        } else {
            gfx_draw_char(px, py, c, COLOR_WINDOWTEXT, COLOR_WINDOW, false);
            px += 8;
            if (px + 8 > tx + tw - 8) {
                py += 16;
                px = tx + 6;
                if (py + 16 > ty + th) break;
            }
        }
    }

    // Cursore solido
    gfx_fill_rect(px, py, 2, 14, 0x000000);

    // Barra di stato inferiore
    gfx_fill_rect(cx, cy + ch - 18, cw, 18, COLOR_BTNFACE);
    gfx_draw_string(cx + 4, cy + ch - 15, state->status, 0x00509E, 0, true);
}

static void notepad_do_copy(notepad_state_t* state) {
    clipboard_set_text(state->text);
    strcpy(state->status, "Testo copiato negli Appunti di sistema (Ctrl+C).");
}

static void notepad_do_paste(notepad_state_t* state) {
    const char* clip = clipboard_get_text();
    if (!clip || !clip[0]) {
        strcpy(state->status, "Appunti vuoti.");
        return;
    }
    size_t clen = strlen(clip);
    for (size_t i = 0; i < clen && state->len < NP_MAX_TEXT - 2; i++) {
        state->text[state->len++] = clip[i];
    }
    state->text[state->len] = '\0';
    strcpy(state->status, "Testo incollato dagli Appunti (Ctrl+V).");
}

static void notepad_do_cut(notepad_state_t* state) {
    clipboard_set_text(state->text);
    state->len = 0;
    state->text[0] = '\0';
    strcpy(state->status, "Testo tagliato negli Appunti (Ctrl+X).");
}

static void notepad_mouse(window_t* win, int32_t lx, int32_t ly, bool clicked) {
    if (!clicked || !win || !win->app_data) return;

    notepad_state_t* state = (notepad_state_t*)win->app_data;
    int32_t rx = lx - 4;
    int32_t ry = ly - 24;

    if (ry >= 2 && ry <= 22) {
        if (rx >= 2 && rx <= 56) {
            state->len = 0;
            state->text[0] = '\0';
            state->filepath[0] = '\0';
            strcpy(win->title, "Senza nome - Blocco note");
            strcpy(state->status, "Nuovo documento creato.");
        } else if (rx >= 58 && rx <= 120) {
            comdlg_open_file_dialog("C:\\User\\Morgan\\Desktop", notepad_on_open_file_cb, win);
        } else if (rx >= 122 && rx <= 184) {
            comdlg_save_file_dialog("C:\\User\\Morgan\\Desktop", "NOTA.TXT", notepad_on_save_file_cb, win);
        } else if (rx >= 190 && rx <= 246) {
            notepad_do_copy(state);
        } else if (rx >= 248 && rx <= 312) {
            notepad_do_paste(state);
        } else if (rx >= 314 && rx <= 372) {
            notepad_do_cut(state);
        }
    }
}

static void notepad_key(window_t* win, char c) {
    notepad_state_t* state = (notepad_state_t*)win->app_data;
    if (!state) return;

    uint8_t uc = (uint8_t)c;
    if (uc == KEY_CTRL_O) {
        comdlg_open_file_dialog("C:\\User\\Morgan\\Desktop", notepad_on_open_file_cb, win);
        return;
    }
    if (uc == KEY_CTRL_S) {
        comdlg_save_file_dialog("C:\\User\\Morgan\\Desktop", "NOTA.TXT", notepad_on_save_file_cb, win);
        return;
    }
    if (uc == KEY_CTRL_C) {
        notepad_do_copy(state);
        return;
    }
    if (uc == KEY_CTRL_V) {
        notepad_do_paste(state);
        return;
    }
    if (uc == KEY_CTRL_X) {
        notepad_do_cut(state);
        return;
    }
    if (uc == KEY_CTRL_N) {
        state->len = 0;
        state->text[0] = '\0';
        strcpy(state->status, "Nuovo documento (Ctrl+N).");
        return;
    }

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

    window_t* win = wm_create_window(win_title, 150, 95, 440, 300, APP_NOTEPAD);
    if (!win) return;

    notepad_state_t* state = (notepad_state_t*)kmalloc(sizeof(notepad_state_t));
    if (state) {
        memset(state, 0, sizeof(notepad_state_t));
        if (content && content[0] != '\0') {
            strncpy(state->text, content, NP_MAX_TEXT - 1);
            state->len = (int)strlen(state->text);
        }
        strcpy(state->status, "Pronto | Ctrl+O Apri | Ctrl+S Salva | Ctrl+C/V Appunti");
        win->app_data = state;
        win->on_draw  = notepad_draw;
        win->on_mouse = notepad_mouse;
        win->on_key   = notepad_key;
    }
}

void app_notepad_launch(void) {
    app_notepad_open_file("Senza nome", "Benvenuto in NewOS V1 Notepad!\nUsa i pulsanti Apri/Salva (COMDLG.DLL) o Ctrl+O / Ctrl+S.");
}
