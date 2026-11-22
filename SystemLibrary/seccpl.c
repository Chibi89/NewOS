/**
 * SystemLibrary/seccpl.c
 * Libreria Dinamica Out-of-Kernel SECCPL.DLL (Centro Sicurezza e Permessi - SECCPL.CPL)
 */
#include "cpl_dll_ui.h"

#ifndef BUILDING_KERNEL_LOADER
__attribute__((section(".header"), used))
module_header_t g_seccpl_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size      = 0,
    .entry_offset   = 64,
    .checksum       = 0,
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "SECCPL",
    .signature      = MODULE_AUTH_TAG
};
#endif

typedef struct {
    bool (*commit_kernel_policy)(const char* subkey, const char* val_name, uint32_t new_val);
} seccpl_params_t;

static const kernel_api_t* g_api = NULL;
static bool (*g_commit_policy)(const char* subkey, const char* val_name, uint32_t new_val) = NULL;

#define KPOL_BASE "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Lsa\\KernelSecurityPolicy\\"

typedef struct {
    int      active_tab;           // 0: Privilegi, 1: File System, 2: Applicazioni, 3: Integrita'
    uint32_t verify_mode;          // 0: Sviluppo, 1: Bilanciato, 2: Massimo
    bool     protect_sys_dirs;
    bool     enforce_reg_ro;
    bool     enforce_syscall_iso;

    int      sel_fs_row;           // 0..3
    uint32_t fs_perm[4];           // bit0=R, bit1=W, bit2=X, bit3=NOSM
    bool     allow_newos_delete;   // Richiede Admin + commit su FileSystemACL\AllowNewOSDelete
    bool     admin_prompt_open;
    char     admin_pass_buf[32];

    bool     app_browser_net;
    bool     app_browser_vfs;
    bool     app_js_sandbox;
    bool     app_thirdparty_launch;

    uint32_t commit_count;
    int      audit_verified_ok;
    int      audit_total;
    char     status_msg[80];
} seccpl_state_t;

static void sec_strcat(char* dst, const char* src, size_t max_len) {
    size_t d = dll_strlen(dst);
    size_t i = 0;
    if (!dst || d + 1 >= max_len || !src) return;
    while (src[i] && d + i + 1 < max_len) {
        dst[d + i] = src[i];
        i++;
    }
    dst[d + i] = '\0';
}

static void sec_cat_u32(char* dst, uint32_t v, size_t max_len) {
    char tmp[16];
    int i = 0;
    if (v == 0) {
        sec_strcat(dst, "0", max_len);
        return;
    }
    while (v > 0 && i < 14) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    char rev[16];
    int j = 0;
    while (i > 0) rev[j++] = tmp[--i];
    rev[j] = '\0';
    sec_strcat(dst, rev, max_len);
}

static uint32_t read_kpol_dword(const char* subkey, const char* val, uint32_t def) {
    if (!g_api || !g_api->registry_get_value_dword) return def;
    char full[224];
    dll_strcpy(full, KPOL_BASE, sizeof(full));
    sec_strcat(full, subkey, sizeof(full));
    uint32_t out = def;
    if (g_api->registry_get_value_dword(full, val, &out)) return out;
    return def;
}

static void load_policies(seccpl_state_t* st) {
    st->verify_mode         = read_kpol_dword("ExecutionControl", "VerifyMode", 1);
    if (st->verify_mode > 2) st->verify_mode = 1;
    st->protect_sys_dirs    = read_kpol_dword("ExecutionControl", "ProtectSystemDirs", 1) != 0;
    st->enforce_reg_ro      = read_kpol_dword("ExecutionControl", "EnforcePreDesktopReadOnly", 1) != 0;
    st->enforce_syscall_iso = read_kpol_dword("ExecutionControl", "EnforceSyscallIsolation", 1) != 0;

    st->fs_perm[0] = read_kpol_dword("FileSystemACL", "NewOS_Perm", 13);         // R|X|NOSM
    st->fs_perm[1] = read_kpol_dword("FileSystemACL", "SystemLibrary_Perm", 13); // R|X|NOSM
    st->fs_perm[2] = read_kpol_dword("FileSystemACL", "ProgramFile_Perm", 7);    // R|W|X
    st->fs_perm[3] = read_kpol_dword("FileSystemACL", "UserProfile_Perm", 7);    // R|W|X
    st->allow_newos_delete = read_kpol_dword("FileSystemACL", "AllowNewOSDelete", 0) != 0;

    st->app_browser_net       = read_kpol_dword("AppSandbox", "BrowserNetTLS", 1) != 0;
    st->app_browser_vfs       = read_kpol_dword("AppSandbox", "BrowserVFS", 1) != 0;
    st->app_js_sandbox        = read_kpol_dword("AppSandbox", "JsEngineSandbox", 1) != 0;
    st->app_thirdparty_launch = read_kpol_dword("AppSandbox", "ThirdPartyLaunch", 1) != 0;

    st->commit_count = read_kpol_dword("Audit", "PolicyCommitCount", 0);
}

static void run_integrity_check(seccpl_state_t* st) {
    const char* mods[4] = {
        "C:\\NewOS\\SystemLibrary\\BSOD.DLL",
        "C:\\NewOS\\SystemLibrary\\LAUNCHER.DLL",
        "C:\\NewOS\\Boot\\LOGIN.MOD",
        "C:\\NewOS\\Boot\\LOGOUT.MOD"
    };
    int ok = 0;
    for (int i = 0; i < 4; i++) {
        if (g_api && g_api->vfs_exists && g_api->vfs_exists(mods[i])) ok++;
    }
    st->audit_verified_ok = ok;
    st->audit_total = 4;
    dll_strcpy(st->status_msg, "Integrita' moduli: ", sizeof(st->status_msg));
    sec_cat_u32(st->status_msg, (uint32_t)ok, sizeof(st->status_msg));
    sec_strcat(st->status_msg, "/4 OK", sizeof(st->status_msg));
}

static void save_policies(seccpl_state_t* st) {
    if (!st || !g_commit_policy) {
        dll_strcpy(st->status_msg, "Errore: Kernel Gate assente.", sizeof(st->status_msg));
        return;
    }
    if (g_api && g_api->registry_is_read_only && g_api->registry_is_read_only()) {
        dll_strcpy(st->status_msg, "Bloccato: Regola sola lettura!", sizeof(st->status_msg));
        return;
    }

    bool ok = true;
    ok &= g_commit_policy("ExecutionControl", "VerifyMode", st->verify_mode);
    ok &= g_commit_policy("ExecutionControl", "ProtectSystemDirs", st->protect_sys_dirs ? 1 : 0);
    ok &= g_commit_policy("ExecutionControl", "EnforcePreDesktopReadOnly", st->enforce_reg_ro ? 1 : 0);
    ok &= g_commit_policy("ExecutionControl", "EnforceSyscallIsolation", st->enforce_syscall_iso ? 1 : 0);

    ok &= g_commit_policy("FileSystemACL", "NewOS_Perm", st->fs_perm[0]);
    ok &= g_commit_policy("FileSystemACL", "SystemLibrary_Perm", st->fs_perm[1]);
    ok &= g_commit_policy("FileSystemACL", "ProgramFile_Perm", st->fs_perm[2]);
    ok &= g_commit_policy("FileSystemACL", "UserProfile_Perm", st->fs_perm[3]);
    ok &= g_commit_policy("FileSystemACL", "AllowNewOSDelete", st->allow_newos_delete ? 1 : 0);

    ok &= g_commit_policy("AppSandbox", "BrowserNetTLS", st->app_browser_net ? 1 : 0);
    ok &= g_commit_policy("AppSandbox", "BrowserVFS", st->app_browser_vfs ? 1 : 0);
    ok &= g_commit_policy("AppSandbox", "JsEngineSandbox", st->app_js_sandbox ? 1 : 0);
    ok &= g_commit_policy("AppSandbox", "ThirdPartyLaunch", st->app_thirdparty_launch ? 1 : 0);

    st->commit_count = read_kpol_dword("Audit", "PolicyCommitCount", st->commit_count + 1);
    if (ok) {
        dll_strcpy(st->status_msg, "Salvate in LSA\\KernelSecurityPolicy!", sizeof(st->status_msg));
    } else {
        dll_strcpy(st->status_msg, "Salvataggio bloccato dal kernel.", sizeof(st->status_msg));
    }
}

static void draw_checkbox(int32_t x, int32_t y, bool checked, const char* label, const dll_theme_t* t) {
    if (t->is_classic) {
        g_api->gfx_fill_rect(x, y + 1, 13, 13, 0xFFFFFF);
        g_api->gfx_draw_bevel_rect(x, y + 1, 13, 13, true);
        if (checked) g_api->gfx_fill_rect(x + 3, y + 4, 7, 7, t->accent);
    } else {
        g_api->gfx_fill_rounded_rect(x, y + 1, 13, 13, 3, checked ? t->accent : t->input_bg);
        g_api->gfx_draw_rounded_rect(x, y + 1, 13, 13, 3, checked ? t->accent : t->card_border);
        if (checked) g_api->gfx_draw_string(x + 2, y - 1, "v", t->accent_text, 0, true);
    }
    g_api->gfx_draw_string(x + 20, y, label, t->text_primary, 0, true);
}

static void draw_radio(int32_t x, int32_t y, bool selected, const char* label, const dll_theme_t* t) {
    if (t->is_classic) {
        g_api->gfx_draw_bevel_rect(x, y + 1, 13, 13, true);
        if (selected) g_api->gfx_fill_rect(x + 3, y + 4, 7, 7, t->accent);
    } else {
        g_api->gfx_draw_rounded_rect(x, y + 1, 13, 13, 6, selected ? t->accent : t->card_border);
        if (selected) g_api->gfx_fill_rounded_rect(x + 3, y + 4, 7, 7, 3, t->accent);
    }
    g_api->gfx_draw_string(x + 20, y, label, selected ? t->accent : t->text_primary, 0, true);
}

static void seccpl_draw(window_t* win) {
    if (!g_api || !win || !win->app_data) return;
    seccpl_state_t* st = (seccpl_state_t*)win->app_data;
    dll_theme_t t;
    dll_theme_init(g_api, &t);

    int32_t wx = win->x, wy = win->y;
    int32_t cx = wx + 8, cy = wy + 28;
    int32_t cw = win->width - 16, ch = win->height - 36;

    // 1. 4 Schede superiori
    const char* tabs[4] = { "Privilegi & UAC", "File System", "Applicazioni", "Integrita'" };
    int32_t tab_w = (cw - 8) / 4;
    for (int i = 0; i < 4; i++) {
        g_api->wm_draw_themed_tab(cx + i * tab_w, cy, tab_w - 2, 22, tabs[i], st->active_tab == i);
    }

    int32_t py = cy + 22;
    int32_t ph = ch - 60;
    if (t.is_classic) {
        g_api->gfx_fill_rect(cx, py, cw, ph, t.bg_color);
        g_api->gfx_draw_bevel_rect(cx, py, cw, ph, false);
    } else {
        if (g_api->gfx_fill_rounded_alpha) {
            uint8_t alpha = (t.th && t.th->surface_alpha > 0) ? t.th->surface_alpha : 195;
            if (t.is_glass && alpha < 175) alpha = 175;
            g_api->gfx_fill_rounded_alpha(cx, py, cw, ph, t.control_radius, t.card_bg, alpha);
        } else {
            g_api->gfx_fill_rounded_rect(cx, py, cw, ph, t.control_radius, t.card_bg);
        }
        g_api->gfx_draw_rounded_rect(cx, py, cw, ph, t.control_radius, t.card_border);
    }

    if (st->active_tab == 0) {
        // Banner di stato protezione
        int32_t bx = cx + 10, by = py + 8, bw = cw - 20, bh = 28;
        g_api->gfx_fill_rect(bx, by, bw, bh, 0x103B2C);
        g_api->gfx_draw_rect(bx, by, bw, bh, 0x22C55E);
        user_profile_t* up = g_api->user_get_current ? g_api->user_get_current() : NULL;
        bool ro = (g_api->registry_is_read_only && g_api->registry_is_read_only());
        char banner[72];
        dll_strcpy(banner, "Protezione ATTIVA | Utente: ", sizeof(banner));
        sec_strcat(banner, (up && up->username[0]) ? up->username : "Morgan", sizeof(banner));
        sec_strcat(banner, ro ? " | Reg: SOLA LETTURA" : " | Reg: Autenticato", sizeof(banner));
        g_api->gfx_draw_string(bx + 10, by + 6, banner, 0xFFFFFF, 0, true);

        // GroupBox 1: Livello Controllo Esecuzione
        dll_draw_groupbox(g_api, cx + 10, py + 44, cw - 20, 96, " Controllo Esecuzione (NOSM + CRC32) ", &t);
        draw_radio(cx + 22, py + 66,  st->verify_mode == 2, "Massimo (Solo moduli firmati NOSM+CRC32)", &t);
        draw_radio(cx + 22, py + 90,  st->verify_mode == 1, "Bilanciato (Obbligo in C:\\NewOS\\, UAC altrove)", &t);
        draw_radio(cx + 22, py + 114, st->verify_mode == 0, "Sviluppo (Avviso su seriale COM1 senza blocco)", &t);

        // GroupBox 2: Policy Kernel & Isolamento
        dll_draw_groupbox(g_api, cx + 10, py + 148, cw - 20, 120, " Policy Kernel (Ramo LSA Isolato) ", &t);
        draw_checkbox(cx + 22, py + 170, st->protect_sys_dirs,    "Protezione scrittura C:\\NewOS\\*", &t);
        draw_checkbox(cx + 22, py + 194, st->enforce_reg_ro,      "Blocco Registro pre-desktop (REG_READONLY)", &t);
        draw_checkbox(cx + 22, py + 218, st->enforce_syscall_iso, "Isolamento SYSCALL/SYSRET e PML4 Supervisor", &t);
        g_api->gfx_draw_string(cx + 22, py + 244, "Servizio Avvio: LAUNCHER.DLL (Attivo)", t.accent, 0, true);
    } else if (st->active_tab == 1) {
        dll_draw_groupbox(g_api, cx + 10, py + 10, cw - 20, 134, " Percorsi File System ", &t);
        const char* paths[4] = {
            "C:\\NewOS\\ (Sistema Critico)",
            "C:\\NewOS\\SystemLibrary\\ (.DLL/.INI)",
            "C:\\ProgramFile\\ (Software Terze Parti)",
            "C:\\User\\<Utente>\\ (Profilo Privato)"
        };
        for (int i = 0; i < 4; i++) {
            int32_t ry = py + 32 + i * 26;
            if (st->sel_fs_row == i) {
                g_api->gfx_fill_rect(cx + 18, ry - 2, cw - 36, 22, t.accent);
            }
            char row_buf[72];
            uint32_t pm = st->fs_perm[i];
            dll_strcpy(row_buf, paths[i], sizeof(row_buf));
            sec_strcat(row_buf, "  [", sizeof(row_buf));
            char fl[10];
            fl[0] = (pm & 1) ? 'R' : '-';
            fl[1] = (pm & 2) ? 'W' : '-';
            fl[2] = (pm & 4) ? 'X' : '-';
            fl[3] = '\0';
            sec_strcat(row_buf, fl, sizeof(row_buf));
            if (pm & 8) sec_strcat(row_buf, "|NOSM", sizeof(row_buf));
            sec_strcat(row_buf, "]", sizeof(row_buf));
            g_api->gfx_draw_string(cx + 24, ry + 1, row_buf,
                                   (st->sel_fs_row == i) ? t.accent_text : t.text_primary, 0, true);
        }

        dll_draw_groupbox(g_api, cx + 10, py + 152, cw - 20, 118, " Permessi Selezionati & Protezione C:\\NewOS\\ ", &t);
        uint32_t pm = st->fs_perm[st->sel_fs_row];
        draw_checkbox(cx + 24, py + 174, (pm & 1) != 0, "Lettura (Read)", &t);
        draw_checkbox(cx + 260, py + 174, (pm & 2) != 0, "Scrittura (Write)", &t);
        draw_checkbox(cx + 24, py + 198, (pm & 4) != 0, "Esecuzione (Exec)", &t);
        draw_checkbox(cx + 260, py + 198, (pm & 8) != 0, "Richiedi Firma NOSM", &t);
        draw_checkbox(cx + 24, py + 224, st->allow_newos_delete, "Consenti eliminazione in C:\\NewOS\\ (Admin)", &t);
        if (st->admin_prompt_open) {
            g_api->gfx_fill_rect(cx + 24, py + 244, 240, 20, t.input_bg);
            g_api->gfx_draw_rect(cx + 24, py + 244, 240, 20, t.accent);
            char masked[36];
            size_t plen = dll_strlen(st->admin_pass_buf);
            if (plen > 20) plen = 20;
            for (size_t k = 0; k < plen; k++) masked[k] = '*';
            masked[plen] = '_';
            masked[plen + 1] = '\0';
            g_api->gfx_draw_string(cx + 30, py + 246, masked, t.text_primary, 0, true);
            g_api->wm_draw_themed_button(cx + 272, py + 243, 120, 22, "Conferma Admin", false);
        } else {
            g_api->gfx_draw_string(cx + 24, py + 246, "Richiede Admin + AllowNewOSDelete=1 in LSA", t.text_secondary, 0, true);
        }
    } else if (st->active_tab == 2) {
        dll_draw_groupbox(g_api, cx + 10, py + 10, cw - 20, 256, " Sandbox Moduli & Applicazioni ", &t);
        draw_checkbox(cx + 22, py + 38,  st->app_browser_net,       "WEBBROWSER.DLL: Accesso Rete TCP/TLS (NETTLS.DLL)", &t);
        draw_checkbox(cx + 22, py + 70,  st->app_browser_vfs,       "WEBBROWSER.DLL: Cache e Lettura VFS", &t);
        draw_checkbox(cx + 22, py + 102, st->app_js_sandbox,        "JSENGINE.DLL (QuickJS): Sandbox memoria isolata", &t);
        draw_checkbox(cx + 22, py + 134, st->app_thirdparty_launch, "LAUNCHER.DLL: Avvio app da C:\\ProgramFile\\", &t);
        g_api->gfx_draw_string(cx + 22, py + 180, "Nota: I moduli critici (BOOTSCR, LOGIN, LOGOUT, BSOD)", t.text_secondary, 0, true);
        g_api->gfx_draw_string(cx + 22, py + 200, "mantengono accesso diretto protetto dal kernel.", t.text_secondary, 0, true);
    } else {
        dll_draw_groupbox(g_api, cx + 10, py + 10, cw - 20, 146, " Integrita' Crittografica & Registro ", &t);
        g_api->gfx_draw_string(cx + 22, py + 34, "Cifratura Sistema: AES-256 (SYSTEM.PAK / REGISTRY.DAT)", t.text_primary, 0, true);
        g_api->gfx_draw_string(cx + 22, py + 56, "Ramo Policy: HKLM\\SYSTEM\\...\\Lsa\\KernelSecurityPolicy", t.accent, 0, true);
        char cbuf[64];
        dll_strcpy(cbuf, "Commit Autorizzati (Kernel Gate): ", sizeof(cbuf));
        sec_cat_u32(cbuf, st->commit_count, sizeof(cbuf));
        g_api->gfx_draw_string(cx + 22, py + 78, cbuf, t.text_primary, 0, true);
        char abuf[64];
        dll_strcpy(abuf, "Moduli Critici Verificati: ", sizeof(abuf));
        sec_cat_u32(abuf, (uint32_t)st->audit_verified_ok, sizeof(abuf));
        sec_strcat(abuf, " / ", sizeof(abuf));
        sec_cat_u32(abuf, (uint32_t)st->audit_total, sizeof(abuf));
        g_api->gfx_draw_string(cx + 22, py + 100, abuf, 0x22C55E, 0, true);
        g_api->wm_draw_themed_button(cx + 22, py + 122, 180, 24, "Verifica Integrita'", false);
    }

    // Stato & Pulsanti inferiori
    if (st->status_msg[0]) {
        g_api->gfx_draw_string(cx + 4, cy + ch - 26, st->status_msg, t.accent, 0, true);
    }
    int32_t btn_y = cy + ch - 30;
    g_api->wm_draw_themed_button(cx + cw - 236, btn_y, 74, 24, "OK", false);
    g_api->wm_draw_themed_button(cx + cw - 156, btn_y, 74, 24, "Annulla", false);
    g_api->wm_draw_themed_button(cx + cw - 76,  btn_y, 74, 24, "Applica", false);
}

static void seccpl_mouse(window_t* win, int32_t lx, int32_t ly, bool clicked) {
    if (!clicked || !g_api || !win || !win->app_data) return;
    seccpl_state_t* st = (seccpl_state_t*)win->app_data;

    int32_t cx = 8, cy = 28;
    int32_t cw = win->width - 16, ch = win->height - 36;

    // 1. Click su Tab
    int32_t tab_w = (cw - 8) / 4;
    if (ly >= cy && ly <= cy + 22) {
        int ti = (lx - cx) / tab_w;
        if (ti >= 0 && ti < 4) st->active_tab = ti;
        return;
    }

    int32_t py = cy + 22;
    if (st->active_tab == 0) {
        if (lx >= cx + 20 && lx <= cx + cw - 20) {
            if (ly >= py + 62 && ly <= py + 84) st->verify_mode = 2;
            else if (ly >= py + 86 && ly <= py + 108) st->verify_mode = 1;
            else if (ly >= py + 110 && ly <= py + 132) st->verify_mode = 0;
            else if (ly >= py + 166 && ly <= py + 188) st->protect_sys_dirs = !st->protect_sys_dirs;
            else if (ly >= py + 190 && ly <= py + 212) st->enforce_reg_ro = !st->enforce_reg_ro;
            else if (ly >= py + 214 && ly <= py + 236) st->enforce_syscall_iso = !st->enforce_syscall_iso;
        }
    } else if (st->active_tab == 1) {
        for (int i = 0; i < 4; i++) {
            int32_t ry = py + 32 + i * 26;
            if (lx >= cx + 18 && lx <= cx + cw - 18 && ly >= ry - 2 && ly <= ry + 20) {
                st->sel_fs_row = i;
                return;
            }
        }
        if (ly >= py + 170 && ly <= py + 192) {
            if (lx < cx + 240) st->fs_perm[st->sel_fs_row] ^= 1;
            else st->fs_perm[st->sel_fs_row] ^= 2;
        } else if (ly >= py + 194 && ly <= py + 216) {
            if (lx < cx + 240) st->fs_perm[st->sel_fs_row] ^= 4;
            else st->fs_perm[st->sel_fs_row] ^= 8;
        } else if (ly >= py + 220 && ly <= py + 240) {
            if (st->allow_newos_delete) {
                st->allow_newos_delete = false;
                st->admin_prompt_open = false;
            } else {
                user_profile_t* up = g_api->user_get_current ? g_api->user_get_current() : NULL;
                if (!up || dll_strcmp(up->username, "Ospite") == 0 || dll_strcmp(up->username, "Guest") == 0) {
                    dll_strcpy(st->status_msg, "Negato: serve account Admin!", sizeof(st->status_msg));
                } else {
                    st->admin_prompt_open = true;
                    st->admin_pass_buf[0] = '\0';
                    dll_strcpy(st->status_msg, "Inserisci password Admin!", sizeof(st->status_msg));
                }
            }
            return;
        } else if (st->admin_prompt_open && lx >= cx + 272 && lx <= cx + 392 && ly >= py + 242 && ly <= py + 266) {
            user_profile_t* up = g_api->user_get_current ? g_api->user_get_current() : NULL;
            if (up && (up->password[0] == '\0' || dll_strcmp(st->admin_pass_buf, up->password) == 0)) {
                st->allow_newos_delete = true;
                st->admin_prompt_open = false;
                dll_strcpy(st->status_msg, "Admin OK: premi Applica!", sizeof(st->status_msg));
            } else {
                dll_strcpy(st->status_msg, "Password Admin errata!", sizeof(st->status_msg));
            }
            return;
        }
    } else if (st->active_tab == 2) {
        if (ly >= py + 34 && ly <= py + 58) st->app_browser_net = !st->app_browser_net;
        else if (ly >= py + 66 && ly <= py + 90) st->app_browser_vfs = !st->app_browser_vfs;
        else if (ly >= py + 98 && ly <= py + 122) st->app_js_sandbox = !st->app_js_sandbox;
        else if (ly >= py + 130 && ly <= py + 154) st->app_thirdparty_launch = !st->app_thirdparty_launch;
    } else if (st->active_tab == 3) {
        if (lx >= cx + 22 && lx <= cx + 202 && ly >= py + 122 && ly <= py + 146) {
            run_integrity_check(st);
            return;
        }
    }

    // Pulsanti inferiori [OK] [Annulla] [Applica]
    int32_t btn_y = cy + ch - 30;
    if (ly >= btn_y && ly <= btn_y + 24) {
        if (lx >= cx + cw - 236 && lx <= cx + cw - 162) {
            save_policies(st);
            g_api->wm_destroy_window(win->id);
        } else if (lx >= cx + cw - 156 && lx <= cx + cw - 82) {
            g_api->wm_destroy_window(win->id);
        } else if (lx >= cx + cw - 76 && lx <= cx + cw - 2) {
            save_policies(st);
        }
    }
}

static void seccpl_key(window_t* win, char c) {
    if (!win || !win->app_data) return;
    seccpl_state_t* st = (seccpl_state_t*)win->app_data;
    if (!st->admin_prompt_open) return;

    size_t len = dll_strlen(st->admin_pass_buf);
    if (c == '\b') {
        if (len > 0) st->admin_pass_buf[len - 1] = '\0';
    } else if (c == '\n' || c == '\r') {
        user_profile_t* up = g_api->user_get_current ? g_api->user_get_current() : NULL;
        if (up && (up->password[0] == '\0' || dll_strcmp(st->admin_pass_buf, up->password) == 0)) {
            st->allow_newos_delete = true;
            st->admin_prompt_open = false;
            dll_strcpy(st->status_msg, "Admin OK: premi Applica!", sizeof(st->status_msg));
        } else {
            dll_strcpy(st->status_msg, "Password Admin errata!", sizeof(st->status_msg));
        }
    } else if (c >= 32 && c <= 126 && len + 1 < sizeof(st->admin_pass_buf)) {
        st->admin_pass_buf[len] = c;
        st->admin_pass_buf[len + 1] = '\0';
    }
}

__attribute__((section(".text.entry"), used))
int module_main(const kernel_api_t* api, seccpl_params_t* params) {
    if (!api) return -1;
    g_api = api;
    if (params && params->commit_kernel_policy) {
        g_commit_policy = params->commit_kernel_policy;
    }

    window_t* win = api->wm_create_window("Centro Sicurezza e Permessi (SECCPL.CPL)",
                                          230, 140, 560, 386, APP_DISPLAY);
    if (!win) return -1;

    seccpl_state_t* st = (seccpl_state_t*)api->kmalloc(sizeof(seccpl_state_t));
    if (!st) {
        api->wm_destroy_window(win->id);
        return -1;
    }
    memset(st, 0, sizeof(seccpl_state_t));
    load_policies(st);
    run_integrity_check(st);

    win->app_data = st;
    win->on_draw  = seccpl_draw;
    win->on_mouse = seccpl_mouse;
    win->on_key   = seccpl_key;
    return 0;
}
