#include <apps/regedit.h>
#include <fs/registry.h>
#include <gui/wm.h>
#include <gui/graphics.h>
#include <gui/icons.h>
#include <drivers/vga.h>
#include <mm/heap.h>
#include <string.h>

typedef enum {
    REGEDIT_TAB_KEYS = 0,
    REGEDIT_TAB_AUDIT
} regedit_tab_t;

typedef struct {
    regedit_tab_t current_tab;
    char selected_path[REG_MAX_PATH_LEN];
    int selected_val_idx;
    int scroll_tree;
    int scroll_values;
    int scroll_audit;

    // Stato modale Modifica
    bool edit_dialog_open;
    char edit_val_name[REG_MAX_NAME_LEN];
    char edit_buffer[REG_MAX_VAL_LEN];
    char old_buffer[REG_MAX_VAL_LEN];
    reg_type_t edit_type;

    // Stato modale Alert di Sicurezza
    bool security_alert_open;
    reg_risk_t security_risk;

    // Stato feedback
    char status_msg[96];
} regedit_state_t;

// Lista lineare dei nodi visibili dell'albero per click e navigazione
typedef struct {
    reg_key_t* key;
    int depth;
    bool has_children;
    bool is_expanded;
} tree_row_t;

static int collect_tree_rows(reg_key_t* k, int depth, tree_row_t* rows, int max_rows, int cur_count) {
    if (!k || cur_count >= max_rows) return cur_count;

    rows[cur_count].key = k;
    rows[cur_count].depth = depth;
    rows[cur_count].has_children = (k->num_subkeys > 0);
    rows[cur_count].is_expanded = k->expanded;
    cur_count++;

    if (k->expanded) {
        for (int i = 0; i < k->num_subkeys; i++) {
            cur_count = collect_tree_rows(k->subkeys[i], depth + 1, rows, max_rows, cur_count);
            if (cur_count >= max_rows) break;
        }
    }
    return cur_count;
}

static void regedit_draw(window_t* win) {
    regedit_state_t* state = (regedit_state_t*)win->app_data;
    if (!state) return;

    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    // 1. Sfondo finestra
    gfx_fill_rect(cx, cy, cw, ch, COLOR_BTNFACE);

    // 2. Tab Bar in alto
    int32_t tab_y = cy + 4;
    int32_t tab_w = 170;
    int32_t tab_h = 24;

    // Tab 1: Registro
    bool tab1_active = (state->current_tab == REGEDIT_TAB_KEYS);
    gfx_fill_rect(cx + 8, tab_y, tab_w, tab_h, tab1_active ? COLOR_BTNFACE : 0xD4D0C8);
    gfx_draw_bevel_rect(cx + 8, tab_y, tab_w, tab_h, !tab1_active);
    gfx_draw_icon_16(cx + 14, tab_y + 4, ICON_MAINTENANCE);
    gfx_draw_string(cx + 34, tab_y + 5, "Chiavi di Registro", tab1_active ? 0x0A246A : 0x444444, 0, true);

    // Tab 2: Audit Log
    bool tab2_active = (state->current_tab == REGEDIT_TAB_AUDIT);
    int32_t tab2_x = cx + 182;
    int32_t tab2_w = 180;
    gfx_fill_rect(tab2_x, tab_y, tab2_w, tab_h, tab2_active ? COLOR_BTNFACE : 0xD4D0C8);
    gfx_draw_bevel_rect(tab2_x, tab_y, tab2_w, tab_h, !tab2_active);
    gfx_draw_icon_16(tab2_x + 6, tab_y + 4, ICON_INFO);
    gfx_draw_string(tab2_x + 26, tab_y + 5, "Audit Log (Sicurezza)", tab2_active ? 0x0A246A : 0x444444, 0, true);

    // Separatore orizzontale
    gfx_fill_rect(cx + 4, tab_y + tab_h, cw - 8, 1, COLOR_BTNHIGHLIGHT);

    if (state->current_tab == REGEDIT_TAB_KEYS) {
        // 3. Barra Indirizzo / Percorso Chiave
        int32_t addr_y = tab_y + tab_h + 4;
        gfx_draw_string(cx + 8, addr_y + 4, "Indirizzo:", COLOR_WINDOWTEXT, 0, true);
        int32_t box_x = cx + 80;
        int32_t box_w = cw - 88 - 180;
        gfx_fill_rect(box_x, addr_y, box_w, 20, 0xFFFFFF);
        gfx_draw_bevel_rect(box_x, addr_y, box_w, 20, true);
        gfx_draw_string(box_x + 4, addr_y + 3, state->selected_path, 0x000080, 0, true);

        // Pulsanti nella Toolbar
        int32_t btn_edit_x = cx + cw - 180;
        gfx_fill_rect(btn_edit_x, addr_y, 85, 20, COLOR_BTNFACE);
        gfx_draw_bevel_rect(btn_edit_x, addr_y, 85, 20, false);
        gfx_draw_string(btn_edit_x + 6, addr_y + 3, "Modifica...", COLOR_WINDOWTEXT, 0, true);

        int32_t btn_save_x = btn_edit_x + 90;
        gfx_fill_rect(btn_save_x, addr_y, 82, 20, COLOR_BTNFACE);
        gfx_draw_bevel_rect(btn_save_x, addr_y, 82, 20, false);
        gfx_draw_string(btn_save_x + 6, addr_y + 3, "Salva su C:", COLOR_WINDOWTEXT, 0, true);

        // 4. Pannello Sinistro: Albero Chiavi (Tree View)
        int32_t tree_x = cx + 8;
        int32_t tree_y = addr_y + 26;
        int32_t tree_w = 210;
        int32_t tree_h = ch - 82;

        gfx_fill_rect(tree_x, tree_y, tree_w, tree_h, 0xFFFFFF);
        gfx_draw_bevel_rect(tree_x, tree_y, tree_w, tree_h, true);

        tree_row_t t_rows[32];
        int total_t_rows = 0;
        for (int r = 0; r < registry_get_root_count(); r++) {
            total_t_rows = collect_tree_rows(registry_get_root(r), 0, t_rows, 32, total_t_rows);
        }

        int item_h = 20;
        for (int i = 0; i < total_t_rows; i++) {
            int32_t ry = tree_y + 4 + i * item_h;
            if (ry + item_h > tree_y + tree_h) break;

            reg_key_t* k = t_rows[i].key;
            int indent = tree_x + 4 + t_rows[i].depth * 14;

            // Disegna quadratino [+] o [-] se ha sottochiavi
            if (t_rows[i].has_children) {
                gfx_fill_rect(indent, ry + 4, 9, 9, 0xFFFFFF);
                gfx_draw_rect(indent, ry + 4, 9, 9, 0x808080);
                gfx_draw_string(indent + 1, ry + 2, t_rows[i].is_expanded ? "-" : "+", 0x000000, 0, true);
            }

            int icon_x = indent + 12;
            gfx_draw_icon_16(icon_x, ry + 2, ICON_FOLDER);

            int text_x = icon_x + 18;
            bool is_sel = (strcasecmp(state->selected_path, k->full_path) == 0);

            if (is_sel) {
                int txt_w = (int)strlen(k->name) * 8 + 6;
                if (text_x + txt_w > tree_x + tree_w - 4) txt_w = tree_x + tree_w - 4 - text_x;
                gfx_fill_rect(text_x - 2, ry + 1, txt_w, 17, 0x0A246A);
                gfx_draw_string(text_x, ry + 2, k->name, 0xFFFFFF, 0, true);
            } else {
                gfx_draw_string(text_x, ry + 2, k->name, COLOR_WINDOWTEXT, 0, true);
            }
        }

        // 5. Pannello Destro: Tabella Valori (List View)
        int32_t val_x = tree_x + tree_w + 6;
        int32_t val_y = tree_y;
        int32_t val_w = cw - tree_w - 20;
        int32_t val_h = tree_h;

        gfx_fill_rect(val_x, val_y, val_w, val_h, 0xFFFFFF);
        gfx_draw_bevel_rect(val_x, val_y, val_w, val_h, true);

        // Header Colonne List View
        int32_t hdr_h = 20;
        int32_t col_nome_w = 145;
        int32_t col_tipo_w = 90;
        gfx_fill_rect(val_x + 1, val_y + 1, val_w - 2, hdr_h, COLOR_BTNFACE);
        gfx_draw_bevel_rect(val_x + 1, val_y + 1, col_nome_w, hdr_h, false);
        gfx_draw_string(val_x + 6, val_y + 3, "Nome", COLOR_WINDOWTEXT, 0, true);

        gfx_draw_bevel_rect(val_x + 1 + col_nome_w, val_y + 1, col_tipo_w, hdr_h, false);
        gfx_draw_string(val_x + 6 + col_nome_w, val_y + 3, "Tipo", COLOR_WINDOWTEXT, 0, true);

        gfx_draw_bevel_rect(val_x + 1 + col_nome_w + col_tipo_w, val_y + 1, val_w - col_nome_w - col_tipo_w - 2, hdr_h, false);
        gfx_draw_string(val_x + 6 + col_nome_w + col_tipo_w, val_y + 3, "Dati", COLOR_WINDOWTEXT, 0, true);

        // Disegna Valori della chiave selezionata
        reg_key_t* cur_k = registry_get_key(state->selected_path);
        if (cur_k) {
            int v_y = val_y + hdr_h + 2;
            for (int v = 0; v < cur_k->num_values; v++) {
                int32_t row_y = v_y + v * 19;
                if (row_y + 18 > val_y + val_h) break;

                bool is_v_sel = (v == state->selected_val_idx);
                if (is_v_sel) {
                    gfx_fill_rect(val_x + 2, row_y, val_w - 4, 18, 0x0A246A);
                }

                uint32_t t_color = is_v_sel ? 0xFFFFFF : COLOR_WINDOWTEXT;

                // Icona tipo
                reg_value_t* val = &cur_k->values[v];
                icon_id_t ic = (val->type == REG_DWORD) ? ICON_INFO : ICON_DOCUMENT;
                gfx_draw_icon_16(val_x + 4, row_y + 1, ic);

                // Nome (troncato pulito se lungo per non sbordare)
                char short_name[20];
                strncpy(short_name, val->name, 14);
                short_name[14] = '\0';
                gfx_draw_string(val_x + 24, row_y + 2, short_name, t_color, 0, true);

                // Tipo
                const char* type_str = (val->type == REG_DWORD) ? "REG_DWORD" : (val->type == REG_BINARY ? "REG_BINARY" : "REG_SZ");
                gfx_draw_string(val_x + 6 + col_nome_w, row_y + 2, type_str, t_color, 0, true);

                // Dati formattati
                char data_buf[64];
                if (val->type == REG_DWORD) {
                    mini_snprintf(data_buf, sizeof(data_buf), "0x%08X (%u)", val->dword_val, val->dword_val);
                } else {
                    mini_snprintf(data_buf, sizeof(data_buf), "\"%s\"", val->str_val);
                }
                gfx_draw_string(val_x + 6 + col_nome_w + col_tipo_w, row_y + 2, data_buf, t_color, 0, true);
            }
        }

    } else {
        // Tab 2: Audit Log (Sicurezza e Audit Trail)
        int32_t log_x = cx + 8;
        int32_t log_y = tab_y + tab_h + 6;
        int32_t log_w = cw - 16;
        int32_t log_h = ch - 58;

        gfx_fill_rect(log_x, log_y, log_w, log_h, 0x000000);
        gfx_draw_bevel_rect(log_x, log_y, log_w, log_h, true);

        gfx_draw_string(log_x + 8, log_y + 6, "=== Registro Operazioni e Audit Trail di NewOS V1 (C:\\SYSTEM\\REGISTRY.LOG) ===", 0x00FF00, 0, true);

        int log_count = registry_get_log_count();
        int max_lines = (log_h - 30) / 16;
        int start_line = (log_count > max_lines) ? (log_count - max_lines) : 0;

        for (int l = 0; l < max_lines && (start_line + l) < log_count; l++) {
            const char* line = registry_get_log_line(start_line + l);
            uint32_t l_col = 0xCCCCCC;
            if (strstr(line, "CRITICO")) l_col = 0xFF5555;
            else if (strstr(line, "MEDIO")) l_col = 0xFFAA55;
            else if (strstr(line, "AGGIUNTA") || strstr(line, "MODIFICA")) l_col = 0x55FF55;

            gfx_draw_string(log_x + 8, log_y + 26 + l * 16, line, l_col, 0, true);
        }
    }

    // 6. Barra di Stato Inferiore
    int32_t sb_y = cy + ch - 20;
    gfx_fill_rect(cx + 8, sb_y, cw - 16, 18, COLOR_BTNFACE);
    gfx_draw_bevel_rect(cx + 8, sb_y, cw - 16, 18, true);

    char sb_text[128];
    reg_key_t* ck = registry_get_key(state->selected_path);
    int num_v = ck ? ck->num_values : 0;
    mini_snprintf(sb_text, sizeof(sb_text), "Percorso: %s | %d valore/i | %s", state->selected_path, num_v, state->status_msg);
    gfx_draw_string(cx + 12, sb_y + 2, sb_text, COLOR_WINDOWTEXT, 0, true);

    // =========================================================================
    // 7. DIALOGO MODALE: MODIFICA VALORE
    // =========================================================================
    if (state->edit_dialog_open && !state->security_alert_open) {
        int32_t dw = 420;
        int32_t dh = 180;
        int32_t dx = cx + (cw - dw) / 2;
        int32_t dy = cy + (ch - dh) / 2;

        gfx_fill_rect(dx, dy, dw, dh, COLOR_BTNFACE);
        gfx_draw_bevel_rect(dx, dy, dw, dh, false);

        // Barra del titolo del dialogo
        gfx_draw_gradient_h(dx + 3, dy + 3, dw - 6, 18, 0x0A246A, 0xA6CAF0);
        gfx_draw_icon_16(dx + 5, dy + 4, ICON_MAINTENANCE);
        gfx_draw_string(dx + 25, dy + 4, "Modifica Valore - NewOS", 0xFFFFFF, 0, true);

        // Etichetta Nome Valore
        gfx_draw_string(dx + 16, dy + 32, "Nome valore:", COLOR_WINDOWTEXT, 0, true);
        gfx_fill_rect(dx + 16, dy + 48, dw - 32, 20, 0xEEEEEE);
        gfx_draw_bevel_rect(dx + 16, dy + 48, dw - 32, 20, true);
        gfx_draw_string(dx + 20, dy + 50, state->edit_val_name, 0x333333, 0, true);

        // Etichetta Dati Valore
        gfx_draw_string(dx + 16, dy + 76, "Dati valore:", COLOR_WINDOWTEXT, 0, true);
        gfx_fill_rect(dx + 16, dy + 92, dw - 32, 22, 0xFFFFFF);
        gfx_draw_bevel_rect(dx + 16, dy + 92, dw - 32, 22, true);
        gfx_draw_string(dx + 20, dy + 95, state->edit_buffer, 0x000000, 0, true);

        // Cursore lampeggiante
        int cur_x = dx + 20 + (int)strlen(state->edit_buffer) * 8;
        gfx_fill_rect(cur_x, dy + 95, 1, 15, 0x000000);

        // Pulsanti OK e Annulla
        int32_t btn_ok_x = dx + dw - 170;
        int32_t btn_canc_x = dx + dw - 85;
        int32_t btn_y = dy + dh - 34;

        gfx_fill_rect(btn_ok_x, btn_y, 75, 24, COLOR_BTNFACE);
        gfx_draw_bevel_rect(btn_ok_x, btn_y, 75, 24, false);
        gfx_draw_string(btn_ok_x + 26, btn_y + 5, "OK", COLOR_WINDOWTEXT, 0, true);

        gfx_fill_rect(btn_canc_x, btn_y, 75, 24, COLOR_BTNFACE);
        gfx_draw_bevel_rect(btn_canc_x, btn_y, 75, 24, false);
        gfx_draw_string(btn_canc_x + 12, btn_y + 5, "Annulla", COLOR_WINDOWTEXT, 0, true);
    }

    // =========================================================================
    // 8. DIALOGO MODALE: AVVISO DI SICUREZZA PREVENTIVO DEL REGISTRO
    // =========================================================================
    if (state->security_alert_open) {
        int32_t aw = 480;
        int32_t ah = 230;
        int32_t ax = cx + (cw - aw) / 2;
        int32_t ay = cy + (ch - ah) / 2;

        gfx_fill_rect(ax, ay, aw, ah, COLOR_BTNFACE);
        gfx_draw_bevel_rect(ax, ay, aw, ah, false);

        // Barra del titolo con gradiente giallo/arancione di attenzione
        gfx_draw_gradient_h(ax + 3, ay + 3, aw - 6, 20, 0xB04000, 0xFF9000);
        gfx_draw_icon_16(ax + 5, ay + 5, ICON_INFO);
        gfx_draw_string(ax + 25, ay + 5, "Avviso di Sicurezza del Registro - NewOS V1", 0xFFFFFF, 0, true);

        // Triangolo giallo con punto esclamativo
        int32_t warn_x = ax + 16;
        int32_t warn_y = ay + 36;
        for (int row = 0; row < 32; row++) {
            int w_span = row;
            gfx_fill_rect(warn_x + 16 - w_span / 2, warn_y + row, w_span, 1, 0xFFCC00);
        }
        gfx_draw_string(warn_x + 13, warn_y + 10, "!", 0x000000, 0, true);

        // Testo di avviso
        gfx_draw_string(ax + 60, ay + 34, "ATTENZIONE: Modifica Parametro di Configurazione", 0x900000, 0, true);
        gfx_draw_string(ax + 60, ay + 50, "La modifica di questo valore impatta direttamente la stabilita'", 0x333333, 0, true);
        gfx_draw_string(ax + 60, ay + 64, "del kernel o delle periferiche hardware di sistema.", 0x333333, 0, true);

        // Riquadro dettagli modifica
        int32_t dt_y = ay + 86;
        gfx_fill_rect(ax + 16, dt_y, aw - 32, 90, 0xFFFFFF);
        gfx_draw_bevel_rect(ax + 16, dt_y, aw - 32, 90, true);

        char line1[96], line2[96], line3[96], line4[96];
        mini_snprintf(line1, sizeof(line1), "Chiave:   %s", state->selected_path);
        mini_snprintf(line2, sizeof(line2), "Valore:   %s", state->edit_val_name);
        mini_snprintf(line3, sizeof(line3), "Attuale:  %s  -->  Nuovo: %s", state->old_buffer, state->edit_buffer);
        mini_snprintf(line4, sizeof(line4), "Rischio:  %s", registry_get_risk_string(state->security_risk));

        gfx_draw_string(ax + 24, dt_y + 8, line1, 0x000080, 0, true);
        gfx_draw_string(ax + 24, dt_y + 26, line2, 0x000000, 0, true);
        gfx_draw_string(ax + 24, dt_y + 44, line3, 0x006600, 0, true);

        uint32_t risk_col = (state->security_risk == REG_RISK_CRITICAL) ? 0xFF0000 : (state->security_risk == REG_RISK_MEDIUM ? 0xFF8000 : 0x008000);
        gfx_draw_string(ax + 24, dt_y + 64, line4, risk_col, 0, true);

        // Pulsanti Conferma Modifica e Annulla
        int32_t btn_conf_w = 150;
        int32_t btn_conf_x = ax + aw - btn_conf_w - 95;
        int32_t btn_ann_x = ax + aw - 85;
        int32_t btn_b_y = ay + ah - 32;

        gfx_fill_rect(btn_conf_x, btn_b_y, btn_conf_w, 24, 0x0A246A);
        gfx_draw_bevel_rect(btn_conf_x, btn_b_y, btn_conf_w, 24, false);
        gfx_draw_string(btn_conf_x + 10, btn_b_y + 5, "Conferma Modifica", 0xFFFFFF, 0, true);

        gfx_fill_rect(btn_ann_x, btn_b_y, 75, 24, COLOR_BTNFACE);
        gfx_draw_bevel_rect(btn_ann_x, btn_b_y, 75, 24, false);
        gfx_draw_string(btn_ann_x + 12, btn_b_y + 5, "Annulla", COLOR_WINDOWTEXT, 0, true);
    }
}

static void open_edit_for_selected(window_t* win) {
    regedit_state_t* state = (regedit_state_t*)win->app_data;
    if (!state) return;

    reg_key_t* k = registry_get_key(state->selected_path);
    if (!k || state->selected_val_idx < 0 || state->selected_val_idx >= k->num_values) return;

    reg_value_t* val = &k->values[state->selected_val_idx];
    strncpy(state->edit_val_name, val->name, sizeof(state->edit_val_name) - 1);
    state->edit_type = val->type;

    if (val->type == REG_DWORD) {
        mini_snprintf(state->edit_buffer, sizeof(state->edit_buffer), "%u", val->dword_val);
        mini_snprintf(state->old_buffer, sizeof(state->old_buffer), "%u", val->dword_val);
    } else {
        strncpy(state->edit_buffer, val->str_val, sizeof(state->edit_buffer) - 1);
        strncpy(state->old_buffer, val->str_val, sizeof(state->old_buffer) - 1);
    }
    state->edit_dialog_open = true;
    state->security_alert_open = false;
}

static void apply_confirmed_modification(window_t* win) {
    regedit_state_t* state = (regedit_state_t*)win->app_data;
    if (!state) return;

    if (state->edit_type == REG_DWORD) {
        uint32_t val = 0;
        const char* p = state->edit_buffer;
        while (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            p++;
        }
        registry_set_value_dword(state->selected_path, state->edit_val_name, val);
    } else {
        registry_set_value_sz(state->selected_path, state->edit_val_name, state->edit_buffer);

        // Se è stata modificata la risoluzione in SYSTEM, applicala dinamicamente!
        if (strstr(state->selected_path, "SYSTEM") && strcasecmp(state->edit_val_name, "CurrentResolution") == 0) {
            uint32_t nw = 1024, nh = 768;
            if (strcasecmp(state->edit_buffer, "800x600") == 0)   { nw = 800;  nh = 600; }
            if (strcasecmp(state->edit_buffer, "1024x768") == 0)  { nw = 1024; nh = 768; }
            if (strcasecmp(state->edit_buffer, "1280x720") == 0)  { nw = 1280; nh = 720; }
            if (strcasecmp(state->edit_buffer, "1280x1024") == 0) { nw = 1280; nh = 1024; }
            if (strcasecmp(state->edit_buffer, "1440x900") == 0)  { nw = 1440; nh = 900; }
            if (strcasecmp(state->edit_buffer, "1920x1080") == 0) { nw = 1920; nh = 1080; }
            vga_set_resolution(nw, nh);
        }
    }

    strcpy(state->status_msg, "Modifica registrata in REGISTRY.LOG.");
    state->security_alert_open = false;
    state->edit_dialog_open = false;
}

static void regedit_mouse(window_t* win, int32_t lx, int32_t ly, bool clicked) {
    regedit_state_t* state = (regedit_state_t*)win->app_data;
    if (!state || !clicked) return;

    int32_t cx = 4;
    int32_t cy = 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    // Se l'alert di sicurezza è aperto, gestisci solo i suoi pulsanti
    if (state->security_alert_open) {
        int32_t aw = 480;
        int32_t ah = 230;
        int32_t ax = cx + (cw - aw) / 2;
        int32_t ay = cy + (ch - ah) / 2;

        int32_t btn_conf_w = 150;
        int32_t btn_conf_x = ax + aw - btn_conf_w - 95;
        int32_t btn_ann_x = ax + aw - 85;
        int32_t btn_b_y = ay + ah - 32;

        // Clic [ Conferma Modifica ]
        if (lx >= btn_conf_x && lx <= btn_conf_x + btn_conf_w && ly >= btn_b_y && ly <= btn_b_y + 24) {
            apply_confirmed_modification(win);
            return;
        }
        // Clic [ Annulla ]
        if (lx >= btn_ann_x && lx <= btn_ann_x + 75 && ly >= btn_b_y && ly <= btn_b_y + 24) {
            state->security_alert_open = false;
            strcpy(state->status_msg, "Modifica annullata dall'utente.");
            return;
        }
        return;
    }

    // Se il dialogo di modifica è aperto
    if (state->edit_dialog_open) {
        int32_t dw = 420;
        int32_t dh = 180;
        int32_t dx = cx + (cw - dw) / 2;
        int32_t dy = cy + (ch - dh) / 2;

        int32_t btn_ok_x = dx + dw - 170;
        int32_t btn_canc_x = dx + dw - 85;
        int32_t btn_y = dy + dh - 34;

        // Clic [ OK ] -> apre alert di sicurezza
        if (lx >= btn_ok_x && lx <= btn_ok_x + 75 && ly >= btn_y && ly <= btn_y + 24) {
            state->security_risk = registry_get_risk_level(state->selected_path, state->edit_val_name);
            state->security_alert_open = true;
            return;
        }
        // Clic [ Annulla ]
        if (lx >= btn_canc_x && lx <= btn_canc_x + 75 && ly >= btn_y && ly <= btn_y + 24) {
            state->edit_dialog_open = false;
            return;
        }
        return;
    }

    // Switch schede Tab
    int32_t tab_y = cy + 4;
    if (ly >= tab_y && ly <= tab_y + 24) {
        if (lx >= cx + 8 && lx <= cx + 178) {
            state->current_tab = REGEDIT_TAB_KEYS;
            return;
        } else if (lx >= cx + 182 && lx <= cx + 362) {
            state->current_tab = REGEDIT_TAB_AUDIT;
            return;
        }
    }

    if (state->current_tab == REGEDIT_TAB_KEYS) {
        int32_t addr_y = tab_y + 28;

        // Clic su pulsante "Modifica..."
        int32_t btn_edit_x = cx + cw - 180;
        if (lx >= btn_edit_x && lx <= btn_edit_x + 85 && ly >= addr_y && ly <= addr_y + 20) {
            open_edit_for_selected(win);
            return;
        }

        // Clic su pulsante "Salva su C:"
        int32_t btn_save_x = btn_edit_x + 90;
        if (lx >= btn_save_x && lx <= btn_save_x + 82 && ly >= addr_y && ly <= addr_y + 20) {
            registry_save_to_disk();
            strcpy(state->status_msg, "Registro salvato su C:\\NewOS\\Registry\\REGISTRY.DAT.");
            return;
        }

        // Clic nel Pannello Sinistro (Tree View)
        int32_t tree_x = cx + 8;
        int32_t tree_y = addr_y + 26;
        int32_t tree_w = 210;
        int32_t tree_h = ch - 82;

        if (lx >= tree_x && lx <= tree_x + tree_w && ly >= tree_y && ly <= tree_y + tree_h) {
            tree_row_t t_rows[32];
            int total_t_rows = 0;
            for (int r = 0; r < registry_get_root_count(); r++) {
                total_t_rows = collect_tree_rows(registry_get_root(r), 0, t_rows, 32, total_t_rows);
            }

            int item_h = 20;
            int idx = (ly - tree_y - 4) / item_h;
            if (idx >= 0 && idx < total_t_rows) {
                reg_key_t* clicked_k = t_rows[idx].key;
                int indent = tree_x + 4 + t_rows[idx].depth * 14;

                // Clic sul quadratino [+] / [-]
                if (t_rows[idx].has_children && lx >= indent && lx <= indent + 12) {
                    clicked_k->expanded = !clicked_k->expanded;
                } else {
                    // Clic sul nodo per selezionarlo
                    strncpy(state->selected_path, clicked_k->full_path, sizeof(state->selected_path) - 1);
                    state->selected_val_idx = 0;
                    strcpy(state->status_msg, "Pronto");
                }
                return;
            }
        }

        // Clic nel Pannello Destro (List View dei Valori)
        int32_t val_x = tree_x + tree_w + 6;
        int32_t val_y = tree_y;
        int32_t val_w = cw - tree_w - 20;
        int32_t val_h = tree_h;

        if (lx >= val_x && lx <= val_x + val_w && ly >= val_y + 20 && ly <= val_y + val_h) {
            reg_key_t* cur_k = registry_get_key(state->selected_path);
            if (cur_k) {
                int v_idx = (ly - val_y - 22) / 19;
                if (v_idx >= 0 && v_idx < cur_k->num_values) {
                    if (state->selected_val_idx == v_idx) {
                        // Doppio clic virtuale: apre subito la modifica
                        open_edit_for_selected(win);
                    } else {
                        state->selected_val_idx = v_idx;
                    }
                    return;
                }
            }
        }
    }
}

static void regedit_key(window_t* win, char c) {
    regedit_state_t* state = (regedit_state_t*)win->app_data;
    if (!state) return;

    // Se l'alert di sicurezza è aperto
    if (state->security_alert_open) {
        if (c == '\n' || c == '\r') {
            apply_confirmed_modification(win);
        } else if (c == 27) { // ESC
            state->security_alert_open = false;
        }
        return;
    }

    // Se il dialogo di modifica è aperto, digita nel buffer
    if (state->edit_dialog_open) {
        size_t len = strlen(state->edit_buffer);
        if (c == '\b') {
            if (len > 0) state->edit_buffer[len - 1] = '\0';
        } else if (c == '\n' || c == '\r') {
            state->security_risk = registry_get_risk_level(state->selected_path, state->edit_val_name);
            state->security_alert_open = true;
        } else if (c == 27) { // ESC
            state->edit_dialog_open = false;
        } else if (c >= 32 && c <= 126 && len < sizeof(state->edit_buffer) - 1) {
            state->edit_buffer[len] = c;
            state->edit_buffer[len + 1] = '\0';
        }
        return;
    }

    // Tasti rapidi nella finestra principale
    if (c == '\t') {
        state->current_tab = (state->current_tab == REGEDIT_TAB_KEYS) ? REGEDIT_TAB_AUDIT : REGEDIT_TAB_KEYS;
        return;
    }
    if (c == '\n' || c == '\r') {
        open_edit_for_selected(win);
    }
}

void app_regedit_launch(void) {
    uint32_t sw = gfx_get_width();
    uint32_t sh = gfx_get_height();

    int32_t dw = 620;
    int32_t dh = 420;
    if (dw > (int32_t)sw - 40) dw = sw - 40;
    if (dh > (int32_t)sh - 60) dh = sh - 60;

    int32_t dx = (sw - dw) / 2;
    int32_t dy = (sh - 28 - dh) / 2;
    if (dy < 10) dy = 10;

    window_t* win = wm_create_window("Editor del Registro di Sistema - NewOS V1 (REGEDIT.EXE)", dx, dy, dw, dh, APP_DEVMGMT);
    if (!win) return;

    regedit_state_t* state = (regedit_state_t*)kmalloc(sizeof(regedit_state_t));
    memset(state, 0, sizeof(regedit_state_t));

    state->current_tab = REGEDIT_TAB_KEYS;
    strcpy(state->selected_path, "HKEY_LOCAL_MACHINE\\SYSTEM");
    state->selected_val_idx = 0;
    strcpy(state->status_msg, "Pronto");

    win->app_data = state;
    win->on_draw = regedit_draw;
    win->on_mouse = regedit_mouse;
    win->on_key = regedit_key;
}
