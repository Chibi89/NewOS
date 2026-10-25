#include <gui/wm.h>
#include <gui/graphics.h>
#include <gui/icons.h>
#include <gui/image.h>
#include <apps/explorer.h>
#include <apps/power_props.h>
#include <apps/installer.h>
#include <apps/player.h>
#include <apps/taskmgr.h>
#include <user.h>
#include <fs/vfs.h>
#include <drivers/power.h>
#include <drivers/rtc.h>
#include <mm/heap.h>
#include <string.h>
#include <arch/x86_64/io.h>

static window_t* windows[MAX_WINDOWS];
static int32_t active_window_id = -1;
static bool start_menu_open = false;

// Sfondo del desktop corrente
static int current_bg_type = BG_TEAL_2000;

// Tema visivo corrente (THEME_CLASSIC / THEME_LIQUID_GLASS)
static int current_theme = THEME_CLASSIC;

// Stato Menu Contestuale (Tasto destro)
static bool ctx_menu_open = false;
static int32_t ctx_x = 0;
static int32_t ctx_y = 0;
static bool ctx_submenu_open = false;

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
    int type; // 0: Computer, 1: Cmd, 2: Notepad, 3: Calc, 4: About, 5: Folder, 6: TextFile
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
#define DESKTOP_GRID_COL_W    116   // Larghezza colonna (distanza orizzontale)
#define DESKTOP_GRID_ROW_H    88    // Altezza cella riga (distanza verticale)
#define DESKTOP_GRID_START_X  18    // Margine orizzontale sinistro
#define DESKTOP_GRID_START_Y  16    // Margine verticale superiore

static int wm_get_desktop_max_rows(void) {
    uint32_t sh = gfx_get_height();
    int32_t taskbar_y = (int32_t)sh - 28;
    int32_t usable_h = taskbar_y - DESKTOP_GRID_START_Y - 8;
    int max_rows = usable_h / DESKTOP_GRID_ROW_H;
    if (max_rows < 3) max_rows = 3;
    return max_rows;
}

static void wm_get_icon_slot_pos(int slot, int32_t* out_x, int32_t* out_y) {
    int max_rows = wm_get_desktop_max_rows();
    int col = slot / max_rows;
    int row = slot % max_rows;

    *out_x = DESKTOP_GRID_START_X + col * DESKTOP_GRID_COL_W + (DESKTOP_GRID_COL_W - 32) / 2;
    *out_y = DESKTOP_GRID_START_Y + row * DESKTOP_GRID_ROW_H + 4;
}

void wm_rearrange_desktop_icons(void) {
    for (int i = 0; i < num_desktop_icons; i++) {
        wm_get_icon_slot_pos(i, &desktop_icons[i].x, &desktop_icons[i].y);
    }
}

static void wm_split_icon_label(const char* label, char line1[16], char line2[16]) {
    line1[0] = '\0';
    line2[0] = '\0';
    if (!label) return;

    int len = (int)strlen(label);
    if (len <= 12) {
        strncpy(line1, label, 15);
        line1[15] = '\0';
        return;
    }

    // Cerca l'ultimo spazio utile entro i primi 12 caratteri
    int split_idx = -1;
    for (int i = 0; i <= 12 && i < len; i++) {
        if (label[i] == ' ') split_idx = i;
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
    if (cp1 > 14) cp1 = 14;
    strncpy(line1, label, cp1);
    line1[cp1] = '\0';

    const char* rem = label + split_idx;
    if (*rem == ' ') rem++;
    strncpy(line2, rem, 14);
    line2[14] = '\0';
}

static bool icon_hit_test(int idx, int32_t mx, int32_t my, int32_t sw) {
    (void)sw;
    if (idx < 0 || idx >= num_desktop_icons) return false;
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
        if (current_theme == THEME_LIQUID_GLASS) {
            gfx_precompute_blur();
        }
    }
}

int wm_get_background_type(void) {
    return current_bg_type;
}

void wm_set_theme(int theme) {
    current_theme = theme;
    if (theme == THEME_LIQUID_GLASS) {
        gfx_precompute_blur();
    }
}

int wm_get_theme(void) {
    return current_theme;
}

void wm_add_desktop_file(const char* name, bool is_folder) {
    if (num_desktop_icons >= MAX_DESKTOP_ICONS) return;

    int idx = num_desktop_icons++;
    wm_get_icon_slot_pos(idx, &desktop_icons[idx].x, &desktop_icons[idx].y);

    if (is_folder) {
        desktop_icons[idx].type = 5;
        if (name && name[0] != '\0') {
            strncpy(desktop_icons[idx].label, name, sizeof(desktop_icons[idx].label) - 1);
        } else {
            strcpy(desktop_icons[idx].label, "Nuova cartella");
        }
        desktop_icons[idx].content[0] = '\0';
    } else {
        desktop_icons[idx].type = 6;
        if (name && name[0] != '\0') {
            strncpy(desktop_icons[idx].label, name, sizeof(desktop_icons[idx].label) - 1);
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
    const char* base_labels[] = {
        "Risorse PC",
        "Web Browser",
        "Cestino",
        "Prompt Comandi",
        "Blocco Note",
        "Calcolatrice",
        "Info Sistema"
    };
    int base_types[] = { 0, 9, 7, 1, 2, 3, 4 };

    for (int i = 0; i < 7; i++) {
        int idx = num_desktop_icons++;
        wm_get_icon_slot_pos(idx, &desktop_icons[idx].x, &desktop_icons[idx].y);
        strncpy(desktop_icons[idx].label, base_labels[i], sizeof(desktop_icons[idx].label) - 1);
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

    // Carica dinamicamente i documenti e le cartelle da C:\USER\[nomeUser]\DESKTOP
    user_profile_t* uprof = user_get_current();
    if (uprof && uprof->desktop_path[0]) {
        vfs_dirent_t u_entries[8];
        int32_t count = vfs_readdir(uprof->desktop_path, u_entries, 8);
        if (count > 0) {
            for (int e = 0; e < count; e++) {
                if (strcmp(u_entries[e].name, ".") == 0 || strcmp(u_entries[e].name, "..") == 0) continue;
                wm_add_desktop_file(u_entries[e].name, u_entries[e].is_directory);
            }
        }
    }
}

window_t* wm_create_window(const char* title, int32_t x, int32_t y, int32_t w, int32_t h, app_type_t type) {
    int slot = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i] == NULL) {
            slot = i;
            break;
        }
    }
    if (slot == -1) return NULL; // Raggiunto numero massimo finestre

    window_t* win = (window_t*)kmalloc(sizeof(window_t));
    if (!win) return NULL;

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

static void draw_window_frame(window_t* win) {
    if (win->is_minimized) return;

    int32_t x = win->x;
    int32_t y = win->y;
    int32_t w = win->width;
    int32_t h = win->height;

    // ================================================================
    //  TEMA LIQUID GLASS
    // ================================================================
    if (current_theme == THEME_LIQUID_GLASS) {
        // 1. Ombra morbida multi-layer
        gfx_draw_drop_shadow(x, y, w, h, 4);

        // 2. Pannello vetro frosted (campiona blurred_buf + tint)
        uint32_t tint  = win->is_active ? LG_GLASS_BLUE  : LG_GLASS_TINT;
        uint8_t  alpha = win->is_active ? LG_GLASS_BLUE_ALPHA : LG_GLASS_ALPHA;
        gfx_fill_rounded_alpha(x, y, w, h, LG_CORNER_RADIUS, tint, alpha);

        // 3. Speculare rim + bordo sottile
        gfx_draw_specular_rim(x, y, w, h, LG_CORNER_RADIUS);

        // 4. Barra del titolo: striscia piu' opaca in cima (prima 22px)
        uint32_t cap_tint  = win->is_active ? 0x4080FF : 0x808090;
        uint8_t  cap_alpha = win->is_active ? 70 : 45;
        gfx_fill_rounded_alpha(x, y, w, 22, LG_CORNER_RADIUS, cap_tint, cap_alpha);
        // Secondo speculare sulla sola titlebar
        gfx_draw_specular_rim(x, y, w, 22, LG_CORNER_RADIUS);

        // 5. Icona applicazione (16x16)
        icon_id_t w_icon = ICON_INFO;
        if (win->app_type == APP_CMD)      w_icon = ICON_RUN;
        else if (win->app_type == APP_NOTEPAD)  w_icon = ICON_DOCUMENT;
        else if (win->app_type == APP_CALC)     w_icon = ICON_MAINTENANCE;
        else if (win->app_type == APP_ABOUT)    w_icon = ICON_INFO;
        else if (win->app_type == APP_DISPLAY)  w_icon = ICON_DESKTOP;
        else if (win->app_type == APP_SOUND)    w_icon = ICON_SOUND;
        else if (win->app_type == APP_EXPLORER) {
            w_icon = ICON_THIS_PC;
            if (strstr(win->title, "Disco locale") || strstr(win->title, "C:\\")) w_icon = ICON_HD;
            else if (strstr(win->title, "CD-ROM") || strstr(win->title, "D:\\")) w_icon = ICON_CD;
            else if (strstr(win->title, "Floppy") || strstr(win->title, "A:\\")) w_icon = ICON_FLOPPY;
            else if (strstr(win->title, "Cestino"))  w_icon = ICON_CESTINO;
            else if (strstr(win->title, "Pannello")) w_icon = ICON_CONTROL_PANEL;
            else if (strstr(win->title, "Risorse"))  w_icon = ICON_THIS_PC;
            else w_icon = ICON_FOLDER;
        }
        gfx_draw_icon_16(x + 5, y + 3, w_icon);

        // 6. Titolo bianco con ombra nera per leggibilita'
        gfx_draw_string(x + 24, y + 3, win->title, 0x101010, 0, true); // ombra
        gfx_draw_string(x + 23, y + 2, win->title, LG_TEXT_PRIMARY, 0, true);

        // 7. Pulsante chiudi: capsule glass
        int32_t close_x = x + w - 22;
        int32_t close_y = y + 4;
        gfx_fill_rounded_alpha(close_x, close_y, 16, 14, 6, 0xFF3030, 140);
        gfx_draw_specular_rim(close_x, close_y, 16, 14, 6);
        gfx_draw_string(close_x + 5, close_y + 1, "x", LG_TEXT_PRIMARY, 0, true);

        // 8. Pulsante minimizza: capsule glass
        int32_t min_x = close_x - 19;
        int32_t min_y = close_y;
        gfx_fill_rounded_alpha(min_x, min_y, 16, 14, 6, 0x404040, 100);
        gfx_draw_specular_rim(min_x, min_y, 16, 14, 6);
        gfx_draw_string(min_x + 4, min_y - 1, "_", LG_TEXT_PRIMARY, 0, true);

        // 9. Contenuto applicazione
        if (win->on_draw) win->on_draw(win);
        return;
    }

    // ================================================================
    //  TEMA CLASSIC (Windows 2000 — invariato)
    // ================================================================

    // Cornice esterna in rilievo 3D
    gfx_fill_rect(x, y, w, h, COLOR_BTNFACE);
    gfx_draw_bevel_rect(x, y, w, h, false);

    // Barra del titolo con gradiente
    uint32_t c_left  = win->is_active ? COLOR_ACTIVECAPTION_LEFT  : COLOR_INACTIVECAPTION_LEFT;
    uint32_t c_right = win->is_active ? COLOR_ACTIVECAPTION_RIGHT : COLOR_INACTIVECAPTION_RIGHT;
    uint32_t c_text  = win->is_active ? COLOR_CAPTIONTEXT         : COLOR_INACTIVECAPTIONTEXT;

    gfx_draw_gradient_h(x + 3, y + 3, w - 6, 18, c_left, c_right);

    // Icona 16x16 specifica per l'applicazione
    icon_id_t w_icon = ICON_INFO;
    if (win->app_type == APP_CMD) w_icon = ICON_RUN;
    else if (win->app_type == APP_NOTEPAD) w_icon = ICON_DOCUMENT;
    else if (win->app_type == APP_CALC) w_icon = ICON_MAINTENANCE;
    else if (win->app_type == APP_ABOUT) w_icon = ICON_INFO;
    else if (win->app_type == APP_DISPLAY) w_icon = ICON_DESKTOP;
    else if (win->app_type == APP_SOUND) w_icon = ICON_SOUND;
    else if (win->app_type == APP_RULES) w_icon = ICON_DOCUMENT;
    else if (win->app_type == APP_BROWSER) w_icon = ICON_NET;
    else if (win->app_type == APP_PLAYER) w_icon = ICON_PLAY;
    else if (win->app_type == APP_EXPLORER) {
        w_icon = ICON_THIS_PC;
        if (strstr(win->title, "Disco locale") || strstr(win->title, "C:\\")) w_icon = ICON_HD;
        else if (strstr(win->title, "CD-ROM") || strstr(win->title, "D:\\")) w_icon = ICON_CD;
        else if (strstr(win->title, "Floppy") || strstr(win->title, "A:\\")) w_icon = ICON_FLOPPY;
        else if (strstr(win->title, "Cestino")) w_icon = ICON_CESTINO;
        else if (strstr(win->title, "Pannello")) w_icon = ICON_CONTROL_PANEL;
        else if (strstr(win->title, "Risorse")) w_icon = ICON_THIS_PC;
        else w_icon = ICON_FOLDER;
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

    // Pulsante Riduci a icona [_]
    int32_t min_x = close_x - 18;
    int32_t min_y = close_y;
    gfx_fill_rect(min_x, min_y, 16, 14, COLOR_BTNFACE);
    gfx_draw_bevel_rect(min_x, min_y, 16, 14, false);
    gfx_draw_string(min_x + 4, min_y - 2, "_", 0x000000, 0, true);

    // Disegna contenuto specifico dell'applicazione
    if (win->on_draw) {
        win->on_draw(win);
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
    // Scritta NewOS V1 sul banner verticale (8 linee da 16px = 128px, termina a my + mh - 8)
    gfx_draw_string(mx + 6, my + mh - 136, "N\ne\nw\nO\nS\n \nV\n1", 0xFFFFFF, 0, true);

    // Voci di menu con icone
    const char* items[] = {
        "Web Browser",
        "Prompt Comandi",
        "Blocco Note",
        "Calcolatrice",
        "Lettore Multimediale",
        "Regole di Sistema",
        "Pannello di controllo",
        "Editor del Registro",
        "Info su NewOS V1",
        "Arresta il sistema..."
    };
    icon_id_t item_icons[] = {
        ICON_NET,
        ICON_RUN,
        ICON_DOCUMENT,
        ICON_MAINTENANCE,
        ICON_PLAY,
        ICON_DOCUMENT,
        ICON_CONTROL_PANEL,
        ICON_MAINTENANCE,
        ICON_INFO,
        ICON_ENERGY
    };

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
    if (!ctx_menu_open) return;

    int32_t mw = 160;
    int32_t mh = 98;

    // Sfondo menu contestuale con rilievo 3D
    gfx_fill_rect(ctx_x, ctx_y, mw, mh, COLOR_BTNFACE);
    gfx_draw_bevel_rect(ctx_x, ctx_y, mw, mh, false);

    bool hov_nuovo = (mx >= ctx_x && mx <= ctx_x + mw && my >= ctx_y + 3 && my < ctx_y + 25) || ctx_submenu_open;
    bool hov_disponi = (!hov_nuovo && mx >= ctx_x && mx <= ctx_x + mw && my >= ctx_y + 28 && my < ctx_y + 49);
    bool hov_personalizza = (!hov_nuovo && mx >= ctx_x && mx <= ctx_x + mw && my >= ctx_y + 49 && my < ctx_y + 71);
    bool hov_prop = (!hov_nuovo && mx >= ctx_x && mx <= ctx_x + mw && my >= ctx_y + 71 && my <= ctx_y + 95);

    // Voce 0: Nuovo ▶
    if (hov_nuovo) {
        gfx_fill_rect(ctx_x + 3, ctx_y + 3, mw - 6, 22, 0x0A246A);
        gfx_draw_string(ctx_x + 12, ctx_y + 6, "Nuovo", 0xFFFFFF, 0, true);
        gfx_draw_string(ctx_x + mw - 18, ctx_y + 6, ">", 0xFFFFFF, 0, true);
    } else {
        gfx_draw_string(ctx_x + 12, ctx_y + 6, "Nuovo", COLOR_WINDOWTEXT, 0, true);
        gfx_draw_string(ctx_x + mw - 18, ctx_y + 6, ">", 0x808080, 0, true);
    }

    // Separatore scavato
    gfx_draw_bevel_rect(ctx_x + 4, ctx_y + 26, mw - 8, 2, true);

    // Voce 1: Disponi icone
    if (hov_disponi) {
        gfx_fill_rect(ctx_x + 3, ctx_y + 28, mw - 6, 20, 0x0A246A);
        gfx_draw_string(ctx_x + 12, ctx_y + 30, "Disponi icone", 0xFFFFFF, 0, true);
    } else {
        gfx_draw_string(ctx_x + 12, ctx_y + 30, "Disponi icone", COLOR_WINDOWTEXT, 0, true);
    }

    // Voce 2: Personalizza...
    if (hov_personalizza) {
        gfx_fill_rect(ctx_x + 3, ctx_y + 49, mw - 6, 21, 0x0A246A);
        gfx_draw_icon_16(ctx_x + 8, ctx_y + 51, ICON_DESKTOP);
        gfx_draw_string(ctx_x + 30, ctx_y + 52, "Personalizza...", 0xFFFFFF, 0, true);
    } else {
        gfx_draw_icon_16(ctx_x + 8, ctx_y + 51, ICON_DESKTOP);
        gfx_draw_string(ctx_x + 30, ctx_y + 52, "Personalizza...", COLOR_WINDOWTEXT, 0, true);
    }

    // Voce 3: Proprietà
    if (hov_prop) {
        gfx_fill_rect(ctx_x + 3, ctx_y + 71, mw - 6, 22, 0x0A246A);
        gfx_draw_icon_16(ctx_x + 8, ctx_y + 73, ICON_INFO);
        gfx_draw_string(ctx_x + 30, ctx_y + 74, "Proprieta", 0xFFFFFF, 0, true);
    } else {
        gfx_draw_icon_16(ctx_x + 8, ctx_y + 73, ICON_INFO);
        gfx_draw_string(ctx_x + 30, ctx_y + 74, "Proprieta", COLOR_WINDOWTEXT, 0, true);
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

        gfx_fill_rect(sm_x, sm_y, sm_w, sm_h, COLOR_BTNFACE);
        gfx_draw_bevel_rect(sm_x, sm_y, sm_w, sm_h, false);

        bool hov_sub_folder = (mx >= sm_x && mx <= sm_x + sm_w && my >= sm_y + 3 && my < sm_y + 28);
        bool hov_sub_doc    = (mx >= sm_x && mx <= sm_x + sm_w && my >= sm_y + 28 && my <= sm_y + 55);

        // Sub 0: Cartella
        if (hov_sub_folder) {
            gfx_fill_rect(sm_x + 3, sm_y + 3, sm_w - 6, 24, 0x0A246A);
            gfx_draw_icon_16(sm_x + 8, sm_y + 6, ICON_FOLDER);
            gfx_draw_string(sm_x + 30, sm_y + 8, "Cartella", 0xFFFFFF, 0, true);
        } else {
            gfx_draw_icon_16(sm_x + 8, sm_y + 6, ICON_FOLDER);
            gfx_draw_string(sm_x + 30, sm_y + 8, "Cartella", COLOR_WINDOWTEXT, 0, true);
        }

        // Sub 1: Documento di testo
        if (hov_sub_doc) {
            gfx_fill_rect(sm_x + 3, sm_y + 29, sm_w - 6, 25, 0x0A246A);
            gfx_draw_icon_16(sm_x + 8, sm_y + 32, ICON_DOCUMENT);
            gfx_draw_string(sm_x + 30, sm_y + 34, "Doc. di testo", 0xFFFFFF, 0, true);
        } else {
            gfx_draw_icon_16(sm_x + 8, sm_y + 32, ICON_DOCUMENT);
            gfx_draw_string(sm_x + 30, sm_y + 34, "Doc. di testo", COLOR_WINDOWTEXT, 0, true);
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

        if (desktop_icons[i].type == 0) gfx_draw_icon_32(ix, iy, ICON_THIS_PC);
        else if (desktop_icons[i].type == 1) gfx_draw_icon_32(ix, iy, ICON_RUN);
        else if (desktop_icons[i].type == 2) gfx_draw_icon_32(ix, iy, ICON_DOCUMENT);
        else if (desktop_icons[i].type == 3) gfx_draw_icon_calc(ix, iy);
        else if (desktop_icons[i].type == 4) gfx_draw_icon_32(ix, iy, ICON_INFO);
        else if (desktop_icons[i].type == 5) gfx_draw_icon_32(ix, iy, ICON_FOLDER);
        else if (desktop_icons[i].type == 6) {
            const char* dot = strstr(desktop_icons[i].label, ".");
            if (dot && (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".bmp") == 0 || strcasecmp(dot, ".png") == 0)) {
                gfx_draw_icon_32(ix, iy, ICON_IMAGE);
            } else {
                gfx_draw_icon_32(ix, iy, ICON_DOCUMENT);
            }
        }
        else if (desktop_icons[i].type == 7) gfx_draw_icon_32(ix, iy, ICON_CESTINO);
        else if (desktop_icons[i].type == 8) gfx_draw_icon_32(ix, iy, ICON_MAINTENANCE);
        else if (desktop_icons[i].type == 9) gfx_draw_icon_32(ix, iy, ICON_NET);

        // Formatta etichetta su 1 o 2 righe centrate all'interno della colonna
        char l1[16], l2[16];
        wm_split_icon_label(desktop_icons[i].label, l1, l2);

        int32_t cx = ix + 16;
        int32_t lw1 = (int32_t)strlen(l1) * 8;
        int32_t lx1 = cx - (lw1 / 2);
        if (lx1 < 2) lx1 = 2;
        if (lx1 + lw1 > (int32_t)sw - 2) lx1 = (int32_t)sw - 2 - lw1;

        bool has_line2 = (l2[0] != '\0');
        int32_t lw2 = has_line2 ? ((int32_t)strlen(l2) * 8) : 0;
        int32_t lx2 = has_line2 ? (cx - (lw2 / 2)) : 0;
        if (lx2 < 2) lx2 = 2;
        if (has_line2 && lx2 + lw2 > (int32_t)sw - 2) lx2 = (int32_t)sw - 2 - lw2;

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

    if (current_theme == THEME_LIQUID_GLASS) {
        // Taskbar glass scura
        gfx_fill_alpha(0, ty, sw, 28, LG_TASKBAR_COLOR, LG_TASKBAR_ALPHA);
        // Riga speculare superiore
        gfx_fill_alpha(0, ty, sw, 1, LG_SPECULAR_COLOR, 60);
    } else {
        gfx_fill_rect(0, ty, sw, 28, COLOR_BTNFACE);
        for (uint32_t x = 0; x < sw; x++) {
            gfx_draw_pixel(x, ty, COLOR_BTNHIGHLIGHT);
        }
    }

    // Pulsante "Start"
    bool start_pressed = start_menu_open;
    if (current_theme == THEME_LIQUID_GLASS) {
        // Capsule glass con icona e scritta
        uint32_t st_tint  = start_pressed ? 0x5090FF : 0x3060CC;
        uint8_t  st_alpha = start_pressed ? 180 : 140;
        gfx_fill_rounded_alpha(2, ty + 2, 68, 22, 8, st_tint, st_alpha);
        gfx_draw_specular_rim(2, ty + 2, 68, 22, 8);
        gfx_draw_windows_logo(6, ty + 5);
        gfx_draw_string(24, ty + 6, "Start", LG_TEXT_PRIMARY, 0, true);
    } else {
        gfx_fill_rect(2, ty + 2, 60, 24, COLOR_BTNFACE);
        gfx_draw_bevel_rect(2, ty + 2, 60, 24, start_pressed);
        gfx_draw_windows_logo(6, ty + 6);
        gfx_draw_string(24, ty + 6, "Start", COLOR_WINDOWTEXT, 0, true);
    }

    // Pulsanti delle finestre aperte sulla Taskbar
    int32_t btn_x = 76;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i]) {
            int32_t bw = 130;
            bool pressed = (windows[i]->is_active && !windows[i]->is_minimized);

            if (current_theme == THEME_LIQUID_GLASS) {
                uint32_t b_tint  = pressed ? 0x4080FF : 0x202030;
                uint8_t  b_alpha = pressed ? 170 : 120;
                gfx_fill_rounded_alpha(btn_x, ty + 3, bw, 20, 6, b_tint, b_alpha);
                if (pressed) gfx_draw_specular_rim(btn_x, ty + 3, bw, 20, 6);
            } else {
                gfx_fill_rect(btn_x, ty + 2, bw, 24, COLOR_BTNFACE);
                gfx_draw_bevel_rect(btn_x, ty + 2, bw, 24, pressed);
            }

            char short_title[16];
            strncpy(short_title, windows[i]->title, 13);
            short_title[13] = '\0';
            uint32_t t_col = (current_theme == THEME_LIQUID_GLASS) ? LG_TEXT_PRIMARY : (uint32_t)COLOR_WINDOWTEXT;
            int32_t  t_y   = (current_theme == THEME_LIQUID_GLASS) ? ty + 5 : ty + 6;
            gfx_draw_string(btn_x + 6, t_y, short_title, t_col, 0, true);

            btn_x += bw + 4;
            if (btn_x + bw > (int32_t)sw - 100) break;
        }
    }

    // System Tray con icona batteria/alimentazione e orologio CMOS in tempo reale
    int32_t tray_w = 110;
    int32_t tray_x = sw - tray_w - 4;
    int32_t tray_y = ty + 3;
    if (current_theme == THEME_LIQUID_GLASS) {
        gfx_fill_rounded_alpha(tray_x, tray_y, tray_w, 22, 6, 0x101020, 150);
        gfx_draw_specular_rim(tray_x, tray_y, tray_w, 22, 6);
    } else {
        gfx_fill_rect(tray_x, tray_y, tray_w, 22, COLOR_BTNFACE);
        gfx_draw_bevel_rect(tray_x, tray_y, tray_w, 22, true);
    }

    // Icona alimentazione/batteria nella tray
    gfx_draw_icon_16(tray_x + 6, tray_y + 3, ICON_ENERGY);

    // Orologio CMOS in tempo reale
    rtc_time_t now;
    rtc_get_time(&now);
    char time_str[16];
    mini_snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", now.hour, now.minute, now.second);
    uint32_t clk_col = (current_theme == THEME_LIQUID_GLASS) ? LG_TEXT_PRIMARY : (uint32_t)COLOR_WINDOWTEXT;
    gfx_draw_string(tray_x + 28, tray_y + 4, time_str, clk_col, 0, true);

    // 5. Menu Start (se aperto)
    if (start_menu_open) {
        draw_start_menu(sw, sh);
    }

    // 6. Menu Contestuale Tasto Destro (se aperto)
    if (ctx_menu_open) {
        draw_context_menu(last_mouse_x, last_mouse_y);
    }

    // 7. Cursore mouse
    gfx_draw_cursor(last_mouse_x, last_mouse_y);

    // 8. Blit ultra-veloce a schermo senza flickering
    gfx_flip();
}

void wm_update(mouse_state_t* mouse, char key) {
    app_player_update_tick();
    last_mouse_x = mouse->x;
    last_mouse_y = mouse->y;

    uint32_t sw = gfx_get_width();
    uint32_t sh = gfx_get_height();
    (void)sw;
    int32_t ty = sh - 28;

    bool mouse_clicked = mouse->left_button && !prev_mouse_left;
    bool mouse_released = !mouse->left_button && prev_mouse_left;
    bool right_clicked = mouse->right_button && !prev_mouse_right;

    // Trascinamento con tasto sinistro (finestre o icone del desktop)
    if (mouse->left_button) {
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (windows[i] && windows[i]->is_dragging) {
                windows[i]->x = mouse->x - windows[i]->drag_off_x;
                windows[i]->y = mouse->y - windows[i]->drag_off_y;
                if (windows[i]->y < 0) windows[i]->y = 0;
                if (windows[i]->y > ty - 20) windows[i]->y = ty - 20;
            }
        }

        // Trascinamento icone del desktop
        if (dragging_icon_idx >= 0 && dragging_icon_idx < num_desktop_icons) {
            int32_t dx = mouse->x - icon_click_start_x;
            int32_t dy = mouse->y - icon_click_start_y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx > 3 || dy > 3) {
                icon_was_dragged = true;
            }

            if (icon_was_dragged) {
                int32_t new_x = mouse->x - icon_drag_offset_x;
                int32_t new_y = mouse->y - icon_drag_offset_y;

                if (new_x < 4) new_x = 4;
                if (new_x > (int32_t)sw - 60) new_x = sw - 60;
                if (new_y < 4) new_y = 4;
                if (new_y > ty - 56) new_y = ty - 56;

                desktop_icons[dragging_icon_idx].x = new_x;
                desktop_icons[dragging_icon_idx].y = new_y;
            }
        }
    }

    if (mouse_released) {
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (windows[i]) windows[i]->is_dragging = false;
        }

        // Rilascio del trascinamento icona o esecuzione al clic
        if (dragging_icon_idx >= 0 && dragging_icon_idx < num_desktop_icons) {
            if (!icon_was_dragged) {
                int idx = dragging_icon_idx;
                if (desktop_icons[idx].type == 0) app_explorer_open_this_pc();
                else if (desktop_icons[idx].type == 1) app_cmd_launch();
                else if (desktop_icons[idx].type == 2) app_notepad_launch();
                else if (desktop_icons[idx].type == 3) app_calc_launch();
                else if (desktop_icons[idx].type == 4) app_about_launch();
                else if (desktop_icons[idx].type == 5) {
                    app_explorer_open_folder(desktop_icons[idx].label);
                }
                else if (desktop_icons[idx].type == 6) {
                    const char* dot = strstr(desktop_icons[idx].label, ".");
                    if (dot && (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".bmp") == 0)) {
                        user_profile_t* up = user_get_current();
                        const char* uname = (up && up->username[0]) ? up->username : "Morgan";
                        char wp_path[128];
                        mini_snprintf(wp_path, sizeof(wp_path), "/USER/%s/DESKTOP/%s", uname, desktop_icons[idx].label);
                        image_t* img = image_load_from_file(wp_path);
                        if (!img) {
                            mini_snprintf(wp_path, sizeof(wp_path), "C:/USER/%s/DESKTOP/%s", uname, desktop_icons[idx].label);
                            img = image_load_from_file(wp_path);
                        }
                        if (img) {
                            gfx_set_custom_wallpaper(img->pixels, img->width, img->height);
                            image_free(img);
                        }
                    } else {
                        app_notepad_open_file(desktop_icons[idx].label, desktop_icons[idx].content);
                    }
                }
                else if (desktop_icons[idx].type == 7) {
                    app_explorer_open_recycle_bin();
                }
                else if (desktop_icons[idx].type == 8) {
                    app_installer_launch();
                }
                else if (desktop_icons[idx].type == 9) {
                    app_browser_launch();
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
                window_t* w = windows[i];
                if (!w || w->is_minimized) continue;
                if (step == 0 && !w->is_active) continue;
                if (step == 1 && w->is_active) continue;

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
            if (ctx_x + 160 > (int32_t)sw) ctx_x = sw - 165;
            if (ctx_y + 102 > ty) ctx_y = ty - 105;
            ctx_submenu_open = false;
            start_menu_open = false;

            prev_mouse_left = mouse->left_button;
            prev_mouse_right = mouse->right_button;
            return;
        } else if (!on_win && mouse->y >= ty) {
            // Clic destro sulla barra delle applicazioni -> Avvia Gestione attivita' (Task Manager)
            app_taskmgr_launch();
            prev_mouse_left = mouse->left_button;
            prev_mouse_right = mouse->right_button;
            return;
        }
    }

    // Aggiorna stato apertura sottomenu contestuale all'hover del mouse
    if (ctx_menu_open) {
        if (mouse->x >= ctx_x && mouse->x <= ctx_x + 160 && mouse->y >= ctx_y + 3 && mouse->y < ctx_y + 25) {
            ctx_submenu_open = true;
        } else if (ctx_submenu_open) {
            int32_t sm_x = ctx_x + 156;
            int32_t sm_y = ctx_y;
            int32_t sm_w = 165;
            int32_t sm_h = 58;
            if (sm_x + sm_w > (int32_t)sw) sm_x = ctx_x - sm_w + 4;

            if (!(mouse->x >= sm_x && mouse->x <= sm_x + sm_w && mouse->y >= sm_y && mouse->y <= sm_y + sm_h) &&
                !(mouse->x >= ctx_x && mouse->x <= ctx_x + 160 && mouse->y >= ctx_y && mouse->y <= ctx_y + 25)) {
                ctx_submenu_open = false;
            }
        }
    }

    if (mouse_clicked) {
        // Interazione con Menu Contestuale se aperto
        if (ctx_menu_open) {
            int32_t sm_x = ctx_x + 156;
            int32_t sm_y = ctx_y;
            int32_t sm_w = 165;
            int32_t sm_h = 58;
            if (sm_x + sm_w > (int32_t)sw) sm_x = ctx_x - sm_w + 4;

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

            // Click su voce del menu contestuale principale (altezza 98px)
            if (mouse->x >= ctx_x && mouse->x <= ctx_x + 160 &&
                mouse->y >= ctx_y && mouse->y <= ctx_y + 98) {
                if (mouse->y >= ctx_y + 3 && mouse->y < ctx_y + 25) {
                    ctx_submenu_open = !ctx_submenu_open;
                    prev_mouse_left = mouse->left_button;
                    prev_mouse_right = mouse->right_button;
                    return;
                } else if (mouse->y >= ctx_y + 28 && mouse->y < ctx_y + 49) {
                    wm_rearrange_desktop_icons();
                    ctx_menu_open = false;
                    ctx_submenu_open = false;
                    prev_mouse_left = mouse->left_button;
                    prev_mouse_right = mouse->right_button;
                    return;
                } else if (mouse->y >= ctx_y + 49 && mouse->y < ctx_y + 71) {
                    app_display_props_launch();
                    ctx_menu_open = false;
                    ctx_submenu_open = false;
                    prev_mouse_left = mouse->left_button;
                    prev_mouse_right = mouse->right_button;
                    return;
                } else if (mouse->y >= ctx_y + 71 && mouse->y <= ctx_y + 96) {
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
        if (mouse->x >= 2 && mouse->x <= 62 && mouse->y >= ty + 2 && mouse->y <= ty + 26) {
            start_menu_open = !start_menu_open;
            prev_mouse_left = mouse->left_button;
            prev_mouse_right = mouse->right_button;
            return;
        }

        // Clic dentro il menu Start se aperto
        if (start_menu_open) {
            int32_t mw = 200, mh = 336, mx = 2, my = sh - 28 - mh;
            if (mouse->x >= mx && mouse->x <= mx + mw && mouse->y >= my && mouse->y <= my + mh) {
                int item = (mouse->y - my - 8) / 32;
                if (item == 0) app_browser_launch();
                else if (item == 1) app_cmd_launch();
                else if (item == 2) app_notepad_launch();
                else if (item == 3) app_calc_launch();
                else if (item == 4) app_player_launch();
                else if (item == 5) app_rules_viewer_launch();
                else if (item == 6) app_control_panel_open();
                else if (item == 7) app_regedit_launch();
                else if (item == 8) app_about_launch();
                else if (item == 9) {
                    app_shutdown_dialog_launch();
                }
                start_menu_open = false;
                prev_mouse_left = mouse->left_button;
                prev_mouse_right = mouse->right_button;
                return;
            } else {
                start_menu_open = false;
            }
        }

        // Clic sulla System Tray (icona alimentazione o orologio)
        int32_t tray_w = 110;
        int32_t tray_x = sw - tray_w - 4;
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
                if (mouse->x >= btn_x && mouse->x <= btn_x + bw && mouse->y >= ty + 2 && mouse->y <= ty + 26) {
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
                window_t* w = windows[i];
                if (!w || w->is_minimized) continue;
                if (step == 0 && !w->is_active) continue;
                if (step == 1 && w->is_active) continue;

                if (mouse->x >= w->x && mouse->x <= w->x + w->width &&
                    mouse->y >= w->y && mouse->y <= w->y + w->height) {

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

                    // Clic sul pulsante Riduci a icona [_]
                    int32_t min_x = close_x - 18;
                    int32_t min_y = close_y;
                    if (mouse->x >= min_x && mouse->x <= min_x + 16 &&
                        mouse->y >= min_y && mouse->y <= min_y + 14) {
                        wm_toggle_minimize(w->id);
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

    // Inoltro tasti della tastiera alla finestra attiva
    if (key != 0 && active_window_id != -1) {
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (windows[i] && windows[i]->id == active_window_id && !windows[i]->is_minimized) {
                if (windows[i]->on_key) {
                    windows[i]->on_key(windows[i], key);
                }
                break;
            }
        }
    }

    prev_mouse_left = mouse->left_button;
    prev_mouse_right = mouse->right_button;
}

void wm_on_resolution_changed(uint32_t old_w, uint32_t old_h, uint32_t new_w, uint32_t new_h) {
    (void)old_w; (void)old_h;
    int32_t sw = (int32_t)new_w;
    int32_t sh = (int32_t)new_h;

    // Chiude menu a comparsa per evitare disallineamenti grafici
    start_menu_open = false;
    ctx_menu_open = false;
    ctx_submenu_open = false;

    // Riorganizza e adatta automaticamente le icone del desktop alla nuova risoluzione
    wm_rearrange_desktop_icons();

    // Ricalibra posizione e dimensioni delle finestre
    for (int i = 0; i < MAX_WINDOWS; i++) {
        window_t* win = windows[i];
        if (!win) continue;

        // Se la finestra sfora i nuovi margini, riposizionala nell'area visibile
        if (win->x + win->width > sw) {
            win->x = sw - win->width;
            if (win->x < 0) {
                win->x = 0;
                if (win->width > sw) win->width = sw;
            }
        }
        if (win->y + win->height > sh - 28) {
            win->y = (sh - 28) - win->height;
            if (win->y < 0) {
                win->y = 0;
                if (win->height > sh - 28) win->height = sh - 28;
            }
        }
    }
}
