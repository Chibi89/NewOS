/**
 * notepad/notepad.c
 * Editor di testo Blocco Note (NOTEPAD.EXE) per NewOS V1 (64-bit)
 * Integrato con COMDLG.DLL (Apri/Salva con nome) e Appunti Globali
 * (Ctrl+C/V/X/S/O/N). (C) Newspoint Software - NewSoft - NewOS V1
 */

#include "notepad.h"
#include <drivers/keyboard.h>
#include <fs/vfs.h>
#include <gui/graphics.h>
#include <gui/wm.h>
#include <include/clipboard.h>
#include <include/comdlg.h>
#include <mm/heap.h>
#include <string.h>

#define NP_MAX_TEXT 8192

typedef struct {
  char text[NP_MAX_TEXT];
  int len;
  char undo_text[NP_MAX_TEXT];
  int undo_len;
  bool show_line_numbers;
  bool find_open;
  char find_query[32];
  char filepath[128];
  char status[64];
} notepad_state_t;

static void notepad_save_undo(notepad_state_t *state) {
  memcpy(state->undo_text, state->text, (size_t)state->len + 1);
  state->undo_len = state->len;
}

static int notepad_match_at(const char *txt, int idx, const char *q) {
  if (!q || !q[0])
    return 0;
  int j = 0;
  while (q[j]) {
    char a = txt[idx + j], b = q[j];
    if (!a)
      return 0;
    if (a >= 'A' && a <= 'Z')
      a += 32;
    if (b >= 'A' && b <= 'Z')
      b += 32;
    if (a != b)
      return 0;
    j++;
  }
  return j;
}

static void notepad_on_open_file_cb(const char *selected_path,
                                    void *user_data) {
  window_t *win = (window_t *)user_data;
  if (!win || !win->app_data || !selected_path || !selected_path[0])
    return;
  notepad_state_t *state = (notepad_state_t *)win->app_data;

  uint32_t sz = 0;
  char *buf = (char *)vfs_read_entire_file(selected_path, &sz);
  if (buf) {
    if (sz >= NP_MAX_TEXT)
      sz = NP_MAX_TEXT - 1;
    memcpy(state->text, buf, sz);
    state->text[sz] = '\0';
    state->len = (int)sz;
    kfree(buf);
    strncpy(state->filepath, selected_path, sizeof(state->filepath) - 1);
    mini_snprintf(win->title, sizeof(win->title), "%s - Blocco note",
                  selected_path);
    strcpy(state->status, "File caricato da disco con successo.");
  } else {
    strcpy(state->status, "Impossibile aprire il file selezionato.");
  }
}

static void notepad_on_save_file_cb(const char *selected_path,
                                    void *user_data) {
  window_t *win = (window_t *)user_data;
  if (!win || !win->app_data || !selected_path || !selected_path[0])
    return;
  notepad_state_t *state = (notepad_state_t *)win->app_data;

  if (!vfs_exists(selected_path)) {
    vfs_create(selected_path);
  }
  if (vfs_write_entire_file_inplace(selected_path, state->text,
                                    (uint32_t)state->len) >= 0) {
    strncpy(state->filepath, selected_path, sizeof(state->filepath) - 1);
    mini_snprintf(win->title, sizeof(win->title), "%s - Blocco note",
                  selected_path);
    strcpy(state->status, "File salvato su disco con successo.");
  } else {
    strcpy(state->status, "Errore durante il salvataggio del file.");
  }
}

static void notepad_draw(window_t *win) {
  notepad_state_t *state = (notepad_state_t *)win->app_data;
  if (!state)
    return;

  int32_t cx = win->x + 4;
  int32_t cy = win->y + 24;
  int32_t cw = win->width - 8;
  int32_t ch = win->height - 28;

  // Barra strumenti / menu interattiva
  gfx_fill_rect(cx, cy, cw, 24, COLOR_BTNFACE);
  wm_draw_themed_button(cx + 2, cy + 2, 52, 20, "Nuovo", false);
  wm_draw_themed_button(cx + 56, cy + 2, 58, 20, "Apri...", false);
  wm_draw_themed_button(cx + 116, cy + 2, 58, 20, "Salva...", false);
  wm_draw_themed_button(cx + 178, cy + 2, 50, 20, "Copia", false);
  wm_draw_themed_button(cx + 230, cy + 2, 58, 20, "Incolla", false);
  wm_draw_themed_button(cx + 290, cy + 2, 54, 20, "Taglia", false);
  wm_draw_themed_button(cx + 348, cy + 2, 50, 20, "Trova", state->find_open);
  wm_draw_themed_button(cx + 400, cy + 2, 52, 20, "Righe",
                        state->show_line_numbers);

  int32_t find_h = 0;
  if (state->find_open) {
    find_h = 24;
    gfx_fill_rect(cx, cy + 24, cw, 24, 0xE2E8F0);
    gfx_draw_string(cx + 6, cy + 29, "Trova:", 0x1E293B, 0, true);
    gfx_fill_rect(cx + 58, cy + 27, 170, 18, 0xFFFFFF);
    gfx_draw_bevel_rect(cx + 58, cy + 27, 170, 18, true);
    gfx_draw_string(cx + 62, cy + 29, state->find_query, 0x000000, 0, true);
    int matches = 0;
    int qlen = (int)strlen(state->find_query);
    if (qlen > 0) {
      for (int i = 0; i <= state->len - qlen; i++) {
        if (notepad_match_at(state->text, i, state->find_query) > 0)
          matches++;
      }
    }
    char m_str[32];
    mini_snprintf(m_str, sizeof(m_str), "%d trovati (Esc chiude)", matches);
    gfx_draw_string(cx + 236, cy + 29, m_str, 0x334155, 0, true);
  }

  // Area di testo bianca con bordo scavato 3D
  int32_t tx = cx;
  int32_t ty = cy + 26 + find_h;
  int32_t tw = cw;
  int32_t th = ch - 46 - find_h;

  gfx_fill_rect(tx, ty, tw, th, COLOR_WINDOW);
  gfx_draw_bevel_rect(tx, ty, tw, th, true);

  int32_t gutter_w = state->show_line_numbers ? 32 : 0;
  if (state->show_line_numbers) {
    gfx_fill_rect(tx + 2, ty + 2, gutter_w - 2, th - 4, 0xF1F5F9);
    gfx_fill_rect(tx + gutter_w, ty + 2, 1, th - 4, 0xCBD5E1);
  }

  // Renderizza il testo inserito
  int32_t text_left = tx + 6 + gutter_w;
  int32_t px = text_left;
  int32_t py = ty + 6;
  int line_no = 1;
  if (state->show_line_numbers) {
    char lbuf[8];
    mini_snprintf(lbuf, sizeof(lbuf), "%d", line_no);
    gfx_draw_string(tx + 5, py, lbuf, 0x64748B, 0, true);
  }

  int hl_remain = 0;
  int qlen = (int)strlen(state->find_query);
  for (int i = 0; i < state->len; i++) {
    char c = state->text[i];
    if (c == '\r')
      continue;
    if (c == '\n') {
      py += 16;
      px = text_left;
      line_no++;
      if (py + 16 > ty + th)
        break;
      if (state->show_line_numbers) {
        char lbuf[8];
        mini_snprintf(lbuf, sizeof(lbuf), "%d", line_no);
        gfx_draw_string(tx + 5, py, lbuf, 0x64748B, 0, true);
      }
    } else {
      if (state->find_open && qlen > 0 && hl_remain == 0) {
        int m = notepad_match_at(state->text, i, state->find_query);
        if (m > 0)
          hl_remain = m;
      }
      uint32_t bg = (hl_remain > 0) ? 0xFEF08A : COLOR_WINDOW;
      if (hl_remain > 0)
        hl_remain--;
      gfx_draw_char(px, py, c, COLOR_WINDOWTEXT, bg, false);
      px += 8;
      if (px + 8 > tx + tw - 8) {
        py += 16;
        px = text_left;
        if (py + 16 > ty + th)
          break;
      }
    }
  }

  // Cursore solido
  gfx_fill_rect(px, py, 2, 14, 0x000000);

  // Barra di stato inferiore
  gfx_fill_rect(cx, cy + ch - 18, cw, 18, COLOR_BTNFACE);
  gfx_draw_string(cx + 4, cy + ch - 15, state->status, 0x00509E, 0, true);
}

static void notepad_do_copy(notepad_state_t *state) {
  clipboard_set_text(state->text);
  strcpy(state->status, "Testo copiato negli Appunti di sistema (Ctrl+C).");
}

static void notepad_do_paste(notepad_state_t *state) {
  const char *clip = clipboard_get_text();
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

static void notepad_do_cut(notepad_state_t *state) {
  clipboard_set_text(state->text);
  state->len = 0;
  state->text[0] = '\0';
  strcpy(state->status, "Testo tagliato negli Appunti (Ctrl+X).");
}

static void notepad_mouse(window_t *win, int32_t lx, int32_t ly, bool clicked) {
  if (!clicked || !win || !win->app_data)
    return;

  notepad_state_t *state = (notepad_state_t *)win->app_data;
  int32_t rx = lx - 4;
  int32_t ry = ly - 24;

  if (ry >= 2 && ry <= 22) {
    if (rx >= 2 && rx <= 54) {
      notepad_save_undo(state);
      state->len = 0;
      state->text[0] = '\0';
      state->filepath[0] = '\0';
      strcpy(win->title, "Senza nome - Blocco note");
      strcpy(state->status, "Nuovo documento creato.");
    } else if (rx >= 56 && rx <= 114) {
      comdlg_open_file_dialog("C:\\User\\Morgan\\Desktop",
                              notepad_on_open_file_cb, win);
    } else if (rx >= 116 && rx <= 174) {
      comdlg_save_file_dialog("C:\\User\\Morgan\\Desktop", "NOTA.TXT",
                              notepad_on_save_file_cb, win);
    } else if (rx >= 178 && rx <= 228) {
      notepad_do_copy(state);
    } else if (rx >= 230 && rx <= 288) {
      notepad_save_undo(state);
      notepad_do_paste(state);
    } else if (rx >= 290 && rx <= 344) {
      notepad_save_undo(state);
      notepad_do_cut(state);
    } else if (rx >= 348 && rx <= 398) {
      state->find_open = !state->find_open;
      if (!state->find_open)
        state->find_query[0] = '\0';
    } else if (rx >= 400 && rx <= 452) {
      state->show_line_numbers = !state->show_line_numbers;
    }
  }
}

static void notepad_key(window_t *win, char c) {
  notepad_state_t *state = (notepad_state_t *)win->app_data;
  if (!state)
    return;

  uint8_t uc = (uint8_t)c;
  if (uc == KEY_CTRL_F) {
    state->find_open = !state->find_open;
    return;
  }
  if (uc == KEY_CTRL_Z) {
    if (state->undo_len >= 0) {
      memcpy(state->text, state->undo_text, (size_t)state->undo_len + 1);
      state->len = state->undo_len;
      strcpy(state->status, "Azione annullata (Ctrl+Z).");
    }
    return;
  }
  if (state->find_open) {
    int qlen = (int)strlen(state->find_query);
    if (c == 27) { // Esc
      state->find_open = false;
      return;
    } else if (c == '\b') {
      if (qlen > 0)
        state->find_query[qlen - 1] = '\0';
      return;
    } else if (c >= 32 && c <= 126 && qlen < 30) {
      state->find_query[qlen] = c;
      state->find_query[qlen + 1] = '\0';
      return;
    }
  }
  if (uc == KEY_CTRL_O) {
    comdlg_open_file_dialog("C:\\User\\Morgan\\Desktop",
                            notepad_on_open_file_cb, win);
    return;
  }
  if (uc == KEY_CTRL_S) {
    comdlg_save_file_dialog("C:\\User\\Morgan\\Desktop", "NOTA.TXT",
                            notepad_on_save_file_cb, win);
    return;
  }
  if (uc == KEY_CTRL_C) {
    notepad_do_copy(state);
    return;
  }
  if (uc == KEY_CTRL_V) {
    notepad_save_undo(state);
    notepad_do_paste(state);
    return;
  }
  if (uc == KEY_CTRL_X) {
    notepad_save_undo(state);
    notepad_do_cut(state);
    return;
  }
  if (uc == KEY_CTRL_N) {
    notepad_save_undo(state);
    state->len = 0;
    state->text[0] = '\0';
    strcpy(state->status, "Nuovo documento (Ctrl+N).");
    return;
  }

  if (c == '\b') {
    if (state->len > 0) {
      notepad_save_undo(state);
      state->len--;
      state->text[state->len] = '\0';
    }
  } else if (c == '\n' || (c >= 32 && c <= 126)) {
    if (state->len < NP_MAX_TEXT - 2) {
      notepad_save_undo(state);
      state->text[state->len++] = c;
      state->text[state->len] = '\0';
    }
  }
}

void app_notepad_open_file(const char *title, const char *content) {
  char win_title[64];
  if (title) {
    mini_snprintf(win_title, sizeof(win_title), "%s - Blocco note", title);
  } else {
    strcpy(win_title, "Senza nome - Blocco note");
  }

  window_t *win = wm_create_window(win_title, 150, 95, 490, 310, APP_NOTEPAD);
  if (!win)
    return;

  notepad_state_t *state = (notepad_state_t *)kmalloc(sizeof(notepad_state_t));
  if (state) {
    memset(state, 0, sizeof(notepad_state_t));
    state->show_line_numbers = true;
    if (content && content[0] != '\0') {
      strncpy(state->text, content, NP_MAX_TEXT - 1);
      state->len = (int)strlen(state->text);
    }
    notepad_save_undo(state);
    strcpy(state->status,
           "Pronto | Ctrl+F Trova | Ctrl+Z Annulla | Ctrl+O/S Apri/Salva");
    win->app_data = state;
    win->on_draw = notepad_draw;
    win->on_mouse = notepad_mouse;
    win->on_key = notepad_key;
  }
}

void app_notepad_launch(void) {
  app_notepad_open_file("Senza nome",
                        "Benvenuto in NewOS V1 Notepad!\nUsa i pulsanti "
                        "Apri/Salva (COMDLG.DLL) o Ctrl+O / Ctrl+S.");
}
