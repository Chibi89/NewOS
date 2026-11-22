/**
 * SystemLibrary/pkgsvc.c
 * Servizio Standardizzato Obbligatorio per l'Installazione di Software Terzi (PKGSVC.DLL)
 * Regola 6: Ogni software utente/terze parti puo' essere installato ESCLUSIVAMENTE in C:\ProgramFile\<App>\.
 * Qualsiasi tentativo di scrittura in C:\NewOS\ viene bloccato.
 * (C) Newspoint Software - NewSoft Dynamic System Library
 */

#include "cpl_dll_ui.h"
#include <kernel_ext_api.h>
#include <fs/vfs.h>

#ifndef BUILDING_KERNEL_LOADER
__attribute__((section(".header"), used))
module_header_t g_pkgsvc_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size      = 0,
    .entry_offset   = 64,
    .checksum       = 0,
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "PKGSVC",
    .signature      = MODULE_AUTH_TAG
};
#endif

#define MAX_PKG_APPS 16

static const kernel_api_t* g_api = NULL;

typedef struct {
    char name[40];
    char version[16];
    char path[80];
    bool is_system_verified;
} pkg_app_entry_t;

typedef struct {
    pkg_app_entry_t apps[MAX_PKG_APPS];
    int  num_apps;
    int  selected_idx;
    char new_app_name[32];
    char target_dir[64];
    bool focused_name;
    bool focused_dir;
    char status_msg[72];
    bool status_err;
} pkgsvc_state_t;

static bool is_valid_programfile_path(const char* path) {
    if (!path) return false;
    const char* pfx1 = "C:\\ProgramFile\\";
    const char* pfx2 = "C:/ProgramFile/";
    bool match1 = true, match2 = true;
    for (int i = 0; pfx1[i]; i++) {
        char a = path[i];
        char b = pfx1[i];
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) { match1 = false; break; }
    }
    for (int i = 0; pfx2[i]; i++) {
        char a = path[i];
        char b = pfx2[i];
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) { match2 = false; break; }
    }
    if (!match1 && !match2) return false;
    // Vieta path traversal ".."
    for (int i = 0; path[i]; i++) {
        if (path[i] == '.' && path[i + 1] == '.') return false;
    }
    return true;
}

static void pkgsvc_scan_installed(pkgsvc_state_t* st) {
    st->num_apps = 0;
    if (g_api && g_api->vfs_readdir && g_api->kmalloc && g_api->kfree) {
        vfs_dirent_t* entries = (vfs_dirent_t*)g_api->kmalloc(sizeof(vfs_dirent_t) * 16);
        if (entries) {
            int32_t cnt = g_api->vfs_readdir("C:/ProgramFile", entries, 16);
            for (int32_t i = 0; i < cnt && st->num_apps < MAX_PKG_APPS; i++) {
                if (entries[i].is_directory && entries[i].name[0] != '.') {
                    pkg_app_entry_t* e = &st->apps[st->num_apps++];
                    dll_strcpy(e->name, entries[i].name, sizeof(e->name));
                    dll_strcpy(e->version, "1.0", sizeof(e->version));
                    dll_strcpy(e->path, "C:\\ProgramFile\\", sizeof(e->path));
                    size_t l = dll_strlen(e->path);
                    dll_strcpy(e->path + l, entries[i].name, sizeof(e->path) - l);
                    e->is_system_verified = true;
                }
            }
            g_api->kfree(entries);
        }
    }
    if (st->selected_idx >= st->num_apps) {
        st->selected_idx = (st->num_apps > 0) ? 0 : -1;
    }
}

static void pkgsvc_install_app(pkgsvc_state_t* st) {
    if (!g_api || !st) return;
    if (st->new_app_name[0] == '\0') {
        st->status_err = true;
        dll_strcpy(st->status_msg, "Inserire il nome del software da installare!", sizeof(st->status_msg));
        return;
    }

    // Costruisci percorso di destinazione e verifica Regola 6 (solo C:\ProgramFile\)
    char dest[96];
    dll_strcpy(dest, st->target_dir, sizeof(dest));
    size_t dlen = dll_strlen(dest);
    if (dlen > 0 && dest[dlen - 1] != '\\' && dest[dlen - 1] != '/') {
        dest[dlen++] = '\\';
        dest[dlen] = '\0';
    }
    dll_strcpy(dest + dlen, st->new_app_name, sizeof(dest) - dlen);

    if (!is_valid_programfile_path(dest)) {
        st->status_err = true;
        dll_strcpy(st->status_msg, "BLOCCO ACL: Ammesso solo C:\\ProgramFile\\!", sizeof(st->status_msg));
        return;
    }

    // Crea cartella C:\ProgramFile (se assente) e sottocartella app
    if (g_api->vfs_mkdir) {
        g_api->vfs_mkdir("C:/ProgramFile");
        char vfs_dir[96];
        for (size_t i = 0; dest[i] && i < sizeof(vfs_dir) - 1; i++) {
            vfs_dir[i] = (dest[i] == '\\') ? '/' : dest[i];
            vfs_dir[i + 1] = '\0';
        }
        g_api->vfs_mkdir(vfs_dir);

        // Scrivi manifest .INI del pacchetto installato
        char ini_path[112];
        dll_strcpy(ini_path, vfs_dir, sizeof(ini_path));
        size_t il = dll_strlen(ini_path);
        dll_strcpy(ini_path + il, "/APP.INI", sizeof(ini_path) - il);
        if (g_api->vfs_create && g_api->vfs_write_entire_file_inplace) {
            const char* body = "[Package]\nInstalledBy=PKGSVC.DLL\nZone=ProgramFile\n";
            g_api->vfs_create(ini_path);
            g_api->vfs_write_entire_file_inplace(ini_path, body, (uint32_t)dll_strlen(body));
        }
    }

    // Registra nel Registro di Sistema
    if (g_api->registry_set_value_sz && g_api->registry_set_value_dword) {
        char reg_key[128];
        dll_strcpy(reg_key, "HKEY_LOCAL_MACHINE\\SOFTWARE\\NewOS\\InstalledApps\\", sizeof(reg_key));
        size_t rl = dll_strlen(reg_key);
        dll_strcpy(reg_key + rl, st->new_app_name, sizeof(reg_key) - rl);
        g_api->registry_set_value_sz(reg_key, "InstallPath", dest);
        g_api->registry_set_value_sz(reg_key, "Version", "1.0.0");
        g_api->registry_set_value_dword(reg_key, "IsNew", 1);
        if (g_api->registry_save_to_disk) g_api->registry_save_to_disk();
    }

    st->status_err = false;
    dll_strcpy(st->status_msg, "Installato in C:\\ProgramFile\\!", sizeof(st->status_msg));
    pkgsvc_scan_installed(st);
}

static void pkgsvc_uninstall_selected(pkgsvc_state_t* st) {
    if (!g_api || !st || st->selected_idx < 0 || st->selected_idx >= st->num_apps)
        return;
    const pkg_app_entry_t* app = &st->apps[st->selected_idx];
    char vfs_dir[96], ini_path[112];
    dll_strcpy(vfs_dir, "C:/ProgramFile/", sizeof(vfs_dir));
    size_t l = dll_strlen(vfs_dir);
    dll_strcpy(vfs_dir + l, app->name, sizeof(vfs_dir) - l);
    dll_strcpy(ini_path, vfs_dir, sizeof(ini_path));
    size_t il = dll_strlen(ini_path);
    dll_strcpy(ini_path + il, "/APP.INI", sizeof(ini_path) - il);

    if (g_api->vfs_unlink) {
        g_api->vfs_unlink(ini_path);
        g_api->vfs_unlink(vfs_dir);
    }
    st->status_err = false;
    dll_strcpy(st->status_msg, "Software rimosso in sicurezza da C:\\ProgramFile.", sizeof(st->status_msg));
    pkgsvc_scan_installed(st);
}

static void ps_draw_card(int32_t x, int32_t y, int32_t w, int32_t h, const dll_theme_t* t) {
    if (!g_api) return;
    if (t->is_glass) {
        g_api->gfx_fill_rounded_alpha(x, y, w, h, 8, t->card_bg, 210);
        g_api->gfx_draw_rounded_rect(x, y, w, h, 8, t->card_border);
    } else {
        g_api->gfx_fill_rect(x, y, w, h, t->card_bg);
        g_api->gfx_draw_bevel_rect(x, y, w, h, false);
    }
}

static void ps_draw_button(int32_t x, int32_t y, int32_t w, int32_t h, const char* lbl, uint32_t col, bool filled, const dll_theme_t* t) {
    if (!g_api) return;
    if (t->is_glass) {
        g_api->gfx_fill_rounded_alpha(x, y, w, h, 6, filled ? col : t->card_bg, filled ? 235 : 215);
        g_api->gfx_draw_rounded_rect(x, y, w, h, 6, filled ? col : t->card_border);
    } else {
        g_api->gfx_fill_rect(x, y, w, h, filled ? col : t->card_bg);
        g_api->gfx_draw_bevel_rect(x, y, w, h, false);
    }
    int32_t tx = x + (w - (int32_t)dll_strlen(lbl) * 8) / 2;
    if (tx < x + 4) tx = x + 4;
    g_api->gfx_draw_string(tx, y + (h - 14) / 2, lbl, filled ? 0xFFFFFF : t->text_primary, 0, true);
}

static void pkgsvc_draw(window_t* win) {
    pkgsvc_state_t* st = (pkgsvc_state_t*)win->app_data;
    if (!st || !g_api) return;

    dll_theme_t t;
    dll_theme_init(g_api, &t);

    int32_t cx = win->x + 12;
    int32_t cy = win->y + 32;
    int32_t cw = win->width - 24;
    int32_t ch = win->height - 44;

    // Header
    ps_draw_card(cx, cy, cw, 46, &t);
    g_api->gfx_draw_icon_32(cx + 8, cy + 7, ICON_MAINTENANCE);
    g_api->gfx_draw_string(cx + 48, cy + 8, "Servizio Standard Installazione Software Terzi (PKGSVC)", t.text_primary, 0, true);
    g_api->gfx_draw_string(cx + 48, cy + 25, "Isolamento obbligatorio in C:\\ProgramFile\\ (Regola 6)", t.accent, 0, true);

    // Card superiore: Installa nuovo pacchetto
    int32_t inst_y = cy + 54;
    ps_draw_card(cx, inst_y, cw, 118, &t);
    g_api->gfx_draw_string(cx + 10, inst_y + 8, "Installa Software / Pacchetto (.NPK):", t.text_primary, 0, true);

    g_api->gfx_draw_string(cx + 10, inst_y + 30, "Nome App:", t.text_secondary, 0, true);
    dll_draw_input_box(g_api, cx + 102, inst_y + 26, cw - 116, 22, st->new_app_name, st->focused_name, false, &t);

    g_api->gfx_draw_string(cx + 10, inst_y + 58, "Destinazione:", t.text_secondary, 0, true);
    dll_draw_input_box(g_api, cx + 102, inst_y + 54, cw - 116, 22, st->target_dir, st->focused_dir, false, &t);

    ps_draw_button(cx + 10,  inst_y + 84, 155, 24, "Installa Pacchetto", t.accent, true, &t);
    ps_draw_button(cx + 174, inst_y + 84, 165, 24, "Test Path C:\\NewOS", 0xDC2626, true, &t);

    // Card inferiore: Programmi installati in C:\ProgramFile
    int32_t list_y = inst_y + 126;
    int32_t list_h = ch - (list_y - cy) - 36;
    ps_draw_card(cx, list_y, cw, list_h, &t);
    g_api->gfx_draw_string(cx + 10, list_y + 8, "Programmi Installati in C:\\ProgramFile\\:", t.text_secondary, 0, true);

    if (st->num_apps == 0) {
        g_api->gfx_draw_string(cx + 14, list_y + 32, "(Nessun software di terze parti installato)", 0x707885, 0, true);
    } else {
        int max_r = (list_h - 32) / 22;
        for (int i = 0; i < st->num_apps && i < max_r; i++) {
            int32_t ry = list_y + 28 + i * 22;
            bool sel = (i == st->selected_idx);
            if (sel) g_api->gfx_fill_rect(cx + 8, ry, cw - 16, 20, t.accent);
            g_api->gfx_draw_icon_16(cx + 12, ry + 2, ICON_FOLDER);
            g_api->gfx_draw_string(cx + 34, ry + 3, st->apps[i].name, sel ? 0xFFFFFF : t.text_primary, 0, true);
            g_api->gfx_draw_string(cx + 210, ry + 3, st->apps[i].path, sel ? 0xE2E8F0 : t.text_secondary, 0, true);
        }
    }

    // Footer
    int32_t foot_y = cy + ch - 28;
    if (st->status_msg[0] != '\0') {
        g_api->gfx_draw_string(cx + 4, foot_y + 5, st->status_msg, st->status_err ? 0xEF4444 : t.accent, 0, true);
    }
    ps_draw_button(cx + cw - 196, foot_y, 108, 24, "Disinstalla", 0xB91C1C, true, &t);
    ps_draw_button(cx + cw - 82,  foot_y, 82,  24, "Chiudi", t.card_bg, false, &t);
}

static void pkgsvc_mouse(window_t* win, int32_t lx, int32_t ly, bool clicked) {
    pkgsvc_state_t* st = (pkgsvc_state_t*)win->app_data;
    if (!st || !g_api || !clicked) return;

    int32_t cx = 12, cy = 32, cw = win->width - 24, ch = win->height - 44;
    int32_t inst_y = cy + 54;

    // Input boxes
    if (lx >= cx + 102 && lx <= cx + cw - 14) {
        if (ly >= inst_y + 26 && ly <= inst_y + 48) {
            st->focused_name = true; st->focused_dir = false;
            return;
        }
        if (ly >= inst_y + 54 && ly <= inst_y + 76) {
            st->focused_name = false; st->focused_dir = true;
            return;
        }
    }

    // Bottoni installazione / test sicurezza
    if (ly >= inst_y + 84 && ly <= inst_y + 108) {
        if (lx >= cx + 10 && lx <= cx + 165) {
            pkgsvc_install_app(st);
            return;
        }
        if (lx >= cx + 174 && lx <= cx + 339) {
            dll_strcpy(st->target_dir, "C:\\NewOS\\SystemLibrary\\", sizeof(st->target_dir));
            pkgsvc_install_app(st);
            return;
        }
    }

    int32_t list_y = inst_y + 126;
    int32_t list_h = ch - (list_y - cy) - 36;
    if (lx >= cx + 8 && lx <= cx + cw - 8 && ly >= list_y + 28 && ly <= list_y + list_h - 4) {
        int idx = (ly - (list_y + 28)) / 22;
        if (idx >= 0 && idx < st->num_apps) st->selected_idx = idx;
        return;
    }

    int32_t foot_y = cy + ch - 28;
    if (ly >= foot_y && ly <= foot_y + 24) {
        if (lx >= cx + cw - 196 && lx <= cx + cw - 88) {
            pkgsvc_uninstall_selected(st);
            return;
        }
        if (lx >= cx + cw - 82 && lx <= cx + cw) {
            g_api->wm_destroy_window(win->id);
            return;
        }
    }
}

static void pkgsvc_key(window_t* win, char c) {
    pkgsvc_state_t* st = (pkgsvc_state_t*)win->app_data;
    if (!st || !g_api) return;
    if (c == 27) { g_api->wm_destroy_window(win->id); return; }
    char* buf = st->focused_dir ? st->target_dir : st->new_app_name;
    size_t cap = st->focused_dir ? sizeof(st->target_dir) : sizeof(st->new_app_name);
    size_t len = dll_strlen(buf);
    if (c == '\b') {
        if (len > 0) buf[len - 1] = '\0';
    } else if (c >= 32 && c <= 126 && len + 1 < cap) {
        buf[len] = c;
        buf[len + 1] = '\0';
    }
}

__attribute__((section(".text.entry"), used))
int pkgsvc_dll_entry(const kernel_api_t* api, window_t* win) {
    if (!api || !win) return -1;
    g_api = api;

    pkgsvc_state_t* st = (pkgsvc_state_t*)g_api->kmalloc(sizeof(pkgsvc_state_t));
    if (!st) return -1;
    for (size_t i = 0; i < sizeof(pkgsvc_state_t); i++) ((uint8_t*)st)[i] = 0;

    dll_strcpy(st->new_app_name, "MyDemoApp", sizeof(st->new_app_name));
    dll_strcpy(st->target_dir, "C:\\ProgramFile\\", sizeof(st->target_dir));
    st->focused_name = true;
    st->selected_idx = -1;
    pkgsvc_scan_installed(st);

    win->app_data = st;
    win->on_draw  = pkgsvc_draw;
    win->on_mouse = pkgsvc_mouse;
    win->on_key   = pkgsvc_key;
    return 0;
}
