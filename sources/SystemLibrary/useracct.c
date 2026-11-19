/**
 * SystemLibrary/useracct.c
 * Libreria Dinamica Out-of-Kernel USERACCT.DLL (Applet Account Utente - USERACCT.CPL)
 * Gestione profilo utente attivo, migrazione cartella C:\User\<NomeUtente>,
 * modifica Hostname e configurazione Password/PIN di accesso.
 * (C) Newspoint Software - NewSoft Dynamic System Library
 */

#include "cpl_dll_ui.h"

#ifndef BUILDING_KERNEL_LOADER
__attribute__((section(".header"), used))
module_header_t g_useracct_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size      = 0,
    .entry_offset   = 64,
    .checksum       = 0,
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "USERACCT",
    .signature      = MODULE_AUTH_TAG
};
#endif

static const kernel_api_t* g_api = NULL;

typedef struct {
    int  active_tab;           // 0: Profilo Utente, 1: Sicurezza & Accesso
    int  focused_field;        // Tab 0: 0=Username, 1=Hostname | Tab 1: 0=NewPass, 1=ConfirmPass
    char edit_username[32];
    char edit_hostname[32];
    char edit_newpass[32];
    char edit_confpass[32];
    char status_msg[72];
    bool status_ok;
} useracct_state_t;

static void sync_oobe_ini(const char* username, const char* hostname) {
    if (!g_api || !g_api->vfs_write_entire_file_inplace) return;

    char buf[256];
    int p = 0;
    const char* h1 = "[OOBE]\r\nCompleted=1\r\nVersion=1.0\r\nDefaultUser=";
    while (*h1) buf[p++] = *h1++;
    for (int i = 0; username[i] && p < 200; i++) buf[p++] = username[i];
    const char* h2 = "\r\nHostname=";
    while (*h2) buf[p++] = *h2++;
    for (int i = 0; hostname[i] && p < 240; i++) buf[p++] = hostname[i];
    const char* h3 = "\r\n";
    while (*h3) buf[p++] = *h3++;
    buf[p] = '\0';

    if (!g_api->vfs_exists("C:\\NewOS\\OOBE\\OOBE.INI") && g_api->vfs_create) {
        g_api->vfs_create("C:\\NewOS\\OOBE\\OOBE.INI");
    }
    g_api->vfs_write_entire_file_inplace("C:\\NewOS\\OOBE\\OOBE.INI", buf, (uint32_t)p);
}

static void apply_profile_changes(useracct_state_t* st) {
    if (!g_api || !st) return;
    if (g_api->registry_is_read_only && g_api->registry_is_read_only()) {
        dll_strcpy(st->status_msg, "Errore: Registro in sola lettura!", sizeof(st->status_msg));
        st->status_ok = false;
        return;
    }

    if (st->edit_username[0] == '\0') {
        dll_strcpy(st->status_msg, "Inserire un nome utente valido.", sizeof(st->status_msg));
        st->status_ok = false;
        return;
    }

    user_profile_t* prof = g_api->user_get_current ? g_api->user_get_current() : NULL;
    char old_user[32];
    dll_strcpy(old_user, (prof && prof->username[0]) ? prof->username : "Morgan", sizeof(old_user));
    if (g_api->user_create_or_migrate) {
        g_api->user_create_or_migrate(old_user, st->edit_username);
    }

    if (prof) {
        g_api->user_set_profile(st->edit_username, prof->password,
                                st->edit_hostname[0] ? st->edit_hostname : prof->computer_name,
                                prof->preferred_theme, prof->power_profile, prof->is_installed);
        g_api->user_save_config();
    }

    if (g_api->registry_set_value_sz) {
        char home_path[64];
        dll_strcpy(home_path, "C:\\User\\", sizeof(home_path));
        size_t l = dll_strlen(home_path);
        dll_strcpy(home_path + l, st->edit_username, sizeof(home_path) - l);

        g_api->registry_set_value_sz("HKEY_LOCAL_MACHINE\\SOFTWARE\\NewSoft\\NewOS\\OOBE", "DefaultUser", st->edit_username);
        g_api->registry_set_value_sz("HKEY_CURRENT_USER\\Volatile Environment", "Username", st->edit_username);
        g_api->registry_set_value_sz("HKEY_CURRENT_USER\\Volatile Environment", "UserDesktopPath", home_path);
        if (st->edit_hostname[0] != '\0') {
            g_api->registry_set_value_sz("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\ComputerName", "ComputerName", st->edit_hostname);
        }
        if (g_api->registry_save_to_disk) g_api->registry_save_to_disk();
    }

    sync_oobe_ini(st->edit_username, st->edit_hostname);
    dll_strcpy(st->status_msg, "Profilo e cartella C:\\User aggiornati!", sizeof(st->status_msg));
    st->status_ok = true;
}

static void apply_password_changes(useracct_state_t* st, bool clear_pass) {
    if (!g_api || !st) return;
    user_profile_t* prof = g_api->user_get_current ? g_api->user_get_current() : NULL;
    if (!prof) return;

    if (clear_pass) {
        g_api->user_set_profile(prof->username, "", prof->computer_name,
                                prof->preferred_theme, prof->power_profile, prof->is_installed);
        g_api->user_save_config();
        st->edit_newpass[0] = '\0';
        st->edit_confpass[0] = '\0';
        dll_strcpy(st->status_msg, "Password rimossa (Accesso diretto attivo).", sizeof(st->status_msg));
        st->status_ok = true;
        return;
    }

    if (dll_strcmp(st->edit_newpass, st->edit_confpass) != 0) {
        dll_strcpy(st->status_msg, "Errore: Le password non coincidono!", sizeof(st->status_msg));
        st->status_ok = false;
        return;
    }

    g_api->user_set_profile(prof->username, st->edit_newpass, prof->computer_name,
                            prof->preferred_theme, prof->power_profile, prof->is_installed);
    g_api->user_save_config();
    st->edit_newpass[0] = '\0';
    st->edit_confpass[0] = '\0';
    dll_strcpy(st->status_msg, "Nuova password salvata con successo!", sizeof(st->status_msg));
    st->status_ok = true;
}

static void useracct_draw(window_t* win) {
    useracct_state_t* st = (useracct_state_t*)win->app_data;
    if (!st || !g_api) return;

    dll_theme_t t;
    dll_theme_init(g_api, &t);

    int32_t cx = win->x + 6;
    int32_t cy = win->y + 28;
    int32_t cw = win->width - 12;
    int32_t ch = win->height - 34;

    dll_draw_dialog_bg(g_api, &t, cx, cy, cw, ch);

    // Schede (Tabs)
    int32_t tab_y = cy + 4;
    int32_t tab_h = 22;
    g_api->wm_draw_themed_tab(cx + 8,   tab_y, 130, tab_h, "Profilo Utente", st->active_tab == 0);
    g_api->wm_draw_themed_tab(cx + 140, tab_y, 160, tab_h, "Sicurezza & Accesso", st->active_tab == 1);
    dll_draw_tabs_divider(g_api, cx, tab_y, tab_h, cw, &t);

    user_profile_t* prof = g_api->user_get_current ? g_api->user_get_current() : NULL;
    const char* cur_user = (prof && prof->username[0]) ? prof->username : st->edit_username;
    bool has_pwd = (prof && prof->password[0] != '\0');

    if (st->active_tab == 0) {
        // Box 1: Riepilogo Account Attivo
        int32_t b1_x = cx + 10;
        int32_t b1_y = cy + 36;
        int32_t b1_w = cw - 20;
        int32_t b1_h = 90;
        dll_draw_groupbox(g_api, b1_x, b1_y, b1_w, b1_h, " Account Attivo su NewOS ", &t);

        g_api->gfx_draw_icon_32(b1_x + 16, b1_y + 24, ICON_ACCESSIBILITY);
        g_api->gfx_draw_string(b1_x + 60, b1_y + 18, cur_user, t.accent, 0, true);
        g_api->gfx_draw_string(b1_x + 60, b1_y + 36, "Ruolo: Amministratore di Sistema (Ring 0/3)", t.text_primary, 0, true);

        char home_line[64];
        dll_strcpy(home_line, "Cartella Profilo: C:\\User\\", sizeof(home_line));
        size_t hl = dll_strlen(home_line);
        dll_strcpy(home_line + hl, cur_user, sizeof(home_line) - hl);
        g_api->gfx_draw_string(b1_x + 60, b1_y + 56, home_line, t.text_secondary, 0, true);

        // Box 2: Rinomina Profilo & Hostname
        int32_t b2_x = cx + 10;
        int32_t b2_y = b1_y + b1_h + 12;
        int32_t b2_w = cw - 20;
        int32_t b2_h = 160;
        dll_draw_groupbox(g_api, b2_x, b2_y, b2_w, b2_h, " Modifica Identita' e Migrazione Cartella Utente ", &t);

        g_api->gfx_draw_string(b2_x + 14, b2_y + 20, "Nome Utente (rinomina anche C:\\User\\<Nome>):", t.text_primary, 0, true);
        dll_draw_input_box(g_api, b2_x + 14, b2_y + 38, b2_w - 28, 24, st->edit_username, (st->focused_field == 0), false, &t);

        g_api->gfx_draw_string(b2_x + 14, b2_y + 72, "Nome Computer (Hostname di rete):", t.text_primary, 0, true);
        dll_draw_input_box(g_api, b2_x + 14, b2_y + 90, b2_w - 28, 24, st->edit_hostname, (st->focused_field == 1), false, &t);

        g_api->wm_draw_themed_button(b2_x + 14,  b2_y + 124, 165, 24, "Applica Profilo", false);
        g_api->wm_draw_themed_button(b2_x + 190, b2_y + 124, 185, 24, "Apri Cartella Utente", false);
    } else {
        // Scheda 1: Sicurezza & Accesso
        int32_t b1_x = cx + 10;
        int32_t b1_y = cy + 36;
        int32_t b1_w = cw - 20;
        int32_t b1_h = 74;
        dll_draw_groupbox(g_api, b1_x, b1_y, b1_w, b1_h, " Stato Protezione Sessione (LOGIN.MOD) ", &t);

        g_api->gfx_draw_icon_32(b1_x + 16, b1_y + 20, ICON_REGISTRY);
        if (has_pwd) {
            g_api->gfx_draw_string(b1_x + 60, b1_y + 20, "Stato: PROTETTO DA PASSWORD", 0x107C41, 0, true);
            g_api->gfx_draw_string(b1_x + 60, b1_y + 40, "All'avvio o blocco sessione viene richiesta la password.", t.text_secondary, 0, true);
        } else {
            g_api->gfx_draw_string(b1_x + 60, b1_y + 20, "Stato: ACCESSO RAPIDO (Nessuna password)", t.accent, 0, true);
            g_api->gfx_draw_string(b1_x + 60, b1_y + 40, "Imposta una password qui sotto per proteggere l'account.", t.text_secondary, 0, true);
        }

        int32_t b2_x = cx + 10;
        int32_t b2_y = b1_y + b1_h + 12;
        int32_t b2_w = cw - 20;
        int32_t b2_h = 176;
        dll_draw_groupbox(g_api, b2_x, b2_y, b2_w, b2_h, " Modifica Password o PIN di Accesso ", &t);

        g_api->gfx_draw_string(b2_x + 14, b2_y + 20, "Nuova Password / PIN:", t.text_primary, 0, true);
        dll_draw_input_box(g_api, b2_x + 14, b2_y + 38, b2_w - 28, 24, st->edit_newpass, (st->focused_field == 0), true, &t);

        g_api->gfx_draw_string(b2_x + 14, b2_y + 72, "Conferma Nuova Password / PIN:", t.text_primary, 0, true);
        dll_draw_input_box(g_api, b2_x + 14, b2_y + 90, b2_w - 28, 24, st->edit_confpass, (st->focused_field == 1), true, &t);

        g_api->wm_draw_themed_button(b2_x + 14,  b2_y + 132, 165, 24, "Salva Password", false);
        g_api->wm_draw_themed_button(b2_x + 190, b2_y + 132, 165, 24, "Rimuovi Password", false);
    }

    // Messaggio di stato e pulsanti footer OK / Chiudi
    int32_t foot_y = cy + ch - 32;
    if (st->status_msg[0]) {
        uint32_t msg_col = st->status_ok ? (t.is_dark ? 0x4EC9B0 : 0x107C41) : 0xD83B01;
        g_api->gfx_draw_string(cx + 12, foot_y - 18, st->status_msg, msg_col, 0, true);
    }

    g_api->wm_draw_themed_button(cx + cw - 176, foot_y, 80, 24, "OK", false);
    g_api->wm_draw_themed_button(cx + cw - 88,  foot_y, 80, 24, "Chiudi", false);
}

static void useracct_mouse(window_t* win, int32_t lx, int32_t ly, bool clicked) {
    if (!clicked) return;
    useracct_state_t* st = (useracct_state_t*)win->app_data;
    if (!st || !g_api) return;

    int32_t cx = 6;
    int32_t cy = 28;
    int32_t cw = win->width - 12;
    int32_t ch = win->height - 34;

    // Clic sulle schede
    if (ly >= cy + 4 && ly <= cy + 26) {
        if (lx >= cx + 8 && lx < cx + 138) {
            st->active_tab = 0;
            st->focused_field = 0;
            st->status_msg[0] = '\0';
            return;
        }
        if (lx >= cx + 140 && lx < cx + 300) {
            st->active_tab = 1;
            st->focused_field = 0;
            st->status_msg[0] = '\0';
            return;
        }
    }

    if (st->active_tab == 0) {
        int32_t b2_x = cx + 10;
        int32_t b2_y = cy + 36 + 90 + 12;
        int32_t b2_w = cw - 20;

        // Focus campo Username
        if (lx >= b2_x + 14 && lx <= b2_x + b2_w - 14 && ly >= b2_y + 38 && ly <= b2_y + 62) {
            st->focused_field = 0;
            return;
        }
        // Focus campo Hostname
        if (lx >= b2_x + 14 && lx <= b2_x + b2_w - 14 && ly >= b2_y + 90 && ly <= b2_y + 114) {
            st->focused_field = 1;
            return;
        }
        // Bottone "Applica Profilo"
        if (lx >= b2_x + 14 && lx <= b2_x + 179 && ly >= b2_y + 124 && ly <= b2_y + 148) {
            apply_profile_changes(st);
            return;
        }
        // Bottone "Apri Cartella Utente"
        if (lx >= b2_x + 190 && lx <= b2_x + 375 && ly >= b2_y + 124 && ly <= b2_y + 148) {
            if (g_api->app_explorer_open_folder) {
                char path[64];
                dll_strcpy(path, "C:\\User\\", sizeof(path));
                size_t l = dll_strlen(path);
                dll_strcpy(path + l, st->edit_username, sizeof(path) - l);
                g_api->app_explorer_open_folder(path);
            }
            return;
        }
    } else {
        int32_t b2_x = cx + 10;
        int32_t b2_y = cy + 36 + 74 + 12;
        int32_t b2_w = cw - 20;

        if (lx >= b2_x + 14 && lx <= b2_x + b2_w - 14 && ly >= b2_y + 38 && ly <= b2_y + 62) {
            st->focused_field = 0;
            return;
        }
        if (lx >= b2_x + 14 && lx <= b2_x + b2_w - 14 && ly >= b2_y + 90 && ly <= b2_y + 114) {
            st->focused_field = 1;
            return;
        }
        // Salva Password
        if (lx >= b2_x + 14 && lx <= b2_x + 179 && ly >= b2_y + 132 && ly <= b2_y + 156) {
            apply_password_changes(st, false);
            return;
        }
        // Rimuovi Password
        if (lx >= b2_x + 190 && lx <= b2_x + 355 && ly >= b2_y + 132 && ly <= b2_y + 156) {
            apply_password_changes(st, true);
            return;
        }
    }

    // Pulsanti Footer OK / Chiudi
    int32_t foot_y = cy + ch - 32;
    if (ly >= foot_y && ly <= foot_y + 24) {
        if (lx >= cx + cw - 176 && lx <= cx + cw - 96) {
            if (st->active_tab == 0) apply_profile_changes(st);
            else apply_password_changes(st, false);
            g_api->wm_destroy_window(win->id);
            return;
        }
        if (lx >= cx + cw - 88 && lx <= cx + cw - 8) {
            g_api->wm_destroy_window(win->id);
            return;
        }
    }
}

static void useracct_key(window_t* win, char c) {
    useracct_state_t* st = (useracct_state_t*)win->app_data;
    if (!st || !g_api) return;

    if (c == 27) {
        g_api->wm_destroy_window(win->id);
        return;
    }
    if (c == '\t') {
        st->focused_field = (st->focused_field + 1) % 2;
        return;
    }
    if (c == '\n' || c == '\r') {
        if (st->active_tab == 0) apply_profile_changes(st);
        else apply_password_changes(st, false);
        return;
    }

    char* target = NULL;
    if (st->active_tab == 0) {
        target = (st->focused_field == 0) ? st->edit_username : st->edit_hostname;
    } else {
        target = (st->focused_field == 0) ? st->edit_newpass : st->edit_confpass;
    }

    if (!target) return;
    size_t len = dll_strlen(target);

    if (c == '\b') {
        if (len > 0) target[len - 1] = '\0';
        return;
    }
    if (c >= 32 && c <= 126 && len < 28) {
        // Evita caratteri invalidi nei nomi cartella per username
        if (st->active_tab == 0 && st->focused_field == 0) {
            if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                return;
        }
        target[len] = c;
        target[len + 1] = '\0';
    }
}

__attribute__((section(".text.entry"), used))
int useracct_dll_entry(const kernel_api_t* api, window_t* win) {
    if (!api || !win) return -1;
    g_api = api;

    useracct_state_t* st = (useracct_state_t*)g_api->kmalloc(sizeof(useracct_state_t));
    if (!st) return -1;
    for (size_t i = 0; i < sizeof(useracct_state_t); i++) ((uint8_t*)st)[i] = 0;

    user_profile_t* prof = g_api->user_get_current ? g_api->user_get_current() : NULL;
    if (prof) {
        dll_strcpy(st->edit_username, prof->username, sizeof(st->edit_username));
        dll_strcpy(st->edit_hostname, prof->computer_name, sizeof(st->edit_hostname));
    } else {
        dll_strcpy(st->edit_username, "User", sizeof(st->edit_username));
        dll_strcpy(st->edit_hostname, "NEWOS-PC", sizeof(st->edit_hostname));
    }

    win->app_data = st;
    win->on_draw  = useracct_draw;
    win->on_mouse = useracct_mouse;
    win->on_key   = useracct_key;
    return 0;
}
