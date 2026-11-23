/**
 * SystemLibrary/startmenu_dll.c
 * Menu Start Unificato Moderno Out-of-Kernel (STARTMENU.DLL)
 * Stile Windows 11 24H2/25H2: Barra di ricerca unificata, sezione Preferiti,
 * sezione Tutto con raggruppamento alfabetico / badge "Nuova" / vista Griglia o Elenco,
 * e footer inferiore con nome utente e icone di accesso rapido.
 * (C) Newspoint Software - NewSoft Dynamic System Library
 */

#include "cpl_dll_ui.h"
#include <kernel_ext_api.h>

#ifndef BUILDING_KERNEL_LOADER
__attribute__((section(".header"), used))
module_header_t g_startmenu_header = {
    .magic          = MODULE_MAGIC_NEWOS,
    .header_size    = sizeof(module_header_t),
    .module_version = MODULE_VERSION_CURRENT,
    .module_type    = MODULE_TYPE_SYSTEM_LIBRARY,
    .code_size      = 0,
    .entry_offset   = 64,
    .checksum       = 0,
    .security_flags = SEC_FLAG_PROTECTED | SEC_FLAG_SYSTEM_SIGNED | SEC_FLAG_VERIFIED,
    .module_name    = "STARTMENU",
    .signature      = MODULE_AUTH_TAG
};
#endif

#define STARTMENU_CMD_DRAW   0
#define STARTMENU_CMD_MOUSE  1
#define STARTMENU_CMD_KEY    2

typedef struct {
    uint32_t cmd;
    int32_t  sw;
    int32_t  sh;
    int32_t  mx;
    int32_t  my;
    bool     clicked;
    char     key;
    int32_t  result; // 0=none, 1=handled (keep open), 2=handled (close), 3=logout
} startmenu_params_t;

typedef struct {
    char        label[20];
    char        target[12];
    icon_id_t   icon;
    bool        is_new;
} sm_app_item_t;

static char g_search_buf[32];
static int  g_view_list_mode = 0; // 0 = Griglia, 1 = Elenco
static int  g_scroll_off = 0;
static sm_app_item_t g_pinned[8];
static sm_app_item_t g_all_apps[14];
static bool g_items_init = false;

static void sm_set_item(sm_app_item_t* it, const char* lbl, const char* tgt, icon_id_t ic, bool nw) {
    dll_strcpy(it->label, lbl, 20);
    dll_strcpy(it->target, tgt, 12);
    it->icon = ic;
    it->is_new = nw;
}

static void sm_ensure_items(void) {
    if (g_items_init) return;
    g_items_init = true;
    sm_set_item(&g_pinned[0], "Impostazioni", "display",  ICON_SETTINGS,     false);
    sm_set_item(&g_pinned[1], "Esplora file", "thispc",   ICON_THIS_PC,      false);
    sm_set_item(&g_pinned[2], "Web Browser",  "browser",  ICON_BROWSER,      false);
    sm_set_item(&g_pinned[3], "Blocco note",  "notepad",  ICON_NOTEPAD,      false);
    sm_set_item(&g_pinned[4], "Calcolatrice", "calc",     ICON_MAINTENANCE,  false);
    sm_set_item(&g_pinned[5], "Media Player", "player",   ICON_MEDIA_PLAYER, false);
    sm_set_item(&g_pinned[6], "Prompt CMD",   "cmd",      ICON_RUN,          false);
    sm_set_item(&g_pinned[7], "Installa App", "pkgsvc",   ICON_INSTALLER,    true);

    sm_set_item(&g_all_apps[0],  "Blocco note",      "notepad",  ICON_NOTEPAD,      false);
    sm_set_item(&g_all_apps[1],  "Calcolatrice",     "calc",     ICON_MAINTENANCE,  false);
    sm_set_item(&g_all_apps[2],  "Caratteri (Font)", "fonts",    ICON_FONT_FILE,    true);
    sm_set_item(&g_all_apps[3],  "Esplora Risorse",  "thispc",   ICON_THIS_PC,      false);
    sm_set_item(&g_all_apps[4],  "Gestione Attivita","taskmgr",  ICON_CHIP,         false);
    sm_set_item(&g_all_apps[5],  "Info Sistema",     "about",    ICON_INFO,         false);
    sm_set_item(&g_all_apps[6],  "Installa App",     "pkgsvc",   ICON_INSTALLER,    true);
    sm_set_item(&g_all_apps[7],  "Media Player",     "player",   ICON_MEDIA_PLAYER, false);
    sm_set_item(&g_all_apps[8],  "Pannello Contr.",  "cpanel",   ICON_CONTROL_PANEL,false);
    sm_set_item(&g_all_apps[9],  "Porte COM / LPT",  "ports",    ICON_PRINTER,      true);
    sm_set_item(&g_all_apps[10], "Prompt Comandi",   "cmd",      ICON_RUN,          false);
    sm_set_item(&g_all_apps[11], "Regedit",          "regedit",  ICON_REGISTRY,     false);
    sm_set_item(&g_all_apps[12], "Sicurezza",        "seccpl",   ICON_INFO,         true);
    sm_set_item(&g_all_apps[13], "Web Browser",      "browser",  ICON_BROWSER,      false);
}

static bool sm_contains_nocase(const char* hay, const char* needle) {
    if (!needle || !needle[0]) return true;
    if (!hay) return false;
    for (int i = 0; hay[i]; i++) {
        int j = 0;
        while (needle[j] && hay[i + j]) {
            char a = hay[i + j], b = needle[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
            j++;
        }
        if (!needle[j]) return true;
    }
    return false;
}

static void sm_get_pinned(const sm_app_item_t** out_items, int* out_count) {
    sm_ensure_items();
    *out_items = g_pinned;
    *out_count = 8;
}

static void sm_get_all_apps(const sm_app_item_t** out_items, int* out_count) {
    sm_ensure_items();
    *out_items = g_all_apps;
    *out_count = 14;
}

static void sm_launch_target(const kernel_api_t* api, const char* target) {
    if (!api || !target) return;
    const kernel_ext_api_t* ext = (const kernel_ext_api_t*)api->ext_api;
    if (ext && ext->launcher_open) {
        ext->launcher_open(target, NULL);
    }
}

static void sm_draw(const kernel_api_t* api, int32_t sw, int32_t sh) {
    (void)sw;
    dll_theme_t t;
    dll_theme_init(api, &t);

    int32_t mw = 520;
    int32_t mh = 510;
    int32_t mx = 8;
    int32_t my = sh - 34 - mh;
    if (my < 8) my = 8;

    // Sfondo traslucido arrotondato stile Windows 11
    api->gfx_draw_drop_shadow(mx, my, mw, mh, 10);
    if (t.is_glass) {
        api->gfx_fill_rounded_alpha(mx, my, mw, mh, 12, t.card_bg ? t.card_bg : 0xEEF3FA, 238);
        api->gfx_draw_rounded_rect(mx, my, mw, mh, 12, t.card_border ? t.card_border : 0xB8C6D8);
    } else {
        api->gfx_fill_rect(mx, my, mw, mh, 0xF1F5F9);
        api->gfx_draw_rect(mx, my, mw, mh, 0x94A3B8);
    }

    // 1. Barra di ricerca unificata in alto
    int32_t sb_x = mx + 20, sb_y = my + 16, sb_w = mw - 40, sb_h = 28;
    api->gfx_fill_rounded_alpha(sb_x, sb_y, sb_w, sb_h, 14, 0xFFFFFF, 245);
    api->gfx_draw_rounded_rect(sb_x, sb_y, sb_w, sb_h, 14, t.accent);
    api->gfx_draw_icon_16(sb_x + 10, sb_y + 6, ICON_SEARCH);
    if (g_search_buf[0] != '\0') {
        char qdisp[40];
        dll_strcpy(qdisp, g_search_buf, sizeof(qdisp));
        size_t ql = dll_strlen(qdisp);
        if (ql + 1 < sizeof(qdisp)) { qdisp[ql] = '|'; qdisp[ql + 1] = '\0'; }
        api->gfx_draw_string(sb_x + 32, sb_y + 7, qdisp, t.text_primary, 0, true);
    } else {
        api->gfx_draw_string(sb_x + 32, sb_y + 7, "Cerca app, impostazioni e documenti", 0x64748B, 0, true);
    }

    // 2. Sezione "Preferiti" (griglia 4x2 o 8 elementi)
    int32_t pin_y = sb_y + 40;
    api->gfx_draw_string(mx + 22, pin_y, "Preferiti", t.text_primary, 0, true);

    const sm_app_item_t* pinned = NULL;
    int pin_cnt = 0;
    sm_get_pinned(&pinned, &pin_cnt);

    int32_t cell_w = (mw - 44) / 4;
    for (int i = 0; i < pin_cnt; i++) {
        int col = i % 4;
        int row = i / 4;
        int32_t ix = mx + 22 + col * cell_w;
        int32_t iy = pin_y + 20 + row * 64;
        api->gfx_draw_icon_32(ix + cell_w / 2 - 16, iy + 2, pinned[i].icon);
        char lbl[16];
        dll_strcpy(lbl, pinned[i].label, sizeof(lbl));
        int32_t lx = ix + (cell_w - (int32_t)dll_strlen(lbl) * 8) / 2;
        if (lx < ix + 2) lx = ix + 2;
        api->gfx_draw_string(lx, iy + 38, lbl, t.text_primary, 0, true);
        if (pinned[i].is_new) {
            int32_t nx = ix + (cell_w - 40) / 2;
            api->gfx_draw_string(nx, iy + 52, "Nuova", t.accent, 0, true);
        }
    }

    // 3. Sezione "Tutto" + Toggle Visualizzazione
    int32_t all_y = pin_y + 156;
    api->gfx_fill_alpha(mx + 20, all_y - 6, mw - 40, 1, 0xCBD5E1, 180);
    api->gfx_draw_string(mx + 22, all_y + 2, "Tutto", t.text_primary, 0, true);
    api->gfx_draw_string(mx + mw - 220, all_y + 2,
                         g_view_list_mode ? "Visualizzazione: Elenco v" : "Visualizzazione: Griglia v",
                         0x475569, 0, true);

    const sm_app_item_t* all_apps = NULL;
    int all_cnt = 0;
    sm_get_all_apps(&all_apps, &all_cnt);

    int shown = 0;
    int32_t list_top = all_y + 24;
    if (g_view_list_mode == 0) {
        // Griglia 4 colonne
        for (int i = 0; i < all_cnt; i++) {
            if (!sm_contains_nocase(all_apps[i].label, g_search_buf)) continue;
            if (shown < g_scroll_off * 4) { shown++; continue; }
            int vis_i = shown - g_scroll_off * 4;
            if (vis_i >= 12) break;
            int col = vis_i % 4;
            int row = vis_i / 4;
            int32_t ix = mx + 22 + col * cell_w;
            int32_t iy = list_top + row * 64;
            api->gfx_draw_icon_32(ix + cell_w / 2 - 16, iy + 2, all_apps[i].icon);
            char lbl[15];
            dll_strcpy(lbl, all_apps[i].label, sizeof(lbl));
            int32_t lx = ix + (cell_w - (int32_t)dll_strlen(lbl) * 8) / 2;
            if (lx < ix + 2) lx = ix + 2;
            api->gfx_draw_string(lx, iy + 38, lbl, t.text_primary, 0, true);
            if (all_apps[i].is_new) {
                api->gfx_draw_string(ix + (cell_w - 40) / 2, iy + 52, "Nuova", t.accent, 0, true);
            }
            shown++;
        }
    } else {
        // Elenco a 2 colonne
        int32_t col_w2 = (mw - 48) / 2;
        for (int i = 0; i < all_cnt; i++) {
            if (!sm_contains_nocase(all_apps[i].label, g_search_buf)) continue;
            if (shown >= 14) break;
            int col = shown % 2;
            int row = shown / 2;
            int32_t ix = mx + 22 + col * col_w2;
            int32_t iy = list_top + row * 24;
            api->gfx_draw_icon_16(ix + 4, iy + 4, all_apps[i].icon);
            api->gfx_draw_string(ix + 26, iy + 5, all_apps[i].label, t.text_primary, 0, true);
            if (all_apps[i].is_new) {
                api->gfx_draw_string(ix + col_w2 - 48, iy + 5, "Nuova", t.accent, 0, true);
            }
            shown++;
        }
    }

    // 4. Footer inferiore con Utente + Icone Accesso Rapido
    int32_t fy = my + mh - 42;
    api->gfx_fill_rounded_alpha(mx + 1, fy, mw - 2, 41, 10, t.is_glass ? 0xDCE5F2 : 0xE2E8F0, 225);
    api->gfx_fill_alpha(mx + 8, fy, mw - 16, 1, 0xCBD5E1, 180);

    api->gfx_draw_icon_16(mx + 22, fy + 12, ICON_ACCESSIBILITY);
    const char* uname = "Morgan (NewOS)";
    api->gfx_draw_string(mx + 44, fy + 13, uname, t.text_primary, 0, true);

    // 5 icone rapide a destra: Esplora File, CMD, Impostazioni, Porte/HW, Spegnimento
    const icon_id_t q_icons[5] = { ICON_FOLDER, ICON_RUN, ICON_CONTROL_PANEL, ICON_MAINTENANCE, ICON_ENERGY };
    for (int q = 0; q < 5; q++) {
        int32_t qx = mx + mw - 170 + q * 30;
        api->gfx_draw_icon_16(qx, fy + 12, q_icons[q]);
    }
}

static int32_t sm_mouse(const kernel_api_t* api, int32_t sw, int32_t sh,
                        int32_t mx_in, int32_t my_in, bool clicked) {
    (void)sw;
    if (!clicked) return 0;

    int32_t mw = 520, mh = 510, mx = 8, my = sh - 34 - mh;
    if (my < 8) my = 8;

    if (!(mx_in >= mx && mx_in <= mx + mw && my_in >= my && my_in <= my + mh)) {
        return 0; // Click fuori
    }

    int32_t sb_y = my + 16;
    int32_t pin_y = sb_y + 40;
    int32_t cell_w = (mw - 44) / 4;

    // Click su Preferiti
    const sm_app_item_t* pinned = NULL;
    int pin_cnt = 0;
    sm_get_pinned(&pinned, &pin_cnt);
    if (my_in >= pin_y + 20 && my_in < pin_y + 148) {
        int row = (my_in - (pin_y + 20)) / 64;
        int col = (mx_in - (mx + 22)) / cell_w;
        if (col >= 0 && col < 4) {
            int idx = row * 4 + col;
            if (idx >= 0 && idx < pin_cnt) {
                sm_launch_target(api, pinned[idx].target);
                return 2;
            }
        }
        return 1;
    }

    // Click su "Visualizzazione: Griglia/Elenco"
    int32_t all_y = pin_y + 156;
    if (my_in >= all_y && my_in <= all_y + 20 && mx_in >= mx + mw - 225) {
        g_view_list_mode = !g_view_list_mode;
        return 1;
    }

    // Click su "Tutto"
    int32_t list_top = all_y + 24;
    int32_t fy = my + mh - 42;
    const sm_app_item_t* all_apps = NULL;
    int all_cnt = 0;
    sm_get_all_apps(&all_apps, &all_cnt);

    if (my_in >= list_top && my_in < fy) {
        int hit_vis = -1;
        if (g_view_list_mode == 0) {
            int row = (my_in - list_top) / 64;
            int col = (mx_in - (mx + 22)) / cell_w;
            if (col >= 0 && col < 4 && row >= 0) hit_vis = row * 4 + col + g_scroll_off * 4;
        } else {
            int32_t col_w2 = (mw - 48) / 2;
            int row = (my_in - list_top) / 24;
            int col = (mx_in - (mx + 22)) / col_w2;
            if (col >= 0 && col < 2 && row >= 0) hit_vis = row * 2 + col;
        }
        if (hit_vis >= 0) {
            int shown = 0;
            for (int i = 0; i < all_cnt; i++) {
                if (!sm_contains_nocase(all_apps[i].label, g_search_buf)) continue;
                if (shown == hit_vis) {
                    sm_launch_target(api, all_apps[i].target);
                    return 2;
                }
                shown++;
            }
        }
        return 1;
    }

    // Click sul Footer
    if (my_in >= fy && my_in <= my + mh) {
        const char* q_targets[5] = { "thispc", "cmd", "display", "ports", NULL };
        for (int q = 0; q < 5; q++) {
            int32_t qx = mx + mw - 170 + q * 30;
            if (mx_in >= qx - 6 && mx_in <= qx + 22) {
                if (q == 4) return 3; // Logout / Spegnimento
                sm_launch_target(api, q_targets[q]);
                return 2;
            }
        }
    }
    return 1;
}

__attribute__((section(".text.entry"), used))
int startmenu_dll_entry(const kernel_api_t* api, startmenu_params_t* p) {
    if (!api || !p) return -1;
    if (p->cmd == STARTMENU_CMD_DRAW) {
        sm_draw(api, p->sw, p->sh);
        return 0;
    }
    if (p->cmd == STARTMENU_CMD_MOUSE) {
        p->result = sm_mouse(api, p->sw, p->sh, p->mx, p->my, p->clicked);
        return 0;
    }
    if (p->cmd == STARTMENU_CMD_KEY) {
        char c = p->key;
        size_t len = dll_strlen(g_search_buf);
        if (c == '\b') {
            if (len > 0) g_search_buf[len - 1] = '\0';
            p->result = 1;
        } else if (c >= 32 && c <= 126 && len + 1 < sizeof(g_search_buf)) {
            g_search_buf[len] = c;
            g_search_buf[len + 1] = '\0';
            p->result = 1;
        } else {
            p->result = 0;
        }
        return 0;
    }
    return -1;
}
