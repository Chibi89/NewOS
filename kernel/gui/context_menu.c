#include <gui/context_menu.h>
#include <gui/wm.h>
#include <gui/graphics.h>
#include <gui/image.h>
#include <fs/registry.h>
#include <fs/vfs.h>
#include <apps/devmgmt.h>
#include <apps/oobe.h>
#include <string.h>
#include <mm/heap.h>

#define MAX_CACHED_ASSOCS 32

typedef struct {
    char ext[16];
    char desc[32];
    char action[32];
    char cmd[32];
} cached_assoc_t;

static cached_assoc_t g_assoc_cache[MAX_CACHED_ASSOCS];
static int g_assoc_count = 0;
static bool g_assoc_loaded = false;

// Funzione interna per estrarre campi dal formato: Descrizione|Azione|Comando
static void parse_registry_assoc(const char* reg_val, char* out_desc, size_t desc_sz,
                                 char* out_action, size_t action_sz,
                                 char* out_cmd, size_t cmd_sz) {
    if (!reg_val) return;

    const char* p1 = strchr(reg_val, '|');
    if (p1) {
        size_t len1 = p1 - reg_val;
        if (len1 >= desc_sz) len1 = desc_sz - 1;
        strncpy(out_desc, reg_val, len1);
        out_desc[len1] = '\0';

        p1++;
        const char* p2 = strchr(p1, '|');
        if (p2) {
            size_t len2 = p2 - p1;
            if (len2 >= action_sz) len2 = action_sz - 1;
            strncpy(out_action, p1, len2);
            out_action[len2] = '\0';

            p2++;
            strncpy(out_cmd, p2, cmd_sz - 1);
            out_cmd[cmd_sz - 1] = '\0';
        } else {
            strncpy(out_action, p1, action_sz - 1);
            out_action[action_sz - 1] = '\0';
            strncpy(out_cmd, "NOTEPAD.EXE", cmd_sz - 1);
        }
    } else {
        // Formato legacy o descrittivo semplice
        strncpy(out_desc, reg_val, desc_sz - 1);
        out_desc[desc_sz - 1] = '\0';

        if (strstr(reg_val, "Configurazione") || strstr(reg_val, "ini") || strstr(reg_val, "inf") || strstr(reg_val, "cfg")) {
            strncpy(out_action, "Modifica configurazione", action_sz - 1);
            strncpy(out_cmd, "NOTEPAD.EXE", cmd_sz - 1);
        } else if (strstr(reg_val, "SNDPLAYER") || strstr(reg_val, "wav") || strstr(reg_val, "Audio") || strstr(reg_val, "PLAYER")) {
            strncpy(out_action, "Riproduci audio", action_sz - 1);
            strncpy(out_cmd, "PLAYER.EXE", cmd_sz - 1);
        } else if (strstr(reg_val, "avi") || strstr(reg_val, "mp4") || strstr(reg_val, "Video")) {
            strncpy(out_action, "Riproduci video", action_sz - 1);
            strncpy(out_cmd, "PLAYER.EXE", cmd_sz - 1);
        } else if (strstr(reg_val, "VIEWER") || strstr(reg_val, "bmp") || strstr(reg_val, "jpeg") || strstr(reg_val, "Immagine")) {
            strncpy(out_action, "Imposta come sfondo", action_sz - 1);
            strncpy(out_cmd, "WALLPAPER", cmd_sz - 1);
        } else if (strstr(reg_val, "RULES") || strstr(reg_val, "nor")) {
            strncpy(out_action, "Apri regola", action_sz - 1);
            strncpy(out_cmd, "RULES.EXE", cmd_sz - 1);
        } else {
            strncpy(out_action, "Apri", action_sz - 1);
            strncpy(out_cmd, "NOTEPAD.EXE", cmd_sz - 1);
        }
        out_action[action_sz - 1] = '\0';
        out_cmd[cmd_sz - 1] = '\0';
    }
}

void context_menu_reload_from_registry(void) {
    g_assoc_count = 0;

    reg_key_t* hkcr = registry_get_key("HKEY_CLASSES_ROOT");
    if (!hkcr) return;

    for (int i = 0; i < hkcr->num_values && g_assoc_count < MAX_CACHED_ASSOCS; i++) {
        const char* vname = hkcr->values[i].name;
        if (!vname || vname[0] != '.') continue; // Solo estensioni

        cached_assoc_t* a = &g_assoc_cache[g_assoc_count++];
        strncpy(a->ext, vname, sizeof(a->ext) - 1);
        a->ext[sizeof(a->ext) - 1] = '\0';

        parse_registry_assoc(hkcr->values[i].str_val,
                             a->desc, sizeof(a->desc),
                             a->action, sizeof(a->action),
                             a->cmd, sizeof(a->cmd));
    }

    g_assoc_loaded = true;
}

void context_menu_init(void) {
    context_menu_reload_from_registry();
}

bool context_menu_query_extension(const char* ext, char* out_desc, size_t desc_sz,
                                   char* out_action, size_t action_sz,
                                   char* out_cmd, size_t cmd_sz) {
    if (!ext || ext[0] == '\0') return false;

    // Ricarica automaticamente se non ancora caricato
    if (!g_assoc_loaded) {
        context_menu_reload_from_registry();
    }

    // 1. Cerca nella tabella caricata da registry.dat
    for (int i = 0; i < g_assoc_count; i++) {
        if (strcasecmp(g_assoc_cache[i].ext, ext) == 0) {
            if (out_desc) { strncpy(out_desc, g_assoc_cache[i].desc, desc_sz - 1); out_desc[desc_sz - 1] = '\0'; }
            if (out_action) { strncpy(out_action, g_assoc_cache[i].action, action_sz - 1); out_action[action_sz - 1] = '\0'; }
            if (out_cmd) { strncpy(out_cmd, g_assoc_cache[i].cmd, cmd_sz - 1); out_cmd[cmd_sz - 1] = '\0'; }
            return true;
        }
    }

    // 2. Interroga direttamente il registro se un nuovo valore è stato inserito a runtime
    reg_key_t* hkcr = registry_get_key("HKEY_CLASSES_ROOT");
    if (hkcr) {
        for (int i = 0; i < hkcr->num_values; i++) {
            if (strcasecmp(hkcr->values[i].name, ext) == 0) {
                char d[32], a[32], c[32];
                parse_registry_assoc(hkcr->values[i].str_val, d, sizeof(d), a, sizeof(a), c, sizeof(c));
                if (out_desc) { strncpy(out_desc, d, desc_sz - 1); out_desc[desc_sz - 1] = '\0'; }
                if (out_action) { strncpy(out_action, a, action_sz - 1); out_action[action_sz - 1] = '\0'; }
                if (out_cmd) { strncpy(out_cmd, c, cmd_sz - 1); out_cmd[cmd_sz - 1] = '\0'; }

                // Auto-aggiorna la cache
                if (g_assoc_count < MAX_CACHED_ASSOCS) {
                    cached_assoc_t* na = &g_assoc_cache[g_assoc_count++];
                    strncpy(na->ext, ext, sizeof(na->ext) - 1);
                    na->ext[sizeof(na->ext) - 1] = '\0';
                    strncpy(na->desc, d, sizeof(na->desc) - 1);
                    strncpy(na->action, a, sizeof(na->action) - 1);
                    strncpy(na->cmd, c, sizeof(na->cmd) - 1);
                }
                return true;
            }
        }
    }

    // 3. Fallback di sicurezza standard per i 4 casi se non presenti nel file
    if (strcasecmp(ext, ".wav") == 0 || strcasecmp(ext, ".mp3") == 0 ||
        strcasecmp(ext, ".flac") == 0 || strcasecmp(ext, ".alac") == 0 ||
        strcasecmp(ext, ".mid") == 0) {
        if (out_desc) strncpy(out_desc, "File audio", desc_sz - 1);
        if (out_action) strncpy(out_action, "Riproduci audio", action_sz - 1);
        if (out_cmd) strncpy(out_cmd, "PLAYER.EXE", cmd_sz - 1);
        return true;
    } else if (strcasecmp(ext, ".avi") == 0 || strcasecmp(ext, ".mp4") == 0 ||
               strcasecmp(ext, ".av1") == 0 || strcasecmp(ext, ".webm") == 0) {
        if (out_desc) strncpy(out_desc, "Clip video", desc_sz - 1);
        if (out_action) strncpy(out_action, "Riproduci video", action_sz - 1);
        if (out_cmd) strncpy(out_cmd, "PLAYER.EXE", cmd_sz - 1);
        return true;
    } else if (strcasecmp(ext, ".bmp") == 0 || strcasecmp(ext, ".jpg") == 0 ||
               strcasecmp(ext, ".jpeg") == 0 || strcasecmp(ext, ".png") == 0 ||
               strcasecmp(ext, ".ico") == 0) {
        if (out_desc) strncpy(out_desc, "Grafica raster", desc_sz - 1);
        if (out_action) strncpy(out_action, "Imposta come sfondo", action_sz - 1);
        if (out_cmd) strncpy(out_cmd, "WALLPAPER", cmd_sz - 1);
        return true;
    } else if (strcasecmp(ext, ".ini") == 0 || strcasecmp(ext, ".inf") == 0 ||
               strcasecmp(ext, ".cfg") == 0 || strcasecmp(ext, ".conf") == 0) {
        if (out_desc) strncpy(out_desc, "File impostazioni", desc_sz - 1);
        if (out_action) strncpy(out_action, "Modifica configurazione", action_sz - 1);
        if (out_cmd) strncpy(out_cmd, "NOTEPAD.EXE", cmd_sz - 1);
        return true;
    }

    if (out_desc) strncpy(out_desc, "Documento", desc_sz - 1);
    if (out_action) strncpy(out_action, "Apri", action_sz - 1);
    if (out_cmd) strncpy(out_cmd, "NOTEPAD.EXE", cmd_sz - 1);
    return false;
}

static void add_menu_item(context_menu_t* menu, const char* label, context_menu_action_t act,
                          bool is_sep, bool is_dis, bool is_def, const char* cmd) {
    if (!menu || menu->num_items >= CTX_MENU_MAX_ITEMS) return;
    context_menu_item_t* it = &menu->items[menu->num_items++];
    strncpy(it->label, label, sizeof(it->label) - 1);
    it->label[sizeof(it->label) - 1] = '\0';
    it->action = act;
    it->is_separator = is_sep;
    it->is_disabled = is_dis;
    it->is_default = is_def;
    if (cmd) {
        strncpy(it->command, cmd, sizeof(it->command) - 1);
        it->command[sizeof(it->command) - 1] = '\0';
    } else {
        it->command[0] = '\0';
    }
}

void context_menu_build_for_item(context_menu_t* menu, const char* name, const char* full_path,
                                 bool is_folder, bool can_paste) {
    (void)can_paste;
    if (!menu) return;
    memset(menu, 0, sizeof(context_menu_t));
    menu->is_item = true;
    menu->is_folder = is_folder;
    strncpy(menu->target_name, name ? name : "", sizeof(menu->target_name) - 1);
    strncpy(menu->target_path, full_path ? full_path : "", sizeof(menu->target_path) - 1);

    if (is_folder) {
        add_menu_item(menu, "Apri cartella", CTX_ACTION_OPEN_PRIMARY, false, false, true, "FOLDER");
    } else {
        const char* dot = strrchr(menu->target_name, '.');
        char desc[32] = {0};
        char action[32] = "Apri";
        char cmd[32] = "NOTEPAD.EXE";

        if (dot) {
            context_menu_query_extension(dot, desc, sizeof(desc), action, sizeof(action), cmd, sizeof(cmd));
        }
        add_menu_item(menu, action, CTX_ACTION_OPEN_PRIMARY, false, false, true, cmd);
    }

    add_menu_item(menu, "", CTX_ACTION_NONE, true, false, false, NULL);
    add_menu_item(menu, "Taglia", CTX_ACTION_CUT, false, false, false, NULL);
    add_menu_item(menu, "Copia", CTX_ACTION_COPY, false, false, false, NULL);
    add_menu_item(menu, "", CTX_ACTION_NONE, true, false, false, NULL);
    add_menu_item(menu, "Elimina", CTX_ACTION_DELETE, false, false, false, NULL);
    add_menu_item(menu, "Rinomina", CTX_ACTION_RENAME, false, false, false, NULL);
    add_menu_item(menu, "", CTX_ACTION_NONE, true, false, false, NULL);
    add_menu_item(menu, "Proprieta", CTX_ACTION_PROPERTIES, false, false, false, NULL);
}

void context_menu_build_for_empty(context_menu_t* menu, const char* current_dir, bool can_paste) {
    if (!menu) return;
    memset(menu, 0, sizeof(context_menu_t));
    menu->is_item = false;
    menu->is_folder = false;
    strncpy(menu->target_path, current_dir ? current_dir : "", sizeof(menu->target_path) - 1);

    add_menu_item(menu, "Nuova cartella", CTX_ACTION_NEW_FOLDER, false, false, false, NULL);
    add_menu_item(menu, "Nuovo testo", CTX_ACTION_NEW_FILE, false, false, false, NULL);
    add_menu_item(menu, "", CTX_ACTION_NONE, true, false, false, NULL);
    add_menu_item(menu, "Incolla", CTX_ACTION_PASTE, false, !can_paste, false, NULL);
    add_menu_item(menu, "", CTX_ACTION_NONE, true, false, false, NULL);
    add_menu_item(menu, "Aggiorna", CTX_ACTION_REFRESH, false, false, false, NULL);
    add_menu_item(menu, "", CTX_ACTION_NONE, true, false, false, NULL);
    add_menu_item(menu, "Proprieta unita", CTX_ACTION_DRIVE_PROPS, false, false, false, NULL);
}

void context_menu_show(context_menu_t* menu, int32_t screen_x, int32_t screen_y,
                       int32_t bound_w, int32_t bound_h) {
    if (!menu) return;

    // Calcolo dinamico della larghezza in base alla voce di testo piu' lunga (8px/char + margini)
    int32_t max_w = 190;
    for (int i = 0; i < menu->num_items; i++) {
        if (!menu->items[i].is_separator && menu->items[i].label[0] != '\0') {
            int32_t needed = 12 + ((int32_t)strlen(menu->items[i].label) * 8) + 24;
            if (needed > max_w) {
                max_w = needed;
            }
        }
    }
    menu->width = max_w;

    int32_t total_h = 8;
    for (int i = 0; i < menu->num_items; i++) {
        total_h += menu->items[i].is_separator ? 6 : 18;
    }
    menu->height = total_h;

    menu->x = screen_x;
    menu->y = screen_y;

    if (menu->x + menu->width > bound_w) {
        menu->x = bound_w - menu->width - 4;
    }
    if (menu->y + menu->height > bound_h) {
        menu->y = bound_h - menu->height - 4;
    }
    if (menu->x < 4) menu->x = 4;
    if (menu->y < 4) menu->y = 4;

    menu->is_open = true;
}

void context_menu_hide(context_menu_t* menu) {
    if (menu) menu->is_open = false;
}

void context_menu_draw(const context_menu_t* menu) {
    if (!menu || !menu->is_open) return;

    // 1. Ombra proiettata grigia (stile Windows 2000)
    gfx_fill_rect(menu->x + 3, menu->y + 3, menu->width, menu->height, 0x808080);

    // 2. Sfondo pannello
    gfx_fill_rect(menu->x, menu->y, menu->width, menu->height, COLOR_BTNFACE);

    // 3. Cornice Bevel 3D in rilievo
    gfx_draw_bevel_rect(menu->x, menu->y, menu->width, menu->height, false);

    // 4. Rendering voci e separatori incisi
    int32_t cur_y = menu->y + 4;
    for (int i = 0; i < menu->num_items; i++) {
        const context_menu_item_t* it = &menu->items[i];
        if (it->is_separator) {
            gfx_draw_bevel_rect(menu->x + 4, cur_y + 2, menu->width - 8, 2, true);
            cur_y += 6;
        } else {
            uint32_t col = it->is_disabled ? 0x808080 : COLOR_WINDOWTEXT;
            gfx_draw_string(menu->x + 12, cur_y + 2, it->label, col, 0, true);
            if (it->is_default && !it->is_disabled) {
                // Grassetto simulato per azione primaria
                gfx_draw_string(menu->x + 13, cur_y + 2, it->label, col, 0, true);
            }
            cur_y += 18;
        }
    }
}

context_menu_action_t context_menu_handle_click(context_menu_t* menu, int32_t screen_x, int32_t screen_y) {
    if (!menu || !menu->is_open) return CTX_ACTION_NONE;

    if (screen_x < menu->x || screen_x > menu->x + menu->width ||
        screen_y < menu->y || screen_y > menu->y + menu->height) {
        menu->is_open = false;
        return CTX_ACTION_NONE;
    }

    int32_t cur_y = menu->y + 4;
    for (int i = 0; i < menu->num_items; i++) {
        context_menu_item_t* it = &menu->items[i];
        int item_h = it->is_separator ? 6 : 18;

        if (screen_y >= cur_y && screen_y < cur_y + item_h) {
            menu->is_open = false;
            if (it->is_separator || it->is_disabled) return CTX_ACTION_NONE;
            return it->action;
        }
        cur_y += item_h;
    }

    menu->is_open = false;
    return CTX_ACTION_NONE;
}

bool context_menu_execute_primary(const context_menu_t* menu) {
    if (!menu || menu->num_items == 0) return false;
    const char* cmd = menu->items[0].command;
    const char* path = menu->target_path;

    if (strcmp(cmd, "PLAYER.EXE") == 0) {
        app_player_open_file(path);
        return true;
    } else if (strcmp(cmd, "WALLPAPER") == 0) {
        image_t* img = image_load_from_file(path);
        if (img) {
            gfx_set_custom_wallpaper(img->pixels, img->width, img->height);
            image_free(img);
            app_notepad_open_file("Sfondo Desktop", "Nuovo sfondo applicato con successo dal file immagine selezionato sul desktop di NewOS.");
        } else {
            app_notepad_open_file(menu->target_name, "Impossibile decodificare l'immagine selezionata.");
        }
        return true;
    } else if (strcmp(cmd, "WEBBROWSER.EXE") == 0) {
        app_browser_open_url(path);
        return true;
    } else if (strcmp(cmd, "RULES.EXE") == 0) {
        app_rules_viewer_open_file(path);
        return true;
    } else if (strcmp(cmd, "DEVMGMT.EXE") == 0) {
        app_devmgmt_launch();
        return true;
    } else if (strcmp(cmd, "REGEDIT.EXE") == 0) {
        app_regedit_launch();
        return true;
    } else if (strcmp(cmd, "CPL") == 0) {
        if (strcasecmp(menu->target_name, "DISPLAY.CPL") == 0) app_display_props_launch();
        else if (strcasecmp(menu->target_name, "SOUND.CPL") == 0) app_sound_props_launch();
        else if (strcasecmp(menu->target_name, "MAIN.CPL") == 0 || strcasecmp(menu->target_name, "MOUSE.CPL") == 0) app_mouse_props_launch();
        else if (strcasecmp(menu->target_name, "POWER.CPL") == 0) app_power_props_launch();
        else if (strcasecmp(menu->target_name, "DEVMGMT.CPL") == 0) app_devmgmt_launch();
        else if (strcasecmp(menu->target_name, "REGEDIT.CPL") == 0) app_regedit_launch();
        else app_about_launch();
        return true;
    } else if (strcmp(cmd, "EXEC") == 0) {
        if (strcasecmp(menu->target_name, "CALC.EXE") == 0) app_calc_launch();
        else if (strcasecmp(menu->target_name, "NOTEPAD.EXE") == 0) app_notepad_launch();
        else if (strcasecmp(menu->target_name, "CMD.EXE") == 0) app_cmd_launch();
        else if (strcasecmp(menu->target_name, "TASKMGR.EXE") == 0) app_taskmgr_launch();
        else if (strcasecmp(menu->target_name, "EXPLORER.EXE") == 0) app_explorer_open_this_pc();
        else if (strcasecmp(menu->target_name, "PLAYER.EXE") == 0) app_player_launch();
        else if (strcasecmp(menu->target_name, "REGEDIT.EXE") == 0) app_regedit_launch();
        else if (strcasecmp(menu->target_name, "RULES.EXE") == 0) app_rules_viewer_launch();
        else if (strcasecmp(menu->target_name, "WEBBROWSER.EXE") == 0 || strcasecmp(menu->target_name, "BROWSER.EXE") == 0) app_browser_launch();
        else if (strcasecmp(menu->target_name, "SETUP.EXE") == 0 || strcasecmp(menu->target_name, "OOBE.EXE") == 0) app_oobe_launch();
        else app_notepad_open_file(menu->target_name, "Esecuzione applicazione PE64...");
        return true;
    } else {
        // Default: Notepad / file di testo o configurazione
        uint32_t fsz = 0;
        char* txt = (char*)vfs_read_entire_file(path, &fsz);
        char title_buf[64];
        const char* dot = strrchr(menu->target_name, '.');
        if (dot && (strcasecmp(dot, ".ini") == 0 || strcasecmp(dot, ".inf") == 0 || strcasecmp(dot, ".cfg") == 0)) {
            mini_snprintf(title_buf, sizeof(title_buf), "Configurazione - %s", menu->target_name);
        } else {
            mini_snprintf(title_buf, sizeof(title_buf), "%s", menu->target_name);
        }
        if (txt) {
            app_notepad_open_file(title_buf, txt);
            kfree(txt);
        } else {
            app_notepad_open_file(title_buf, "; File vuoto\n");
        }
        return true;
    }
}
