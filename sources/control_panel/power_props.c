#include "control_panel.h"
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

static void power_props_draw_tabs(int32_t cx, int32_t cy, int32_t cw, int active_tab) {
    const char* tab_labels[] = { "  Schemi energia  ", "  Alimentazione  " };
    int32_t tx = cx + 8;
    int32_t ty = cy + 4;
    int32_t tw = 120;
    int32_t th = 20;

    for (int i = 0; i < 2; i++) {
        int32_t bx = tx + i * (tw + 4);
        bool sel = (i == active_tab);
        gfx_fill_rect(bx, ty, tw, th + (sel ? 2 : 0), sel ? COLOR_BTNFACE : COLOR_BTNSHADOW);
        gfx_draw_bevel_rect(bx, ty, tw, th, sel);
        gfx_draw_string(bx + 8, ty + 2, tab_labels[i],
                        sel ? COLOR_WINDOWTEXT : COLOR_BTNHIGHLIGHT, 0, true);
    }

    gfx_fill_rect(cx + 4, ty + th, cw - 8, 2, COLOR_BTNSHADOW);
    gfx_fill_rect(cx + 4, ty + th + 2, cw - 8, 1, COLOR_BTNHIGHLIGHT);
}

static void power_props_draw(window_t* win) {
    power_props_state_t* state = (power_props_state_t*)win->app_data;
    if (!state) return;

    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    // Sfondo finestra grigio standard
    gfx_fill_rect(cx, cy, cw, ch, COLOR_BTNFACE);

    // Schede superiori
    power_props_draw_tabs(cx, cy, cw, state->active_tab);

    if (state->active_tab == 0) {
        // ─── TAB 0: SCHEMI COMBINAZIONI ENERGETICHE ───
        gfx_draw_string(cx + 14, cy + 32, "Seleziona la combinazione di risparmio energia:", COLOR_WINDOWTEXT, 0, true);

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
            gfx_fill_rect(cx + 14, py, cw - 28, 40, is_sel ? 0xDCEBFA : COLOR_BTNFACE);
            gfx_draw_bevel_rect(cx + 14, py, cw - 28, 40, is_sel);

            // Radio button simulato
            gfx_draw_bevel_rect(cx + 22, py + 12, 14, 14, true);
            if (is_sel) {
                gfx_fill_rect(cx + 25, py + 15, 8, 8, 0x0A246A);
            }

            gfx_draw_string(cx + 42, py + 6, profiles[i], is_sel ? 0x0A246A : COLOR_WINDOWTEXT, 0, true);
            gfx_draw_string(cx + 42, py + 22, p_desc[i], 0x555555, 0, true);
        }

        // Groupbox: Spegnimento schermo
        int32_t box_y = cy + 196;
        int32_t box_h = 92;
        gfx_draw_bevel_rect(cx + 14, box_y, cw - 28, box_h, true);
        gfx_fill_rect(cx + 24, box_y - 6, 170, 12, COLOR_BTNFACE);
        gfx_draw_string(cx + 26, box_y - 6, " Standby Monitor (DPMS) ", COLOR_WINDOWTEXT, 0, true);

        gfx_draw_string(cx + 24, box_y + 14, "Spegnimento automatico dello schermo dopo:", COLOR_WINDOWTEXT, 0, true);

        // Pulsanti selettori timeout: 1 min, 3 min, 5 min, Mai
        const char* to_labels[] = { "1 Min", "3 Min", "5 Min", "Mai" };
        const uint32_t to_vals[] = { 60, 180, 300, 0 };

        for (int i = 0; i < 4; i++) {
            int32_t bx = cx + 24 + i * 62;
            int32_t by = box_y + 34;
            bool sel_to = (state->selected_timeout == to_vals[i]);

            gfx_fill_rect(bx, by, 56, 22, sel_to ? 0x0A246A : COLOR_BTNFACE);
            gfx_draw_bevel_rect(bx, by, 56, 22, sel_to);
            gfx_draw_string(bx + 10, by + 4, to_labels[i], sel_to ? 0xFFFFFF : COLOR_WINDOWTEXT, 0, true);
        }

        // Pulsante "Standby immediato"
        int32_t st_bx = cx + cw - 160;
        int32_t st_by = box_y + 60;
        gfx_fill_rect(st_bx, st_by, 132, 22, COLOR_BTNFACE);
        gfx_draw_bevel_rect(st_bx, st_by, 132, 22, false);
        gfx_draw_icon_16(st_bx + 4, st_by + 3, ICON_ENERGY);
        gfx_draw_string(st_bx + 24, st_by + 4, "Standby Adesso", COLOR_WINDOWTEXT, 0, true);

    } else {
        // ─── TAB 1: ALIMENTAZIONE & STATO BATTERIA ───
        int32_t gy = cy + 34;

        gfx_draw_icon_32(cx + 20, gy + 4, ICON_ENERGY);
        gfx_draw_string(cx + 62, gy + 4, "Stato di alimentazione del sistema", COLOR_WINDOWTEXT, 0, true);
        gfx_draw_string(cx + 62, gy + 22, "Alimentazione da rete elettrica CA: COLLEGATA", 0x008000, 0, true);

        // Scheda informativa
        int32_t info_y = cy + 78;
        gfx_draw_bevel_rect(cx + 14, info_y, cw - 28, 204, true);

        gfx_draw_string(cx + 26, info_y + 14, "Livello carica batteria principale:", COLOR_WINDOWTEXT, 0, true);

        // Barra di carica della batteria stile Windows 2000
        int32_t bar_x = cx + 26;
        int32_t bar_y = info_y + 34;
        int32_t bar_w = cw - 52;
        int32_t bar_h = 24;

        gfx_draw_bevel_rect(bar_x, bar_y, bar_w, bar_h, true);
        gfx_fill_rect(bar_x + 2, bar_y + 2, bar_w - 4, bar_h - 4, 0xFFFFFF);

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

        gfx_draw_string(cx + 26, info_y + 74, "Dettagli Hardware e CPU Governor:", 0x0A246A, 0, true);
        gfx_draw_string(cx + 26, info_y + 96, "  - Modalita' CPU Idle: Istruzione HLT x86_64 nativa", COLOR_WINDOWTEXT, 0, true);
        gfx_draw_string(cx + 26, info_y + 116, "  - Risparmio termico Host: Attivo (carico 0% in idle)", COLOR_WINDOWTEXT, 0, true);
        gfx_draw_string(cx + 26, info_y + 136, "  - ACPI Poweroff: Porte 0x604 (QEMU) / 0x4004 (VBox)", COLOR_WINDOWTEXT, 0, true);
        gfx_draw_string(cx + 26, info_y + 156, idle_str, 0x444444, 0, true);
        gfx_draw_string(cx + 26, info_y + 176, "  - Stato batteria: Ottimale (Tampone di emergenza pronto)", 0x008000, 0, true);
    }

    // ─── PULSANTI DI CONTROLLO INFERIORI ───
    int32_t btn_y = cy + ch - 30;

    // [ Applica ]
    int32_t ap_x = cx + cw - 250;
    gfx_fill_rect(ap_x, btn_y, 74, 23, COLOR_BTNFACE);
    gfx_draw_bevel_rect(ap_x, btn_y, 74, 23, false);
    gfx_draw_string(ap_x + 12, btn_y + 4, "Applica", COLOR_WINDOWTEXT, 0, true);

    // [ OK ]
    int32_t ok_x = cx + cw - 170;
    gfx_fill_rect(ok_x, btn_y, 74, 23, COLOR_BTNFACE);
    gfx_draw_bevel_rect(ok_x, btn_y, 74, 23, false);
    gfx_draw_string(ok_x + 24, btn_y + 4, "OK", COLOR_WINDOWTEXT, 0, true);

    // [ Annulla ]
    int32_t can_x = cx + cw - 90;
    gfx_fill_rect(can_x, btn_y, 74, 23, COLOR_BTNFACE);
    gfx_draw_bevel_rect(can_x, btn_y, 74, 23, false);
    gfx_draw_string(can_x + 12, btn_y + 4, "Annulla", COLOR_WINDOWTEXT, 0, true);
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
    if (ly >= cy + 4 && ly <= cy + 24) {
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
    window_t* win = wm_create_window("Proprieta' - Opzioni risparmio energia", 160, 80, 430, 365, APP_POWER);
    if (!win) return;

    power_props_state_t* state = (power_props_state_t*)kmalloc(sizeof(power_props_state_t));
    if (!state) return;

    power_status_t cur = power_get_status();
    state->active_tab = 0;
    state->selected_profile = cur.profile;
    state->selected_timeout = cur.screen_timeout_sec;

    win->app_data = state;
    win->on_draw = power_props_draw;
    win->on_mouse = power_props_mouse;
    win->on_key = NULL;
}


// ═══════════════════════════════════════════════════════════════════════════
// 2. DIALOG BOX MODAL: "CHIUDI SESSIONE DI NEWOS V1" (ARRESTA IL SISTEMA)
// ═══════════════════════════════════════════════════════════════════════════

typedef struct {
    int selected_action; // 0 = Arresta il sistema, 1 = Riavvia il sistema, 2 = Standby
} shutdown_state_t;

static void shutdown_draw(window_t* win) {
    shutdown_state_t* state = (shutdown_state_t*)win->app_data;
    if (!state) return;

    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    // Sfondo finestra grigio chiaro
    gfx_fill_rect(cx, cy, cw, ch, COLOR_BTNFACE);

    // Icona alimentazione grande a sinistra
    gfx_draw_icon_32(cx + 16, cy + 14, ICON_ENERGY);

    // Domanda centrale
    gfx_draw_string(cx + 60, cy + 14, "Cosa desideri far fare al computer?", COLOR_WINDOWTEXT, 0, true);

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
        gfx_draw_bevel_rect(cx + 60, oy + 1, 14, 14, true);
        if (is_sel) {
            gfx_fill_rect(cx + 63, oy + 4, 8, 8, 0x0A246A);
        }

        gfx_draw_string(cx + 82, oy + 1, actions[i], is_sel ? 0x0A246A : COLOR_WINDOWTEXT, 0, true);
    }

    // Box descrittivo dell'azione scelta
    int32_t desc_y = cy + 120;
    gfx_draw_bevel_rect(cx + 14, desc_y, cw - 28, 42, true);
    gfx_fill_rect(cx + 16, desc_y + 2, cw - 32, 38, 0xFFFFFF);

    const char* desc_txt = "";
    if (state->selected_action == 0) {
        desc_txt = "Salva i dati e spegne l'alimentazione del computer in sicurezza.";
    } else if (state->selected_action == 1) {
        desc_txt = "Chiude la sessione e riavvia il sistema operativo NewOS V1 64-bit.";
    } else {
        desc_txt = "Mette il monitor in standby e attiva il risparmio energetico della CPU.";
    }
    gfx_draw_string(cx + 24, desc_y + 14, desc_txt, 0x222222, 0, true);

    // Pulsanti inferiori: [ OK ] e [ Annulla ]
    int32_t btn_y = cy + ch - 30;

    int32_t ok_x = cx + cw - 170;
    gfx_fill_rect(ok_x, btn_y, 74, 23, COLOR_BTNFACE);
    gfx_draw_bevel_rect(ok_x, btn_y, 74, 23, false);
    gfx_draw_string(ok_x + 26, btn_y + 4, "OK", COLOR_WINDOWTEXT, 0, true);

    int32_t can_x = cx + cw - 90;
    gfx_fill_rect(can_x, btn_y, 74, 23, COLOR_BTNFACE);
    gfx_draw_bevel_rect(can_x, btn_y, 74, 23, false);
    gfx_draw_string(can_x + 12, btn_y + 4, "Annulla", COLOR_WINDOWTEXT, 0, true);
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

    // Clic sui pulsanti
    int32_t btn_y = cy + ch - 30;
    int32_t ok_x = cx + cw - 170;
    int32_t can_x = cx + cw - 90;

    // [ OK ]
    if (lx >= ok_x && lx <= ok_x + 74 && ly >= btn_y && ly <= btn_y + 23) {
        int action = state->selected_action;
        wm_destroy_window(win->id);

        if (action == 0) {
            power_shutdown();
        } else if (action == 1) {
            power_reboot();
        } else if (action == 2) {
            power_enter_screen_standby();
        }
        return;
    }

    // [ Annulla ]
    if (lx >= can_x && lx <= can_x + 74 && ly >= btn_y && ly <= btn_y + 23) {
        wm_destroy_window(win->id);
        return;
    }
}

void app_shutdown_dialog_launch(void) {
    logoutscreen_result_t res = screen_mgr_run_logoutscreen();
    if (res == LOGOUT_ACTION_DISCONNECT) {
        loginscreen_result_t lres = screen_mgr_run_loginscreen();
        if (lres == LOGIN_SHUTDOWN) {
            power_shutdown();
        } else if (lres == LOGIN_REBOOT) {
            power_reboot();
        }
        wm_init();
    }
}
