#include "control_panel.h"
#include "cpl_theme.h"
#include <gui/wm.h>
#include <gui/graphics.h>
#include <gui/icons.h>
#include <gui/screen_mgr.h>
#include <drivers/power.h>
#include <mm/heap.h>
#include <string.h>

// ═══════════════════════════════════════════════════════════════════════════
// 1. APPLET PANNELLO DI CONTROLLO: "OPZIONI RISPARMIO ENERGIA"
// ═══════════════════════════════════════════════════════════════════════════

typedef struct {
    int active_tab; // 0 = Schemi, 1 = Batteria & Rete
    power_profile_t selected_profile;
    uint32_t selected_timeout;
} power_props_state_t;

static void power_props_draw_tabs(int32_t cx, int32_t cy, int32_t cw, int active_tab, const cpl_theme_t* t) {
    const char* tab_labels[] = { "Schemi energia", "Alimentazione" };
    int32_t tx = cx + 8;
    int32_t ty = cy + 4;
    int32_t tw = 120;
    int32_t th = 22;

    for (int i = 0; i < 2; i++) {
        int32_t bx = tx + i * (tw + 4);
        bool sel = (i == active_tab);
        wm_draw_themed_tab(bx, ty, tw, th, tab_labels[i], sel);
    }

    cpl_draw_tabs_divider(cx, ty, th, cw, t);
}

static void power_props_draw(window_t* win) {
    power_props_state_t* state = (power_props_state_t*)win->app_data;
    if (!state) return;

    cpl_theme_t theme;
    cpl_theme_init(&theme);

    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    // Sfondo finestra coordinato con il tema attivo
    cpl_draw_dialog_bg(win, &theme, cx, cy, cw, ch);

    // Schede superiori
    power_props_draw_tabs(cx, cy, cw, state->active_tab, &theme);

    if (state->active_tab == 0) {
        // ─── TAB 0: SCHEMI COMBINAZIONI ENERGETICHE ───
        gfx_draw_string(cx + 14, cy + 32, "Seleziona la combinazione di risparmio energia:", theme.text_primary, 0, true);

        const char* profiles[] = {
            "Bilanciato (consigliato)",
            "Risparmio di energia",
            "Prestazioni elevate"
        };
        const char* p_desc[] = {
            "Bilancia prestazioni e consumi. Throttling CPU dopo 5s di inattivita'.",
            "Minimizza consumo energetico e calore. Throttling rapido dopo 2s.",
            "Massima frequenza grafica e fluidita' continua senza throttling."
        };

        for (int i = 0; i < 3; i++) {
            int32_t py = cy + 54 + i * 44;
            bool is_sel = (state->selected_profile == (power_profile_t)i);

            // Riquadro opzione
            if (is_sel) {
                if (theme.is_classic) {
                    gfx_fill_rect(cx + 14, py, cw - 28, 40, theme.sel_bg);
                } else {
                    gfx_fill_rounded_rect(cx + 14, py, cw - 28, 40, theme.control_radius, theme.sel_bg);
                }
                gfx_draw_string(cx + 42, py + 6, profiles[i], theme.sel_text, 0, true);
                gfx_draw_string(cx + 42, py + 22, p_desc[i], theme.sel_text, 0, true);
            } else {
                if (theme.is_classic) {
                    gfx_fill_rect(cx + 14, py, cw - 28, 40, theme.input_bg);
                    gfx_draw_bevel_rect(cx + 14, py, cw - 28, 40, true);
                } else {
                    gfx_fill_rounded_rect(cx + 14, py, cw - 28, 40, theme.control_radius, theme.card_bg);
                    gfx_draw_rounded_rect(cx + 14, py, cw - 28, 40, theme.control_radius, theme.card_border);
                }
                gfx_draw_string(cx + 42, py + 6, profiles[i], theme.text_primary, 0, true);
                gfx_draw_string(cx + 42, py + 22, p_desc[i], theme.text_secondary, 0, true);
            }

            // Radio button simulato
            if (theme.is_classic) {
                gfx_draw_bevel_rect(cx + 22, py + 12, 14, 14, true);
                if (is_sel) gfx_fill_rect(cx + 25, py + 15, 8, 8, theme.sel_text);
            } else {
                gfx_draw_rounded_rect(cx + 22, py + 12, 14, 14, 7, is_sel ? theme.sel_text : theme.card_border);
                if (is_sel) gfx_fill_rounded_rect(cx + 25, py + 15, 8, 8, 4, theme.sel_text);
            }
        }

        // Groupbox: Spegnimento schermo
        int32_t box_y = cy + 196;
        int32_t box_h = 92;
        cpl_draw_groupbox(cx + 14, box_y, cw - 28, box_h, " Standby Monitor (DPMS) ", &theme);

        gfx_draw_string(cx + 24, box_y + 14, "Spegnimento automatico dello schermo dopo:", theme.text_primary, 0, true);

        // Pulsanti selettori timeout: 1 min, 3 min, 5 min, Mai
        const char* to_labels[] = { "1 Min", "3 Min", "5 Min", "Mai" };
        const uint32_t to_vals[] = { 60, 180, 300, 0 };

        for (int i = 0; i < 4; i++) {
            int32_t bx = cx + 24 + i * 62;
            int32_t by = box_y + 34;
            bool sel_to = (state->selected_timeout == to_vals[i]);
            wm_draw_themed_button(bx, by, 56, 22, to_labels[i], sel_to);
        }

        // Pulsante "Standby immediato"
        int32_t st_bx = cx + cw - 160;
        int32_t st_by = box_y + 60;
        wm_draw_themed_button(st_bx, st_by, 132, 22, "Standby Adesso", false);
        gfx_draw_icon_16(st_bx + 4, st_by + 3, ICON_ENERGY);

    } else {
        // ─── TAB 1: ALIMENTAZIONE & STATO BATTERIA ───
        int32_t gy = cy + 34;

        gfx_draw_icon_32(cx + 20, gy + 4, ICON_ENERGY);
        gfx_draw_string(cx + 62, gy + 4, "Stato di alimentazione del sistema", theme.accent, 0, true);
        uint32_t ac_green = theme.is_dark ? 0x2ECC71 : 0x008000;
        gfx_draw_string(cx + 62, gy + 22, "Alimentazione da rete elettrica CA: COLLEGATA", ac_green, 0, true);

        // Scheda informativa
        int32_t info_y = cy + 78;
        cpl_draw_groupbox(cx + 14, info_y, cw - 28, 204, " Dettagli Alimentazione ", &theme);

        gfx_draw_string(cx + 26, info_y + 14, "Livello carica batteria principale:", theme.text_primary, 0, true);

        // Barra di carica della batteria
        int32_t bar_x = cx + 26;
        int32_t bar_y = info_y + 34;
        int32_t bar_w = cw - 52;
        int32_t bar_h = 24;

        gfx_fill_rect(bar_x, bar_y, bar_w, bar_h, theme.input_bg);
        if (theme.is_classic) {
            gfx_draw_bevel_rect(bar_x, bar_y, bar_w, bar_h, true);
        } else {
            gfx_draw_rounded_rect(bar_x, bar_y, bar_w, bar_h, 3, theme.input_border);
        }

        // Segmenti verdi di carica 100%
        int num_blocks = 18;
        int block_w = (bar_w - 8) / num_blocks;
        for (int b = 0; b < num_blocks; b++) {
            gfx_fill_rect(bar_x + 4 + b * block_w, bar_y + 4, block_w - 2, bar_h - 8, 0x107C10);
        }

        gfx_draw_string(bar_x + bar_w / 2 - 32, bar_y + 5, "100% Carica", 0xFFFFFF, 0, true);

        // Statistiche Governor ed Efficienza
        uint32_t idle_sec = power_get_idle_seconds();
        char idle_str[64];
        mini_snprintf(idle_str, sizeof(idle_str), "Tempo di inattivita' registrato: %d secondi", idle_sec);

        gfx_draw_string(cx + 26, info_y + 74, "Dettagli Hardware e CPU Governor:", theme.accent, 0, true);
        gfx_draw_string(cx + 26, info_y + 96, "  - Modalita' CPU Idle: Istruzione HLT x86_64 nativa", theme.text_primary, 0, true);
        gfx_draw_string(cx + 26, info_y + 116, "  - Risparmio termico Host: Attivo (carico 0% in idle)", theme.text_secondary, 0, true);
        gfx_draw_string(cx + 26, info_y + 136, "  - ACPI Poweroff: Porte 0x604 (QEMU) / 0x4004 (VBox)", theme.text_secondary, 0, true);
        gfx_draw_string(cx + 26, info_y + 156, idle_str, theme.text_secondary, 0, true);
        gfx_draw_string(cx + 26, info_y + 176, "  - Stato batteria: Ottimale (Tampone di emergenza pronto)", ac_green, 0, true);
    }

    // ─── PULSANTI DI CONTROLLO INFERIORI ───
    int32_t btn_y = cy + ch - 30;

    int32_t ap_x = cx + cw - 250;
    int32_t ok_x = cx + cw - 170;
    int32_t can_x = cx + cw - 90;

    wm_draw_themed_button(ap_x, btn_y, 74, 23, "Applica", false);
    wm_draw_themed_button(ok_x, btn_y, 74, 23, "OK", false);
    wm_draw_themed_button(can_x, btn_y, 74, 23, "Annulla", false);
}

static void power_props_mouse(window_t* win, int32_t lx, int32_t ly, bool clicked) {
    if (!clicked) return;
    power_props_state_t* state = (power_props_state_t*)win->app_data;
    if (!state) return;

    int32_t cx = 4;
    int32_t cy = 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    // Clic sulle schede
    if (ly >= cy + 4 && ly <= cy + 26) {
        if (lx >= cx + 8 && lx < cx + 130) {
            state->active_tab = 0;
            return;
        } else if (lx >= cx + 132 && lx < cx + 256) {
            state->active_tab = 1;
            return;
        }
    }

    if (state->active_tab == 0) {
        // Clic sui profili
        for (int i = 0; i < 3; i++) {
            int32_t py = cy + 54 + i * 44;
            if (lx >= cx + 14 && lx <= cx + cw - 14 && ly >= py && ly <= py + 40) {
                state->selected_profile = (power_profile_t)i;
                if (i == 0) state->selected_timeout = 180;
                else if (i == 1) state->selected_timeout = 60;
                else if (i == 2) state->selected_timeout = 0;
                return;
            }
        }

        // Clic sui bottoni timeout: 1 min, 3 min, 5 min, Mai
        int32_t box_y = cy + 196;
        const uint32_t to_vals[] = { 60, 180, 300, 0 };
        for (int i = 0; i < 4; i++) {
            int32_t bx = cx + 24 + i * 62;
            int32_t by = box_y + 34;
            if (lx >= bx && lx <= bx + 56 && ly >= by && ly <= by + 22) {
                state->selected_timeout = to_vals[i];
                return;
            }
        }

        // Clic sul pulsante "Standby Adesso"
        int32_t st_bx = cx + cw - 160;
        int32_t st_by = box_y + 60;
        if (lx >= st_bx && lx <= st_bx + 132 && ly >= st_by && ly <= st_by + 22) {
            power_enter_screen_standby();
            return;
        }
    }

    // Clic sui pulsanti inferiori
    int32_t btn_y = cy + ch - 30;
    int32_t ap_x = cx + cw - 250;
    int32_t ok_x = cx + cw - 170;
    int32_t can_x = cx + cw - 90;

    // [ Applica ]
    if (lx >= ap_x && lx <= ap_x + 74 && ly >= btn_y && ly <= btn_y + 23) {
        power_set_profile(state->selected_profile);
        power_set_screen_timeout(state->selected_timeout);
        return;
    }

    // [ OK ]
    if (lx >= ok_x && lx <= ok_x + 74 && ly >= btn_y && ly <= btn_y + 23) {
        power_set_profile(state->selected_profile);
        power_set_screen_timeout(state->selected_timeout);
        wm_destroy_window(win->id);
        return;
    }

    // [ Annulla ]
    if (lx >= can_x && lx <= can_x + 74 && ly >= btn_y && ly <= btn_y + 23) {
        wm_destroy_window(win->id);
        return;
    }
}

void app_power_props_launch(void) {
    window_t* win = wm_create_window("Proprieta' - Risparmio energia", 120, 80, 460, 390, APP_POWER);
    if (!win) return;

    power_props_state_t* state = (power_props_state_t*)kmalloc(sizeof(power_props_state_t));
    if (state) {
        state->active_tab = 0;
        power_status_t ps = power_get_status();
        state->selected_profile = ps.profile;
        state->selected_timeout = ps.screen_timeout_sec;

        win->app_data = state;
        win->on_draw = power_props_draw;
        win->on_mouse = power_props_mouse;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. DIALOG DI ARRESTO SISTEMA (SHUTDOWN DIALOG)
// ═══════════════════════════════════════════════════════════════════════════

typedef struct {
    int selected_action; // 0 = Spegni, 1 = Riavvia, 2 = Sospendi
} shutdown_state_t;

static void shutdown_draw(window_t* win) {
    shutdown_state_t* state = (shutdown_state_t*)win->app_data;
    if (!state) return;

    cpl_theme_t theme;
    cpl_theme_init(&theme);

    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    cpl_draw_dialog_bg(win, &theme, cx, cy, cw, ch);

    // Icona alimentazione grande a sinistra
    gfx_draw_icon_32(cx + 16, cy + 14, ICON_ENERGY);

    // Domanda centrale
    gfx_draw_string(cx + 60, cy + 14, "Cosa desideri far fare al computer?", theme.accent, 0, true);

    // Opzioni radio
    const char* actions[] = {
        "Arresta il sistema (Spegne il computer)",
        "Riavvia il sistema",
        "Sospendi (Modalita' Standby a basso consumo)"
    };

    for (int i = 0; i < 3; i++) {
        int32_t oy = cy + 40 + i * 26;
        bool is_sel = (state->selected_action == i);

        // Cerchietto radio
        if (theme.is_classic) {
            gfx_draw_bevel_rect(cx + 60, oy + 1, 14, 14, true);
            if (is_sel) {
                gfx_fill_rect(cx + 63, oy + 4, 8, 8, theme.accent);
            }
        } else {
            gfx_draw_rounded_rect(cx + 60, oy + 1, 14, 14, 7, is_sel ? theme.accent : theme.card_border);
            if (is_sel) {
                gfx_fill_rounded_rect(cx + 63, oy + 4, 8, 8, 4, theme.accent);
            }
        }

        gfx_draw_string(cx + 82, oy + 1, actions[i], is_sel ? theme.accent : theme.text_primary, 0, true);
    }

    // Box descrittivo dell'azione scelta
    int32_t desc_y = cy + 120;
    gfx_fill_rect(cx + 16, desc_y + 2, cw - 32, 38, theme.input_bg);
    if (theme.is_classic) {
        gfx_draw_bevel_rect(cx + 14, desc_y, cw - 28, 42, true);
    } else {
        gfx_draw_rounded_rect(cx + 14, desc_y, cw - 28, 42, theme.control_radius, theme.card_border);
    }

    const char* desc_txt = "";
    if (state->selected_action == 0) {
        desc_txt = "Salva i dati e spegne l'alimentazione del computer in sicurezza.";
    } else if (state->selected_action == 1) {
        desc_txt = "Chiude la sessione e riavvia il sistema operativo NewOS V1 64-bit.";
    } else {
        desc_txt = "Mette il monitor in standby e attiva il risparmio energetico della CPU.";
    }
    gfx_draw_string(cx + 24, desc_y + 14, desc_txt, theme.text_primary, 0, true);

    // Pulsanti inferiori: [ OK ] e [ Annulla ]
    int32_t btn_y = cy + ch - 30;
    int32_t ok_x = cx + cw - 170;
    int32_t can_x = cx + cw - 90;

    wm_draw_themed_button(ok_x, btn_y, 74, 23, "OK", false);
    wm_draw_themed_button(can_x, btn_y, 74, 23, "Annulla", false);
}

static void shutdown_mouse(window_t* win, int32_t lx, int32_t ly, bool clicked) {
    if (!clicked) return;
    shutdown_state_t* state = (shutdown_state_t*)win->app_data;
    if (!state) return;

    int32_t cx = 4;
    int32_t cy = 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    // Clic sulle 3 opzioni radio
    for (int i = 0; i < 3; i++) {
        int32_t oy = cy + 40 + i * 26;
        if (lx >= cx + 60 && lx <= cx + cw - 14 && ly >= oy && ly <= oy + 22) {
            state->selected_action = i;
            return;
        }
    }

    // Pulsanti inferiori
    int32_t btn_y = cy + ch - 30;
    int32_t ok_x = cx + cw - 170;
    int32_t can_x = cx + cw - 90;

    // Clic su [ OK ]
    if (lx >= ok_x && lx <= ok_x + 74 && ly >= btn_y && ly <= btn_y + 23) {
        int act = state->selected_action;
        wm_destroy_window(win->id);

        if (act == 0) {
            power_shutdown();
        } else if (act == 1) {
            power_reboot();
        } else if (act == 2) {
            power_enter_screen_standby();
        }
        return;
    }

    // Clic su [ Annulla ]
    if (lx >= can_x && lx <= can_x + 74 && ly >= btn_y && ly <= btn_y + 23) {
        wm_destroy_window(win->id);
        return;
    }
}

void app_shutdown_dialog_launch(void) {
    window_t* win = wm_create_window("Fine della sessione di lavoro", 180, 160, 410, 220, APP_SHUTDOWN);
    if (!win) return;

    shutdown_state_t* state = (shutdown_state_t*)kmalloc(sizeof(shutdown_state_t));
    if (state) {
        state->selected_action = 0; // Predefinito: Spegni
        win->app_data = state;
        win->on_draw = shutdown_draw;
        win->on_mouse = shutdown_mouse;
    }
}
