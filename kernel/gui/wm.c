#include <apps/explorer.h>
#include <apps/installer.h>
#include <apps/player.h>
#include <apps/power_props.h>
#include <apps/taskmgr.h>
#include <arch/x86_64/io.h>
#include <drivers/keyboard.h>
#include <drivers/power.h>
#include <drivers/rtc.h>
#include <fs/registry.h>
#include <fs/vfs.h>
#include <fs/vfs_cache.h>
#include <gui/graphics.h>
#include <gui/icons.h>
#include <gui/image.h>
#include <gui/screen_mgr.h>
#include <gui/wm.h>
#include <launcher.h>
#include <gui/cursor_loader.h>
#include <gui/uistyle_loader.h>
#include <include/comdlg.h>
#include <include/clipboard.h>
#include <gui/startmenu.h>
#include <mm/heap.h>
#include <string.h>
#include <user.h>


static window_t *windows[MAX_WINDOWS];
static int32_t active_window_id = -1;
static bool start_menu_open = false;
static int g_start_menu_mode = STARTMENU_MODE_MODERN;

void wm_set_start_menu_mode(int mode) {
  g_start_menu_mode = (mode == STARTMENU_MODE_CLASSIC) ? STARTMENU_MODE_CLASSIC : STARTMENU_MODE_MODERN;
}

int wm_get_start_menu_mode(void) {
  return g_start_menu_mode;
}

// Sfondo del desktop corrente
static int current_bg_type = BG_TEAL_2000;

// Tema visivo corrente e proprietà grafiche del tema attivo
static int current_theme = THEME_CLASSIC;
static theme_info_t g_active_theme_props = {.name = "Windows 2000 Classic",
                                            .theme_id = THEME_CLASSIC,
                                            .surface_alpha = 255,
                                            .taskbar_alpha = 255,
                                            .btn_face = COLOR_BTNFACE,
                                            .btn_text = COLOR_WINDOWTEXT};

// Stato Menu Contestuale (Tasto destro)
static bool ctx_menu_open = false;
static int32_t ctx_x = 0;
static int32_t ctx_y = 0;
static bool ctx_submenu_open = false;

// Window Snap, Alt+Tab grafico, Mostra Desktop & Menu Contestuale Taskbar
static int snap_zone = 0; // 0=none, 1=left, 2=right, 3=maximize
static int alt_tab_overlay_ticks = 0;
static bool tb_ctx_open = false;
static int32_t tb_ctx_win_id = -1;
static int32_t tb_ctx_x = 0;
static int32_t tb_ctx_y = 0;
static bool show_desktop_active = false;

// Notifiche Toast & Centro Notifiche
#define WM_MAX_TOASTS 6
typedef struct {
  char title[32];
  char msg[64];
  char time_str[10];
  icon_id_t icon;
} wm_toast_t;

static wm_toast_t g_toasts[WM_MAX_TOASTS];
static int g_toast_count = 0;
static int g_toast_banner_ticks = 0;
static bool g_notif_center_open = false;

void wm_push_toast(const char *title, const char *msg, int icon) {
  if (!title || !msg) return;
  if (g_toast_count >= WM_MAX_TOASTS) {
    for (int i = 0; i < WM_MAX_TOASTS - 1; i++) {
      g_toasts[i] = g_toasts[i + 1];
    }
    g_toast_count = WM_MAX_TOASTS - 1;
  }
  wm_toast_t *t = &g_toasts[g_toast_count++];
  memset(t, 0, sizeof(wm_toast_t));
  strncpy(t->title, title, sizeof(t->title) - 1);
  strncpy(t->msg, msg, sizeof(t->msg) - 1);
  t->icon = (icon_id_t)icon;
  rtc_time_t now;
  rtc_get_time(&now);
  mini_snprintf(t->time_str, sizeof(t->time_str), "%02d:%02d", now.hour, now.minute);
  g_toast_banner_ticks = 220;
}

static void wm_snap_window(window_t *w, int zone, int32_t sw, int32_t ty) {
  if (!w || zone <= 0)
    return;
  if (!w->is_maximized) {
    w->saved_x = w->x;
    w->saved_y = w->y;
    w->saved_w = w->width;
    w->saved_h = w->height;
  }
  if (zone == 1) {
    w->x = 0;
    w->y = 0;
    w->width = sw / 2;
    w->height = ty;
    w->is_maximized = false;
  } else if (zone == 2) {
    w->x = sw / 2;
    w->y = 0;
    w->width = sw - (sw / 2);
    w->height = ty;
    w->is_maximized = false;
  } else if (zone == 3) {
    w->x = 0;
    w->y = 0;
    w->width = sw;
    w->height = ty;
    w->is_maximized = true;
  }
}

static void wm_toggle_show_desktop(void) {
  bool any_visible = false;
  for (int i = 0; i < MAX_WINDOWS; i++) {
    if (windows[i] && !windows[i]->is_minimized) {
      any_visible = true;
      break;
    }
  }
  if (any_visible) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
      if (windows[i])
        windows[i]->is_minimized = true;
    }
    active_window_id = -1;
    show_desktop_active = true;
  } else if (show_desktop_active) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
      if (windows[i]) {
        windows[i]->is_minimized = false;
        wm_focus_window(windows[i]->id);
      }
    }
    show_desktop_active = false;
  }
}

// Stato mouse precedente per rilevare transizioni di click
static bool prev_mouse_left = false;
static bool prev_mouse_right = false;
static int32_t last_mouse_x = 0;
static int32_t last_mouse_y = 0;

// Icone del desktop (dinamiche e riposizionabili)
#define MAX_DESKTOP_ICONS 24
typedef struct {
  int32_t x;
  int32_t y;
  char label[32];
  int type; // 0: Computer, 1: Cmd, 2: Notepad, 3: Calc, 4: About, 5: Folder, 6:
            // TextFile
  char content[256];
} desktop_icon_t;

static desktop_icon_t desktop_icons[MAX_DESKTOP_ICONS];
static int num_desktop_icons = 0;

// Stato selezione e trascinamento icone del desktop
static int dragging_icon_idx = -1;
static int selected_icon_idx = -1;
static int32_t icon_drag_offset_x = 0;
static int32_t icon_drag_offset_y = 0;
static int32_t icon_click_start_x = 0;
static int32_t icon_click_start_y = 0;
static bool icon_was_dragged = false;

// Parametri Griglia Desktop ordinata stile Windows 2000
#define DESKTOP_GRID_COL_W 116  // Larghezza colonna (distanza orizzontale)
#define DESKTOP_GRID_ROW_H 88   // Altezza cella riga (distanza verticale)
#define DESKTOP_GRID_START_X 18 // Margine orizzontale sinistro
#define DESKTOP_GRID_START_Y 16 // Margine verticale superiore

static int wm_get_desktop_max_rows(void) {
  uint32_t sh = gfx_get_height();
  int32_t taskbar_y = (int32_t)sh - 28;
  int32_t usable_h = taskbar_y - DESKTOP_GRID_START_Y - 8;
  int max_rows = usable_h / DESKTOP_GRID_ROW_H;
  if (max_rows < 3)
    max_rows = 3;
  return max_rows;
}

static void wm_get_icon_slot_pos(int slot, int32_t *out_x, int32_t *out_y) {
  int max_rows = wm_get_desktop_max_rows();
  int col = slot / max_rows;
  int row = slot % max_rows;

  *out_x = DESKTOP_GRID_START_X + col * DESKTOP_GRID_COL_W +
           (DESKTOP_GRID_COL_W - 32) / 2;
  *out_y = DESKTOP_GRID_START_Y + row * DESKTOP_GRID_ROW_H + 4;
}

void wm_rearrange_desktop_icons(void) {
  for (int i = 0; i < num_desktop_icons; i++) {
    wm_get_icon_slot_pos(i, &desktop_icons[i].x, &desktop_icons[i].y);
  }
}

static void wm_split_icon_label(const char *label, char line1[16],
                                char line2[16]) {
  line1[0] = '\0';
  line2[0] = '\0';
  if (!label)
    return;

  int len = (int)strlen(label);
  if (len <= 12) {
    strncpy(line1, label, 15);
    line1[15] = '\0';
    return;
  }

  // Cerca l'ultimo spazio utile entro i primi 12 caratteri
  int split_idx = -1;
  for (int i = 0; i <= 12 && i < len; i++) {
    if (label[i] == ' ')
      split_idx = i;
  }
  // Se assente, cerca trattino, underscore o punto
  if (split_idx <= 0) {
    for (int i = 0; i <= 12 && i < len; i++) {
      if (label[i] == '-' || label[i] == '_' || label[i] == '.') {
        split_idx = i;
      }
    }
  }
  // Altrimenti spezza forzatamente a 11 caratteri
  if (split_idx <= 0) {
    split_idx = 11;
  }

  int cp1 = split_idx;
  if (cp1 > 14)
    cp1 = 14;
  strncpy(line1, label, cp1);
  line1[cp1] = '\0';

  const char *rem = label + split_idx;
  if (*rem == ' ')
    rem++;
  strncpy(line2, rem, 14);
  line2[14] = '\0';
}

static bool icon_hit_test(int idx, int32_t mx, int32_t my, int32_t sw) {
  (void)sw;
  if (idx < 0 || idx >= num_desktop_icons)
    return false;
  int32_t ix = desktop_icons[idx].x;
  int32_t iy = desktop_icons[idx].y;

  int32_t min_x = ix + 16 - 52;
  int32_t max_x = ix + 16 + 52;
  int32_t min_y = iy - 2;
  int32_t max_y = iy + 68;
  return (mx >= min_x && mx <= max_x && my >= min_y && my <= max_y);
}

void wm_set_background_type(int bg_type) {
  if (bg_type >= 0 && bg_type < NUM_BACKGROUNDS) {
    current_bg_type = bg_type;
    gfx_draw_background(bg_type);
    if (current_theme == THEME_LIQUID_GLASS ||
        current_theme == THEME_MICA_LIGHT || current_theme == THEME_MICA_DARK ||
        g_active_theme_props.surface_alpha < 255) {
      gfx_precompute_blur();
    }
  }
}

int wm_get_background_type(void) { return current_bg_type; }

void wm_apply_theme_props(const theme_info_t *props) {
  if (!props)
    return;
  g_active_theme_props = *props;
  current_theme = props->theme_id;
  gfx_draw_background(current_bg_type);
  if (current_theme != THEME_CLASSIC ||
      g_active_theme_props.surface_alpha < 255) {
    gfx_precompute_blur();
  }
  wm_render();
  gfx_flip();
}

const theme_info_t *wm_get_current_theme_props(void) {
  return &g_active_theme_props;
}

void wm_set_theme(int theme) {
  current_theme = theme;
  int idx = theme_mgr_find_by_id(theme);
  if (idx >= 0) {
    const theme_info_t *th = theme_mgr_get(idx);
    if (th) {
      g_active_theme_props = *th;
    }
  } else {
    g_active_theme_props.theme_id = theme;
  }
  gfx_draw_background(current_bg_type);
  if (theme != THEME_CLASSIC || g_active_theme_props.surface_alpha < 255) {
    gfx_precompute_blur();
  }
  wm_render();
  gfx_flip();
}

int wm_get_theme(void) { return current_theme; }

void wm_add_desktop_file(const char *name, bool is_folder) {
  if (num_desktop_icons >= MAX_DESKTOP_ICONS)
    return;

  int idx = num_desktop_icons++;
  wm_get_icon_slot_pos(idx, &desktop_icons[idx].x, &desktop_icons[idx].y);

  if (is_folder) {
    desktop_icons[idx].type = 5;
    if (name && name[0] != '\0') {
      strncpy(desktop_icons[idx].label, name,
              sizeof(desktop_icons[idx].label) - 1);
    } else {
      strcpy(desktop_icons[idx].label, "Nuova cartella");
    }
    desktop_icons[idx].content[0] = '\0';
  } else {
    desktop_icons[idx].type = 6;
    if (name && name[0] != '\0') {
      strncpy(desktop_icons[idx].label, name,
              sizeof(desktop_icons[idx].label) - 1);
    } else {
      strcpy(desktop_icons[idx].label, "Nuovo testo.txt");
    }
    strcpy(desktop_icons[idx].content, "File creato dal desktop di NewOS V1.");
  }
}

void wm_init(void) {
  for (int i = 0; i < MAX_WINDOWS; i++) {
    windows[i] = NULL;
  }
  wm_push_toast("Sicurezza LSA", "Protezione C:\\NewOS attiva", ICON_INFO);
  wm_push_toast("NewOS V1", "Desktop 64-bit e servizi pronti", ICON_DESKTOP);
  active_window_id = -1;
  start_menu_open = false;
  ctx_menu_open = false;
  ctx_x = 0;
  ctx_y = 0;
  ctx_submenu_open = false;
  current_bg_type = BG_TEAL_2000;

  // Inizializza il repository icone autentiche da disco
  icons_init();

  dragging_icon_idx = -1;
  selected_icon_idx = -1;
  icon_was_dragged = false;

  num_desktop_icons = 0;
  const char *base_labels[] = {"Risorse PC",     "Web Browser", "Cestino",
                               "Prompt Comandi", "Blocco Note", "Calcolatrice",
                               "Info Sistema"};
  int base_types[] = {0, 9, 7, 1, 2, 3, 4};

  for (int i = 0; i < 7; i++) {
    int idx = num_desktop_icons++;
    wm_get_icon_slot_pos(idx, &desktop_icons[idx].x, &desktop_icons[idx].y);
    strncpy(desktop_icons[idx].label, base_labels[i],
            sizeof(desktop_icons[idx].label) - 1);
    desktop_icons[idx].type = base_types[i];
    desktop_icons[idx].content[0] = '\0';
  }

  // Se in modalità Live USB, mostra l'icona per installare NewOS V1 fisicamente
  if (user_is_live_mode()) {
    int idx = num_desktop_icons++;
    wm_get_icon_slot_pos(idx, &desktop_icons[idx].x, &desktop_icons[idx].y);
    strcpy(desktop_icons[idx].label, "Installa NewOS V1");
    desktop_icons[idx].type = 8; // 8: Installer
    desktop_icons[idx].content[0] = '\0';
  }

  // Carica dinamicamente i documenti e le cartelle da
  // C:\USER\[nomeUser]\DESKTOP
  user_profile_t *uprof = user_get_current();
  if (uprof && uprof->desktop_path[0]) {
    vfs_dirent_t u_entries[8];
    int32_t count = vfs_readdir(uprof->desktop_path, u_entries, 8);
    if (count > 0) {
      for (int e = 0; e < count; e++) {
        if (strcmp(u_entries[e].name, ".") == 0 ||
            strcmp(u_entries[e].name, "..") == 0)
          continue;
        wm_add_desktop_file(u_entries[e].name, u_entries[e].is_directory);
      }
    }
  }
}

window_t *wm_create_window(const char *title, int32_t x, int32_t y, int32_t w,
                           int32_t h, app_type_t type) {
  int slot = -1;
  for (int i = 0; i < MAX_WINDOWS; i++) {
    if (windows[i] == NULL) {
      slot = i;
      break;
    }
  }
  if (slot == -1)
    return NULL; // Raggiunto numero massimo finestre

  window_t *win = (window_t *)kmalloc(sizeof(window_t));
  if (!win)
    return NULL;

  win->id = slot + 1;
  strncpy(win->title, title, sizeof(win->title) - 1);
  win->x = x;
  win->y = y;
  win->width = w;
  win->height = h;
  win->is_active = true;
  win->is_minimized = false;
  win->is_dragging = false;
  win->drag_off_x = 0;
  win->drag_off_y = 0;

  // Inizializzazione parametri di ridimensionamento (Window Resize)
  win->is_resizing = false;
  win->resize_edge = RESIZE_NONE;
  win->resize_start_mx = 0;
  win->resize_start_my = 0;
  win->resize_orig_x = x;
  win->resize_orig_y = y;
  win->resize_orig_w = w;
  win->resize_orig_h = h;
  win->min_w = 200;
  win->min_h = 120;
  win->is_maximized = false;
  win->saved_x = x;
  win->saved_y = y;
  win->saved_w = w;
  win->saved_h = h;
  win->resizable = (type != APP_ABOUT && type != APP_SHUTDOWN);

  win->app_type = type;
  win->app_data = NULL;
  win->on_draw = NULL;
  win->on_mouse = NULL;
  win->on_right_click = NULL;
  win->on_key = NULL;

  windows[slot] = win;
  wm_focus_window(win->id);
  return win;
}

void wm_destroy_window(int32_t id) {
  for (int i = 0; i < MAX_WINDOWS; i++) {
    if (windows[i] && windows[i]->id == id) {
      if (windows[i]->app_type == APP_PLAYER) {
        app_player_on_close();
      }
      if (windows[i]->app_data) {
        kfree(windows[i]->app_data);
      }
      kfree(windows[i]);
      windows[i] = NULL;

      // Porta in primo piano un'altra finestra aperta
      active_window_id = -1;
      for (int j = MAX_WINDOWS - 1; j >= 0; j--) {
        if (windows[j] && !windows[j]->is_minimized) {
          wm_focus_window(windows[j]->id);
          break;
        }
      }
      break;
    }
  }
}

void wm_focus_window(int32_t id) {
  active_window_id = id;
  for (int i = 0; i < MAX_WINDOWS; i++) {
    if (windows[i]) {
      windows[i]->is_active = (windows[i]->id == id);
    }
  }
}

void wm_toggle_minimize(int32_t id) {
  for (int i = 0; i < MAX_WINDOWS; i++) {
    if (windows[i] && windows[i]->id == id) {
      if (windows[i]->is_minimized) {
        windows[i]->is_minimized = false;
        wm_focus_window(id);
      } else if (windows[i]->is_active) {
        windows[i]->is_minimized = true;
        windows[i]->is_active = false;
        // Trova un'altra finestra attiva
        active_window_id = -1;
        for (int j = MAX_WINDOWS - 1; j >= 0; j--) {
          if (windows[j] && !windows[j]->is_minimized && windows[j]->id != id) {
            wm_focus_window(windows[j]->id);
            break;
          }
        }
      } else {
        wm_focus_window(id);
      }
      break;
    }
  }
}

void wm_toggle_maximize(int32_t id) {
  for (int i = 0; i < MAX_WINDOWS; i++) {
    if (windows[i] && windows[i]->id == id) {
      window_t *w = windows[i];
      if (!w->resizable)
        return;

      uint32_t sw = gfx_get_width();
      uint32_t sh = gfx_get_height();
      int32_t ty = sh - 28;

      if (w->is_maximized) {
        // Ripristina coordinate e dimensioni precedenti
        w->x = w->saved_x;
        w->y = w->saved_y;
        w->width = w->saved_w;
        w->height = w->saved_h;
        w->is_maximized = false;
      } else {
        // Salva dimensioni attuali e massimizza a pieno schermo
        w->saved_x = w->x;
        w->saved_y = w->y;
        w->saved_w = w->width;
        w->saved_h = w->height;
        w->x = 0;
        w->y = 0;
        w->width = sw;
        w->height = ty;
        w->is_maximized = true;
      }
      char lbuf[48];
      mini_snprintf(lbuf, sizeof(lbuf), "[WM] Maximize id=%d max=%d\n", id, (int)w->is_maximized);
      const char *lp = lbuf;
      while (*lp) outb(0x3F8, *lp++);

      wm_focus_window(id);
      break;
    }
  }
}

static resize_edge_t wm_detect_resize_edge(window_t *w, int32_t mx, int32_t my) {
  if (!w || !w->resizable || w->is_minimized || w->is_maximized)
    return RESIZE_NONE;

  int32_t x_min = w->x - 6;
  int32_t x_max = w->x + w->width + 8;
  int32_t y_min = w->y - 6;
  int32_t y_max = w->y + w->height + 8;

  if (mx < x_min || mx > x_max || my < y_min || my > y_max)
    return RESIZE_NONE;

  int corner_sz = 24;
  int b_thick = 8;

  // Angolo inferiore destro (Resize Grip primario)
  if (mx >= w->x + w->width - corner_sz && mx <= x_max &&
      my >= w->y + w->height - corner_sz && my <= y_max) {
    return RESIZE_BOTTOM_RIGHT;
  }
  // Angolo inferiore sinistro
  if (mx >= x_min && mx <= w->x + corner_sz &&
      my >= w->y + w->height - corner_sz && my <= y_max) {
    return RESIZE_BOTTOM_LEFT;
  }
  // Fascia superiore (primi 4px perimetrali per non collidere con controlli)
  if (my >= y_min && my <= w->y + 4) {
    if (mx >= x_min && mx <= w->x + corner_sz)
      return RESIZE_TOP_LEFT;
    if (mx >= w->x + w->width - corner_sz && mx <= x_max)
      return RESIZE_TOP_RIGHT;
    return RESIZE_TOP;
  }

  // Bordo destro
  if (mx >= w->x + w->width - b_thick && mx <= x_max) {
    return RESIZE_RIGHT;
  }
  // Bordo sinistro
  if (mx >= x_min && mx <= w->x + b_thick) {
    return RESIZE_LEFT;
  }
  // Bordo inferiore
  if (my >= w->y + w->height - b_thick && my <= y_max) {
    return RESIZE_BOTTOM;
  }

  return RESIZE_NONE;
}

static resize_edge_t wm_get_hover_resize_edge(int32_t mx, int32_t my) {
  for (int i = 0; i < MAX_WINDOWS; i++) {
    if (windows[i] && windows[i]->is_resizing) {
      return windows[i]->resize_edge;
    }
  }
  if (active_window_id != -1) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
      if (windows[i] && windows[i]->id == active_window_id &&
          !windows[i]->is_minimized && !windows[i]->is_maximized &&
          windows[i]->resizable) {
        return wm_detect_resize_edge(windows[i], mx, my);
      }
    }
  }
  return RESIZE_NONE;
}

static icon_id_t wm_get_app_icon(window_t *win) {
  if (win->app_type == APP_CMD)
    return ICON_RUN;
  if (win->app_type == APP_NOTEPAD)
    return ICON_DOCUMENT;
  if (win->app_type == APP_CALC)
    return ICON_MAINTENANCE;
  if (win->app_type == APP_ABOUT)
    return ICON_INFO;
  if (win->app_type == APP_DISPLAY)
    return ICON_DESKTOP;
  if (win->app_type == APP_SOUND)
    return ICON_SOUND;
  if (win->app_type == APP_MOUSE)
    return ICON_MOUSE;
  if (win->app_type == APP_USERACCT)
    return ICON_ACCESSIBILITY;
  if (win->app_type == APP_TIMEDATE)
    return ICON_INFO;
  if (win->app_type == APP_NETCPL)
    return ICON_NET;
  if (win->app_type == APP_KEYBCPL)
    return ICON_KEYBOARD;
  if (win->app_type == APP_COMDLG) {
    if (strstr(win->title, "%") || strstr(win->title, "complete") ||
        strstr(win->title, "completato"))
      return ICON_INFO;
    return ICON_FOLDER;
  }
  if (win->app_type == APP_RULES)
    return ICON_DOCUMENT;
  if (win->app_type == APP_BROWSER)
    return ICON_NET;
  if (win->app_type == APP_PLAYER)
    return ICON_PLAY;
  if (win->app_type == APP_EXPLORER) {
    if (strstr(win->title, "Disco locale") || strstr(win->title, "C:\\"))
      return ICON_HD;
    if (strstr(win->title, "CD-ROM") || strstr(win->title, "D:\\"))
      return ICON_CD;
    if (strstr(win->title, "Floppy") || strstr(win->title, "A:\\"))
      return ICON_FLOPPY;
    if (strstr(win->title, "Cestino"))
      return ICON_CESTINO;
    if (strstr(win->title, "Pannello"))
      return ICON_CONTROL_PANEL;
    if (strstr(win->title, "Risorse"))
      return ICON_THIS_PC;
    return ICON_FOLDER;
  }
  return ICON_INFO;
}

static void draw_window_frame(window_t *win) {
  if (win->is_minimized)
    return;

  int32_t x = win->x;
  int32_t y = win->y;
  int32_t w = win->width;
  int32_t h = win->height;

  // ================================================================
  //  TEMA GUIDATO DALLE VARIABILI DEL TEMA (Fluent Glass / Mica / Modern)
  // ================================================================
  if (current_theme != THEME_CLASSIC || g_active_theme_props.drop_shadow ||
      g_active_theme_props.corner_radius > 0) {
    uint16_t r = g_active_theme_props.corner_radius > 0
                     ? g_active_theme_props.corner_radius
                     : 8;
    uint16_t th = g_active_theme_props.title_height > 0
                      ? g_active_theme_props.title_height
                      : 22;

    // 1. Ombra diffusa Drop Shadow se abilitata nel tema
    if (g_active_theme_props.drop_shadow) {
      int spread = g_active_theme_props.shadow_spread > 0
                       ? g_active_theme_props.shadow_spread
                       : 4;
      gfx_draw_drop_shadow(x, y, w, h, spread);
    }

    // 2. Pannello superficie finestra (Vetro Frosted trasparente o Mica
    // acrilico semi-opaco)
    uint32_t surf_col = g_active_theme_props.surface_base;
    uint8_t surf_alp = g_active_theme_props.surface_alpha;
    if (surf_alp == 0)
      surf_alp = 240;

    // Adattamento colore/trasparenza tra attivo e inattivo
    if (!win->is_active) {
      if (surf_alp < 200) {
        if (g_active_theme_props.inactive_title)
          surf_col = g_active_theme_props.inactive_title;
        surf_alp = (surf_alp > 30) ? (surf_alp - 20) : surf_alp;
      } else {
        surf_alp = (surf_alp > 15) ? (surf_alp - 10) : surf_alp;
      }
    } else {
      if (surf_alp < 200 && g_active_theme_props.active_title) {
        surf_col = g_active_theme_props.active_title;
      }
    }

    gfx_fill_rounded_alpha(x, y, w, h, r, surf_col, surf_alp);

    // 3. Bordo perimetrale finestra (gestito interamente dalle specifiche del tema: border_radius e border_follows_corner)
    if (g_active_theme_props.border_width > 0) {
      uint32_t b_col = win->is_active ? g_active_theme_props.border_color
                                      : g_active_theme_props.border_inactive;
      int bw = g_active_theme_props.border_width;
      uint8_t br = g_active_theme_props.border_radius;
      for (int b = 0; b < bw; b++) {
        int cr = (br > b) ? (br - b) : 0;
        gfx_draw_rounded_rect(x + b, y + b, w - 2 * b, h - 2 * b, cr, b_col);
      }
    }
    if (g_active_theme_props.specular_rim) {
      gfx_draw_specular_rim(x, y, w, h, r);
    }

    // Esecuzione eventuale script globale di rendering della finestra (gestito totalmente out-of-kernel)
    if (g_active_theme_props.render_script[0] != '\0') {
      theme_mgr_render_script(g_active_theme_props.render_script, x, y, w, h, r, win->is_active ? 2 : 0);
    }
    // Esecuzione eventuale script barra del titolo (gestito totalmente out-of-kernel)
    if (g_active_theme_props.titlebar_script[0] != '\0') {
      theme_mgr_render_script(g_active_theme_props.titlebar_script, x, y, w, th, r, win->is_active ? 2 : 0);
    }

    // 4. Barra del titolo flat: la superficie e' continua e seamless (nessun
    // cap 3D separato ne specular rim)

    // 5. Icona applicazione (16x16)
    gfx_draw_icon_16(x + 6, y + (th - 16) / 2, wm_get_app_icon(win));

    // 6. Titolo della finestra (flat e pulito, senza ombre 3D)
    uint32_t title_col = win->is_active
                             ? g_active_theme_props.active_title_text
                             : g_active_theme_props.inactive_title_text;
    if (title_col == 0) {
      title_col = (current_theme == THEME_MICA_DARK || surf_alp < 200)
                      ? 0x1A1A1A
                      : 0x1E1E1E;
    }
    gfx_draw_string(x + 26, y + (th - 16) / 2, win->title, title_col, 0, true);

    // 7. Pulsanti di controllo finestra (Chiudi, Massimizza, Minimizza)
    int32_t close_x = x + w - 21;
    int32_t close_y = y + 4;
    int32_t max_x = close_x - 18;
    int32_t max_y = close_y;
    int32_t min_x = max_x - 18;
    int32_t min_y = close_y;

    if (g_active_theme_props.modern_controls) {
      uint32_t ctrl_fg = win->is_active ? title_col : 0x707070;

      // Chiudi [x]: glifo flat Windows 11 (nessuna capsula 3D, nessun rilievo)
      int32_t cx_cl = close_x + 4;
      int32_t cy_cl = y + (th - 8) / 2;
      for (int k = 0; k < 8; k++) {
        gfx_draw_pixel(cx_cl + k, cy_cl + k, ctrl_fg);
        gfx_draw_pixel(cx_cl + 7 - k, cy_cl + k, ctrl_fg);
      }

      // Massimizza / Ripristina [□ / ❐]: glifo flat Windows 11
      if (win->resizable) {
        int32_t cx_mx = max_x + 3;
        int32_t cy_mx = y + (th - 8) / 2;
        if (win->is_maximized) {
          gfx_draw_rect(cx_mx + 2, cy_mx - 2, 7, 7, ctrl_fg);
          gfx_draw_rect(cx_mx, cy_mx, 7, 7, ctrl_fg);
        } else {
          gfx_draw_rect(cx_mx, cy_mx, 9, 8, ctrl_fg);
        }
      }

      // Minimizza [_]: glifo flat Windows 11
      int32_t cx_mn = min_x + 3;
      int32_t cy_mn = y + (th / 2) + 2;
      gfx_fill_rect(cx_mn, cy_mn, 9, 1, ctrl_fg);
    } else {
      gfx_fill_rect(close_x, close_y, 16, 14, COLOR_BTNFACE);
      gfx_draw_bevel_rect(close_x, close_y, 16, 14, false);
      gfx_draw_string(close_x + 5, close_y + 1, "x", 0x000000, 0, true);

      if (win->resizable) {
        gfx_fill_rect(max_x, max_y, 16, 14, COLOR_BTNFACE);
        gfx_draw_bevel_rect(max_x, max_y, 16, 14, false);
        if (win->is_maximized) {
          gfx_draw_rect(max_x + 5, max_y + 2, 6, 6, 0x000000);
          gfx_fill_rect(max_x + 3, max_y + 4, 6, 6, COLOR_BTNFACE);
          gfx_draw_rect(max_x + 3, max_y + 4, 6, 6, 0x000000);
          gfx_fill_rect(max_x + 4, max_y + 5, 4, 1, 0x000000);
        } else {
          gfx_draw_rect(max_x + 3, max_y + 3, 9, 8, 0x000000);
          gfx_fill_rect(max_x + 4, max_y + 4, 7, 2, 0x000000);
        }
      }

      gfx_fill_rect(min_x, min_y, 16, 14, COLOR_BTNFACE);
      gfx_draw_bevel_rect(min_x, min_y, 16, 14, false);
      gfx_draw_string(min_x + 4, min_y - 2, "_", 0x000000, 0, true);
    }

    // 8. Contenuto applicazione
    if (win->on_draw)
      win->on_draw(win);

    // 9. Resize Grip nell'angolo inferiore destro (stile moderno/fluent)
    if (win->resizable && !win->is_maximized) {
      int32_t gx = x + w - 14;
      int32_t gy = y + h - 14;
      uint32_t c_hi = 0xD0D0D0;
      uint32_t c_sh = 0x505050;
      gfx_draw_pixel(gx + 10, gy + 10, c_hi);
      gfx_draw_pixel(gx + 9,  gy + 9,  c_sh);
      gfx_draw_pixel(gx + 10, gy + 6,  c_hi);
      gfx_draw_pixel(gx + 9,  gy + 5,  c_sh);
      gfx_draw_pixel(gx + 6,  gy + 10, c_hi);
      gfx_draw_pixel(gx + 5,  gy + 9,  c_sh);
      gfx_draw_pixel(gx + 10, gy + 2,  c_hi);
      gfx_draw_pixel(gx + 9,  gy + 1,  c_sh);
      gfx_draw_pixel(gx + 6,  gy + 6,  c_hi);
      gfx_draw_pixel(gx + 5,  gy + 5,  c_sh);
      gfx_draw_pixel(gx + 2,  gy + 10, c_hi);
      gfx_draw_pixel(gx + 1,  gy + 9,  c_sh);
    }
    return;
  }

  // ================================================================
  //  TEMA CLASSIC (Windows 2000 — invariato)
  // ================================================================

  // Cornice esterna in rilievo 3D
  gfx_fill_rect(x, y, w, h, COLOR_BTNFACE);
  gfx_draw_bevel_rect(x, y, w, h, false);

  // Barra del titolo con gradiente
  uint32_t c_left =
      win->is_active ? COLOR_ACTIVECAPTION_LEFT : COLOR_INACTIVECAPTION_LEFT;
  uint32_t c_right =
      win->is_active ? COLOR_ACTIVECAPTION_RIGHT : COLOR_INACTIVECAPTION_RIGHT;
  uint32_t c_text =
      win->is_active ? COLOR_CAPTIONTEXT : COLOR_INACTIVECAPTIONTEXT;

  gfx_draw_gradient_h(x + 3, y + 3, w - 6, 18, c_left, c_right);

  // Icona 16x16 specifica per l'applicazione
  icon_id_t w_icon = ICON_INFO;
  if (win->app_type == APP_CMD)
    w_icon = ICON_RUN;
  else if (win->app_type == APP_NOTEPAD)
    w_icon = ICON_DOCUMENT;
  else if (win->app_type == APP_CALC)
    w_icon = ICON_MAINTENANCE;
  else if (win->app_type == APP_ABOUT)
    w_icon = ICON_INFO;
  else if (win->app_type == APP_DISPLAY)
    w_icon = ICON_DESKTOP;
  else if (win->app_type == APP_SOUND)
    w_icon = ICON_SOUND;
  else if (win->app_type == APP_MOUSE)
    w_icon = ICON_MOUSE;
  else if (win->app_type == APP_USERACCT)
    w_icon = ICON_ACCESSIBILITY;
  else if (win->app_type == APP_TIMEDATE)
    w_icon = ICON_INFO;
  else if (win->app_type == APP_NETCPL)
    w_icon = ICON_NET;
  else if (win->app_type == APP_KEYBCPL)
    w_icon = ICON_KEYBOARD;
  else if (win->app_type == APP_COMDLG)
    w_icon = ICON_FOLDER;
  else if (win->app_type == APP_RULES)
    w_icon = ICON_DOCUMENT;
  else if (win->app_type == APP_BROWSER)
    w_icon = ICON_NET;
  else if (win->app_type == APP_PLAYER)
    w_icon = ICON_PLAY;
  else if (win->app_type == APP_EXPLORER) {
    w_icon = ICON_THIS_PC;
    if (strstr(win->title, "Disco locale") || strstr(win->title, "C:\\"))
      w_icon = ICON_HD;
    else if (strstr(win->title, "CD-ROM") || strstr(win->title, "D:\\"))
      w_icon = ICON_CD;
    else if (strstr(win->title, "Floppy") || strstr(win->title, "A:\\"))
      w_icon = ICON_FLOPPY;
    else if (strstr(win->title, "Cestino"))
      w_icon = ICON_CESTINO;
    else if (strstr(win->title, "Pannello"))
      w_icon = ICON_CONTROL_PANEL;
    else if (strstr(win->title, "Risorse"))
      w_icon = ICON_THIS_PC;
    else
      w_icon = ICON_FOLDER;
  }

  gfx_draw_icon_16(x + 5, y + 4, w_icon);

  // Titolo finestra (spostato a destra di 22px per l'icona)
  gfx_draw_string(x + 25, y + 4, win->title, c_text, 0, true);

  // Pulsante Chiudi [X]
  int32_t close_x = x + w - 21;
  int32_t close_y = y + 4;
  gfx_fill_rect(close_x, close_y, 16, 14, COLOR_BTNFACE);
  gfx_draw_bevel_rect(close_x, close_y, 16, 14, false);
  gfx_draw_string(close_x + 5, close_y + 1, "x", 0x000000, 0, true);

  // Pulsante Massimizza / Ripristina [□ / ❐]
  int32_t max_x = close_x - 18;
  int32_t max_y = close_y;
  if (win->resizable) {
    gfx_fill_rect(max_x, max_y, 16, 14, COLOR_BTNFACE);
    gfx_draw_bevel_rect(max_x, max_y, 16, 14, false);
    if (win->is_maximized) {
      gfx_draw_rect(max_x + 5, max_y + 2, 6, 6, 0x000000);
      gfx_fill_rect(max_x + 3, max_y + 4, 6, 6, COLOR_BTNFACE);
      gfx_draw_rect(max_x + 3, max_y + 4, 6, 6, 0x000000);
      gfx_fill_rect(max_x + 4, max_y + 5, 4, 1, 0x000000);
    } else {
      gfx_draw_rect(max_x + 3, max_y + 3, 9, 8, 0x000000);
      gfx_fill_rect(max_x + 4, max_y + 4, 7, 2, 0x000000);
    }
  }

  // Pulsante Riduci a icona [_]
  int32_t min_x = max_x - 18;
  int32_t min_y = close_y;
  gfx_fill_rect(min_x, min_y, 16, 14, COLOR_BTNFACE);
  gfx_draw_bevel_rect(min_x, min_y, 16, 14, false);
  gfx_draw_string(min_x + 4, min_y - 2, "_", 0x000000, 0, true);

  // Disegna contenuto specifico dell'applicazione
  if (win->on_draw) {
    win->on_draw(win);
  }

  // Resize Grip classico Windows 2000 nell'angolo inferiore destro
  if (win->resizable && !win->is_maximized) {
    int32_t gx = x + w - 14;
    int32_t gy = y + h - 14;
    gfx_draw_pixel(gx + 10, gy + 10, 0xFFFFFF);
    gfx_draw_pixel(gx + 9,  gy + 9,  0x808080);
    gfx_draw_pixel(gx + 10, gy + 6,  0xFFFFFF);
    gfx_draw_pixel(gx + 9,  gy + 5,  0x808080);
    gfx_draw_pixel(gx + 6,  gy + 10, 0xFFFFFF);
    gfx_draw_pixel(gx + 5,  gy + 9,  0x808080);
    gfx_draw_pixel(gx + 10, gy + 2,  0xFFFFFF);
    gfx_draw_pixel(gx + 9,  gy + 1,  0x808080);
    gfx_draw_pixel(gx + 6,  gy + 6,  0xFFFFFF);
    gfx_draw_pixel(gx + 5,  gy + 5,  0x808080);
    gfx_draw_pixel(gx + 2,  gy + 10, 0xFFFFFF);
    gfx_draw_pixel(gx + 1,  gy + 9,  0x808080);
  }
}

static void draw_start_menu(int32_t sw, int32_t sh) {
  int32_t mw = 200;
  int32_t mh = 336;
  int32_t mx = 2;
  int32_t my = sh - 28 - mh;

  // Sfondo menu Start con cornice 3D
  gfx_fill_rect(mx, my, mw, mh, COLOR_BTNFACE);
  gfx_draw_bevel_rect(mx, my, mw, mh, false);

  // Banner laterale blu verticale stile Windows 2000
  gfx_fill_rect(mx + 2, my + 2, 22, mh - 4, COLOR_MENU_SIDEBAR);
  // Scritta NewOS V1 sul banner verticale (8 linee da 16px = 128px, termina a
  // my + mh - 8)
  gfx_draw_string(mx + 6, my + mh - 136, "N\ne\nw\nO\nS\n \nV\n1", 0xFFFFFF, 0,
                  true);

  // Voci di menu con icone
  const char *items[] = {"Web Browser",           "Prompt Comandi",
                         "Blocco Note",           "Calcolatrice",
                         "Lettore Multimediale",  "Regole di Sistema",
                         "Pannello di controllo", "Editor del Registro",
                         "Info su NewOS V1",      "Arresta il sistema..."};
  icon_id_t item_icons[] = {ICON_NET,           ICON_RUN,         ICON_DOCUMENT,
                            ICON_MAINTENANCE,   ICON_PLAY,        ICON_DOCUMENT,
                            ICON_CONTROL_PANEL, ICON_MAINTENANCE, ICON_INFO,
                            ICON_ENERGY};

  for (int i = 0; i < 10; i++) {
    int32_t iy = my + 8 + i * 32;
    if (i == 9) {
      // Linea divisoria prima di Arresta il sistema
      gfx_draw_bevel_rect(mx + 28, iy - 4, mw - 34, 2, true);
    }
    gfx_draw_icon_16(mx + 30, iy + 4, item_icons[i]);
    gfx_draw_string(mx + 52, iy + 6, items[i], COLOR_WINDOWTEXT, 0, true);
  }
}

static void draw_context_menu(int32_t mx, int32_t my) {
  if (!ctx_menu_open)
    return;

  int32_t mw = 176;
  int32_t mh = 166;
  bool is_modern = (current_theme != THEME_CLASSIC || g_active_theme_props.modern_controls);
  uint16_t rad = g_active_theme_props.panel_radius > 0 ? g_active_theme_props.panel_radius : 8;
  uint32_t bg_col = g_active_theme_props.surface_base ? g_active_theme_props.surface_base : 0xF8FAFC;
  uint32_t b_col = g_active_theme_props.control_border_color ? g_active_theme_props.control_border_color : 0xC8D4E2;
  uint32_t txt_col = is_modern ? (g_active_theme_props.window_text ? g_active_theme_props.window_text : 0x1A1A1A) : COLOR_WINDOWTEXT;
  uint32_t acc_col = is_modern ? (g_active_theme_props.accent_color ? g_active_theme_props.accent_color : 0x0078D4) : 0x0A246A;

  if (is_modern) {
    gfx_draw_drop_shadow(ctx_x, ctx_y, mw, mh, 6);
    gfx_fill_rounded_alpha(ctx_x, ctx_y, mw, mh, rad, bg_col, 238);
    gfx_draw_rounded_rect(ctx_x, ctx_y, mw, mh, rad, b_col);
  } else {
    gfx_fill_rect(ctx_x, ctx_y, mw, mh, COLOR_BTNFACE);
    gfx_draw_bevel_rect(ctx_x, ctx_y, mw, mh, false);
  }

  bool hov_nuovo =
      (mx >= ctx_x && mx <= ctx_x + mw && my >= ctx_y + 3 && my < ctx_y + 25) ||
      ctx_submenu_open;
  bool hov_copia = (!hov_nuovo && mx >= ctx_x && mx <= ctx_x + mw &&
                    my >= ctx_y + 28 && my < ctx_y + 48);
  bool hov_sposta = (!hov_nuovo && mx >= ctx_x && mx <= ctx_x + mw &&
                     my >= ctx_y + 48 && my < ctx_y + 68);
  bool hov_incolla = (!hov_nuovo && mx >= ctx_x && mx <= ctx_x + mw &&
                      my >= ctx_y + 68 && my < ctx_y + 88);
  bool hov_disponi = (!hov_nuovo && mx >= ctx_x && mx <= ctx_x + mw &&
                      my >= ctx_y + 92 && my < ctx_y + 112);
  bool hov_personalizza = (!hov_nuovo && mx >= ctx_x && mx <= ctx_x + mw &&
                           my >= ctx_y + 114 && my < ctx_y + 136);
  bool hov_prop = (!hov_nuovo && mx >= ctx_x && mx <= ctx_x + mw &&
                   my >= ctx_y + 136 && my <= ctx_y + 162);

  // Voce 0: Nuovo ▶
  if (hov_nuovo) {
    if (is_modern) gfx_fill_rounded_rect(ctx_x + 4, ctx_y + 3, mw - 8, 22, 4, acc_col);
    else gfx_fill_rect(ctx_x + 3, ctx_y + 3, mw - 6, 22, acc_col);
    gfx_draw_string(ctx_x + 12, ctx_y + 6, "Nuovo", 0xFFFFFF, 0, true);
    gfx_draw_string(ctx_x + mw - 18, ctx_y + 6, ">", 0xFFFFFF, 0, true);
  } else {
    gfx_draw_string(ctx_x + 12, ctx_y + 6, "Nuovo", txt_col, 0, true);
    gfx_draw_string(ctx_x + mw - 18, ctx_y + 6, ">", 0x808080, 0, true);
  }

  // Separatore
  if (is_modern) gfx_fill_alpha(ctx_x + 8, ctx_y + 26, mw - 16, 1, b_col, 180);
  else gfx_draw_bevel_rect(ctx_x + 4, ctx_y + 26, mw - 8, 2, true);

  // Blocco collegato: Copia / Sposta / Incolla
  if (hov_copia) {
    if (is_modern) gfx_fill_rounded_rect(ctx_x + 4, ctx_y + 28, mw - 8, 20, 4, acc_col);
    else gfx_fill_rect(ctx_x + 3, ctx_y + 28, mw - 6, 20, acc_col);
    gfx_draw_string(ctx_x + 12, ctx_y + 30, "Copia (Ctrl+C)", 0xFFFFFF, 0, true);
  } else {
    gfx_draw_string(ctx_x + 12, ctx_y + 30, "Copia (Ctrl+C)", txt_col, 0, true);
  }

  if (hov_sposta) {
    if (is_modern) gfx_fill_rounded_rect(ctx_x + 4, ctx_y + 48, mw - 8, 20, 4, acc_col);
    else gfx_fill_rect(ctx_x + 3, ctx_y + 48, mw - 6, 20, acc_col);
    gfx_draw_string(ctx_x + 12, ctx_y + 50, "Sposta...", 0xFFFFFF, 0, true);
  } else {
    gfx_draw_string(ctx_x + 12, ctx_y + 50, "Sposta...", txt_col, 0, true);
  }

  if (hov_incolla) {
    if (is_modern) gfx_fill_rounded_rect(ctx_x + 4, ctx_y + 68, mw - 8, 20, 4, acc_col);
    else gfx_fill_rect(ctx_x + 3, ctx_y + 68, mw - 6, 20, acc_col);
    gfx_draw_string(ctx_x + 12, ctx_y + 70, "Incolla (Ctrl+V)", 0xFFFFFF, 0, true);
  } else {
    gfx_draw_string(ctx_x + 12, ctx_y + 70, "Incolla (Ctrl+V)", txt_col, 0, true);
  }

  // Separatore
  if (is_modern) gfx_fill_alpha(ctx_x + 8, ctx_y + 90, mw - 16, 1, b_col, 180);
  else gfx_draw_bevel_rect(ctx_x + 4, ctx_y + 90, mw - 8, 2, true);

  // Voce: Disponi icone
  if (hov_disponi) {
    if (is_modern) gfx_fill_rounded_rect(ctx_x + 4, ctx_y + 92, mw - 8, 20, 4, acc_col);
    else gfx_fill_rect(ctx_x + 3, ctx_y + 92, mw - 6, 20, acc_col);
    gfx_draw_string(ctx_x + 12, ctx_y + 94, "Disponi icone", 0xFFFFFF, 0, true);
  } else {
    gfx_draw_string(ctx_x + 12, ctx_y + 94, "Disponi icone", txt_col, 0, true);
  }

  // Voce: Personalizza...
  if (hov_personalizza) {
    if (is_modern) gfx_fill_rounded_rect(ctx_x + 4, ctx_y + 114, mw - 8, 21, 4, acc_col);
    else gfx_fill_rect(ctx_x + 3, ctx_y + 114, mw - 6, 21, acc_col);
    gfx_draw_icon_16(ctx_x + 8, ctx_y + 116, ICON_DESKTOP);
    gfx_draw_string(ctx_x + 30, ctx_y + 117, "Personalizza...", 0xFFFFFF, 0, true);
  } else {
    gfx_draw_icon_16(ctx_x + 8, ctx_y + 116, ICON_DESKTOP);
    gfx_draw_string(ctx_x + 30, ctx_y + 117, "Personalizza...", txt_col, 0, true);
  }

  // Voce: Proprietà
  if (hov_prop) {
    if (is_modern) gfx_fill_rounded_rect(ctx_x + 4, ctx_y + 136, mw - 8, 22, 4, acc_col);
    else gfx_fill_rect(ctx_x + 3, ctx_y + 136, mw - 6, 22, acc_col);
    gfx_draw_icon_16(ctx_x + 8, ctx_y + 138, ICON_INFO);
    gfx_draw_string(ctx_x + 30, ctx_y + 139, "Proprieta", 0xFFFFFF, 0, true);
  } else {
    gfx_draw_icon_16(ctx_x + 8, ctx_y + 138, ICON_INFO);
    gfx_draw_string(ctx_x + 30, ctx_y + 139, "Proprieta", txt_col, 0, true);
  }

  // Sottomenu "Nuovo" se aperto
  if (ctx_submenu_open) {
    int32_t sm_x = ctx_x + mw - 4;
    int32_t sm_y = ctx_y;
    int32_t sm_w = 165;
    int32_t sm_h = 58;

    uint32_t sw = gfx_get_width();
    if (sm_x + sm_w > (int32_t)sw) {
      sm_x = ctx_x - sm_w + 4;
    }

    if (is_modern) {
      gfx_draw_drop_shadow(sm_x, sm_y, sm_w, sm_h, 6);
      gfx_fill_rounded_alpha(sm_x, sm_y, sm_w, sm_h, rad, bg_col, 238);
      gfx_draw_rounded_rect(sm_x, sm_y, sm_w, sm_h, rad, b_col);
    } else {
      gfx_fill_rect(sm_x, sm_y, sm_w, sm_h, COLOR_BTNFACE);
      gfx_draw_bevel_rect(sm_x, sm_y, sm_w, sm_h, false);
    }

    bool hov_sub_folder =
        (mx >= sm_x && mx <= sm_x + sm_w && my >= sm_y + 3 && my < sm_y + 28);
    bool hov_sub_doc =
        (mx >= sm_x && mx <= sm_x + sm_w && my >= sm_y + 28 && my <= sm_y + 55);

    // Sub 0: Cartella
    if (hov_sub_folder) {
      if (is_modern) gfx_fill_rounded_rect(sm_x + 4, sm_y + 3, sm_w - 8, 24, 4, acc_col);
      else gfx_fill_rect(sm_x + 3, sm_y + 3, sm_w - 6, 24, acc_col);
      gfx_draw_icon_16(sm_x + 8, sm_y + 6, ICON_FOLDER);
      gfx_draw_string(sm_x + 30, sm_y + 8, "Cartella", 0xFFFFFF, 0, true);
    } else {
      gfx_draw_icon_16(sm_x + 8, sm_y + 6, ICON_FOLDER);
      gfx_draw_string(sm_x + 30, sm_y + 8, "Cartella", txt_col, 0, true);
    }

    // Sub 1: Documento di testo
    if (hov_sub_doc) {
      if (is_modern) gfx_fill_rounded_rect(sm_x + 4, sm_y + 29, sm_w - 8, 25, 4, acc_col);
      else gfx_fill_rect(sm_x + 3, sm_y + 29, sm_w - 6, 25, acc_col);
      gfx_draw_icon_16(sm_x + 8, sm_y + 32, ICON_DOCUMENT);
      gfx_draw_string(sm_x + 30, sm_y + 34, "Doc. di testo", 0xFFFFFF, 0, true);
    } else {
      gfx_draw_icon_16(sm_x + 8, sm_y + 32, ICON_DOCUMENT);
      gfx_draw_string(sm_x + 30, sm_y + 34, "Doc. di testo", txt_col, 0, true);
    }
  }
}

void wm_render(void) {
  uint32_t sw = gfx_get_width();
  uint32_t sh = gfx_get_height();

  // 1. Sfondo del Desktop personalizzabile
  gfx_draw_background(current_bg_type);

  // 2. Icone del Desktop (disposte a griglia ordinata senza sovrapposizioni)
  for (int i = 0; i < num_desktop_icons; i++) {
    int32_t ix = desktop_icons[i].x;
    int32_t iy = desktop_icons[i].y;

    if (desktop_icons[i].type == 0)
      gfx_draw_icon_32(ix, iy, ICON_THIS_PC);
    else if (desktop_icons[i].type == 1)
      gfx_draw_icon_32(ix, iy, ICON_RUN);
    else if (desktop_icons[i].type == 2)
      gfx_draw_icon_32(ix, iy, ICON_DOCUMENT);
    else if (desktop_icons[i].type == 3)
      gfx_draw_icon_calc(ix, iy);
    else if (desktop_icons[i].type == 4)
      gfx_draw_icon_32(ix, iy, ICON_INFO);
    else if (desktop_icons[i].type == 5)
      gfx_draw_icon_32(ix, iy, ICON_FOLDER);
    else if (desktop_icons[i].type == 6) {
      const char *dot = strstr(desktop_icons[i].label, ".");
      if (dot &&
          (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".bmp") == 0 ||
           strcasecmp(dot, ".png") == 0)) {
        gfx_draw_icon_32(ix, iy, ICON_IMAGE);
      } else {
        gfx_draw_icon_32(ix, iy, ICON_DOCUMENT);
      }
    } else if (desktop_icons[i].type == 7)
      gfx_draw_icon_32(ix, iy, ICON_CESTINO);
    else if (desktop_icons[i].type == 8)
      gfx_draw_icon_32(ix, iy, ICON_MAINTENANCE);
    else if (desktop_icons[i].type == 9)
      gfx_draw_icon_32(ix, iy, ICON_NET);

    // Formatta etichetta su 1 o 2 righe centrate all'interno della colonna
    char l1[16], l2[16];
    wm_split_icon_label(desktop_icons[i].label, l1, l2);

    int32_t cx = ix + 16;
    int32_t lw1 = (int32_t)strlen(l1) * 8;
    int32_t lx1 = cx - (lw1 / 2);
    if (lx1 < 2)
      lx1 = 2;
    if (lx1 + lw1 > (int32_t)sw - 2)
      lx1 = (int32_t)sw - 2 - lw1;

    bool has_line2 = (l2[0] != '\0');
    int32_t lw2 = has_line2 ? ((int32_t)strlen(l2) * 8) : 0;
    int32_t lx2 = has_line2 ? (cx - (lw2 / 2)) : 0;
    if (lx2 < 2)
      lx2 = 2;
    if (has_line2 && lx2 + lw2 > (int32_t)sw - 2)
      lx2 = (int32_t)sw - 2 - lw2;

    bool is_selected = (i == selected_icon_idx || i == dragging_icon_idx);

    if (!has_line2) {
      int32_t text_y = iy + 36;
      if (is_selected) {
        gfx_fill_rect(lx1 - 3, text_y - 1, lw1 + 6, 16, 0x0A246A);
        gfx_draw_string(lx1, text_y, l1, 0xFFFFFF, 0, true);
      } else {
        gfx_draw_string(lx1 + 1, text_y + 1, l1, 0x000000, 0, true);
        gfx_draw_string(lx1, text_y, l1, 0xFFFFFF, 0, true);
      }
    } else {
      int32_t y1 = iy + 35;
      int32_t y2 = iy + 49;
      if (is_selected) {
        gfx_fill_rect(lx1 - 3, y1 - 1, lw1 + 6, 14, 0x0A246A);
        gfx_draw_string(lx1, y1, l1, 0xFFFFFF, 0, true);
        gfx_fill_rect(lx2 - 3, y2 - 1, lw2 + 6, 14, 0x0A246A);
        gfx_draw_string(lx2, y2, l2, 0xFFFFFF, 0, true);
      } else {
        gfx_draw_string(lx1 + 1, y1 + 1, l1, 0x000000, 0, true);
        gfx_draw_string(lx1, y1, l1, 0xFFFFFF, 0, true);
        gfx_draw_string(lx2 + 1, y2 + 1, l2, 0x000000, 0, true);
        gfx_draw_string(lx2, y2, l2, 0xFFFFFF, 0, true);
      }
    }
  }

  // 3. Disegna le finestre in ordine z-order (la finestra attiva per ultima)
  for (int i = 0; i < MAX_WINDOWS; i++) {
    if (windows[i] && !windows[i]->is_active) {
      draw_window_frame(windows[i]);
    }
  }
  for (int i = 0; i < MAX_WINDOWS; i++) {
    if (windows[i] && windows[i]->is_active) {
      draw_window_frame(windows[i]);
    }
  }

  // 4. Barra delle applicazioni (Taskbar) inferiore
  int32_t ty = sh - 28;

  if (current_theme == THEME_CLASSIC && !g_active_theme_props.modern_controls &&
      g_active_theme_props.taskbar_alpha >= 255) {
    gfx_fill_rect(0, ty, sw, 28, COLOR_BTNFACE);
    for (uint32_t x = 0; x < sw; x++) {
      gfx_draw_pixel(x, ty, COLOR_BTNHIGHLIGHT);
    }
  } else {
    uint32_t tb_col = g_active_theme_props.taskbar_color;
    uint8_t tb_alp = g_active_theme_props.taskbar_alpha > 0
                         ? g_active_theme_props.taskbar_alpha
                         : 230;
    gfx_fill_alpha(0, ty, sw, 28, tb_col, tb_alp);
    if (g_active_theme_props.specular_rim) {
      gfx_fill_alpha(0, ty, sw, 1, LG_SPECULAR_COLOR, 60);
    } else {
      gfx_fill_alpha(0, ty, sw, 1,
                     g_active_theme_props.taskbar_border
                         ? g_active_theme_props.taskbar_border
                         : 0x000000,
                     30);
    }
  }

  // Esecuzione eventuale script barra delle applicazioni (gestito out-of-kernel)
  if (g_active_theme_props.taskbar_script[0] != '\0') {
    theme_mgr_render_script(g_active_theme_props.taskbar_script, 0, ty, sw, 28, 0, 0);
  }

  // Pulsante "Start"
  bool start_pressed = start_menu_open;
  if (current_theme == THEME_CLASSIC && !g_active_theme_props.modern_controls) {
    gfx_fill_rect(2, ty + 2, 60, 24, COLOR_BTNFACE);
    gfx_draw_bevel_rect(2, ty + 2, 60, 24, start_pressed);
    gfx_draw_windows_logo(6, ty + 6);
    gfx_draw_string(24, ty + 6, "Start", COLOR_WINDOWTEXT, 0, true);
  } else if (g_active_theme_props.surface_alpha < 200) {
    uint32_t st_tint;
    if (g_active_theme_props.taskbar_color > 0x808080) {
      st_tint = start_pressed ? (g_active_theme_props.selection_color
                                     ? g_active_theme_props.selection_color
                                     : 0xC8E0F8)
                              : (g_active_theme_props.start_btn_color
                                     ? g_active_theme_props.start_btn_color
                                     : 0xFFFFFF);
    } else {
      st_tint = start_pressed ? (g_active_theme_props.accent_color
                                     ? g_active_theme_props.accent_color
                                     : 0x5090FF)
                              : (g_active_theme_props.start_btn_color
                                     ? g_active_theme_props.start_btn_color
                                     : 0x3060CC);
    }
    uint8_t st_alpha = start_pressed ? 180 : 150;
    uint16_t c_rad = g_active_theme_props.start_btn_radius > 0
                         ? g_active_theme_props.start_btn_radius
                         : (g_active_theme_props.control_radius > 0
                                ? g_active_theme_props.control_radius
                                : 8);
    gfx_fill_rounded_alpha(2, ty + 2, 68, 22, c_rad, st_tint, st_alpha);
    if (g_active_theme_props.specular_rim)
      gfx_draw_specular_rim(2, ty + 2, 68, 22, c_rad);
    gfx_draw_windows_logo(6, ty + 5);
    uint32_t st_text_col =
        g_active_theme_props.start_btn_text
            ? g_active_theme_props.start_btn_text
            : (g_active_theme_props.taskbar_color > 0x808080 ? 0x1E1E1E
                                                             : LG_TEXT_PRIMARY);
    gfx_draw_string(24, ty + 6, "Start", st_text_col, 0, true);
  } else {
    uint16_t c_rad = g_active_theme_props.start_btn_radius > 0
                         ? g_active_theme_props.start_btn_radius
                         : (g_active_theme_props.control_radius > 0
                                ? g_active_theme_props.control_radius
                                : 6);
    uint32_t st_bg = start_pressed ? (g_active_theme_props.accent_color
                                          ? g_active_theme_props.accent_color
                                          : 0xCCE8FF)
                                   : (g_active_theme_props.start_btn_color
                                          ? g_active_theme_props.start_btn_color
                                          : 0xE8E8EE);
    uint32_t st_fg = start_pressed ? (g_active_theme_props.accent_text
                                          ? g_active_theme_props.accent_text
                                          : 0x000000)
                                   : (g_active_theme_props.start_btn_text
                                          ? g_active_theme_props.start_btn_text
                                          : 0x1E1E1E);
    gfx_fill_rounded_rect(2, ty + 2, 64, 24, c_rad, st_bg);
    gfx_draw_windows_logo(6, ty + 6);
    gfx_draw_string(24, ty + 6, "Start", st_fg, 0, true);
  }

  // Pulsanti delle finestre aperte sulla Taskbar
  int32_t btn_x = 76;
  for (int i = 0; i < MAX_WINDOWS; i++) {
    if (windows[i]) {
      int32_t bw = 130;
      bool pressed = (windows[i]->is_active && !windows[i]->is_minimized);

      if (current_theme == THEME_CLASSIC &&
          !g_active_theme_props.modern_controls) {
        gfx_fill_rect(btn_x, ty + 2, bw, 24, COLOR_BTNFACE);
        gfx_draw_bevel_rect(btn_x, ty + 2, bw, 24, pressed);
      } else if (g_active_theme_props.surface_alpha < 200) {
        uint32_t b_tint;
        uint8_t b_alpha;
        if (g_active_theme_props.taskbar_color > 0x808080) {
          b_tint = pressed ? (g_active_theme_props.selection_color
                                  ? g_active_theme_props.selection_color
                                  : 0xC8E0F8)
                           : 0xFFFFFF;
          b_alpha = pressed ? 180 : 110;
        } else {
          b_tint = pressed ? (g_active_theme_props.accent_color
                                  ? g_active_theme_props.accent_color
                                  : 0x4080FF)
                           : 0x202030;
          b_alpha = pressed ? 170 : 120;
        }
        uint16_t tb_rad = g_active_theme_props.taskbar_btn_radius > 0
                              ? g_active_theme_props.taskbar_btn_radius
                              : (g_active_theme_props.control_radius > 0
                                     ? g_active_theme_props.control_radius
                                     : 6);
        gfx_fill_rounded_alpha(btn_x, ty + 3, bw, 20, tb_rad, b_tint, b_alpha);
        if (pressed && g_active_theme_props.specular_rim)
          gfx_draw_specular_rim(btn_x, ty + 3, bw, 20, tb_rad);
      } else {
        uint32_t b_bg =
            pressed ? (current_theme == THEME_MICA_DARK ? 0x2E2E38 : 0xFFFFFF)
                    : (current_theme == THEME_MICA_DARK ? 0x202026 : 0xE6E6ED);
        uint16_t tb_rad = g_active_theme_props.taskbar_btn_radius > 0
                              ? g_active_theme_props.taskbar_btn_radius
                              : (g_active_theme_props.control_radius > 0
                                     ? g_active_theme_props.control_radius
                                     : 5);
        gfx_fill_rounded_rect(btn_x, ty + 2, bw, 24, tb_rad, b_bg);
        if (pressed) {
          uint32_t ind_col = g_active_theme_props.active_tab_indicator
                                 ? g_active_theme_props.active_tab_indicator
                                 : 0x0078D4;
          gfx_fill_rect(btn_x + (bw - 24) / 2, ty + 23, 24, 2, ind_col);
        }
      }

      char short_title[16];
      strncpy(short_title, windows[i]->title, 13);
      short_title[13] = '\0';
      uint32_t t_col;
      if (current_theme == THEME_CLASSIC &&
          !g_active_theme_props.modern_controls) {
        t_col = COLOR_WINDOWTEXT;
      } else if (g_active_theme_props.taskbar_color > 0x808080) {
        t_col = g_active_theme_props.start_btn_text
                    ? g_active_theme_props.start_btn_text
                    : 0x1E1E1E;
      } else if (g_active_theme_props.surface_alpha < 200 ||
                 current_theme == THEME_MICA_DARK) {
        t_col = LG_TEXT_PRIMARY;
      } else {
        t_col = 0x1E1E1E;
      }
      int32_t t_y =
          (g_active_theme_props.surface_alpha < 200) ? ty + 5 : ty + 6;
      gfx_draw_string(btn_x + 6, t_y, short_title, t_col, 0, true);

      // Tooltip anteprima titolo completo al passaggio del mouse sul pulsante Taskbar
      mouse_state_t m_hov;
      mouse_get_state(&m_hov);
      if (!start_menu_open && !tb_ctx_open &&
          m_hov.x >= btn_x && m_hov.x < btn_x + bw &&
          m_hov.y >= ty && m_hov.y < (int32_t)sh) {
        char tip[64];
        if (windows[i]->is_minimized) {
          mini_snprintf(tip, sizeof(tip), "%s [Minimizzata]", windows[i]->title);
        } else {
          mini_snprintf(tip, sizeof(tip), "%s", windows[i]->title);
        }
        int32_t tip_w = (int32_t)strlen(tip) * 8 + 16;
        int32_t tip_x = btn_x;
        if (tip_x + tip_w > (int32_t)sw - 4) tip_x = (int32_t)sw - tip_w - 4;
        int32_t tip_y = ty - 24;
        gfx_draw_drop_shadow(tip_x, tip_y, tip_w, 20, 4);
        gfx_fill_rounded_alpha(tip_x, tip_y, tip_w, 20, 4, 0x1E293B, 235);
        gfx_draw_rounded_rect(tip_x, tip_y, tip_w, 20, 4, 0x60A5FA);
        gfx_draw_string(tip_x + 8, tip_y + 3, tip, 0xF8FAFC, 0, true);
      }

      btn_x += bw + 4;
      if (btn_x + bw > (int32_t)sw - 100)
        break;
    }
  }

  // System Tray con icona batteria/alimentazione, orologio e pulsante Mostra Desktop
  int32_t tray_w = 110;
  int32_t tray_x = sw - tray_w - 14;
  int32_t tray_y = ty + 3;
  if (current_theme == THEME_CLASSIC && !g_active_theme_props.modern_controls) {
    gfx_fill_rect(tray_x, tray_y, tray_w, 22, COLOR_BTNFACE);
    gfx_draw_bevel_rect(tray_x, tray_y, tray_w, 22, true);
  } else if (g_active_theme_props.surface_alpha < 200) {
    uint32_t pill_col = g_active_theme_props.tray_pill_color
                            ? g_active_theme_props.tray_pill_color
                            : 0x101020;
    uint16_t tr_rad = g_active_theme_props.tray_radius > 0
                          ? g_active_theme_props.tray_radius
                          : (g_active_theme_props.control_radius > 0
                                 ? g_active_theme_props.control_radius
                                 : 6);
    gfx_fill_rounded_alpha(tray_x, tray_y, tray_w, 22, tr_rad, pill_col, 150);
    if (g_active_theme_props.specular_rim)
      gfx_draw_specular_rim(tray_x, tray_y, tray_w, 22, tr_rad);
  } else {
    uint32_t pill_col =
        g_active_theme_props.tray_pill_color
            ? g_active_theme_props.tray_pill_color
            : (current_theme == THEME_MICA_DARK ? 0x24242A : 0xEAEAEF);
    uint16_t tr_rad = g_active_theme_props.tray_radius > 0
                          ? g_active_theme_props.tray_radius
                          : (g_active_theme_props.control_radius > 0
                                 ? g_active_theme_props.control_radius
                                 : 5);
    gfx_fill_rounded_rect(tray_x, tray_y, tray_w, 22, tr_rad, pill_col);
  }

  // Icona alimentazione/batteria nella tray
  gfx_draw_icon_16(tray_x + 6, tray_y + 3, ICON_ENERGY);

  // Orologio CMOS in tempo reale
  rtc_time_t now;
  rtc_get_time(&now);
  char time_str[16];
  mini_snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", now.hour,
                now.minute, now.second);
  uint32_t clk_col;
  if (g_active_theme_props.taskbar_color > 0x808080) {
    clk_col = g_active_theme_props.start_btn_text
                  ? g_active_theme_props.start_btn_text
                  : 0x1E1E1E;
  } else if (current_theme == THEME_LIQUID_GLASS ||
             current_theme == THEME_MICA_DARK ||
             g_active_theme_props.surface_alpha < 200) {
    clk_col = LG_TEXT_PRIMARY;
  } else {
    clk_col = (uint32_t)COLOR_WINDOWTEXT;
  }
  gfx_draw_string(tray_x + 28, tray_y + 4, time_str, clk_col, 0, true);

  // Pulsante Centro Notifiche (a sinistra della pillola tray)
  int32_t bell_x = tray_x - 28;
  // Indicatore Layout Tastiera (US / IT / UK / DE / FR)
  int32_t kbd_x = bell_x - 32;
  // Pulsante Rimozione Sicura Hardware / Flush Cache USB & Dischi
  int32_t usb_x = kbd_x - 28;

  if (current_theme == THEME_CLASSIC && !g_active_theme_props.modern_controls) {
    gfx_fill_rect(usb_x, tray_y, 24, 22, COLOR_BTNFACE);
    gfx_draw_bevel_rect(usb_x, tray_y, 24, 22, false);
    gfx_fill_rect(kbd_x, tray_y, 28, 22, COLOR_BTNFACE);
    gfx_draw_bevel_rect(kbd_x, tray_y, 28, 22, false);
    gfx_fill_rect(bell_x, tray_y, 24, 22, COLOR_BTNFACE);
    gfx_draw_bevel_rect(bell_x, tray_y, 24, 22, g_notif_center_open);
  } else {
    gfx_fill_rounded_alpha(usb_x, tray_y, 24, 22, 4, 0xFFFFFF, 95);
    gfx_draw_rounded_rect(usb_x, tray_y, 24, 22, 4, 0x94A3B8);
    gfx_fill_rounded_alpha(kbd_x, tray_y, 28, 22, 4, 0xFFFFFF, 95);
    gfx_draw_rounded_rect(kbd_x, tray_y, 28, 22, 4, 0x94A3B8);
    gfx_fill_rounded_alpha(bell_x, tray_y, 24, 22, 4, g_notif_center_open ? 0x3B82F6 : 0xFFFFFF,
                           g_notif_center_open ? 180 : 95);
    gfx_draw_rounded_rect(bell_x, tray_y, 24, 22, 4, 0x94A3B8);
  }
  gfx_draw_icon_16(usb_x + 4, tray_y + 3, ICON_HD);
  gfx_draw_string(kbd_x + 6, tray_y + 7, keyboard_get_layout_code(), clk_col, 0, true);
  gfx_draw_icon_16(bell_x + 4, tray_y + 3, ICON_INFO);
  if (g_toast_count > 0) {
    gfx_fill_rounded_rect(bell_x + 15, tray_y + 1, 8, 8, 4, 0xEF4444);
  }

  // Pulsante Mostra Desktop all'estrema destra della Taskbar
  if (current_theme == THEME_CLASSIC && !g_active_theme_props.modern_controls) {
    gfx_fill_rect(sw - 10, tray_y, 8, 22, COLOR_BTNFACE);
    gfx_draw_bevel_rect(sw - 10, tray_y, 8, 22, false);
  } else {
    gfx_fill_rounded_alpha(sw - 10, tray_y, 8, 22, 3, 0xFFFFFF, 85);
    gfx_draw_rounded_rect(sw - 10, tray_y, 8, 22, 3, 0xC8D8EC);
  }

  // Anteprima Window Snap durante il trascinamento
  if (snap_zone > 0) {
    int32_t sx = (snap_zone == 2) ? (sw / 2) : 0;
    int32_t sy = 0;
    int32_t sw_z = (snap_zone == 3) ? sw : ((snap_zone == 2) ? (sw - sw / 2) : (sw / 2));
    int32_t sh_z = ty;
    gfx_fill_rounded_alpha(sx + 4, sy + 4, sw_z - 8, sh_z - 8, 8, 0x3A82F6, 70);
    gfx_draw_rounded_rect(sx + 4, sy + 4, sw_z - 8, sh_z - 8, 8, 0x60A5FA);
  }

  // 5. Menu Start (se aperto)
  if (start_menu_open) {
    if (g_start_menu_mode == STARTMENU_MODE_MODERN &&
        !(current_theme == THEME_CLASSIC && !g_active_theme_props.modern_controls)) {
      startmenu_draw(sw, sh);
    } else {
      draw_start_menu(sw, sh);
    }
  }

  // Menu Contestuale pulsante Taskbar
  if (tb_ctx_open) {
    int32_t mw = 172, mh = 82;
    gfx_draw_drop_shadow(tb_ctx_x, tb_ctx_y, mw, mh, 6);
    gfx_fill_rounded_alpha(tb_ctx_x, tb_ctx_y, mw, mh, 6, 0xF8FAFC, 240);
    gfx_draw_rounded_rect(tb_ctx_x, tb_ctx_y, mw, mh, 6, 0xB8C4D4);
    window_t *tw = NULL;
    for (int i = 0; i < MAX_WINDOWS; i++) {
      if (windows[i] && windows[i]->id == tb_ctx_win_id) { tw = windows[i]; break; }
    }
    gfx_draw_string(tb_ctx_x + 10, tb_ctx_y + 6, (tw && tw->is_minimized) ? "Ripristina" : "Minimizza", 0x1E293B, 0, true);
    gfx_draw_string(tb_ctx_x + 10, tb_ctx_y + 25, "Massimizza", 0x1E293B, 0, true);
    gfx_draw_string(tb_ctx_x + 10, tb_ctx_y + 44, "Affianca a sinistra", 0x1E293B, 0, true);
    gfx_draw_string(tb_ctx_x + 10, tb_ctx_y + 63, "Chiudi", 0xB91C1C, 0, true);
  }

  // Overlay Alt+Tab Grafico
  if (alt_tab_overlay_ticks > 0) {
    alt_tab_overlay_ticks--;
    int wcount = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
      if (windows[i]) wcount++;
    }
    if (wcount > 0) {
      int32_t card_w = 132, card_h = 84;
      int32_t ow = wcount * (card_w + 8) + 16;
      if (ow > (int32_t)sw - 32) ow = (int32_t)sw - 32;
      int32_t oh = card_h + 32;
      int32_t ox = ((int32_t)sw - ow) / 2;
      int32_t oy = ((int32_t)sh - oh) / 2;
      gfx_draw_drop_shadow(ox, oy, ow, oh, 10);
      gfx_fill_rounded_alpha(ox, oy, ow, oh, 12, 0x182234, 225);
      gfx_draw_rounded_rect(ox, oy, ow, oh, 12, 0x60A5FA);
      gfx_draw_string(ox + 12, oy + 8, "Finestre aperte (Alt+Tab)", 0xE2E8F0, 0, true);
      int32_t cx_c = ox + 12;
      for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i]) continue;
        if (cx_c + card_w > ox + ow - 8) break;
        bool is_cur = (windows[i]->id == active_window_id);
        gfx_fill_rounded_alpha(cx_c, oy + 26, card_w, card_h - 8, 8,
                               is_cur ? 0x2563EB : 0x273549, is_cur ? 210 : 170);
        gfx_draw_rounded_rect(cx_c, oy + 26, card_w, card_h - 8, 8,
                              is_cur ? 0x93C5FD : 0x475569);
        icon_id_t w_ico = ICON_DESKTOP;
        if (windows[i]->app_type == APP_EXPLORER) w_ico = ICON_THIS_PC;
        else if (windows[i]->app_type == APP_CMD) w_ico = ICON_RUN;
        else if (windows[i]->app_type == APP_NOTEPAD) w_ico = ICON_DOCUMENT;
        else if (windows[i]->app_type == APP_CALC) w_ico = ICON_MAINTENANCE;
        else if (windows[i]->app_type == APP_PLAYER) w_ico = ICON_PLAY;
        else if (windows[i]->app_type == APP_BROWSER) w_ico = ICON_NET;
        else if (windows[i]->app_type == APP_TASKMGR) w_ico = ICON_CONTROL_PANEL;
        gfx_draw_icon_32(cx_c + card_w / 2 - 16, oy + 34, w_ico);
        char tshort[16];
        strncpy(tshort, windows[i]->title, 14);
        tshort[14] = '\0';
        gfx_draw_string(cx_c + 8, oy + 74, tshort, 0xFFFFFF, 0, true);
        cx_c += card_w + 8;
      }
    }
  }

  // Popup Toast temporaneo (angolo in basso a destra)
  if (g_toast_banner_ticks > 0 && g_toast_count > 0 && !g_notif_center_open) {
    g_toast_banner_ticks--;
    wm_toast_t *lt = &g_toasts[g_toast_count - 1];
    int32_t tw = 310, th = 62;
    int32_t tx_b = (int32_t)sw - tw - 12;
    int32_t ty_b = ty - th - 10;
    gfx_draw_drop_shadow(tx_b, ty_b, tw, th, 8);
    gfx_fill_rounded_alpha(tx_b, ty_b, tw, th, 8, 0x1E293B, 232);
    gfx_draw_rounded_rect(tx_b, ty_b, tw, th, 8, 0x60A5FA);
    gfx_draw_icon_32(tx_b + 10, ty_b + 15, lt->icon);
    gfx_draw_string(tx_b + 50, ty_b + 12, lt->title, 0xF8FAFC, 0, true);
    gfx_draw_string(tx_b + 50, ty_b + 32, lt->msg, 0xCBD5E1, 0, true);
  }

  // Centro Notifiche (aperto dal campanellino nella System Tray)
  if (g_notif_center_open) {
    int32_t nw = 304, nh = 264;
    int32_t nx = (int32_t)sw - nw - 10;
    int32_t ny = ty - nh - 8;
    gfx_draw_drop_shadow(nx, ny, nw, nh, 10);
    gfx_fill_rounded_alpha(nx, ny, nw, nh, 10, 0xF8FAFC, 240);
    gfx_draw_rounded_rect(nx, ny, nw, nh, 10, 0x94A3B8);
    gfx_draw_string(nx + 12, ny + 10, "Centro Notifiche", 0x0F172A, 0, true);
    gfx_fill_rounded_rect(nx + nw - 68, ny + 7, 56, 20, 4, 0xE2E8F0);
    gfx_draw_string(nx + nw - 60, ny + 10, "Svuota", 0x334155, 0, true);
    gfx_fill_rect(nx + 10, ny + 32, nw - 20, 1, 0xCBD5E1);
    if (g_toast_count == 0) {
      gfx_draw_string(nx + 64, ny + 120, "Nessuna nuova notifica", 0x64748B, 0, true);
    } else {
      for (int i = g_toast_count - 1; i >= 0; i--) {
        int slot = (g_toast_count - 1) - i;
        if (slot >= 4) break;
        int32_t cy_n = ny + 40 + slot * 54;
        gfx_fill_rounded_rect(nx + 10, cy_n, nw - 20, 48, 6, 0xFFFFFF);
        gfx_draw_rounded_rect(nx + 10, cy_n, nw - 20, 48, 6, 0xE2E8F0);
        gfx_draw_icon_16(nx + 18, cy_n + 8, g_toasts[i].icon);
        gfx_draw_string(nx + 40, cy_n + 7, g_toasts[i].title, 0x0F172A, 0, true);
        gfx_draw_string(nx + nw - 54, cy_n + 7, g_toasts[i].time_str, 0x64748B, 0, true);
        gfx_draw_string(nx + 40, cy_n + 26, g_toasts[i].msg, 0x475569, 0, true);
      }
    }
  }

  // 5.5 Overlay UI Style modulare (es. Copenhagen Glass UI da NewOS\Resources\UIStyle)
  if (uistyle_get_active() == UISTYLE_COPENHAGEN) {
    uistyle_draw_desktop(gfx_get_width(), gfx_get_height());
  }

  // 6. Menu Contestuale Tasto Destro (se aperto)
  if (ctx_menu_open) {
    draw_context_menu(last_mouse_x, last_mouse_y);
  }

  // 7. Cursore mouse personalizzato da libreria CURSORMGR.DLL (o fallback legacy)
  resize_edge_t hover_edge = wm_get_hover_resize_edge(last_mouse_x, last_mouse_y);
  cursor_system_state_t cur_state = CURSOR_STATE_NORMAL;
  if (hover_edge == RESIZE_LEFT || hover_edge == RESIZE_RIGHT) {
    cur_state = CURSOR_STATE_RESIZE_EW;
  } else if (hover_edge == RESIZE_TOP || hover_edge == RESIZE_BOTTOM) {
    cur_state = CURSOR_STATE_RESIZE_NS;
  } else if (hover_edge == RESIZE_TOP_LEFT || hover_edge == RESIZE_BOTTOM_RIGHT) {
    cur_state = CURSOR_STATE_RESIZE_NWSE;
  } else if (hover_edge == RESIZE_TOP_RIGHT || hover_edge == RESIZE_BOTTOM_LEFT) {
    cur_state = CURSOR_STATE_RESIZE_NESW;
  }

  if (!cursor_mgr_draw(last_mouse_x, last_mouse_y, cur_state)) {
    if (hover_edge != RESIZE_NONE) {
      gfx_draw_resize_cursor(last_mouse_x, last_mouse_y, hover_edge);
    } else {
      gfx_draw_cursor(last_mouse_x, last_mouse_y);
    }
  }

  // 8. Blit ultra-veloce a schermo senza flickering
  gfx_flip();
}

void wm_update(mouse_state_t *mouse, char key) {
  app_player_update_tick();
  static uint32_t s_cache_flush_ticks = 0;
  if (++s_cache_flush_ticks >= 3000) {
    s_cache_flush_ticks = 0;
    vfs_cache_flush();
  }
  last_mouse_x = mouse->x;
  last_mouse_y = mouse->y;

  uint32_t sw = gfx_get_width();
  uint32_t sh = gfx_get_height();
  (void)sw;
  int32_t ty = sh - 28;

  bool mouse_clicked = mouse->left_button && !prev_mouse_left;
  bool mouse_released = !mouse->left_button && prev_mouse_left;
  bool right_clicked = mouse->right_button && !prev_mouse_right;

  if (uistyle_get_active() == UISTYLE_COPENHAGEN) {
    if (uistyle_handle_desktop_mouse(mouse->x, mouse->y, mouse_clicked) && mouse_clicked) {
      prev_mouse_left = mouse->left_button;
      prev_mouse_right = mouse->right_button;
      return;
    }
  }

  // Trascinamento o ridimensionamento con tasto sinistro (finestre o icone del desktop)
  if (mouse->left_button) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
      window_t *w = windows[i];
      if (!w)
        continue;

      if (w->is_resizing) {
        int32_t dx = mouse->x - w->resize_start_mx;
        int32_t dy = mouse->y - w->resize_start_my;
        int32_t min_w = (w->min_w > 0) ? w->min_w : 200;
        int32_t min_h = (w->min_h > 0) ? w->min_h : 120;
        int32_t max_w = (int32_t)sw;
        int32_t max_h = ty;

        int32_t new_x = w->resize_orig_x;
        int32_t new_y = w->resize_orig_y;
        int32_t new_w = w->resize_orig_w;
        int32_t new_h = w->resize_orig_h;

        // Ridimensionamento orizzontale
        if (w->resize_edge == RESIZE_RIGHT ||
            w->resize_edge == RESIZE_BOTTOM_RIGHT ||
            w->resize_edge == RESIZE_TOP_RIGHT) {
          new_w = w->resize_orig_w + dx;
          if (new_w < min_w)
            new_w = min_w;
          if (new_x + new_w > max_w)
            new_w = max_w - new_x;
        } else if (w->resize_edge == RESIZE_LEFT ||
                   w->resize_edge == RESIZE_BOTTOM_LEFT ||
                   w->resize_edge == RESIZE_TOP_LEFT) {
          new_w = w->resize_orig_w - dx;
          if (new_w < min_w) {
            new_w = min_w;
            new_x = w->resize_orig_x + (w->resize_orig_w - min_w);
          } else {
            new_x = w->resize_orig_x + dx;
            if (new_x < 0) {
              new_w += new_x;
              new_x = 0;
            }
          }
        }

        // Ridimensionamento verticale
        if (w->resize_edge == RESIZE_BOTTOM ||
            w->resize_edge == RESIZE_BOTTOM_RIGHT ||
            w->resize_edge == RESIZE_BOTTOM_LEFT) {
          new_h = w->resize_orig_h + dy;
          if (new_h < min_h)
            new_h = min_h;
          if (new_y + new_h > max_h)
            new_h = max_h - new_y;
        } else if (w->resize_edge == RESIZE_TOP ||
                   w->resize_edge == RESIZE_TOP_RIGHT ||
                   w->resize_edge == RESIZE_TOP_LEFT) {
          new_h = w->resize_orig_h - dy;
          if (new_h < min_h) {
            new_h = min_h;
            new_y = w->resize_orig_y + (w->resize_orig_h - min_h);
          } else {
            new_y = w->resize_orig_y + dy;
            if (new_y < 0) {
              new_h += new_y;
              new_y = 0;
            }
          }
        }

        w->x = new_x;
        w->y = new_y;
        w->width = new_w;
        w->height = new_h;
        w->is_maximized = false;
      } else if (w->is_dragging) {
        w->x = mouse->x - w->drag_off_x;
        w->y = mouse->y - w->drag_off_y;
        if (w->y < 0)
          w->y = 0;
        if (w->y > ty - 20)
          w->y = ty - 20;
        w->is_maximized = false;
        if (mouse->x <= 8)
          snap_zone = 1;
        else if (mouse->x >= (int32_t)sw - 9)
          snap_zone = 2;
        else if (mouse->y <= 6)
          snap_zone = 3;
        else
          snap_zone = 0;
      }
    }

    // Trascinamento icone del desktop
    if (dragging_icon_idx >= 0 && dragging_icon_idx < num_desktop_icons) {
      int32_t dx = mouse->x - icon_click_start_x;
      int32_t dy = mouse->y - icon_click_start_y;
      if (dx < 0)
        dx = -dx;
      if (dy < 0)
        dy = -dy;
      if (dx > 3 || dy > 3) {
        icon_was_dragged = true;
      }

      if (icon_was_dragged) {
        int32_t new_x = mouse->x - icon_drag_offset_x;
        int32_t new_y = mouse->y - icon_drag_offset_y;

        if (new_x < 4)
          new_x = 4;
        if (new_x > (int32_t)sw - 60)
          new_x = sw - 60;
        if (new_y < 4)
          new_y = 4;
        if (new_y > ty - 56)
          new_y = ty - 56;

        desktop_icons[dragging_icon_idx].x = new_x;
        desktop_icons[dragging_icon_idx].y = new_y;
      }
    }
  }

  if (mouse_released) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
      if (windows[i]) {
        if (windows[i]->is_dragging && snap_zone > 0) {
          wm_snap_window(windows[i], snap_zone, (int32_t)sw, ty);
        }
        windows[i]->is_dragging = false;
        windows[i]->is_resizing = false;
        windows[i]->resize_edge = RESIZE_NONE;
      }
    }
    snap_zone = 0;
    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
      window_t *w = windows[i];
      if (w && w->id == active_window_id && !w->is_minimized && w->on_mouse) {
        w->on_mouse(w, mouse->x - w->x, mouse->y - w->y, false);
        break;
      }
    }

    // Rilascio del trascinamento icona o esecuzione al clic
    if (dragging_icon_idx >= 0 && dragging_icon_idx < num_desktop_icons) {
      if (!icon_was_dragged) {
        int idx = dragging_icon_idx;
        if (desktop_icons[idx].type == 0) {
          launcher_open("thispc", NULL);
        } else if (desktop_icons[idx].type == 1) {
          launcher_open("cmd", NULL);
        } else if (desktop_icons[idx].type == 2) {
          launcher_open("notepad", NULL);
        } else if (desktop_icons[idx].type == 3) {
          launcher_open("calc", NULL);
        } else if (desktop_icons[idx].type == 4) {
          launcher_open("about", NULL);
        } else if (desktop_icons[idx].type == 5) {
          launcher_open_typed(LAUNCH_TYPE_FOLDER, desktop_icons[idx].label, NULL, NULL);
        } else if (desktop_icons[idx].type == 6) {
          user_profile_t *up = user_get_current();
          const char *uname = (up && up->username[0]) ? up->username : "Morgan";
          char full_path[128];
          mini_snprintf(full_path, sizeof(full_path), "C:/USER/%s/DESKTOP/%s",
                        uname, desktop_icons[idx].label);
          launcher_open_typed(LAUNCH_TYPE_FILE, full_path, NULL, desktop_icons[idx].content);
        } else if (desktop_icons[idx].type == 7) {
          launcher_open("recycle", NULL);
        } else if (desktop_icons[idx].type == 8) {
          launcher_open("installer", NULL);
        } else if (desktop_icons[idx].type == 9) {
          launcher_open("browser", NULL);
        }
      }
      dragging_icon_idx = -1;
      icon_was_dragged = false;
    }
  }

  // GESTIONE TASTO DESTRO DEL MOUSE (Apre il Menu Contestuale del Desktop)
  if (right_clicked) {
    bool on_win = false;
    for (int step = 0; step < 2; step++) {
      for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        window_t *w = windows[i];
        if (!w || w->is_minimized)
          continue;
        if (step == 0 && !w->is_active)
          continue;
        if (step == 1 && w->is_active)
          continue;

        if (mouse->x >= w->x && mouse->x <= w->x + w->width &&
            mouse->y >= w->y && mouse->y <= w->y + w->height) {
          on_win = true;
          wm_focus_window(w->id);
          if (w->on_right_click) {
            w->on_right_click(w, mouse->x - w->x, mouse->y - w->y, true);
          }
          step = 2;
          break;
        }
      }
    }

    if (!on_win && mouse->y < ty) {
      ctx_menu_open = true;
      ctx_x = mouse->x;
      ctx_y = mouse->y;
      if (ctx_x + 176 > (int32_t)sw)
        ctx_x = sw - 180;
      if (ctx_y + 168 > ty)
        ctx_y = ty - 170;
      ctx_submenu_open = false;
      start_menu_open = false;

      prev_mouse_left = mouse->left_button;
      prev_mouse_right = mouse->right_button;
      return;
    } else if (!on_win && mouse->y >= ty) {
      // Controlla se il clic destro e' su un pulsante finestra nella Taskbar
      int32_t btn_x = 68;
      for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i]) {
          int32_t bw = 130;
          if (mouse->x >= btn_x && mouse->x <= btn_x + bw) {
            tb_ctx_open = true;
            tb_ctx_win_id = windows[i]->id;
            tb_ctx_x = btn_x;
            tb_ctx_y = ty - 84;
            if (tb_ctx_x + 152 > (int32_t)sw)
              tb_ctx_x = (int32_t)sw - 156;
            ctx_menu_open = false;
            start_menu_open = false;
            prev_mouse_left = mouse->left_button;
            prev_mouse_right = mouse->right_button;
            return;
          }
          btn_x += bw + 4;
        }
      }
      // Altrimenti apre Gestione attivita' (Task Manager)
      app_taskmgr_launch();
      prev_mouse_left = mouse->left_button;
      prev_mouse_right = mouse->right_button;
      return;
    }
  }

  // Aggiorna stato apertura sottomenu contestuale all'hover del mouse
  if (ctx_menu_open) {
    if (mouse->x >= ctx_x && mouse->x <= ctx_x + 160 && mouse->y >= ctx_y + 3 &&
        mouse->y < ctx_y + 25) {
      ctx_submenu_open = true;
    } else if (ctx_submenu_open) {
      int32_t sm_x = ctx_x + 156;
      int32_t sm_y = ctx_y;
      int32_t sm_w = 165;
      int32_t sm_h = 58;
      if (sm_x + sm_w > (int32_t)sw)
        sm_x = ctx_x - sm_w + 4;

      if (!(mouse->x >= sm_x && mouse->x <= sm_x + sm_w && mouse->y >= sm_y &&
            mouse->y <= sm_y + sm_h) &&
          !(mouse->x >= ctx_x && mouse->x <= ctx_x + 160 && mouse->y >= ctx_y &&
            mouse->y <= ctx_y + 25)) {
        ctx_submenu_open = false;
      }
    }
  }

  if (mouse_clicked) {
    // Interazione con Menu Contestuale Taskbar se aperto
    if (tb_ctx_open) {
      int32_t mw = 172, mh = 82;
      if (mouse->x >= tb_ctx_x && mouse->x <= tb_ctx_x + mw &&
          mouse->y >= tb_ctx_y && mouse->y <= tb_ctx_y + mh) {
        int32_t ry = mouse->y - tb_ctx_y;
        window_t *tw = NULL;
        for (int i = 0; i < MAX_WINDOWS; i++) {
          if (windows[i] && windows[i]->id == tb_ctx_win_id) { tw = windows[i]; break; }
        }
        if (tw) {
          if (ry < 22) {
            wm_toggle_minimize(tw->id);
          } else if (ry < 41) {
            wm_snap_window(tw, 3, (int32_t)sw, ty);
            tw->is_minimized = false;
            wm_focus_window(tw->id);
          } else if (ry < 60) {
            wm_snap_window(tw, 1, (int32_t)sw, ty);
            tw->is_minimized = false;
            wm_focus_window(tw->id);
          } else {
            wm_destroy_window(tw->id);
          }
        }
        tb_ctx_open = false;
        prev_mouse_left = mouse->left_button;
        prev_mouse_right = mouse->right_button;
        return;
      }
      tb_ctx_open = false;
    }

    // Interazione con Menu Contestuale se aperto
    if (ctx_menu_open) {
      int32_t sm_x = ctx_x + 156;
      int32_t sm_y = ctx_y;
      int32_t sm_w = 165;
      int32_t sm_h = 58;
      if (sm_x + sm_w > (int32_t)sw)
        sm_x = ctx_x - sm_w + 4;

      // Click su voce del sottomenu "Nuovo"
      if (ctx_submenu_open && mouse->x >= sm_x && mouse->x <= sm_x + sm_w &&
          mouse->y >= sm_y && mouse->y <= sm_y + sm_h) {
        if (mouse->y < sm_y + 28) {
          wm_add_desktop_file("Nuova cartella", true);
        } else {
          wm_add_desktop_file("Nuovo testo.txt", false);
        }
        ctx_menu_open = false;
        ctx_submenu_open = false;
        prev_mouse_left = mouse->left_button;
        prev_mouse_right = mouse->right_button;
        return;
      }

      // Click su voce del menu contestuale principale (altezza 166px)
      if (mouse->x >= ctx_x && mouse->x <= ctx_x + 176 && mouse->y >= ctx_y &&
          mouse->y <= ctx_y + 166) {
        if (mouse->y >= ctx_y + 3 && mouse->y < ctx_y + 25) {
          ctx_submenu_open = !ctx_submenu_open;
          prev_mouse_left = mouse->left_button;
          prev_mouse_right = mouse->right_button;
          return;
        } else if (mouse->y >= ctx_y + 28 && mouse->y < ctx_y + 48) {
          app_comdlg_copy_move_launch(COMDLG_OP_COPY, "C:\\User\\Morgan\\Desktop\\BENVENUTO.TXT", "C:\\User\\Morgan\\Desktop");
          ctx_menu_open = false;
          ctx_submenu_open = false;
          prev_mouse_left = mouse->left_button;
          prev_mouse_right = mouse->right_button;
          return;
        } else if (mouse->y >= ctx_y + 48 && mouse->y < ctx_y + 68) {
          app_comdlg_copy_move_launch(COMDLG_OP_MOVE, "C:\\User\\Morgan\\Desktop\\BENVENUTO.TXT", "C:\\User\\Morgan\\Desktop");
          ctx_menu_open = false;
          ctx_submenu_open = false;
          prev_mouse_left = mouse->left_button;
          prev_mouse_right = mouse->right_button;
          return;
        } else if (mouse->y >= ctx_y + 68 && mouse->y < ctx_y + 88) {
          app_comdlg_copy_move_launch(COMDLG_OP_PASTE, NULL, "C:\\User\\Morgan\\Desktop");
          ctx_menu_open = false;
          ctx_submenu_open = false;
          prev_mouse_left = mouse->left_button;
          prev_mouse_right = mouse->right_button;
          return;
        } else if (mouse->y >= ctx_y + 92 && mouse->y < ctx_y + 112) {
          wm_rearrange_desktop_icons();
          ctx_menu_open = false;
          ctx_submenu_open = false;
          prev_mouse_left = mouse->left_button;
          prev_mouse_right = mouse->right_button;
          return;
        } else if (mouse->y >= ctx_y + 114 && mouse->y < ctx_y + 136) {
          app_display_props_launch();
          ctx_menu_open = false;
          ctx_submenu_open = false;
          prev_mouse_left = mouse->left_button;
          prev_mouse_right = mouse->right_button;
          return;
        } else if (mouse->y >= ctx_y + 136 && mouse->y <= ctx_y + 162) {
          app_about_launch();
          ctx_menu_open = false;
          ctx_submenu_open = false;
          prev_mouse_left = mouse->left_button;
          prev_mouse_right = mouse->right_button;
          return;
        }
      } else {
        ctx_menu_open = false;
        ctx_submenu_open = false;
      }
    }

    // Clic sul pulsante Start
    if (mouse->x >= 2 && mouse->x <= 62 && mouse->y >= ty + 2 &&
        mouse->y <= ty + 26) {
      start_menu_open = !start_menu_open;
      prev_mouse_left = mouse->left_button;
      prev_mouse_right = mouse->right_button;
      return;
    }

    // Clic dentro il menu Start se aperto
    if (start_menu_open) {
      if (g_start_menu_mode == STARTMENU_MODE_MODERN &&
          !(current_theme == THEME_CLASSIC && !g_active_theme_props.modern_controls)) {
        int sm_res = startmenu_handle_mouse(sw, sh, mouse->x, mouse->y, true);
        if (sm_res == 3) {
          start_menu_open = false;
          logoutscreen_result_t act = screen_mgr_run_logoutscreen();
          if (act == LOGOUT_ACTION_SHUTDOWN) power_shutdown();
          else if (act == LOGOUT_ACTION_REBOOT) power_reboot();
          else if (act == LOGOUT_ACTION_SUSPEND) power_enter_screen_standby();
          else if (act == LOGOUT_ACTION_DISCONNECT) {
            for (int wi = 0; wi < MAX_WINDOWS; wi++) {
              if (windows[wi]) wm_destroy_window(windows[wi]->id);
            }
            loginscreen_result_t lres = screen_mgr_run_loginscreen();
            if (lres == LOGIN_SHUTDOWN) power_shutdown();
            else if (lres == LOGIN_REBOOT) power_reboot();
            registry_logon_load_readonly_preferences();
          }
          gfx_draw_background(current_bg_type);
          wm_render();
          gfx_flip();
          prev_mouse_left = mouse->left_button;
          prev_mouse_right = mouse->right_button;
          return;
        } else if (sm_res == 2) {
          start_menu_open = false;
          prev_mouse_left = mouse->left_button;
          prev_mouse_right = mouse->right_button;
          return;
        } else if (sm_res == 1) {
          prev_mouse_left = mouse->left_button;
          prev_mouse_right = mouse->right_button;
          return;
        } else {
          start_menu_open = false;
        }
      } else {
      int32_t mw = 200, mh = 336, mx = 2, my = sh - 28 - mh;
      if (mouse->x >= mx && mouse->x <= mx + mw && mouse->y >= my &&
          mouse->y <= my + mh) {
        int item = (mouse->y - my - 8) / 32;
        if (item == 0)
          launcher_open("browser", NULL);
        else if (item == 1)
          launcher_open("cmd", NULL);
        else if (item == 2)
          launcher_open("notepad", NULL);
        else if (item == 3)
          launcher_open("calc", NULL);
        else if (item == 4)
          launcher_open("player", NULL);
        else if (item == 5)
          launcher_open("rules", NULL);
        else if (item == 6)
          launcher_open("control", NULL);
        else if (item == 7)
          launcher_open("regedit", NULL);
        else if (item == 8)
          launcher_open("about", NULL);
        else if (item == 9) {
          start_menu_open = false;
          logoutscreen_result_t res = screen_mgr_run_logoutscreen();
          if (res == LOGOUT_ACTION_SHUTDOWN) {
            power_shutdown();
          } else if (res == LOGOUT_ACTION_REBOOT) {
            power_reboot();
          } else if (res == LOGOUT_ACTION_SUSPEND) {
            power_enter_screen_standby();
            gfx_draw_background(current_bg_type);
            wm_render();
            gfx_flip();
          } else if (res == LOGOUT_ACTION_DISCONNECT) {
            for (int wi = 0; wi < MAX_WINDOWS; wi++) {
              if (windows[wi]) wm_destroy_window(windows[wi]->id);
            }
            loginscreen_result_t lres = screen_mgr_run_loginscreen();
            if (lres == LOGIN_SHUTDOWN) {
              power_shutdown();
            } else if (lres == LOGIN_REBOOT) {
              power_reboot();
            }
            registry_logon_load_readonly_preferences();
            gfx_draw_background(current_bg_type);
            wm_render();
            gfx_flip();
          } else {
            gfx_draw_background(current_bg_type);
            wm_render();
            gfx_flip();
          }
          prev_mouse_left = mouse->left_button;
          prev_mouse_right = mouse->right_button;
          return;
        }
        start_menu_open = false;
        prev_mouse_left = mouse->left_button;
        prev_mouse_right = mouse->right_button;
        return;
      } else {
        start_menu_open = false;
      }
      }
    }

    // Clic sul Centro Notifiche aperto (pulsante Svuota o area interna)
    if (g_notif_center_open) {
      int32_t nw = 304, nh = 264;
      int32_t nx = (int32_t)sw - nw - 10;
      int32_t ny = ty - nh - 8;
      if (mouse->x >= nx + nw - 68 && mouse->x <= nx + nw - 12 &&
          mouse->y >= ny + 7 && mouse->y <= ny + 27) {
        g_toast_count = 0;
        g_toast_banner_ticks = 0;
        prev_mouse_left = mouse->left_button;
        prev_mouse_right = mouse->right_button;
        return;
      }
      if (mouse->x >= nx && mouse->x <= nx + nw && mouse->y >= ny && mouse->y <= ny + nh) {
        prev_mouse_left = mouse->left_button;
        prev_mouse_right = mouse->right_button;
        return;
      }
    }

    // Clic sul pulsante Mostra Desktop (estrema destra della Taskbar)
    if (mouse->x >= (int32_t)sw - 11 && mouse->x <= (int32_t)sw && mouse->y >= ty) {
      wm_toggle_show_desktop();
      prev_mouse_left = mouse->left_button;
      prev_mouse_right = mouse->right_button;
      return;
    }

    // Clic sulla System Tray (USB Rimozione Sicura, Layout Tastiera, Centro Notifiche, Orologio)
    int32_t tray_w = 110;
    int32_t tray_x = sw - tray_w - 14;
    int32_t bell_x = tray_x - 28;
    int32_t kbd_x = bell_x - 32;
    int32_t usb_x = kbd_x - 28;
    if (mouse->x >= usb_x && mouse->x < usb_x + 24 && mouse->y >= ty) {
      vfs_cache_flush();
      wm_push_toast("Rimozione Sicura USB", "Cache dischi e periferiche USB svuotata", ICON_HD);
      prev_mouse_left = mouse->left_button;
      prev_mouse_right = mouse->right_button;
      return;
    }
    if (mouse->x >= kbd_x && mouse->x < kbd_x + 28 && mouse->y >= ty) {
      int next_layout = (keyboard_get_layout() + 1) % KBD_LAYOUT_COUNT;
      keyboard_set_layout(next_layout);
      char msg[48];
      mini_snprintf(msg, sizeof(msg), "Layout attivo: %s", keyboard_get_layout_code());
      wm_push_toast("Layout Tastiera", msg, ICON_INFO);
      prev_mouse_left = mouse->left_button;
      prev_mouse_right = mouse->right_button;
      return;
    }
    if (mouse->x >= bell_x && mouse->x < tray_x && mouse->y >= ty) {
      g_notif_center_open = !g_notif_center_open;
      g_toast_banner_ticks = 0;
      prev_mouse_left = mouse->left_button;
      prev_mouse_right = mouse->right_button;
      return;
    }
    if (mouse->x >= tray_x && mouse->x <= (int32_t)sw && mouse->y >= ty) {
      app_power_props_launch();
      prev_mouse_left = mouse->left_button;
      prev_mouse_right = mouse->right_button;
      return;
    }

    // Clic sui bottoni della barra delle applicazioni
    int32_t btn_x = 68;
    for (int i = 0; i < MAX_WINDOWS; i++) {
      if (windows[i]) {
        int32_t bw = 130;
        if (mouse->x >= btn_x && mouse->x <= btn_x + bw && mouse->y >= ty + 2 &&
            mouse->y <= ty + 26) {
          wm_toggle_minimize(windows[i]->id);
          prev_mouse_left = mouse->left_button;
          prev_mouse_right = mouse->right_button;
          return;
        }
        btn_x += bw + 4;
      }
    }

    // Clic sulle finestre (partendo da quella attiva verso il fondo)
    bool window_hit = false;
    for (int step = 0; step < 2; step++) {
      for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        window_t *w = windows[i];
        if (!w || w->is_minimized)
          continue;
        if (step == 0 && !w->is_active)
          continue;
        if (step == 1 && w->is_active)
          continue;

        if (mouse->x >= w->x - 6 && mouse->x <= w->x + w->width + 8 &&
            mouse->y >= w->y - 6 && mouse->y <= w->y + w->height + 8) {

          wm_focus_window(w->id);
          window_hit = true;
          selected_icon_idx = -1;

          // Clic sul pulsante Chiudi [X]
          int32_t close_x = w->x + w->width - 21;
          int32_t close_y = w->y + 4;
          if (mouse->x >= close_x && mouse->x <= close_x + 16 &&
              mouse->y >= close_y && mouse->y <= close_y + 14) {
            wm_destroy_window(w->id);
            prev_mouse_left = mouse->left_button;
            prev_mouse_right = mouse->right_button;
            return;
          }

          // Clic sul pulsante Massimizza / Ripristina [□]
          int32_t max_x = close_x - 18;
          int32_t max_y = close_y;
          if (w->resizable && mouse->x >= max_x && mouse->x <= max_x + 16 &&
              mouse->y >= max_y && mouse->y <= max_y + 14) {
            wm_toggle_maximize(w->id);
            prev_mouse_left = mouse->left_button;
            prev_mouse_right = mouse->right_button;
            return;
          }

          // Clic sul pulsante Riduci a icona [_]
          int32_t min_x = max_x - 18;
          int32_t min_y = close_y;
          if (mouse->x >= min_x && mouse->x <= min_x + 16 &&
              mouse->y >= min_y && mouse->y <= min_y + 14) {
            wm_toggle_minimize(w->id);
            prev_mouse_left = mouse->left_button;
            prev_mouse_right = mouse->right_button;
            return;
          }

          // Rilevamento click su bordi e angoli per Resize
          resize_edge_t edge = wm_detect_resize_edge(w, mouse->x, mouse->y);
          if (edge != RESIZE_NONE && !w->is_maximized) {
            w->is_resizing = true;
            w->resize_edge = edge;
            w->resize_start_mx = mouse->x;
            w->resize_start_my = mouse->y;
            w->resize_orig_x = w->x;
            w->resize_orig_y = w->y;
            w->resize_orig_w = w->width;
            w->resize_orig_h = w->height;
            char rbuf[64];
            mini_snprintf(rbuf, sizeof(rbuf), "[WM] RESIZE START edge=%d orig=(%d,%d)\n", (int)edge, w->width, w->height);
            const char *rp = rbuf;
            while (*rp) outb(0x3F8, *rp++);
            prev_mouse_left = mouse->left_button;
            prev_mouse_right = mouse->right_button;
            return;
          }

          // Clic sulla barra del titolo per trascinare (drag)
          if (mouse->y >= w->y && mouse->y <= w->y + 22) {
            w->is_dragging = true;
            w->drag_off_x = mouse->x - w->x;
            w->drag_off_y = mouse->y - w->y;
            prev_mouse_left = mouse->left_button;
            prev_mouse_right = mouse->right_button;
            return;
          }

          // Clic nel corpo della finestra
          if (w->on_mouse) {
            w->on_mouse(w, mouse->x - w->x, mouse->y - w->y, true);
          }
          prev_mouse_left = mouse->left_button;
          prev_mouse_right = mouse->right_button;
          return;
        }
      }
    }

    // Clic sulle icone del desktop se nessuna finestra è stata colpita
    if (!window_hit && mouse->y < ty) {
      int hit_icon = -1;
      for (int i = num_desktop_icons - 1; i >= 0; i--) {
        if (icon_hit_test(i, mouse->x, mouse->y, (int32_t)sw)) {
          hit_icon = i;
          break;
        }
      }

      if (hit_icon != -1) {
        selected_icon_idx = hit_icon;
        dragging_icon_idx = hit_icon;
        icon_drag_offset_x = mouse->x - desktop_icons[hit_icon].x;
        icon_drag_offset_y = mouse->y - desktop_icons[hit_icon].y;
        icon_click_start_x = mouse->x;
        icon_click_start_y = mouse->y;
        icon_was_dragged = false;
      } else {
        selected_icon_idx = -1;
        dragging_icon_idx = -1;
        icon_was_dragged = false;
      }
    }
  }

  // Inoltro rotella del mouse (PS/2 IntelliMouse) alla finestra attiva
  int32_t dz = mouse_consume_scroll();
  if (dz != 0 && active_window_id != -1) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
      if (windows[i] && windows[i]->id == active_window_id &&
          !windows[i]->is_minimized && windows[i]->on_key) {
        char wk = (dz > 0) ? (char)KEY_WHEEL_UP : (char)KEY_WHEEL_DOWN;
        windows[i]->on_key(windows[i], wk);
        break;
      }
    }
  }

  // Scorciatoie globali di sistema (Win/Ctrl+Esc, Alt+Tab, Alt+F4)
  if (key == KEY_WIN_START) {
    start_menu_open = !start_menu_open;
    prev_mouse_left = mouse->left_button;
    prev_mouse_right = mouse->right_button;
    return;
  } else if (key == KEY_ALT_F4) {
    if (active_window_id != -1) {
      wm_destroy_window(active_window_id);
    }
    prev_mouse_left = mouse->left_button;
    prev_mouse_right = mouse->right_button;
    return;
  } else if (key == KEY_ALT_TAB) {
    int cur_slot = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
      if (windows[i] && windows[i]->id == active_window_id) {
        cur_slot = i;
        break;
      }
    }
    for (int step = 1; step <= MAX_WINDOWS; step++) {
      int idx = (cur_slot + step) % MAX_WINDOWS;
      if (windows[idx]) {
        windows[idx]->is_minimized = false;
        wm_focus_window(windows[idx]->id);
        break;
      }
    }
    alt_tab_overlay_ticks = 90;
    prev_mouse_left = mouse->left_button;
    prev_mouse_right = mouse->right_button;
    return;
  } else if (key == KEY_TASKMGR) {
    app_taskmgr_launch();
    prev_mouse_left = mouse->left_button;
    prev_mouse_right = mouse->right_button;
    return;
  } else if (key == KEY_WIN_E) {
    launcher_open("thispc", NULL);
    prev_mouse_left = mouse->left_button;
    prev_mouse_right = mouse->right_button;
    return;
  } else if (key == KEY_WIN_R) {
    launcher_open("cmd", NULL);
    prev_mouse_left = mouse->left_button;
    prev_mouse_right = mouse->right_button;
    return;
  } else if (key == KEY_WIN_D) {
    wm_toggle_show_desktop();
    prev_mouse_left = mouse->left_button;
    prev_mouse_right = mouse->right_button;
    return;
  } else if (key == KEY_WIN_L) {
    start_menu_open = false;
    screen_mgr_run_loginscreen();
    prev_mouse_left = mouse->left_button;
    prev_mouse_right = mouse->right_button;
    return;
  }

  // Inoltro tasti alla barra di ricerca del Menu Start Moderno se aperto
  if (key != 0 && start_menu_open && g_start_menu_mode == STARTMENU_MODE_MODERN) {
    if (key == 27) {
      start_menu_open = false;
    } else {
      startmenu_handle_key(key);
    }
    prev_mouse_left = mouse->left_button;
    prev_mouse_right = mouse->right_button;
    return;
  }

  // Inoltro tasti della tastiera alla finestra attiva (o gestione Desktop Ctrl+C / Ctrl+X / Ctrl+V)
  if (key != 0 && active_window_id != -1) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
      if (windows[i] && windows[i]->id == active_window_id &&
          !windows[i]->is_minimized) {
        if (windows[i]->on_key) {
          windows[i]->on_key(windows[i], key);
        }
        break;
      }
    }
  } else if (key != 0 && active_window_id == -1) {
    if ((uint8_t)key == KEY_CTRL_C || key == 0x03) {
      clipboard_push_file("C:\\User\\Morgan\\Desktop\\BENVENUTO.TXT", false);
      app_comdlg_copy_move_launch(COMDLG_OP_COPY, "C:\\User\\Morgan\\Desktop\\BENVENUTO.TXT", "C:\\User\\Morgan\\Desktop");
    } else if ((uint8_t)key == KEY_CTRL_X || key == 0x18) {
      clipboard_push_file("C:\\User\\Morgan\\Desktop\\BENVENUTO.TXT", true);
      app_comdlg_copy_move_launch(COMDLG_OP_MOVE, "C:\\User\\Morgan\\Desktop\\BENVENUTO.TXT", "C:\\User\\Morgan\\Desktop");
    } else if ((uint8_t)key == KEY_CTRL_V || key == 0x16) {
      app_comdlg_copy_move_launch(COMDLG_OP_PASTE, NULL, "C:\\User\\Morgan\\Desktop");
    }
  }

  prev_mouse_left = mouse->left_button;
  prev_mouse_right = mouse->right_button;
}

void wm_on_resolution_changed(uint32_t old_w, uint32_t old_h, uint32_t new_w,
                              uint32_t new_h) {
  (void)old_w;
  (void)old_h;
  int32_t sw = (int32_t)new_w;
  int32_t sh = (int32_t)new_h;

  // Chiude menu a comparsa per evitare disallineamenti grafici
  start_menu_open = false;
  ctx_menu_open = false;
  ctx_submenu_open = false;

  // Riorganizza e adatta automaticamente le icone del desktop alla nuova
  // risoluzione
  wm_rearrange_desktop_icons();

  // Ricalibra posizione e dimensioni delle finestre
  for (int i = 0; i < MAX_WINDOWS; i++) {
    window_t *win = windows[i];
    if (!win)
      continue;

    // Se la finestra sfora i nuovi margini, riposizionala nell'area visibile
    if (win->x + win->width > sw) {
      win->x = sw - win->width;
      if (win->x < 0) {
        win->x = 0;
        if (win->width > sw)
          win->width = sw;
      }
    }
    if (win->y + win->height > sh - 28) {
      win->y = (sh - 28) - win->height;
      if (win->y < 0) {
        win->y = 0;
        if (win->height > sh - 28)
          win->height = sh - 28;
      }
    }
  }
}

void wm_draw_themed_button(int32_t x, int32_t y, int32_t w, int32_t h,
                           const char *label, bool pressed) {
  if (current_theme == THEME_CLASSIC && !g_active_theme_props.modern_controls) {
    gfx_fill_rect(x, y, w, h, COLOR_BTNFACE);
    gfx_draw_bevel_rect(x, y, w, h, pressed);
    int32_t tlen = label ? (int32_t)strlen(label) * 8 : 0;
    int32_t tx = x + (w - tlen) / 2;
    int32_t ty = y + (h - 16) / 2;
    if (pressed) {
      tx++;
      ty++;
    }
    gfx_draw_string(tx, ty, label ? label : "", COLOR_WINDOWTEXT, 0, true);
    return;
  }

  uint16_t rad = g_active_theme_props.button_radius > 0
                     ? g_active_theme_props.button_radius
                     : (g_active_theme_props.control_radius > 0
                            ? g_active_theme_props.control_radius
                            : 5);
  uint32_t bg =
      pressed ? (g_active_theme_props.accent_color
                     ? g_active_theme_props.accent_color
                     : 0x0078D4)
              : (g_active_theme_props.btn_face ? g_active_theme_props.btn_face
                                               : 0xFFFFFF);
  uint32_t fg =
      pressed
          ? (g_active_theme_props.accent_text ? g_active_theme_props.accent_text
                                              : 0xFFFFFF)
          : (g_active_theme_props.btn_text ? g_active_theme_props.btn_text
                                           : 0x1A1A1A);

  // Stile Flat moderno: nessuna striscia speculare 3D, pill arrotondata flat
  if (g_active_theme_props.surface_alpha < 200) {
    gfx_fill_rounded_alpha(x, y, w, h, rad, bg, pressed ? 210 : 160);
  } else {
    gfx_fill_rounded_rect(x, y, w, h, rad, bg);
  }

  if (g_active_theme_props.control_border) {
    uint32_t b_col = g_active_theme_props.control_border_color
                         ? g_active_theme_props.control_border_color
                         : 0xC8D4E2;
    uint8_t b_rad = g_active_theme_props.button_border_radius;
    gfx_draw_rounded_rect(x, y, w, h, b_rad, b_col);
  }

  // Esecuzione eventuale script pulsanti e tab (gestito out-of-kernel)
  if (g_active_theme_props.button_script[0] != '\0') {
    theme_mgr_render_script(g_active_theme_props.button_script, x, y, w, h, rad, pressed ? 4 : 0);
  }

  int32_t tlen = label ? (int32_t)strlen(label) * 8 : 0;
  int32_t tx = x + (w - tlen) / 2;
  int32_t ty = y + (h - 16) / 2;
  gfx_draw_string(tx, ty, label ? label : "", fg, 0, true);
}

void wm_draw_themed_tab(int32_t x, int32_t y, int32_t w, int32_t h,
                        const char *label, bool active) {
  if (current_theme == THEME_CLASSIC && !g_active_theme_props.modern_controls) {
    gfx_fill_rect(x, y, w, h + (active ? 2 : 0),
                  active ? COLOR_BTNFACE : COLOR_BTNSHADOW);
    gfx_draw_bevel_rect(x, y, w, h, active);
    int32_t tlen = label ? (int32_t)strlen(label) * 8 : 0;
    int32_t tx = x + (w - tlen) / 2;
    gfx_draw_string(tx, y + 3, label ? label : "",
                    active ? COLOR_WINDOWTEXT : COLOR_BTNHIGHLIGHT, 0, true);
    return;
  }

  uint16_t rad = g_active_theme_props.tab_radius > 0
                     ? g_active_theme_props.tab_radius
                     : (g_active_theme_props.control_radius > 0
                            ? g_active_theme_props.control_radius
                            : 5);
  if (active) {
    uint32_t bg = g_active_theme_props.surface_base
                      ? g_active_theme_props.surface_base
                      : 0xFFFFFF;
    if (g_active_theme_props.surface_alpha < 200) {
      gfx_fill_rounded_alpha(x, y, w, h, rad, bg, 160);
    } else {
      gfx_fill_rounded_rect(x, y, w, h, rad, bg);
    }
    uint32_t acc = g_active_theme_props.accent_color
                       ? g_active_theme_props.accent_color
                       : 0x0078D4;
    gfx_fill_rect(x + 4, y + h - 2, w - 8, 2, acc);

    int32_t tlen = label ? (int32_t)strlen(label) * 8 : 0;
    int32_t tx = x + (w - tlen) / 2;
    gfx_draw_string(tx, y + (h - 16) / 2, label ? label : "", acc, 0, true);
  } else {
    int32_t tlen = label ? (int32_t)strlen(label) * 8 : 0;
    int32_t tx = x + (w - tlen) / 2;
    uint32_t fg = (current_theme == THEME_MICA_DARK ||
                   g_active_theme_props.surface_alpha < 200)
                      ? 0x909090
                      : 0x606060;
    gfx_draw_string(tx, y + (h - 16) / 2, label ? label : "", fg, 0, true);
  }
}
