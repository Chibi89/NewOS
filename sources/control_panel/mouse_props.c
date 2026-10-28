/**
 * control_panel/mouse_props.c
 * Applet Proprietà Mouse (MAIN.CPL) per NewOS V1 (64-bit)
 * Gestione e configurazione di puntatori personalizzati, schemi (.CUR/.ANI), pulsanti e velocità
 * Conforme alla gerarchia di sistema e pienamente adattata all'estetica del tema visivo attivo
 * (C) Newspoint Software - NewSoft - NewOS V1
 */

#include "control_panel.h"
#include "cpl_theme.h"
#include <gui/wm.h>
#include <gui/graphics.h>
#include <gui/icons.h>
#include <gui/cursor.h>
#include <gui/cursor_loader.h>
#include <drivers/timer.h>
#include <mm/heap.h>
#include <string.h>

typedef struct {
    int      active_tab;            // 0: Puntatori, 1: Pulsanti, 2: Opzioni puntatore, 3: Hardware
    int      selected_scheme_idx;   // Schema cursore selezionato
    int      selected_role_row;     // Riga del ruolo selezionato nella lista (0..8)
    int      list_scroll_idx;       // Offset scorrimento lista ruoli

    // Opzioni Puntatori
    bool     pointer_shadow;        // Ombreggiatura cursore a 32-bit (sempre attiva nel renderer)
    bool     allow_theme_change;    // Consenti ai temi di modificare i cursori

    // Pulsanti
    bool     swap_buttons;          // Inverti tasti (mancini)
    int      double_click_speed;    // 1..10 (default 5)
    bool     folder_open;           // Stato cartellina test doppio clic
    uint64_t last_click_tick;       // Tick dell'ultimo clic per rilevamento doppio clic

    // Opzioni Puntatore (Movimento e Visibilità)
    int      pointer_speed;         // 1..10 (default 5)
    bool     enhance_precision;     // Precisione puntatore attiva
    bool     pointer_trails;        // Tracce puntatore
    bool     hide_while_typing;     // Nascondi durante digitazione
    bool     show_ctrl_loc;         // Mostra posizione con CTRL
} mouse_props_state_t;

static const struct {
    const char* label;
    int32_t     width;
} g_mouse_tabs[] = {
    { "Puntatori", 88 },
    { "Pulsanti", 80 },
    { "Opzioni puntatore", 152 },
    { "Hardware", 80 }
};

static const struct {
    cursor_system_state_t role;
    const char*           name;
} g_preview_roles[] = {
    { CURSOR_STATE_NORMAL,    "Selezione normale" },
    { CURSOR_STATE_HELP,      "Selezione Guida" },
    { CURSOR_STATE_WORKING,   "Attivita' in background" },
    { CURSOR_STATE_BUSY,      "Occupato" },
    { CURSOR_STATE_CROSS,     "Selezione precisione" },
    { CURSOR_STATE_IBEAM,     "Selezione testo" },
    { CURSOR_STATE_RESIZE_NS, "Ridimensionamento vert." },
    { CURSOR_STATE_RESIZE_EW, "Ridimensionamento orizz." },
    { CURSOR_STATE_LINK,      "Selezione collegamento" }
};
#define NUM_PREVIEW_ROLES ((int)(sizeof(g_preview_roles) / sizeof(g_preview_roles[0])))

static void draw_tab_pointers(window_t* win, mouse_props_state_t* state, int32_t cx, int32_t cy, int32_t cw, int32_t ch, const cpl_theme_t* t) {
    (void)win;
    (void)ch;

    // 1. Riquadro "Schema"
    int32_t box1_x = cx + 8;
    int32_t box1_y = cy + 32;
    int32_t box1_w = cw - 16;
    int32_t box1_h = 58;

    cpl_draw_groupbox(box1_x, box1_y, box1_w, box1_h, " Schema ", t);

    // Box selezione schema (Combobox temizzata)
    int32_t combo_x = box1_x + 12;
    int32_t combo_y = box1_y + 16;
    int32_t combo_w = 260;
    int32_t combo_h = 24;

    gfx_fill_rect(combo_x, combo_y, combo_w, combo_h, t->input_bg);
    if (t->is_classic) {
        gfx_draw_bevel_rect(combo_x, combo_y, combo_w, combo_h, true);
    } else {
        gfx_draw_rounded_rect(combo_x, combo_y, combo_w, combo_h, t->control_radius, t->input_border);
    }

    int total_schemes = cursor_mgr_get_scheme_count();
    const cursor_scheme_info_t* sc = cursor_mgr_get_scheme_info(state->selected_scheme_idx);
    const char* sc_name = sc ? sc->name : "Windows Aero (Default)";

    gfx_draw_string(combo_x + 8, combo_y + 5, sc_name, t->text_primary, 0, true);

    // Bottoni [ < ] e [ > ] per cambiare schema
    int32_t btn_prev_x = combo_x + combo_w + 6;
    int32_t btn_next_x = btn_prev_x + 26;
    wm_draw_themed_button(btn_prev_x, combo_y, 22, combo_h, "<", false);
    wm_draw_themed_button(btn_next_x, combo_y, 22, combo_h, ">", false);

    // Bottone [ Predefinito ]
    int32_t btn_def_x = btn_next_x + 28;
    int32_t btn_def_w = 100;
    wm_draw_themed_button(btn_def_x, combo_y, btn_def_w, combo_h, "Predefinito", false);

    // 2. Riquadro "Personalizza"
    int32_t box2_x = cx + 8;
    int32_t box2_y = box1_y + box1_h + 10;
    int32_t box2_w = cw - 16;
    int32_t box2_h = 210;

    cpl_draw_groupbox(box2_x, box2_y, box2_w, box2_h, " Personalizza ", t);

    // Tabella lista ruoli con anteprima del cursore
    int32_t list_x = box2_x + 10;
    int32_t list_y = box2_y + 14;
    int32_t list_w = box2_w - 20;
    int32_t list_h = 155;

    gfx_fill_rect(list_x, list_y, list_w, list_h, t->input_bg);
    if (t->is_classic) {
        gfx_draw_bevel_rect(list_x, list_y, list_w, list_h, true);
    } else {
        gfx_draw_rounded_rect(list_x, list_y, list_w, list_h, t->control_radius, t->input_border);
    }

    int visible_rows = 5;
    for (int r = 0; r < visible_rows; r++) {
        int idx = state->list_scroll_idx + r;
        if (idx >= NUM_PREVIEW_ROLES) break;

        int32_t ry = list_y + 2 + r * 30;
        bool is_sel = (idx == state->selected_role_row);

        if (is_sel) {
            gfx_fill_rect(list_x + 2, ry, list_w - 4, 28, t->sel_bg);
            gfx_draw_string(list_x + 48, ry + 7, g_preview_roles[idx].name, t->sel_text, 0, true);
        } else {
            uint32_t alt_bg = (r % 2 == 1) ? (t->is_dark ? 0x222228 : 0xF7F9FC) : t->input_bg;
            gfx_fill_rect(list_x + 2, ry, list_w - 4, 28, alt_bg);
            gfx_draw_string(list_x + 48, ry + 7, g_preview_roles[idx].name, t->text_primary, 0, true);
        }

        // Disegna anteprima reale del cursore tramite CURSORMGR
        cursor_mgr_draw(list_x + 22, ry + 14, g_preview_roles[idx].role);
    }

    // Bottoni scorrimento lista su / giu
    int32_t btn_up_x = list_x + list_w - 26;
    int32_t btn_up_y = box2_y + box2_h - 32;
    int32_t btn_dn_x = btn_up_x - 30;

    wm_draw_themed_button(btn_dn_x, btn_up_y, 24, 22, "v", false);
    wm_draw_themed_button(btn_up_x, btn_up_y, 24, 22, "^", false);

    // Contatore schemi rilevati
    char cnt_str[48];
    mini_snprintf(cnt_str, sizeof(cnt_str), "Schema %d di %d", state->selected_scheme_idx + 1, total_schemes > 0 ? total_schemes : 1);
    gfx_draw_string(box2_x + 14, box2_y + box2_h - 26, cnt_str, t->text_secondary, 0, true);

    // 3. Checkbox opzioni puntatore
    int32_t chk_y = box2_y + box2_h + 10;
    const char* chk_shadow_str = state->pointer_shadow ? "[x] Abilita ombra alpha del puntatore" : "[ ] Abilita ombra alpha del puntatore";
    const char* chk_theme_str  = state->allow_theme_change ? "[x] Consenti ai temi di modificare i puntatori" : "[ ] Consenti ai temi di modificare i puntatori";
    gfx_draw_string(cx + 12, chk_y, chk_shadow_str, t->text_primary, 0, true);
    gfx_draw_string(cx + 12, chk_y + 20, chk_theme_str, t->text_primary, 0, true);
}

static void draw_tab_buttons(window_t* win, mouse_props_state_t* state, int32_t cx, int32_t cy, int32_t cw, int32_t ch, const cpl_theme_t* t) {
    (void)win;
    (void)ch;

    // 1. Riquadro Configurazione dei pulsanti
    int32_t box1_x = cx + 8;
    int32_t box1_y = cy + 32;
    int32_t box1_w = cw - 16;
    int32_t box1_h = 135;

    cpl_draw_groupbox(box1_x, box1_y, box1_w, box1_h, " Configurazione dei pulsanti ", t);

    const char* chk_swap = state->swap_buttons ? "[x] Inverti pulsante secondario e primario" : "[ ] Inverti pulsante secondario e primario";
    gfx_draw_string(box1_x + 14, box1_y + 18, chk_swap, t->text_primary, 0, true);
    gfx_draw_string(box1_x + 14, box1_y + 40, "Selezionare questa casella per impostare il pulsante destro", t->text_secondary, 0, true);
    gfx_draw_string(box1_x + 14, box1_y + 56, "come primario per le funzioni di selezione e trascinamento.", t->text_secondary, 0, true);

    // Disegno mouse stilizzato
    int32_t mouse_w = 64;
    int32_t mouse_h = 80;
    int32_t mouse_x = box1_x + box1_w - mouse_w - 24;
    int32_t mouse_y = box1_y + 24;

    uint32_t mouse_body = t->is_dark ? 0x383842 : 0xD0D0D0;
    gfx_fill_rect(mouse_x, mouse_y, mouse_w, mouse_h, mouse_body);
    if (t->is_classic) {
        gfx_draw_bevel_rect(mouse_x, mouse_y, mouse_w, mouse_h, false);
    } else {
        gfx_draw_rounded_rect(mouse_x, mouse_y, mouse_w, mouse_h, 6, t->card_border);
    }

    // Tasto sinistro
    uint32_t left_col = state->swap_buttons ? mouse_body : t->accent;
    gfx_fill_rect(mouse_x + 4, mouse_y + 4, 26, 32, left_col);
    if (t->is_classic) {
        gfx_draw_bevel_rect(mouse_x + 4, mouse_y + 4, 26, 32, !state->swap_buttons);
    }

    // Tasto destro
    uint32_t right_col = state->swap_buttons ? t->accent : mouse_body;
    gfx_fill_rect(mouse_x + 34, mouse_y + 4, 26, 32, right_col);
    if (t->is_classic) {
        gfx_draw_bevel_rect(mouse_x + 34, mouse_y + 4, 26, 32, state->swap_buttons);
    }

    // Rotellina centrale
    gfx_fill_rect(mouse_x + 28, mouse_y + 12, 8, 16, t->is_dark ? 0x1A1A20 : 0x404040);

    // 2. Riquadro Velocità doppio clic
    int32_t box2_x = cx + 8;
    int32_t box2_y = box1_y + box1_h + 14;
    int32_t box2_w = cw - 16;
    int32_t box2_h = 135;

    cpl_draw_groupbox(box2_x, box2_y, box2_w, box2_h, " Velocita' doppio clic ", t);

    gfx_draw_string(box2_x + 14, box2_y + 18, "Fare doppio clic sulla cartella per testare l'impostazione.", t->text_primary, 0, true);

    // Slider velocità
    int32_t sl_x = box2_x + 14;
    int32_t sl_y = box2_y + 52;
    int32_t sl_w = box2_w - 140;
    gfx_draw_string(sl_x, sl_y - 14, "Lenta", t->text_secondary, 0, true);
    gfx_draw_string(sl_x + sl_w - 48, sl_y - 14, "Veloce", t->text_secondary, 0, true);

    gfx_fill_rect(sl_x, sl_y, sl_w, 6, t->input_bg);
    if (t->is_classic) {
        gfx_draw_bevel_rect(sl_x, sl_y, sl_w, 6, true);
    } else {
        gfx_draw_rounded_rect(sl_x, sl_y, sl_w, 6, 2, t->card_border);
    }

    int32_t thumb_x = sl_x + ((sl_w - 14) * (state->double_click_speed - 1)) / 9;
    wm_draw_themed_button(thumb_x, sl_y - 6, 14, 18, "", false);

    // Bottoni [ - ] e [ + ]
    int32_t btn_sub_x = sl_x + sl_w + 10;
    int32_t btn_add_x = btn_sub_x + 24;
    wm_draw_themed_button(btn_sub_x, sl_y - 8, 22, 22, "-", false);
    wm_draw_themed_button(btn_add_x, sl_y - 8, 22, 22, "+", false);

    // Riquadro cartellina interattiva di test
    int32_t fold_box_x = box2_x + box2_w - 80;
    int32_t fold_box_y = box2_y + 42;
    gfx_fill_rect(fold_box_x, fold_box_y, 60, 60, t->input_bg);
    if (t->is_classic) {
        gfx_draw_bevel_rect(fold_box_x, fold_box_y, 60, 60, true);
    } else {
        gfx_draw_rounded_rect(fold_box_x, fold_box_y, 60, 60, t->control_radius, t->card_border);
    }

    gfx_draw_icon_32(fold_box_x + 14, fold_box_y + 8, state->folder_open ? ICON_FOLDER_VOID : ICON_FOLDER);
    const char* fold_lbl = state->folder_open ? "Aperta" : "Chiusa";
    gfx_draw_string(fold_box_x + 8, fold_box_y + 44, fold_lbl, t->accent, 0, true);
}

static void draw_tab_options(window_t* win, mouse_props_state_t* state, int32_t cx, int32_t cy, int32_t cw, int32_t ch, const cpl_theme_t* t) {
    (void)win;
    (void)ch;

    // 1. Riquadro Movimento
    int32_t box1_x = cx + 8;
    int32_t box1_y = cy + 32;
    int32_t box1_w = cw - 16;
    int32_t box1_h = 120;

    cpl_draw_groupbox(box1_x, box1_y, box1_w, box1_h, " Movimento ", t);

    gfx_draw_string(box1_x + 14, box1_y + 18, "Selezionare la velocita' del puntatore:", t->text_primary, 0, true);

    int32_t sl_x = box1_x + 14;
    int32_t sl_y = box1_y + 52;
    int32_t sl_w = box1_w - 100;
    gfx_draw_string(sl_x, sl_y - 14, "Lenta", t->text_secondary, 0, true);
    gfx_draw_string(sl_x + sl_w - 48, sl_y - 14, "Veloce", t->text_secondary, 0, true);

    gfx_fill_rect(sl_x, sl_y, sl_w, 6, t->input_bg);
    if (t->is_classic) {
        gfx_draw_bevel_rect(sl_x, sl_y, sl_w, 6, true);
    } else {
        gfx_draw_rounded_rect(sl_x, sl_y, sl_w, 6, 2, t->card_border);
    }

    int32_t thumb_x = sl_x + ((sl_w - 14) * (state->pointer_speed - 1)) / 9;
    wm_draw_themed_button(thumb_x, sl_y - 6, 14, 18, "", false);

    // Bottoni [ - ] e [ + ]
    int32_t btn_sub_x = sl_x + sl_w + 10;
    int32_t btn_add_x = btn_sub_x + 24;
    wm_draw_themed_button(btn_sub_x, sl_y - 8, 22, 22, "-", false);
    wm_draw_themed_button(btn_add_x, sl_y - 8, 22, 22, "+", false);

    const char* chk_prec = state->enhance_precision ? "[x] Aumenta precisione del puntatore" : "[ ] Aumenta precisione del puntatore";
    gfx_draw_string(box1_x + 14, box1_y + 82, chk_prec, t->text_primary, 0, true);

    // 2. Riquadro Visibilità
    int32_t box2_x = cx + 8;
    int32_t box2_y = box1_y + box1_h + 14;
    int32_t box2_w = cw - 16;
    int32_t box2_h = 120;

    cpl_draw_groupbox(box2_x, box2_y, box2_w, box2_h, " Visibilita' ", t);

    const char* chk_trails = state->pointer_trails ? "[x] Visualizza tracce del puntatore" : "[ ] Visualizza tracce del puntatore";
    gfx_draw_string(box2_x + 14, box2_y + 20, chk_trails, t->text_primary, 0, true);

    const char* chk_hide = state->hide_while_typing ? "[x] Nascondi il puntatore durante la digitazione" : "[ ] Nascondi il puntatore durante la digitazione";
    gfx_draw_string(box2_x + 14, box2_y + 48, chk_hide, t->text_primary, 0, true);

    const char* chk_ctrl = state->show_ctrl_loc ? "[x] Mostra la posizione del puntatore premendo CTRL" : "[ ] Mostra la posizione del puntatore premendo CTRL";
    gfx_draw_string(box2_x + 14, box2_y + 76, chk_ctrl, t->text_primary, 0, true);
}

static void draw_tab_hardware(window_t* win, mouse_props_state_t* state, int32_t cx, int32_t cy, int32_t cw, int32_t ch, const cpl_theme_t* t) {
    (void)win;
    (void)state;
    (void)ch;

    // 1. Riquadro Dispositivi
    int32_t box1_x = cx + 8;
    int32_t box1_y = cy + 32;
    int32_t box1_w = cw - 16;
    int32_t box1_h = 135;

    cpl_draw_groupbox(box1_x, box1_y, box1_w, box1_h, " Dispositivi ", t);

    // Scheda del mouse attivo
    gfx_draw_icon_32(box1_x + 16, box1_y + 20, ICON_MOUSE);
    gfx_draw_string(box1_x + 60, box1_y + 18, "Mouse compatibile PS/2", t->accent, 0, true);
    gfx_draw_string(box1_x + 60, box1_y + 36, "Tipo: Mouse e altre periferiche di puntamento", t->text_primary, 0, true);
    gfx_draw_string(box1_x + 60, box1_y + 54, "Produttore: Controller standard NewSoft PS/2", t->text_secondary, 0, true);
    gfx_draw_string(box1_x + 60, box1_y + 72, "Ubicazione: Porta ausiliaria controller 8042 (IRQ 12)", t->text_secondary, 0, true);

    // 2. Riquadro Stato dispositivo
    int32_t box2_x = cx + 8;
    int32_t box2_y = box1_y + box1_h + 14;
    int32_t box2_w = cw - 16;
    int32_t box2_h = 80;

    cpl_draw_groupbox(box2_x, box2_y, box2_w, box2_h, " Stato del dispositivo ", t);

    uint32_t ok_green = t->is_dark ? 0x2ECC71 : 0x137333;
    gfx_draw_string(box2_x + 14, box2_y + 22, "Il dispositivo funziona correttamente.", ok_green, 0, true);
    gfx_draw_string(box2_x + 14, box2_y + 44, "Driver caricato e attivo: PS2MOUSE.SYS (IRQ 12 - I/O 0x60)", t->text_secondary, 0, true);

    // Bottone Proprietà
    int32_t btn_prop_x = box1_x + box1_w - 100;
    int32_t btn_prop_y = box2_y + box2_h + 14;
    int32_t btn_prop_w = 94;
    int32_t btn_prop_h = 24;
    wm_draw_themed_button(btn_prop_x, btn_prop_y, btn_prop_w, btn_prop_h, "Proprieta'", false);
}

static void mouse_props_draw(window_t* win) {
    mouse_props_state_t* state = (mouse_props_state_t*)win->app_data;
    if (!state) return;

    cpl_theme_t theme;
    cpl_theme_init(&theme);

    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    // Sfondo generale del dialog coordinato al tema attivo
    cpl_draw_dialog_bg(win, &theme, cx, cy, cw, ch);

    // Disegno delle schede (Tabs)
    int32_t cur_x = cx + 8;
    int32_t ty = cy + 4;
    int32_t th = 22;

    for (int i = 0; i < 4; i++) {
        int32_t tw = g_mouse_tabs[i].width;
        bool sel = (i == state->active_tab);
        wm_draw_themed_tab(cur_x, ty, tw, th, g_mouse_tabs[i].label, sel);
        cur_x += tw + 4;
    }

    // Linea divisoria schede
    cpl_draw_tabs_divider(cx, ty, th, cw, &theme);

    // Rendering contenuto della scheda attiva
    if (state->active_tab == 0) {
        draw_tab_pointers(win, state, cx, cy, cw, ch, &theme);
    } else if (state->active_tab == 1) {
        draw_tab_buttons(win, state, cx, cy, cw, ch, &theme);
    } else if (state->active_tab == 2) {
        draw_tab_options(win, state, cx, cy, cw, ch, &theme);
    } else if (state->active_tab == 3) {
        draw_tab_hardware(win, state, cx, cy, cw, ch, &theme);
    }

    // Pulsanti in basso: [ OK ] [ Annulla ] [ Applica ]
    int32_t btn_w = 78;
    int32_t btn_h = 24;
    int32_t btn_y = cy + ch - 32;

    int32_t apply_x = cx + cw - btn_w - 12;
    int32_t cancel_x = apply_x - btn_w - 8;
    int32_t ok_x = cancel_x - btn_w - 8;

    wm_draw_themed_button(ok_x, btn_y, btn_w, btn_h, "OK", false);
    wm_draw_themed_button(cancel_x, btn_y, btn_w, btn_h, "Annulla", false);
    wm_draw_themed_button(apply_x, btn_y, btn_w, btn_h, "Applica", false);
}

static void mouse_props_click(window_t* win, int32_t mx, int32_t my, bool clicked) {
    if (!clicked) return;
    mouse_props_state_t* state = (mouse_props_state_t*)win->app_data;
    if (!state) return;

    int32_t cx = 4;
    int32_t cy = 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    // 1. Clic sulle schede (Tabs)
    int32_t cur_x = cx + 8;
    int32_t ty = cy + 4;
    int32_t th = 22;
    for (int i = 0; i < 4; i++) {
        int32_t tw = g_mouse_tabs[i].width;
        if (mx >= cur_x && mx <= cur_x + tw && my >= ty && my <= ty + th) {
            state->active_tab = i;
            return;
        }
        cur_x += tw + 4;
    }

    // 2. Clic sui pulsanti in basso
    int32_t btn_w = 78;
    int32_t btn_h = 24;
    int32_t btn_y = cy + ch - 32;
    int32_t apply_x = cx + cw - btn_w - 12;
    int32_t cancel_x = apply_x - btn_w - 8;
    int32_t ok_x = cancel_x - btn_w - 8;

    // Clic OK
    if (mx >= ok_x && mx <= ok_x + btn_w && my >= btn_y && my <= btn_y + btn_h) {
        cursor_mgr_set_active_scheme(state->selected_scheme_idx);
        wm_destroy_window(win->id);
        return;
    }

    // Clic Annulla
    if (mx >= cancel_x && mx <= cancel_x + btn_w && my >= btn_y && my <= btn_y + btn_h) {
        wm_destroy_window(win->id);
        return;
    }

    // Clic Applica
    if (mx >= apply_x && mx <= apply_x + btn_w && my >= btn_y && my <= btn_y + btn_h) {
        cursor_mgr_set_active_scheme(state->selected_scheme_idx);
        return;
    }

    // 3. Interazioni all'interno della scheda Puntatori
    if (state->active_tab == 0) {
        int32_t box1_x = cx + 8;
        int32_t box1_y = cy + 32;
        int32_t combo_x = box1_x + 12;
        int32_t combo_y = box1_y + 16;
        int32_t combo_w = 260;
        int32_t combo_h = 24;

        int32_t btn_prev_x = combo_x + combo_w + 6;
        int32_t btn_next_x = btn_prev_x + 26;
        int32_t btn_def_x = btn_next_x + 28;
        int32_t btn_def_w = 100;

        int total_schemes = cursor_mgr_get_scheme_count();

        // Clic [ < ] (Schema precedente)
        if (mx >= btn_prev_x && mx <= btn_prev_x + 22 && my >= combo_y && my <= combo_y + combo_h) {
            if (state->selected_scheme_idx > 0) {
                state->selected_scheme_idx--;
                cursor_mgr_set_active_scheme(state->selected_scheme_idx);
            }
            return;
        }

        // Clic [ > ] (Schema successivo)
        if (mx >= btn_next_x && mx <= btn_next_x + 22 && my >= combo_y && my <= combo_y + combo_h) {
            if (state->selected_scheme_idx < total_schemes - 1) {
                state->selected_scheme_idx++;
                cursor_mgr_set_active_scheme(state->selected_scheme_idx);
            }
            return;
        }

        // Clic [ Predefinito ]
        if (mx >= btn_def_x && mx <= btn_def_x + btn_def_w && my >= combo_y && my <= combo_y + combo_h) {
            state->selected_scheme_idx = 0;
            cursor_mgr_set_active_scheme(0);
            return;
        }

        // Clic sulle frecce su/giu della lista ruoli
        int32_t box2_x = cx + 8;
        int32_t box2_y = box1_y + 58 + 10;
        int32_t box2_w = cw - 16;
        int32_t box2_h = 210;
        int32_t list_x = box2_x + 10;
        int32_t list_w = box2_w - 20;

        int32_t btn_up_x = list_x + list_w - 26;
        int32_t btn_up_y = box2_y + box2_h - 32;
        int32_t btn_dn_x = btn_up_x - 30;

        // Scroll giù
        if (mx >= btn_dn_x && mx <= btn_dn_x + 24 && my >= btn_up_y && my <= btn_up_y + 22) {
            if (state->list_scroll_idx + 5 < NUM_PREVIEW_ROLES) {
                state->list_scroll_idx++;
            }
            return;
        }

        // Scroll su
        if (mx >= btn_up_x && mx <= btn_up_x + 24 && my >= btn_up_y && my <= btn_up_y + 22) {
            if (state->list_scroll_idx > 0) {
                state->list_scroll_idx--;
            }
            return;
        }

        // Selezione riga nella lista
        int32_t list_y = box2_y + 14;
        for (int r = 0; r < 5; r++) {
            int32_t ry = list_y + 2 + r * 30;
            if (mx >= list_x && mx <= list_x + list_w && my >= ry && my <= ry + 28) {
                state->selected_role_row = state->list_scroll_idx + r;
                return;
            }
        }

        // Clic su checkbox opzioni puntatore
        int32_t chk_y = box2_y + box2_h + 10;
        if (mx >= cx + 12 && mx <= cx + 380 && my >= chk_y - 2 && my <= chk_y + 16) {
            state->pointer_shadow = !state->pointer_shadow;
            return;
        }
        if (mx >= cx + 12 && mx <= cx + 420 && my >= chk_y + 18 && my <= chk_y + 36) {
            state->allow_theme_change = !state->allow_theme_change;
            return;
        }
    }
    // 4. Interazioni all'interno della scheda Pulsanti
    else if (state->active_tab == 1) {
        int32_t box1_x = cx + 8;
        int32_t box1_y = cy + 32;

        // Clic su "Inverti pulsante secondario e primario"
        if (mx >= box1_x + 14 && mx <= box1_x + 360 && my >= box1_y + 14 && my <= box1_y + 32) {
            state->swap_buttons = !state->swap_buttons;
            return;
        }

        // Clic sui bottoni [ - ] e [ + ] della velocità doppio clic
        int32_t box2_y = box1_y + 135 + 14;
        int32_t box2_w = cw - 16;
        int32_t sl_x = box1_x + 14;
        int32_t sl_y = box2_y + 52;
        int32_t sl_w = box2_w - 140;
        int32_t btn_sub_x = sl_x + sl_w + 10;
        int32_t btn_add_x = btn_sub_x + 24;

        if (mx >= btn_sub_x && mx <= btn_sub_x + 22 && my >= sl_y - 8 && my <= sl_y + 14) {
            if (state->double_click_speed > 1) state->double_click_speed--;
            return;
        }
        if (mx >= btn_add_x && mx <= btn_add_x + 22 && my >= sl_y - 8 && my <= sl_y + 14) {
            if (state->double_click_speed < 10) state->double_click_speed++;
            return;
        }

        // Clic sulla cartellina di test
        int32_t fold_box_x = box1_x + box2_w - 80;
        int32_t fold_box_y = box2_y + 42;
        if (mx >= fold_box_x && mx <= fold_box_x + 60 && my >= fold_box_y && my <= fold_box_y + 60) {
            uint64_t now = timer_get_ticks();
            uint64_t delta = now - state->last_click_tick;
            state->last_click_tick = now;

            // Rilevamento doppio clic: intervallo dinamico da 25 a 70 tick (250..700ms)
            uint64_t threshold = 20 + (10 - state->double_click_speed) * 6;
            if (delta > 0 && delta <= threshold) {
                state->folder_open = !state->folder_open;
            }
            return;
        }
    }
    // 5. Interazioni all'interno della scheda Opzioni Puntatore
    else if (state->active_tab == 2) {
        int32_t box1_x = cx + 8;
        int32_t box1_y = cy + 32;
        int32_t box1_w = cw - 16;
        int32_t sl_x = box1_x + 14;
        int32_t sl_y = box1_y + 52;
        int32_t sl_w = box1_w - 100;
        int32_t btn_sub_x = sl_x + sl_w + 10;
        int32_t btn_add_x = btn_sub_x + 24;

        if (mx >= btn_sub_x && mx <= btn_sub_x + 22 && my >= sl_y - 8 && my <= sl_y + 14) {
            if (state->pointer_speed > 1) state->pointer_speed--;
            return;
        }
        if (mx >= btn_add_x && mx <= btn_add_x + 22 && my >= sl_y - 8 && my <= sl_y + 14) {
            if (state->pointer_speed < 10) state->pointer_speed++;
            return;
        }

        // Checkbox precisione
        if (mx >= box1_x + 14 && mx <= box1_x + 320 && my >= box1_y + 76 && my <= box1_y + 98) {
            state->enhance_precision = !state->enhance_precision;
            return;
        }

        // Checkbox visibilità
        int32_t box2_y = box1_y + 120 + 14;
        if (mx >= box1_x + 14 && mx <= box1_x + 320 && my >= box2_y + 16 && my <= box2_y + 36) {
            state->pointer_trails = !state->pointer_trails;
            return;
        }
        if (mx >= box1_x + 14 && mx <= box1_x + 400 && my >= box2_y + 44 && my <= box2_y + 64) {
            state->hide_while_typing = !state->hide_while_typing;
            return;
        }
        if (mx >= box1_x + 14 && mx <= box1_x + 420 && my >= box2_y + 72 && my <= box2_y + 92) {
            state->show_ctrl_loc = !state->show_ctrl_loc;
            return;
        }
    }
}

void app_mouse_props_launch(void) {
    window_t* win = wm_create_window("Proprieta' - Mouse", 180, 80, 490, 485, APP_MOUSE);
    if (!win) return;

    mouse_props_state_t* state = (mouse_props_state_t*)kmalloc(sizeof(mouse_props_state_t));
    if (state) {
        memset(state, 0, sizeof(mouse_props_state_t));
        state->active_tab = 0; // Inizia dalla scheda Puntatori
        state->selected_scheme_idx = cursor_mgr_get_active_scheme_index();
        state->selected_role_row = 0;
        state->list_scroll_idx = 0;
        state->pointer_shadow = true;
        state->allow_theme_change = true;
        state->double_click_speed = 5;
        state->pointer_speed = 5;
        state->enhance_precision = true;
        state->show_ctrl_loc = true;

        win->app_data = state;
        win->on_draw = mouse_props_draw;
        win->on_mouse = mouse_props_click;
    }
}
