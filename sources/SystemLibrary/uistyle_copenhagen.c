/**
 * SystemLibrary/uistyle_copenhagen.c -> COPENHAGEN.DLL
 * Modulo UI Style autonomo out-of-kernel in C:\NewOS\Resources\UIStyle\
 * Implementa l'interfaccia Copenhagen Glass UI (Top Module Bar, Menu Morgan's Stuff,
 * Risorse del PC con filtri/anteprima flottante, My Scraps e Note).
 * (C) Newspoint Software - NewSoft Dynamic System Library
 */

#include "cpl_dll_ui.h"
#include <drivers/keyboard.h>
#include <fs/vfs.h>
#include <include/uistyle.h>

#ifndef BUILDING_KERNEL_LOADER
__attribute__((section(".header"), used)) module_header_t g_copenhagen_header = {
    .magic = MODULE_MAGIC_NEWOS,
    .header_size = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size = 0,
    .entry_offset = 64,
    .checksum = 0,
    .security_flags =
        SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name = "COPENHAG",
    .signature = MODULE_AUTH_TAG};
#endif

static const kernel_api_t *g_api = NULL;

// Stato globale della sessione desktop Copenhagen (allocato su heap via kmalloc)
typedef struct {
  bool menu_open;
  int menu_hover;
  char status_msg[48];
  window_t *explorer_win;
  window_t *preview_win;
  window_t *scraps_win;
  window_t *notes_win;
  char notes_text[384];
  char scraps_search[32];
  char preview_title[64];
  char preview_body[768];
} cph_desktop_t;

typedef struct {
  bool is_root_computer;
  char cur_dir[128];
  vfs_dirent_t entries[24];
  int32_t entry_cnt;
  int32_t filtered_idx[24];
  int32_t filtered_cnt;
  int sel_idx;
  int filter_mode; // 0=tutti, 1=a-f, 2=g-l, 3=m-s, 4=t-z, 5=cartelle, 6=file
  bool sort_desc;
} cph_explorer_t;

static cph_desktop_t *g_desk = NULL;

static char to_lower_ch(char c) {
  if (c >= 'A' && c <= 'Z')
    return (char)(c + 32);
  return c;
}

static bool has_text_preview_ext(const char *name) {
  size_t len = dll_strlen(name);
  if (len < 4)
    return false;
  const char *e = name + len - 4;
  char c1 = to_lower_ch(e[1]);
  char c2 = to_lower_ch(e[2]);
  char c3 = to_lower_ch(e[3]);
  if (e[0] == '.') {
    if (c1 == 't' && c2 == 'x' && c3 == 't')
      return true;
    if (c1 == 'i' && c2 == 'n' && c3 == 'i')
      return true;
    if (c1 == 'l' && c2 == 'o' && c3 == 'g')
      return true;
    if (c1 == 'h' && c2 == 't' && c3 == 'm')
      return true;
  }
  return false;
}

static void cph_path_join(const char *dir, const char *name, char *out,
                          size_t max_len) {
  if (!out || max_len == 0)
    return;
  dll_strcpy(out, dir ? dir : "", max_len);
  size_t l = dll_strlen(out);
  if (l > 0 && out[l - 1] != '\\' && out[l - 1] != '/') {
    if (l + 1 < max_len) {
      out[l++] = '\\';
      out[l] = '\0';
    }
  }
  if (name) {
    size_t i = 0;
    while (name[i] && l + 1 < max_len) {
      out[l++] = name[i++];
    }
    out[l] = '\0';
  }
}

static void cph_parent_dir(char *dir) {
  if (!dir)
    return;
  size_t len = dll_strlen(dir);
  if (len <= 3)
    return;
  if (dir[len - 1] == '\\' || dir[len - 1] == '/')
    dir[--len] = '\0';
  while (len > 3 && dir[len - 1] != '\\' && dir[len - 1] != '/')
    len--;
  dir[len] = '\0';
}

// ==============================================================================
// FINESTRA ANTEPRIMA FLOTTANTE (stile Copenhagen 0:57)
// ==============================================================================
static void cph_preview_draw(window_t *win) {
  if (!g_api || !win || !g_desk)
    return;
  int32_t cx = win->x + 3;
  int32_t cy = win->y + 24;
  int32_t cw = win->width - 6;
  int32_t ch = win->height - 27;

  // Sfondo bianco pulito stile lettura documento con barra scrollbar destra
  g_api->gfx_fill_rect(cx, cy, cw, ch, 0xFFFFFF);
  g_api->gfx_fill_rect(cx + cw - 14, cy + 2, 12, ch - 4, 0xE6EEF6);
  g_api->gfx_fill_rect(cx + cw - 12, cy + 18, 8, 48, 0x5B9BD5);
  g_api->gfx_draw_rect(cx, cy, cw, ch, 0x8BA8C6);

  int32_t tx = cx + 8;
  int32_t ty = cy + 8;
  int32_t max_chars = (cw - 26) / 8;
  if (max_chars < 10)
    max_chars = 10;

  char line[64];
  int lp = 0;
  for (int i = 0; g_desk->preview_body[i] && ty + 16 < cy + ch - 4; i++) {
    char ch_c = g_desk->preview_body[i];
    if (ch_c == '\r')
      continue;
    if (ch_c == '\n' || lp >= max_chars || lp >= 60) {
      line[lp] = '\0';
      g_api->gfx_draw_string(tx, ty, line, 0x1A1A1A, 0, true);
      ty += 16;
      lp = 0;
      if (ch_c == '\n')
        continue;
    }
    line[lp++] = ch_c;
  }
  if (lp > 0 && ty + 16 < cy + ch - 4) {
    line[lp] = '\0';
    g_api->gfx_draw_string(tx, ty, line, 0x1A1A1A, 0, true);
  }
}

static void cph_preview_key(window_t *win, char c) {
  if (!win || !g_api)
    return;
  if (c == 27) {
    if (g_desk && g_desk->preview_win == win)
      g_desk->preview_win = NULL;
    g_api->wm_destroy_window(win->id);
  }
}

static void cph_open_file_preview(const char *full_path, const char *title) {
  if (!g_api || !g_desk || !full_path)
    return;

  dll_strcpy(g_desk->preview_title, title ? title : "Anteprima",
             sizeof(g_desk->preview_title));
  g_desk->preview_body[0] = '\0';

  if (g_api->vfs_read_entire_file) {
    uint32_t sz = 0;
    uint8_t *buf = g_api->vfs_read_entire_file(full_path, &sz);
    if (buf) {
      size_t lim = (sz < sizeof(g_desk->preview_body) - 1)
                       ? sz
                       : sizeof(g_desk->preview_body) - 1;
      for (size_t i = 0; i < lim; i++) {
        uint8_t b = buf[i];
        g_desk->preview_body[i] =
            (b == '\n' || b == '\r' || (b >= 32 && b < 127)) ? (char)b : '.';
      }
      g_desk->preview_body[lim] = '\0';
      if (g_api->kfree)
        g_api->kfree(buf);
    } else {
      dll_strcpy(g_desk->preview_body, "[Impossibile leggere il contenuto]",
                 sizeof(g_desk->preview_body));
    }
  }

  if (!g_desk->preview_win && g_api->wm_create_window) {
    g_desk->preview_win = (window_t *)g_api->wm_create_window(
        g_desk->preview_title, 730, 405, 250, 260, 1);
  }
  if (g_desk->preview_win) {
    dll_strcpy(g_desk->preview_win->title, g_desk->preview_title,
               sizeof(g_desk->preview_win->title));
    g_desk->preview_win->on_draw = cph_preview_draw;
    g_desk->preview_win->on_key = cph_preview_key;
  }
}

// ==============================================================================
// FINESTRA RISORSE DEL PC (Vista a Librerie & Stack stile Copenhagen)
// ==============================================================================
static void cph_exp_apply_filter(cph_explorer_t *st) {
  if (!st)
    return;
  st->filtered_cnt = 0;
  for (int32_t i = 0; i < st->entry_cnt; i++) {
    char c = to_lower_ch(st->entries[i].name[0]);
    bool ok = true;
    if (st->filter_mode == 1)
      ok = (c >= 'a' && c <= 'f');
    else if (st->filter_mode == 2)
      ok = (c >= 'g' && c <= 'l');
    else if (st->filter_mode == 3)
      ok = (c >= 'm' && c <= 's');
    else if (st->filter_mode == 4)
      ok = (c >= 't' && c <= 'z');
    else if (st->filter_mode == 5)
      ok = st->entries[i].is_directory;
    else if (st->filter_mode == 6)
      ok = !st->entries[i].is_directory;

    if (ok && st->filtered_cnt < 24) {
      st->filtered_idx[st->filtered_cnt++] = i;
    }
  }
  if (st->sel_idx >= st->filtered_cnt) {
    st->sel_idx = (st->filtered_cnt > 0) ? 0 : -1;
  }
}

static void cph_exp_load_dir(cph_explorer_t *st, const char *path) {
  if (!st || !g_api)
    return;
  if (!path || !path[0]) {
    st->is_root_computer = true;
    st->cur_dir[0] = '\0';
    st->entry_cnt = 0;
    for (int i = 0; i < 5; i++) {
      const char *drv = (i == 0) ? "Disco locale (C:)"
                      : (i == 1) ? "Floppy (A:)"
                      : (i == 2) ? "Blu-Ray (D:)"
                      : (i == 3) ? "Disco USB (U:)"
                                 : "Disco RAM (R:)";
      dll_strcpy(st->entries[st->entry_cnt].name, drv,
                 sizeof(st->entries[st->entry_cnt].name));
      st->entries[st->entry_cnt].is_directory = true;
      st->entries[st->entry_cnt].size = 0;
      st->entry_cnt++;
    }
  } else {
    st->is_root_computer = false;
    dll_strcpy(st->cur_dir, path, sizeof(st->cur_dir));
    st->entry_cnt =
        g_api->vfs_readdir ? g_api->vfs_readdir(st->cur_dir, st->entries, 24) : 0;
    if (st->entry_cnt < 0)
      st->entry_cnt = 0;
  }
  st->sel_idx = (st->entry_cnt > 0) ? 0 : -1;
  cph_exp_apply_filter(st);
}

static void cph_exp_draw(window_t *win) {
  if (!g_api || !win || !win->app_data)
    return;
  cph_explorer_t *st = (cph_explorer_t *)win->app_data;

  int32_t cx = win->x + 3;
  int32_t cy = win->y + 24;
  int32_t cw = win->width - 6;
  int32_t ch = win->height - 27;

  // 1. Banner superiore ghiaccio-azzurro traslucido (stile Copenhagen)
  int32_t banner_h = 66;
  g_api->gfx_draw_gradient_v(cx, cy, cw, banner_h, 0xD8ECFA, 0xB8D8F0);
  g_api->gfx_fill_rect(cx, cy + banner_h - 1, cw, 1, 0x8EB5D6);

  g_api->gfx_draw_icon_32(cx + 14, cy + 16, ICON_THIS_PC);
  char cnt_lbl[32];
  dll_uint_to_str((uint32_t)st->filtered_cnt, cnt_lbl, 0);
  size_t cl = dll_strlen(cnt_lbl);
  dll_strcpy(cnt_lbl + cl,
             (st->filtered_cnt == 1) ? " elemento" : " elementi",
             sizeof(cnt_lbl) - cl);
  g_api->gfx_draw_string(cx + 58, cy + 24, cnt_lbl, 0x102A43, 0, true);

  // Metadati a destra del banner
  const char *sel_name = "specifica";
  char sel_size[24] = "specifica";
  const char *sel_type = "specifica";
  if (st->sel_idx >= 0 && st->sel_idx < st->filtered_cnt) {
    vfs_dirent_t *e = &st->entries[st->filtered_idx[st->sel_idx]];
    sel_name = e->name;
    sel_type = e->is_directory ? "Cartella / Unita'" : "File";
    if (!e->is_directory) {
      dll_uint_to_str(e->size, sel_size, 0);
      size_t sl = dll_strlen(sel_size);
      dll_strcpy(sel_size + sl, " B", sizeof(sel_size) - sl);
    } else {
      dll_strcpy(sel_size, "--", sizeof(sel_size));
    }
  }
  int32_t mx = cx + cw - 260;
  g_api->gfx_draw_string(mx, cy + 8, "Nome:", 0x486581, 0, true);
  g_api->gfx_draw_string(mx + 96, cy + 8, sel_name, 0x102A43, 0, true);
  g_api->gfx_draw_string(mx, cy + 26, "Tipo:", 0x486581, 0, true);
  g_api->gfx_draw_string(mx + 96, cy + 26, sel_type, 0x102A43, 0, true);
  g_api->gfx_draw_string(mx, cy + 44, "Dimensione:", 0x486581, 0, true);
  g_api->gfx_draw_string(mx + 96, cy + 44, sel_size, 0x102A43, 0, true);

  // 2. Sidebar sinistra (Navigazione e Raggruppa per:)
  int32_t side_w = 152;
  int32_t body_y = cy + banner_h;
  int32_t body_h = ch - banner_h;
  g_api->gfx_fill_rect(cx, body_y, side_w, body_h, 0xF2F7FB);
  g_api->gfx_fill_rect(cx + side_w, body_y, 1, body_h, 0xC6D8E8);

  const char *navs[] = {"computer", "rete", "email", "condividi", "pubblica"};
  for (int i = 0; i < 5; i++) {
    int32_t ny = body_y + 10 + i * 22;
    if (i == 0 && st->is_root_computer) {
      g_api->gfx_fill_rect(cx + 6, ny - 2, side_w - 12, 20, 0xCCE4F7);
    }
    g_api->gfx_draw_string(cx + 14, ny, navs[i], 0x1E3A5F, 0, true);
  }

  g_api->gfx_fill_rect(cx + 10, body_y + 126, side_w - 20, 1, 0xD0DFEC);
  g_api->gfx_draw_string(cx + 12, body_y + 136, "Raggruppa per:", 0x334E68, 0,
                         true);

  const char *filters[] = {"tutti", "a-f", "g-l", "m-s", "t-z", "cartelle", "file"};
  for (int f = 0; f < 7; f++) {
    int32_t fy = body_y + 158 + f * 20;
    bool active = (st->filter_mode == f);
    if (active) {
      g_api->gfx_fill_rect(cx + 8, fy - 2, side_w - 16, 18, 0xD8ECFA);
    }
    g_api->gfx_draw_string(cx + 18, fy, filters[f],
                           active ? 0x005A9E : 0x486581, 0, true);
  }

  // 3. Toolbar e Area Contenuti principale
  int32_t main_x = cx + side_w + 1;
  int32_t main_w = cw - side_w - 1;
  g_api->gfx_fill_rect(main_x, body_y, main_w, 30, 0xFFFFFF);
  g_api->gfx_fill_rect(main_x, body_y + 30, main_w, 1, 0xDCE7F2);

  g_api->wm_draw_themed_button(main_x + 8, body_y + 4, 96, 22, "Livello sup.",
                               false);
  g_api->wm_draw_themed_button(main_x + 110, body_y + 4, 130, 22,
                               "2 Percorsi Libr.", false);
  g_api->wm_draw_themed_button(main_x + 246, body_y + 4, 108, 22,
                               st->sort_desc ? "Ordina: Z-A" : "Ordina: A-Z",
                               false);

  // Griglia file / cartelle / dischi
  int32_t grid_y = body_y + 31;
  int32_t grid_h = body_h - 31;
  g_api->gfx_fill_rect(main_x, grid_y, main_w, grid_h, 0xFFFFFF);

  int cols = 2;
  int32_t cell_w = (main_w - 20) / cols;
  int32_t cell_h = 44;
  for (int32_t i = 0; i < st->filtered_cnt && i < 12; i++) {
    int col = i % cols;
    int row = i / cols;
    int32_t ix = main_x + 10 + col * cell_w;
    int32_t iy = grid_y + 10 + row * cell_h;
    if (iy + cell_h > cy + ch)
      break;

    vfs_dirent_t *ent = &st->entries[st->filtered_idx[i]];
    bool is_sel = (i == st->sel_idx);
    if (is_sel) {
      g_api->gfx_fill_rounded_rect(ix, iy, cell_w - 8, 38, 4, 0xDCEFFB);
      g_api->gfx_draw_rect(ix, iy, cell_w - 8, 38, 0x7EB8E5);
    }

    int icon = ent->is_directory ? ICON_FOLDER : ICON_DOCUMENT;
    if (st->is_root_computer) {
      icon = (i == 0 || i == 4) ? ICON_HD
             : (i == 1)         ? ICON_FLOPPY
             : (i == 2)         ? ICON_CD
                                : ICON_HD;
    }
    g_api->gfx_draw_icon_32(ix + 6, iy + 3, icon);
    g_api->gfx_draw_string(ix + 44, iy + 12, ent->name, 0x1A1A1A, 0, true);
  }
}

static void cph_exp_activate_item(cph_explorer_t *st, int idx) {
  if (!st || idx < 0 || idx >= st->filtered_cnt)
    return;
  vfs_dirent_t *ent = &st->entries[st->filtered_idx[idx]];

  if (st->is_root_computer) {
    const char *roots[] = {"C:\\", "A:\\", "D:\\", "U:\\", "R:\\"};
    int orig = st->filtered_idx[idx];
    if (orig >= 0 && orig < 5) {
      cph_exp_load_dir(st, roots[orig]);
    }
    return;
  }

  if (ent->is_directory) {
    char next[128];
    cph_path_join(st->cur_dir, ent->name, next, sizeof(next));
    cph_exp_load_dir(st, next);
  } else if (has_text_preview_ext(ent->name)) {
    char full[128];
    cph_path_join(st->cur_dir, ent->name, full, sizeof(full));
    cph_open_file_preview(full, ent->name);
  }
}

static void cph_exp_mouse(window_t *win, int32_t lx, int32_t ly, bool clicked) {
  if (!clicked || !win || !win->app_data || !g_api)
    return;
  cph_explorer_t *st = (cph_explorer_t *)win->app_data;

  int32_t rx = lx - 3;
  int32_t ry = ly - 24;
  int32_t cw = win->width - 6;
  int32_t side_w = 152;
  int32_t body_y = 66;

  // Click nella sidebar sinistra
  if (rx >= 0 && rx < side_w && ry >= body_y) {
    int32_t sy = ry - body_y;
    if (sy >= 10 && sy < 120) {
      int nav = (sy - 10) / 22;
      if (nav == 0)
        cph_exp_load_dir(st, NULL);
      else if (nav == 1)
        cph_exp_load_dir(st, "C:\\NewOS\\FtpCache");
      return;
    }
    if (sy >= 156 && sy < 300) {
      int f = (sy - 156) / 20;
      if (f >= 0 && f < 7) {
        st->filter_mode = f;
        cph_exp_apply_filter(st);
      }
      return;
    }
  }

  // Click nella toolbar
  if (rx > side_w && ry >= body_y && ry < body_y + 30) {
    int32_t tx = rx - (side_w + 1);
    if (tx >= 8 && tx <= 104) {
      if (!st->is_root_computer) {
        if (dll_strlen(st->cur_dir) <= 3) {
          cph_exp_load_dir(st, NULL);
        } else {
          cph_parent_dir(st->cur_dir);
          cph_exp_load_dir(st, st->cur_dir);
        }
      }
      return;
    }
    if (tx >= 246 && tx <= 354) {
      st->sort_desc = !st->sort_desc;
      // Inversione semplice dell'ordine filtrato
      for (int i = 0; i < st->filtered_cnt / 2; i++) {
        int32_t tmp = st->filtered_idx[i];
        st->filtered_idx[i] = st->filtered_idx[st->filtered_cnt - 1 - i];
        st->filtered_idx[st->filtered_cnt - 1 - i] = tmp;
      }
      return;
    }
  }

  // Click nella griglia elementi
  int32_t grid_y = body_y + 31;
  int32_t main_x = side_w + 1;
  int32_t main_w = cw - side_w - 1;
  if (rx >= main_x + 10 && ry >= grid_y + 10) {
    int32_t cell_w = (main_w - 20) / 2;
    int col = (rx - (main_x + 10)) / cell_w;
    int row = (ry - (grid_y + 10)) / 44;
    if (col >= 0 && col < 2 && row >= 0) {
      int idx = row * 2 + col;
      if (idx >= 0 && idx < st->filtered_cnt) {
        if (st->sel_idx == idx) {
          cph_exp_activate_item(st, idx);
        } else {
          st->sel_idx = idx;
          vfs_dirent_t *ent = &st->entries[st->filtered_idx[idx]];
          if (!ent->is_directory && has_text_preview_ext(ent->name)) {
            char full[128];
            cph_path_join(st->cur_dir, ent->name, full, sizeof(full));
            cph_open_file_preview(full, ent->name);
          }
        }
      }
    }
  }
}

static void cph_exp_key(window_t *win, char c) {
  if (!win || !win->app_data)
    return;
  cph_explorer_t *st = (cph_explorer_t *)win->app_data;
  if (c == 27 && g_api && g_api->wm_destroy_window) {
    if (g_desk && g_desk->explorer_win == win)
      g_desk->explorer_win = NULL;
    g_api->wm_destroy_window(win->id);
  } else if (c == '\n' || c == '\r') {
    cph_exp_activate_item(st, st->sel_idx);
  } else if (c == 8 && !st->is_root_computer) {
    if (dll_strlen(st->cur_dir) <= 3)
      cph_exp_load_dir(st, NULL);
    else {
      cph_parent_dir(st->cur_dir);
      cph_exp_load_dir(st, st->cur_dir);
    }
  }
}

static void cph_open_explorer_window(const char *path) {
  if (!g_api || !g_desk || !g_api->wm_create_window)
    return;

  if (!g_desk->explorer_win) {
    cph_explorer_t *st =
        (cph_explorer_t *)g_api->kmalloc(sizeof(cph_explorer_t));
    if (!st)
      return;
    for (size_t i = 0; i < sizeof(cph_explorer_t); i++)
      ((uint8_t *)st)[i] = 0;

    window_t *win = (window_t *)g_api->wm_create_window("Risorse del PC", 160,
                                                        90, 580, 420, 1);
    if (!win) {
      g_api->kfree(st);
      return;
    }
    win->app_data = st;
    win->on_draw = cph_exp_draw;
    win->on_mouse = cph_exp_mouse;
    win->on_key = cph_exp_key;
    g_desk->explorer_win = win;
  }

  cph_exp_load_dir((cph_explorer_t *)g_desk->explorer_win->app_data, path);
}

// ==============================================================================
// FINESTRE MODULARI MY SCRAPS & NOTE (stile Copenhagen 1:11)
// ==============================================================================
static void cph_scraps_draw(window_t *win) {
  if (!g_api || !win || !g_desk)
    return;
  int32_t cx = win->x + 3;
  int32_t cy = win->y + 24;
  int32_t cw = win->width - 6;
  int32_t ch = win->height - 27;

  g_api->gfx_fill_rect(cx, cy, cw, ch, 0xE6F0FA);

  // Barra superiore My Scraps: freccia verde indietro + box Cerca
  g_api->gfx_fill_rounded_rect(cx + 8, cy + 6, 28, 22, 11, 0x22A84A);
  g_api->gfx_draw_string(cx + 14, cy + 9, "<-", 0xFFFFFF, 0, true);
  g_api->gfx_fill_rounded_rect(cx + cw - 126, cy + 6, 118, 22, 6, 0xFFFFFF);
  g_api->gfx_draw_rect(cx + cw - 126, cy + 6, 118, 22, 0x99B8D4);
  g_api->gfx_draw_string(cx + cw - 118, cy + 9, "Cerca...", 0x7A8B99, 0, true);

  // Sub-pannello scuro
  g_api->gfx_fill_rect(cx + 8, cy + 34, cw - 16, 24, 0x2E3440);
  g_api->gfx_draw_string(cx + 16, cy + 38, "Libreria Ritagli & Articoli",
                         0xFFFFFF, 0, true);

  // Pannello lettura a gradiente verticale
  int32_t pane_h = ch - 128;
  if (pane_h < 60)
    pane_h = 60;
  g_api->gfx_draw_gradient_v(cx + 8, cy + 58, cw - 16, pane_h, 0xFFFFFF,
                             0xD0DFEC);
  g_api->gfx_draw_string(cx + 16, cy + 68, "NewOS Copenhagen Glass UI",
                         0x1A1A1A, 0, true);
  g_api->gfx_draw_string(cx + 16, cy + 88, "Modulo eseguito fuori dal kernel",
                         0x334E68, 0, true);
  g_api->gfx_draw_string(cx + 16, cy + 108, "in C:\\NewOS\\Resources\\UIStyle",
                         0x005A9E, 0, true);

  // 5 miniature inferiori
  int32_t thumb_y = cy + 64 + pane_h;
  uint32_t cols[5] = {0x3898EC, 0x5C646D, 0xD96B27, 0x7F8C8D, 0xC0392B};
  for (int i = 0; i < 5; i++) {
    int32_t tx = cx + 10 + i * 58;
    if (tx + 50 > cx + cw - 8)
      break;
    g_api->gfx_fill_rect(tx, thumb_y, 50, 50, cols[i]);
    g_api->gfx_draw_rect(tx, thumb_y, 50, 50, 0xFFFFFF);
    g_api->gfx_draw_icon_32(tx + 9, thumb_y + 9, ICON_DOCUMENT);
  }
}

static void cph_notes_draw(window_t *win) {
  if (!g_api || !win || !g_desk)
    return;
  int32_t cx = win->x + 3;
  int32_t cy = win->y + 24;
  int32_t cw = win->width - 6;
  int32_t ch = win->height - 27;

  // Corpo giallo post-it + barra inferiore crema
  g_api->gfx_fill_rect(cx, cy, cw, ch - 18, 0xFFF37A);
  g_api->gfx_fill_rect(cx, cy + ch - 18, cw, 18, 0xFAF4BE);

  int32_t tx = cx + 10;
  int32_t ty = cy + 10;
  char line[36];
  int lp = 0;
  for (int i = 0; g_desk->notes_text[i] && ty + 16 < cy + ch - 20; i++) {
    char c = g_desk->notes_text[i];
    if (c == '\n' || lp >= 28) {
      line[lp] = '\0';
      g_api->gfx_draw_string(tx, ty, line, 0x222222, 0, true);
      ty += 16;
      lp = 0;
      if (c == '\n')
        continue;
    }
    line[lp++] = c;
  }
  if (lp > 0) {
    line[lp] = '\0';
    g_api->gfx_draw_string(tx, ty, line, 0x222222, 0, true);
  }
  g_api->gfx_draw_string(cx + 8, cy + ch - 16, "Salvato in NOTE.TXT", 0x6E6A44,
                         0, true);
}

static void cph_notes_key(window_t *win, char c) {
  if (!win || !g_desk || !g_api)
    return;
  if (c == 27) {
    g_desk->notes_win = NULL;
    g_api->wm_destroy_window(win->id);
    return;
  }
  size_t len = dll_strlen(g_desk->notes_text);
  if (c == 8) {
    if (len > 0)
      g_desk->notes_text[len - 1] = '\0';
  } else if ((c == '\n' || c == '\r' || (c >= 32 && c < 127)) &&
             len + 2 < sizeof(g_desk->notes_text)) {
    g_desk->notes_text[len] = (c == '\r') ? '\n' : c;
    g_desk->notes_text[len + 1] = '\0';
  }
  if (g_api->vfs_exists && g_api->vfs_write_entire_file_inplace) {
    if (!g_api->vfs_exists("C:\\User\\Morgan\\NOTE.TXT") && g_api->vfs_create) {
      g_api->vfs_create("C:\\User\\Morgan\\NOTE.TXT");
    }
    g_api->vfs_write_entire_file_inplace("C:\\User\\Morgan\\NOTE.TXT",
                                         g_desk->notes_text,
                                         (uint32_t)dll_strlen(g_desk->notes_text));
  }
}

static void cph_open_workspace_windows(void) {
  if (!g_api || !g_desk || !g_api->wm_create_window)
    return;
  if (!g_desk->scraps_win) {
    window_t *sw = (window_t *)g_api->wm_create_window("My Scraps", 420, 320,
                                                       310, 260, 1);
    if (sw) {
      sw->on_draw = cph_scraps_draw;
      g_desk->scraps_win = sw;
    }
  }
  if (!g_desk->notes_win) {
    window_t *nw =
        (window_t *)g_api->wm_create_window("Note", 744, 355, 240, 225, 1);
    if (nw) {
      nw->on_draw = cph_notes_draw;
      nw->on_key = cph_notes_key;
      g_desk->notes_win = nw;
    }
  }
}

// ==============================================================================
// OVERLAY DESKTOP COPENHAGEN: TOP MODULE BAR, ORB E MENU "MORGAN'S STUFF"
// ==============================================================================
static void cph_draw_desktop(uint32_t sw, uint32_t sh) {
  if (!g_api || !g_desk)
    return;

  // 1. Barra superiore modulare stile Copenhagen
  int32_t top_w = 560;
  int32_t top_x = ((int32_t)sw - top_w) / 2;
  g_api->gfx_fill_rounded_alpha(top_x, 0, top_w, 26, 8, 0xD8ECFA, 190);
  g_api->gfx_draw_rounded_rect(top_x, 0, top_w, 26, 8, 0xFFFFFF);
  g_api->gfx_draw_string(top_x + 16, 5, g_desk->status_msg, 0x102A43, 0, true);

  g_api->gfx_fill_rounded_alpha(top_x + top_w - 280, 3, 86, 20, 4, 0x0078D4, 180);
  g_api->gfx_draw_string(top_x + top_w - 274, 5, "Risorse PC", 0xFFFFFF, 0, true);

  g_api->gfx_fill_rounded_alpha(top_x + top_w - 188, 3, 88, 20, 4, 0x1F5F6B, 180);
  g_api->gfx_draw_string(top_x + top_w - 182, 5, "My Scraps", 0xFFFFFF, 0, true);

  g_api->gfx_fill_rounded_alpha(top_x + top_w - 94, 3, 80, 20, 4, 0x2E7D32, 180);
  g_api->gfx_draw_string(top_x + top_w - 82, 5, "Note...", 0xFFFFFF, 0, true);

  // 2. Orb arancio/rosso in basso a sinistra sopra la taskbar (copre il bottone Start classico)
  int32_t orb_y = (int32_t)sh - 31;
  g_api->gfx_fill_rect(2, orb_y, 60, 26, 0x9FA8B2);
  g_api->gfx_fill_rounded_rect(4, orb_y, 56, 26, 13, 0xD9481E);
  g_api->gfx_fill_rounded_alpha(7, orb_y + 2, 50, 11, 5, 0xFFFFFF, 120);
  g_api->gfx_draw_rounded_rect(4, orb_y, 56, 26, 13, 0xFFB085);
  g_api->gfx_draw_string(22, orb_y + 5, "OS", 0xFFFFFF, 0, true);

  // 3. Menu flottante "Morgan's Stuff" se aperto
  if (g_desk->menu_open) {
    int32_t mx = 80;
    int32_t my = 46;
    int32_t mw = 248;
    int32_t mh = 340;

    g_api->gfx_draw_drop_shadow(mx, my, mw, mh, 6);
    g_api->gfx_fill_rounded_alpha(mx, my, mw, 52, 8, 0x1A525C, 225);
    g_api->gfx_fill_rect(mx, my + 52, mw, 198, 0x7EC4E0);
    g_api->gfx_fill_alpha(mx, my + 52, mw, 198, 0xFFFFFF, 60);
    g_api->gfx_fill_rounded_alpha(mx, my + 250, mw, 90, 8, 0x1E513F, 225);
    g_api->gfx_draw_rounded_rect(mx, my, mw, mh, 8, 0xB8E2F2);

    // Intestazione "Morgan's Stuff" + Avatar
    g_api->gfx_draw_string(mx + 16, my + 18, "Morgan's Stuff", 0xFFFFFF, 0, true);
    g_api->gfx_fill_rect(mx + mw - 48, my + 8, 36, 36, 0xD8ECFA);
    g_api->gfx_draw_rect(mx + mw - 48, my + 8, 36, 36, 0xFFFFFF);
    g_api->gfx_draw_icon_32(mx + mw - 46, my + 10, ICON_ACCESSIBILITY);

    const char *items[4] = {"Documenti", "Immagini", "Musica", "Video"};
    int icons[4] = {ICON_DOCUMENT, ICON_DESKTOP, ICON_SOUND, ICON_PLAY};
    for (int i = 0; i < 4; i++) {
      int32_t iy = my + 66 + i * 44;
      if (g_desk->menu_hover == i) {
        g_api->gfx_fill_rounded_alpha(mx + 10, iy - 4, mw - 20, 36, 4,
                                      0xFFFFFF, 90);
      }
      g_api->gfx_draw_icon_32(mx + 18, iy, icons[i]);
      g_api->gfx_draw_string(mx + 60, iy + 8, items[i], 0x0A2533, 0, true);
    }

    // Footer verde scuro: Programmi e Sistema
    g_api->gfx_fill_rect(mx + 12, my + 250, mw - 24, 1, 0x62A892);
    g_api->gfx_draw_string(mx + 42, my + 266, "Programmi", 0xFFFFFF, 0, true);
    g_api->gfx_draw_string(mx + 42, my + 302, "Sistema", 0xFFFFFF, 0, true);
  }
}

static bool cph_mouse_desktop(uint32_t sw, uint32_t sh, int32_t mx, int32_t my,
                              bool clicked) {
  if (!g_desk)
    return false;

  // Hover nel menu Morgan's Stuff
  if (g_desk->menu_open) {
    int32_t menu_x = 80, menu_y = 46, menu_w = 248, menu_h = 340;
    if (mx >= menu_x && mx <= menu_x + menu_w && my >= menu_y &&
        my <= menu_y + menu_h) {
      if (my >= menu_y + 62 && my < menu_y + 238) {
        g_desk->menu_hover = (my - (menu_y + 62)) / 44;
      } else {
        g_desk->menu_hover = -1;
      }
      if (clicked) {
        if (my >= menu_y + 62 && my < menu_y + 238) {
          int idx = (my - (menu_y + 62)) / 44;
          const char *dirs[4] = {"C:\\User\\Morgan",
                                 "C:\\NewOS\\Resources\\Wallpapers",
                                 "C:\\NewOS\\Resources\\Miusic",
                                 "C:\\NewOS\\Resources\\Videos"};
          g_desk->menu_open = false;
          cph_open_explorer_window(dirs[idx % 4]);
          return true;
        }
        if (my >= menu_y + 256 && my < menu_y + 292) {
          g_desk->menu_open = false;
          cph_open_explorer_window("C:\\ProgramFile");
          return true;
        }
        if (my >= menu_y + 292 && my <= menu_y + menu_h) {
          g_desk->menu_open = false;
          if (g_api && g_api->app_control_panel_open)
            g_api->app_control_panel_open();
          return true;
        }
      }
      return true;
    }
  }

  if (!clicked)
    return false;

  // Click sulla barra superiore modulare
  int32_t top_w = 560;
  int32_t top_x = ((int32_t)sw - top_w) / 2;
  if (my >= 0 && my <= 26 && mx >= top_x && mx <= top_x + top_w) {
    if (mx >= top_x + top_w - 280 && mx < top_x + top_w - 190) {
      cph_open_explorer_window(NULL);
    } else if (mx >= top_x + top_w - 188 && mx < top_x + top_w - 98) {
      cph_open_workspace_windows();
    } else if (mx >= top_x + top_w - 94) {
      cph_open_workspace_windows();
    } else {
      g_desk->menu_open = !g_desk->menu_open;
    }
    return true;
  }

  // Click sull'Orb arancio/rosso in basso a sinistra
  int32_t orb_y = (int32_t)sh - 31;
  if (mx >= 2 && mx <= 40 && my >= orb_y - 2 && my <= (int32_t)sh) {
    g_desk->menu_open = !g_desk->menu_open;
    return true;
  }

  if (g_desk->menu_open) {
    g_desk->menu_open = false;
  }
  return false;
}

// ==============================================================================
// ENTRY POINT DI COPENHAGEN.DLL (Offset 0x40 = 64 byte)
// ==============================================================================
__attribute__((section(".text.entry"), used)) int
copenhagen_dll_entry(const kernel_api_t *api, int cmd,
                     uistyle_params_t *params) {
  if (!api)
    return -1;
  g_api = api;

  if (!g_desk) {
    g_desk = (cph_desktop_t *)g_api->kmalloc(sizeof(cph_desktop_t));
    if (!g_desk)
      return -1;
    for (size_t i = 0; i < sizeof(cph_desktop_t); i++)
      ((uint8_t *)g_desk)[i] = 0;
    dll_strcpy(g_desk->status_msg, "No new messages", sizeof(g_desk->status_msg));
    dll_strcpy(g_desk->notes_text,
               "- Copenhagen Glass UI\n- Modulo in UIStyle\\\n- Font 8x16 NewOS",
               sizeof(g_desk->notes_text));
  }

  if (cmd == UISTYLE_CMD_INIT) {
    return 0;
  }
  if (!params)
    return -1;

  switch (cmd) {
  case UISTYLE_CMD_DRAW_DESKTOP:
    cph_draw_desktop(params->screen_w, params->screen_h);
    return 0;
  case UISTYLE_CMD_MOUSE_DESKTOP:
    params->handled = cph_mouse_desktop(params->screen_w, params->screen_h,
                                        params->mouse_x, params->mouse_y,
                                        params->clicked);
    return 0;
  case UISTYLE_CMD_OPEN_EXPLORER:
    cph_open_explorer_window(params->path_arg);
    params->handled = true;
    return 0;
  case UISTYLE_CMD_OPEN_WORKSPACE:
    cph_open_workspace_windows();
    params->handled = true;
    return 0;
  default:
    return -1;
  }
}
