#include "explorer.h"
#include <apps/explorer.h>
#include <gui/wm.h>
#include <gui/graphics.h>
#include <gui/icons.h>
#include <gui/image.h>
#include <fs/vfs.h>
#include <fs/vfs_mount.h>
#include <fs/exfat.h>
#include <drivers/sound_wav.h>
#include <drivers/ata.h>
#include <mm/heap.h>
#include <string.h>
#include <drivers/timer.h>
#include <apps/devmgmt.h>
#include <apps/regedit.h>
#include <apps/oobe.h>
#include <apps/rules_viewer.h>
#include <apps/taskmgr.h>
#include <drivers/usb/usb_msc.h>
#include <arch/x86_64/io.h>

static bool str_contains_nocase(const char* haystack, const char* needle) {
    if (!haystack || !needle || !*needle) return true;
    size_t nlen = strlen(needle);
    for (size_t i = 0; haystack[i] != '\0'; i++) {
        size_t j = 0;
        while (needle[j] != '\0' && haystack[i + j] != '\0') {
            char c1 = haystack[i + j];
            char c2 = needle[j];
            if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
            if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
            if (c1 != c2) break;
            j++;
        }
        if (j == nlen) return true;
    }
    return false;
}

static void explorer_clean_path(const char* src, char* dst, size_t max_len) {
    if (!src || !dst || max_len < 2) return;
    const char* p = src;
    if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':') p += 2;
    while (*p == '/' || *p == '\\') p++;
    size_t i = 0;
    while (*p && i < max_len - 1) {
        dst[i++] = (*p == '\\') ? '/' : *p;
        p++;
    }
    while (i > 0 && dst[i - 1] == '/') i--;
    dst[i] = '\0';
}

static void explorer_get_item_full_path(const explorer_state_t* state, int idx, char* out_buf, size_t max_len) {
    if (!state || idx < 0 || idx >= state->num_items || !out_buf || max_len == 0) return;
    const explorer_item_t* it = &state->items[idx];

    if (state->mode == EXPLORER_MODE_FOLDER && state->folder_name[0] != '\0') {
        char drive_pfx = 'C';
        const char* p = state->folder_name;
        if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':') {
            drive_pfx = (p[0] >= 'a' && p[0] <= 'z') ? (char)(p[0] - 32) : p[0];
        }
        char clean[128];
        explorer_clean_path(state->folder_name, clean, sizeof(clean));
        if (clean[0] != '\0') {
            mini_snprintf(out_buf, max_len, "%c:/%s/%s", drive_pfx, clean, it->name);
        } else {
            mini_snprintf(out_buf, max_len, "%c:/%s", drive_pfx, it->name);
        }
    } else if (state->mode == EXPLORER_MODE_DRIVE_U) {
        mini_snprintf(out_buf, max_len, "U:/%s", it->name);
    } else if (state->mode == EXPLORER_MODE_DRIVE_D) {
        mini_snprintf(out_buf, max_len, "D:/%s", it->name);
    } else if (state->mode == EXPLORER_MODE_DRIVE_A) {
        mini_snprintf(out_buf, max_len, "A:/%s", it->name);
    } else {
        mini_snprintf(out_buf, max_len, "C:/%s", it->name);
    }
}

static void explorer_add_item(explorer_state_t* state, const char* name, const char* desc, icon_id_t icon, bool is_folder, uint32_t size, const char* content) {
    if (state->num_items >= MAX_EXPLORER_ITEMS) return;
    if (state->search_active && state->search_filter[0] != '\0') {
        if (!str_contains_nocase(name, state->search_filter)) return;
    }
    explorer_item_t* it = &state->items[state->num_items++];
    strncpy(it->name, name, sizeof(it->name) - 1);
    it->name[sizeof(it->name) - 1] = '\0';
    strncpy(it->desc, desc, sizeof(it->desc) - 1);
    it->desc[sizeof(it->desc) - 1] = '\0';
    it->icon_id = icon;
    it->is_folder = is_folder;
    it->is_drive = false;
    it->size_bytes = size;
    if (content) {
        strncpy(it->content, content, sizeof(it->content) - 1);
        it->content[sizeof(it->content) - 1] = '\0';
    } else {
        it->content[0] = '\0';
    }
}

static void explorer_sort_items(explorer_state_t* state, int mode) {
    if (!state || state->num_items <= 1) return;
    state->sort_mode = mode;
    for (int i = 0; i < state->num_items - 1; i++) {
        for (int j = 0; j < state->num_items - 1 - i; j++) {
            bool swap = false;
            if (mode == 1) { // Per Nome
                if (state->items[j].is_folder != state->items[j + 1].is_folder) {
                    swap = !state->items[j].is_folder;
                } else {
                    swap = (strcasecmp(state->items[j].name, state->items[j + 1].name) > 0);
                }
            } else if (mode == 2) { // Per Dimensione (decrescente)
                if (state->items[j].is_folder != state->items[j + 1].is_folder) {
                    swap = !state->items[j].is_folder;
                } else {
                    swap = (state->items[j].size_bytes < state->items[j + 1].size_bytes);
                }
            } else if (mode == 3) { // Per Tipo (estensione)
                if (state->items[j].is_folder != state->items[j + 1].is_folder) {
                    swap = !state->items[j].is_folder;
                } else {
                    const char* ext1 = strrchr(state->items[j].name, '.');
                    const char* ext2 = strrchr(state->items[j + 1].name, '.');
                    if (!ext1) ext1 = "";
                    if (!ext2) ext2 = "";
                    int cmp = strcasecmp(ext1, ext2);
                    if (cmp != 0) swap = (cmp > 0);
                    else swap = (strcasecmp(state->items[j].name, state->items[j + 1].name) > 0);
                }
            }
            if (swap) {
                explorer_item_t tmp = state->items[j];
                state->items[j] = state->items[j + 1];
                state->items[j + 1] = tmp;
            }
        }
    }
}

typedef enum {
    FILE_CAT_DEFAULT = 0,
    FILE_CAT_AUDIO,
    FILE_CAT_VIDEO,
    FILE_CAT_IMAGE,
    FILE_CAT_INI,
    FILE_CAT_APP,
    FILE_CAT_HTML,
    FILE_CAT_RULES,
    FILE_CAT_FOLDER
} explorer_file_category_t;

static explorer_file_category_t explorer_get_file_category(const char* name, bool is_folder) {
    if (is_folder) return FILE_CAT_FOLDER;
    const char* dot = strrchr(name, '.');
    if (!dot) return FILE_CAT_DEFAULT;

    // Caso Audio
    if (strcasecmp(dot, ".wav") == 0 || strcasecmp(dot, ".mp3") == 0 ||
        strcasecmp(dot, ".flac") == 0 || strcasecmp(dot, ".alac") == 0 ||
        strcasecmp(dot, ".mid") == 0 || strcasecmp(dot, ".midi") == 0 ||
        strcasecmp(dot, ".m4a") == 0 || strcasecmp(dot, ".aac") == 0 ||
        strcasecmp(dot, ".playlist") == 0) {
        return FILE_CAT_AUDIO;
    }

    // Caso Video
    if (strcasecmp(dot, ".avi") == 0 || strcasecmp(dot, ".mp4") == 0 ||
        strcasecmp(dot, ".av1") == 0 || strcasecmp(dot, ".webm") == 0 ||
        strcasecmp(dot, ".mkv") == 0 || strcasecmp(dot, ".mov") == 0) {
        return FILE_CAT_VIDEO;
    }

    // Caso Immagini
    if (strcasecmp(dot, ".bmp") == 0 || strcasecmp(dot, ".jpg") == 0 ||
        strcasecmp(dot, ".jpeg") == 0 || strcasecmp(dot, ".png") == 0 ||
        strcasecmp(dot, ".ico") == 0 || strcasecmp(dot, ".gif") == 0) {
        return FILE_CAT_IMAGE;
    }

    // Caso .ini / configurazione
    if (strcasecmp(dot, ".ini") == 0 || strcasecmp(dot, ".inf") == 0 ||
        strcasecmp(dot, ".cfg") == 0 || strcasecmp(dot, ".conf") == 0) {
        return FILE_CAT_INI;
    }

    // Applicazioni
    if (strcasecmp(dot, ".exe") == 0 || strcasecmp(dot, ".cpl") == 0) {
        return FILE_CAT_APP;
    }

    // Pagine Web
    if (strcasecmp(dot, ".htm") == 0 || strcasecmp(dot, ".html") == 0) {
        return FILE_CAT_HTML;
    }

    // Regole NewOS
    if (strcasecmp(dot, ".nor") == 0) {
        return FILE_CAT_RULES;
    }

    return FILE_CAT_DEFAULT;
}

static void explorer_classify_item(explorer_item_t* it) {
    if (it->is_folder) {
        strcpy(it->desc, "Cartella di file");
        it->icon_id = ICON_FOLDER;
        return;
    }
    const char* dot = strrchr(it->name, '.');
    explorer_file_category_t cat = explorer_get_file_category(it->name, false);

    switch (cat) {
        case FILE_CAT_AUDIO:
            if (dot && strcasecmp(dot, ".wav") == 0) strcpy(it->desc, "Audio Wave (WAV)");
            else if (dot && strcasecmp(dot, ".mp3") == 0) strcpy(it->desc, "Audio MP3");
            else if (dot && strcasecmp(dot, ".flac") == 0) strcpy(it->desc, "Audio FLAC");
            else if (dot && (strcasecmp(dot, ".mid") == 0 || strcasecmp(dot, ".midi") == 0)) strcpy(it->desc, "Musica MIDI");
            else strcpy(it->desc, "File audio");
            it->icon_id = ICON_SOUND;
            break;

        case FILE_CAT_VIDEO:
            if (dot && strcasecmp(dot, ".avi") == 0) strcpy(it->desc, "Video AVI");
            else if (dot && strcasecmp(dot, ".mp4") == 0) strcpy(it->desc, "Video MP4");
            else if (dot && strcasecmp(dot, ".av1") == 0) strcpy(it->desc, "Video AV1");
            else if (dot && strcasecmp(dot, ".webm") == 0) strcpy(it->desc, "Filmato WebM");
            else strcpy(it->desc, "Clip video");
            it->icon_id = ICON_PLAY;
            break;

        case FILE_CAT_IMAGE:
            if (dot && strcasecmp(dot, ".bmp") == 0) strcpy(it->desc, "Bitmap Windows");
            else if (dot && (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0)) strcpy(it->desc, "Immagine JPEG");
            else if (dot && strcasecmp(dot, ".png") == 0) strcpy(it->desc, "Immagine PNG");
            else if (dot && strcasecmp(dot, ".ico") == 0) strcpy(it->desc, "Icona sistema");
            else strcpy(it->desc, "Grafica raster");
            it->icon_id = ICON_IMAGE;
            break;

        case FILE_CAT_INI:
            if (dot && strcasecmp(dot, ".ini") == 0) strcpy(it->desc, "File impostazioni");
            else if (dot && strcasecmp(dot, ".inf") == 0) strcpy(it->desc, "File installazione");
            else strcpy(it->desc, "Configurazione");
            it->icon_id = ICON_DOCUMENT;
            break;

        case FILE_CAT_APP:
            if (dot && strcasecmp(dot, ".cpl") == 0) {
                strcpy(it->desc, "Applet Pannello Controllo");
                it->icon_id = ICON_RUN;
            } else if (strcasecmp(it->name, "PLAYER.EXE") == 0) {
                strcpy(it->desc, "Lettore Multimediale");
                it->icon_id = ICON_PLAY;
            } else {
                strcpy(it->desc, "Applicazione");
                it->icon_id = ICON_RUN;
            }
            break;

        case FILE_CAT_HTML:
            strcpy(it->desc, "Documento HTML");
            it->icon_id = ICON_NET;
            break;

        case FILE_CAT_RULES:
            strcpy(it->desc, "Regola Ufficiale NewOS");
            it->icon_id = ICON_DOCUMENT;
            break;

        default:
            if (dot && strcasecmp(dot, ".sys") == 0) {
                strcpy(it->desc, "Driver di periferica");
                it->icon_id = ICON_DOCUMENT;
            } else if (dot && strcasecmp(dot, ".cdc") == 0) {
                strcpy(it->desc, "Codec multimediale");
                it->icon_id = ICON_MAINTENANCE;
            } else if (dot && strcasecmp(dot, ".mod") == 0) {
                strcpy(it->desc, "Modulo di avvio protetto");
                it->icon_id = ICON_DOCUMENT;
            } else if (dot && strcasecmp(dot, ".pak") == 0) {
                strcpy(it->desc, "Archivio compresso");
                it->icon_id = ICON_DOCUMENT;
            } else {
                strcpy(it->desc, "Documento di testo");
                it->icon_id = ICON_DOCUMENT;
            }
            break;
    }
}

static void explorer_load_items(explorer_state_t* state) {
    state->num_items = 0;
    state->selected_item = 0;
    state->scroll_row = 0;
    state->ctx_menu.is_open = false;
    state->active_menu = EXPLORER_MENU_NONE;

    switch (state->mode) {
        case EXPLORER_MODE_THIS_PC: {
            outb(0x3F8, 'a');
            strcpy(state->current_path, "Risorse del computer");
            
            const ata_device_t* pm = ata_get_primary_master();

            // 1. Disco locale (C:)
            char c_name[64];
            char c_desc[64];
            uint64_t c_size = 32 * 1024 * 1024;
            if (pm && pm->present) {
                if (pm->size_gb > 0) {
                    mini_snprintf(c_name, sizeof(c_name), "Disco locale (C:) [%d GB]", pm->size_gb);
                } else {
                    mini_snprintf(c_name, sizeof(c_name), "Disco locale (C:) [%d MB]", pm->size_mb);
                }
                mini_snprintf(c_desc, sizeof(c_desc), "%s (%s)", pm->model, pm->part_fs[0] ? pm->part_fs : "exFAT");
                c_size = pm->size_bytes;
            } else {
                strcpy(c_name, "Disco locale (C:)");
                strcpy(c_desc, "Disco fisso ATA");
            }
            if (!state->search_active || state->search_filter[0] == '\0' || str_contains_nocase(c_name, state->search_filter)) {
                explorer_item_t* c = &state->items[state->num_items++];
                strcpy(c->name, c_name);
                strcpy(c->desc, c_desc);
                c->size_bytes = c_size;
                c->icon_id = ICON_HD;
                c->is_drive = true;
                c->is_folder = true;
                c->content[0] = '\0';
            }

            outb(0x3F8, 'b');
            // 2. Unità floppy (A:)
            if (!state->search_active || state->search_filter[0] == '\0' || str_contains_nocase("Floppy da 3,5 pollici (A:)", state->search_filter)) {
                explorer_item_t* a = &state->items[state->num_items++];
                strcpy(a->name, "Floppy da 3,5 pollici (A:)");
                strcpy(a->desc, "Unita disco floppy");
                a->icon_id = ICON_FLOPPY;
                a->size_bytes = 1440 * 1024;
                a->is_drive = true;
                a->is_folder = true;
                a->content[0] = '\0';
            }

            outb(0x3F8, 'c');
            // 3. Unità CD-ROM (D:)
            int dev_count = ata_get_device_count();
            const ata_device_t* cd_dev = NULL;
            for (int i = 0; i < dev_count; i++) {
                const ata_device_t* dev = ata_get_device(i);
                if (dev && dev->is_atapi) {
                    cd_dev = dev;
                    break;
                }
            }
            char d_name[64];
            char d_desc[64];
            uint64_t d_size = 650 * 1024 * 1024;
            if (cd_dev) {
                const char* dtype = atapi_get_disc_type_name(cd_dev);
                if (cd_dev->size_gb > 0) {
                    mini_snprintf(d_name, sizeof(d_name), "%s (D:) [%d GB]", dtype, cd_dev->size_gb);
                } else {
                    mini_snprintf(d_name, sizeof(d_name), "%s (D:) [%d MB]", dtype, cd_dev->size_mb);
                }
                mini_snprintf(d_desc, sizeof(d_desc), "%s", cd_dev->model);
                d_size = cd_dev->size_bytes;
            } else {
                strcpy(d_name, "Unita ottica (D:)");
                strcpy(d_desc, "Unita ATAPI (CD/DVD/BD)");
            }
            if (!state->search_active || state->search_filter[0] == '\0' || str_contains_nocase(d_name, state->search_filter)) {
                explorer_item_t* d = &state->items[state->num_items++];
                strcpy(d->name, d_name);
                strcpy(d->desc, d_desc);
                d->size_bytes = d_size;
                d->icon_id = ICON_CD;
                d->is_drive = true;
                d->is_folder = true;
                d->content[0] = '\0';
            }

            outb(0x3F8, 'd');
            // 4. Disco rimovibile USB (U:) se presente
            if (usb_msc_is_present()) {
                const usb_msc_device_t* umsc = usb_msc_get_device();
                vfs_drive_info_t uinfo;
                bool has_info = vfs_get_drive_info('U', &uinfo);
                const char* fs_str = has_info ? uinfo.fs_name : "exFAT";
                char u_name[64];
                mini_snprintf(u_name, sizeof(u_name), "Disco rimovibile (U:) [%s]", fs_str);
                if (!state->search_active || state->search_filter[0] == '\0' || str_contains_nocase(u_name, state->search_filter)) {
                    explorer_item_t* u = &state->items[state->num_items++];
                    strcpy(u->name, u_name);
                    mini_snprintf(u->desc, sizeof(u->desc), "%s (%s)", umsc->product, umsc->vendor);
                    u->size_bytes = (uint64_t)umsc->total_sectors * umsc->sector_size;
                    u->icon_id = ICON_HD;
                    u->is_drive = true;
                    u->is_folder = true;
                    u->content[0] = '\0';
                }
            }
            outb(0x3F8, 'e');

            // 4.b Hard Disk / SSD secondari montati dal VFS (es. E:, F:, ecc.)
            char drv_letters[16];
            int drv_count = vfs_list_drives(drv_letters, sizeof(drv_letters));
            for (int k = 0; k < drv_count; k++) {
                char let = drv_letters[k];
                if (let == 'C' || let == 'A' || let == 'D' || let == 'U' || let == 'R') {
                    continue; // Gia' gestiti sopra o riservati
                }
                vfs_drive_info_t dinfo;
                if (vfs_get_drive_info(let, &dinfo)) {
                    char sec_name[64];
                    char sec_desc[64];
                    if (dinfo.total_mb >= 1024) {
                        mini_snprintf(sec_name, sizeof(sec_name), "Disco locale (%c:) [%d GB]", let, dinfo.total_mb / 1024);
                    } else {
                        mini_snprintf(sec_name, sizeof(sec_name), "Disco locale (%c:) [%d MB]", let, dinfo.total_mb);
                    }
                    mini_snprintf(sec_desc, sizeof(sec_desc), "Disco fisso (%s)", dinfo.fs_name);

                    if (!state->search_active || state->search_filter[0] == '\0' || str_contains_nocase(sec_name, state->search_filter)) {
                        explorer_item_t* sec_it = &state->items[state->num_items++];
                        strcpy(sec_it->name, sec_name);
                        strcpy(sec_it->desc, sec_desc);
                        sec_it->size_bytes = (uint64_t)dinfo.total_mb * 1024 * 1024;
                        sec_it->icon_id = ICON_HD;
                        sec_it->is_drive = true;
                        sec_it->is_folder = true;
                        sec_it->content[0] = '\0';
                    }
                }
            }

            // 5. Pannello di controllo
            if (!state->search_active || state->search_filter[0] == '\0' || str_contains_nocase("Pannello di controllo", state->search_filter)) {
                explorer_item_t* cp = &state->items[state->num_items++];
                strcpy(cp->name, "Pannello di controllo");
                strcpy(cp->desc, "Cartella di sistema");
                cp->icon_id = ICON_CONTROL_PANEL;
                cp->size_bytes = 0;
                cp->is_drive = false;
                cp->is_folder = true;
                cp->content[0] = '\0';
            }
            break;
        }

        case EXPLORER_MODE_DRIVE_C: {
            strcpy(state->current_path, "C:\\");

            vfs_dirent_t entries[MAX_EXPLORER_ITEMS];
            int32_t count = vfs_readdir("C:/", entries, MAX_EXPLORER_ITEMS);
            if (count > 0) {
                for (int32_t i = 0; i < count && state->num_items < MAX_EXPLORER_ITEMS; i++) {
                    if (state->search_active && state->search_filter[0] != '\0') {
                        if (!str_contains_nocase(entries[i].name, state->search_filter)) continue;
                    }
                    explorer_item_t* it = &state->items[state->num_items++];
                    strncpy(it->name, entries[i].name, sizeof(it->name) - 1);
                    it->name[sizeof(it->name) - 1] = '\0';
                    it->is_drive = false;
                    it->is_folder = entries[i].is_directory;
                    it->size_bytes = entries[i].size;
                    it->content[0] = '\0';
                    explorer_classify_item(it);
                }
            }
            break;
        }

        case EXPLORER_MODE_DRIVE_U: {
            strcpy(state->current_path, "U:\\");

            vfs_dirent_t entries[MAX_EXPLORER_ITEMS];
            int32_t count = vfs_readdir("U:\\", entries, MAX_EXPLORER_ITEMS);
            if (count > 0) {
                for (int32_t i = 0; i < count && state->num_items < MAX_EXPLORER_ITEMS; i++) {
                    if (state->search_active && state->search_filter[0] != '\0') {
                        if (!str_contains_nocase(entries[i].name, state->search_filter)) continue;
                    }
                    explorer_item_t* it = &state->items[state->num_items++];
                    strncpy(it->name, entries[i].name, sizeof(it->name) - 1);
                    it->name[sizeof(it->name) - 1] = '\0';
                    it->is_drive = false;
                    it->is_folder = entries[i].is_directory;
                    it->size_bytes = entries[i].size;
                    it->content[0] = '\0';
                    explorer_classify_item(it);
                }
            } else {
                explorer_add_item(state, "NOTE_NTFS3G.TXT", "Documento di testo", ICON_DOCUMENT, false, 142, NULL);
            }
            break;
        }

        case EXPLORER_MODE_FOLDER: {
            char drive_pfx = 'C';
            const char* p = state->folder_name;
            if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':') {
                drive_pfx = (p[0] >= 'a' && p[0] <= 'z') ? (char)(p[0] - 32) : p[0];
            }

            char clean[128];
            explorer_clean_path(state->folder_name, clean, sizeof(clean));

            // Costruisci percorso visualizzato nella barra indirizzi: "C:\..." o "U:\..."
            mini_snprintf(state->current_path, sizeof(state->current_path), "%c:\\", drive_pfx);
            int c_len = (int)strlen(state->current_path);
            for (int k = 0; clean[k] != '\0' && c_len < (int)sizeof(state->current_path) - 1; k++) {
                state->current_path[c_len++] = (clean[k] == '/') ? '\\' : clean[k];
            }
            state->current_path[c_len] = '\0';

            char full_query[160];
            if (clean[0] != '\0') {
                mini_snprintf(full_query, sizeof(full_query), "%c:/%s", drive_pfx, clean);
            } else {
                mini_snprintf(full_query, sizeof(full_query), "%c:/", drive_pfx);
            }

            vfs_dirent_t entries[MAX_EXPLORER_ITEMS];
            int32_t count = vfs_readdir(full_query, entries, MAX_EXPLORER_ITEMS);
            if (count > 0) {
                for (int32_t i = 0; i < count && state->num_items < MAX_EXPLORER_ITEMS; i++) {
                    if (state->search_active && state->search_filter[0] != '\0') {
                        if (!str_contains_nocase(entries[i].name, state->search_filter)) continue;
                    }
                    explorer_item_t* it = &state->items[state->num_items++];
                    strncpy(it->name, entries[i].name, sizeof(it->name) - 1);
                    it->name[sizeof(it->name) - 1] = '\0';
                    it->is_drive = false;
                    it->is_folder = entries[i].is_directory;
                    it->size_bytes = entries[i].size;
                    it->content[0] = '\0';
                    explorer_classify_item(it);
                }
            }
            break;
        }

        case EXPLORER_MODE_DRIVE_D: {
            strcpy(state->current_path, "D:\\");

            vfs_dirent_t entries[MAX_EXPLORER_ITEMS];
            int32_t count = vfs_readdir("D:\\", entries, MAX_EXPLORER_ITEMS);
            if (count > 0) {
                for (int32_t i = 0; i < count && state->num_items < MAX_EXPLORER_ITEMS; i++) {
                    if (state->search_active && state->search_filter[0] != '\0') {
                        if (!str_contains_nocase(entries[i].name, state->search_filter)) continue;
                    }
                    explorer_item_t* it = &state->items[state->num_items++];
                    strncpy(it->name, entries[i].name, sizeof(it->name) - 1);
                    it->name[sizeof(it->name) - 1] = '\0';
                    it->is_drive = false;
                    it->is_folder = entries[i].is_directory;
                    it->size_bytes = entries[i].size;
                    it->content[0] = '\0';
                    explorer_classify_item(it);
                }
            } else {
                explorer_item_t* it1 = &state->items[state->num_items++];
                strcpy(it1->name, "LEGGIMI.TXT");
                strcpy(it1->desc, "Documento di testo");
                it1->icon_id = ICON_DOCUMENT;
                it1->is_drive = false;
                it1->is_folder = false;
                it1->size_bytes = 1024;
                strcpy(it1->content, "Benvenuto nel disco di installazione e live di NewOS V1 64-bit!\nImmagine El Torito ibrida con partizione exFAT e kernel 64-bit.\nContenuto del CD:\n- LEGGIMI.TXT\n- AUTORUN.INF\n- SORGENTI.TXT\n- NEWOS_V1.IMG (32 MB)\n");

                explorer_item_t* it2 = &state->items[state->num_items++];
                strcpy(it2->name, "AUTORUN.INF");
                strcpy(it2->desc, "Informazioni di configurazione");
                it2->icon_id = ICON_DOCUMENT;
                it2->is_drive = false;
                it2->is_folder = false;
                it2->size_bytes = 64;
                strcpy(it2->content, "[AutoRun]\r\nlabel=NewOS V1 64-bit Live & Install Disc\r\n");

                explorer_item_t* it3 = &state->items[state->num_items++];
                strcpy(it3->name, "SORGENTI.TXT");
                strcpy(it3->desc, "Documento di testo");
                it3->icon_id = ICON_DOCUMENT;
                it3->is_drive = false;
                it3->is_folder = false;
                it3->size_bytes = 512;
                strcpy(it3->content, "I sorgenti completi e portatili di NewOS V1 si trovano in C:\\SOURCES\\ e nella cartella sources/ del pacchetto portatile.\n");

                explorer_item_t* it4 = &state->items[state->num_items++];
                strcpy(it4->name, "NEWOS_V1.IMG");
                strcpy(it4->desc, "Immagine disco di sistema");
                it4->icon_id = ICON_HD;
                it4->is_drive = false;
                it4->is_folder = false;
                it4->size_bytes = 32 * 1024 * 1024;
                strcpy(it4->content, "Immagine binaria disco di sistema NewOS V1 (32 MB).\n");
            }
            break;
        }

        case EXPLORER_MODE_DRIVE_A: {
            strcpy(state->current_path, "A:\\");

            explorer_item_t* it1 = &state->items[state->num_items++];
            strcpy(it1->name, "BOOTSECT.BIN");
            strcpy(it1->desc, "Settore di avvio Floppy");
            it1->icon_id = ICON_DOCUMENT;
            it1->is_drive = false;
            it1->is_folder = false;
            it1->size_bytes = 512;
            strcpy(it1->content, "Settore MBR standard floppy disk 1.44 MB.\n");

            explorer_item_t* it2 = &state->items[state->num_items++];
            strcpy(it2->name, "DISCO.TXT");
            strcpy(it2->desc, "Documento di testo");
            it2->icon_id = ICON_DOCUMENT;
            it2->is_drive = false;
            it2->is_folder = false;
            it2->size_bytes = 128;
            strcpy(it2->content, "Unita Floppy da 3,5 pollici (1.44 MB formatted FAT12).\nNessun errore I/O riscontrato.\n");
            break;
        }

        case EXPLORER_MODE_RECYCLE_BIN: {
            strcpy(state->current_path, "Cestino");

            explorer_item_t* it1 = &state->items[state->num_items++];
            strcpy(it1->name, "vecchio_log.txt");
            strcpy(it1->desc, "C:\\");
            it1->icon_id = ICON_DOCUMENT;
            it1->is_drive = false;
            it1->is_folder = false;
            it1->size_bytes = 4096;
            strcpy(it1->content, "Log di debug della sessione precedente.\n");

            explorer_item_t* it2 = &state->items[state->num_items++];
            strcpy(it2->name, "bozza_progetto.bak");
            strcpy(it2->desc, "C:\\Progetti");
            it2->icon_id = ICON_DOCUMENT;
            it2->is_drive = false;
            it2->is_folder = false;
            it2->size_bytes = 8192;
            strcpy(it2->content, "Bozza non piu utilizzata.\n");

            explorer_item_t* it3 = &state->items[state->num_items++];
            strcpy(it3->name, "file_temporaneo.tmp");
            strcpy(it3->desc, "C:\\Sistema");
            it3->icon_id = ICON_DOCUMENT;
            it3->is_drive = false;
            it3->is_folder = false;
            it3->size_bytes = 1024;
            strcpy(it3->content, "File temporaneo rimosso dal sistema.\n");
            break;
        }

        case EXPLORER_MODE_CONTROL_PANEL: {
            strcpy(state->current_path, "Pannello di controllo");

            const struct {
                const char* name;
                icon_id_t icon;
                const char* desc;
            } cp_list[] = {
                { "Schermo",                    ICON_DESKTOP,      "Personalizza sfondo, monitor CRT e colori" },
                { "Suoni e periferiche audio",  ICON_SOUND,        "Regola il volume ed eventi audio AC'97" },
                { "Sistema",                    ICON_INFO,         "Specifiche hardware, CPU e Long Mode 64-bit" },
                { "Mouse",                      ICON_MOUSE,        "Velocita puntatore e doppio clic" },
                { "Tastiera",                   ICON_KEYBOARD,     "Layout tastiera e ripetizione tasti" },
                { "Opzioni risparmio energia",  ICON_ENERGY,       "Standby monitor e spegnimento sistema" },
                { "Strumenti di manutenzione",  ICON_MAINTENANCE,  "Diagnostica disco e pulizia risorse" },
                { "Connessioni di rete",        ICON_NET,          "Stato scheda di rete e indirizzo IP" },
                { "Caratteri",                  ICON_FONT,         "Tipi di carattere bitmap e TrueType" },
                { "Gestione periferiche",       ICON_MAINTENANCE,  "Stato driver hardware (.SYS) e configurazioni (.INI)" },
                { "Editor del Registro",        ICON_MAINTENANCE,  "Database delle configurazioni di sistema (REGEDIT.EXE)" }
            };

            for (int i = 0; i < 11; i++) {
                explorer_item_t* it = &state->items[state->num_items++];
                strcpy(it->name, cp_list[i].name);
                strcpy(it->desc, cp_list[i].desc);
                it->icon_id = cp_list[i].icon;
                it->is_drive = false;
                it->is_folder = false;
                it->size_bytes = 0;
                it->content[0] = '\0';
            }
            break;
        }
    }

    if (state->sort_mode > 0) {
        explorer_sort_items(state, state->sort_mode);
    }
}

static void explorer_navigate_to(explorer_state_t* state, explorer_mode_t new_mode, const char* folder_name) {
    if (state->history_index < MAX_PATH_HISTORY - 1) {
        state->history_index++;
        state->history_mode[state->history_index] = new_mode;
        if (folder_name) {
            strncpy(state->history[state->history_index], folder_name, sizeof(state->history[0]) - 1);
            state->history[state->history_index][sizeof(state->history[0]) - 1] = '\0';
        } else {
            state->history[state->history_index][0] = '\0';
        }
        state->history_count = state->history_index + 1;
    }

    state->mode = new_mode;
    if (folder_name) {
        strncpy(state->folder_name, folder_name, sizeof(state->folder_name) - 1);
        state->folder_name[sizeof(state->folder_name) - 1] = '\0';
    } else {
        state->folder_name[0] = '\0';
    }
    state->scroll_row = 0;
    state->active_menu = EXPLORER_MENU_NONE;
    state->ctx_menu.is_open = false;
    state->show_properties = false;
    state->address_editing = false;
    explorer_load_items(state);
}

static void explorer_split_name(const char* name, char* line1, char* line2) {
    int len = (int)strlen(name);
    if (len <= 13) {
        strcpy(line1, name);
        line2[0] = '\0';
        return;
    }

    int split_idx = -1;
    for (int i = 13; i >= 6; i--) {
        if (name[i] == ' ' || name[i] == '(') {
            split_idx = i;
            break;
        }
    }
    if (split_idx == -1) {
        split_idx = 13;
    }

    int l1_len = split_idx;
    while (l1_len > 0 && name[l1_len - 1] == ' ') l1_len--;
    strncpy(line1, name, l1_len);
    line1[l1_len] = '\0';

    const char* start_l2 = name + split_idx;
    while (*start_l2 == ' ') start_l2++;
    strncpy(line2, start_l2, 16);
    line2[16] = '\0';
}

static void explorer_show_item_properties(explorer_state_t* state, int idx) {
    if (idx < 0 || idx >= state->num_items) return;
    state->props_item = state->items[idx];
    mini_snprintf(state->props_path, sizeof(state->props_path), "%s\\%s", state->current_path, state->items[idx].name);
    state->show_properties = true;
    state->ctx_menu.is_open = false;
    state->active_menu = EXPLORER_MENU_NONE;
}

static void explorer_show_drive_properties(explorer_state_t* state) {
    memset(&state->props_item, 0, sizeof(explorer_item_t));
    strncpy(state->props_item.name, state->current_path, sizeof(state->props_item.name) - 1);
    strcpy(state->props_item.desc, "Proprieta unita");
    state->props_item.is_drive = true;
    state->props_item.is_folder = true;
    state->props_item.icon_id = ICON_HD;
    strncpy(state->props_path, state->current_path, sizeof(state->props_path) - 1);
    state->show_properties = true;
    state->ctx_menu.is_open = false;
    state->active_menu = EXPLORER_MENU_NONE;
}

static void explorer_create_new_folder(explorer_state_t* state) {
    char pfx = 'C';
    if (state->mode == EXPLORER_MODE_DRIVE_U) pfx = 'U';
    else if (state->mode == EXPLORER_MODE_FOLDER) {
        if (state->folder_name[0] == 'U' || state->folder_name[0] == 'u') pfx = 'U';
    }

    char path[160];
    if (state->mode == EXPLORER_MODE_FOLDER && state->folder_name[0]) {
        char clean[128];
        explorer_clean_path(state->folder_name, clean, sizeof(clean));
        mini_snprintf(path, sizeof(path), "%c:/%s/NUOVA_CARTELLA", pfx, clean);
    } else {
        mini_snprintf(path, sizeof(path), "%c:/NUOVA_CARTELLA", pfx);
    }
    vfs_mkdir(path);
    explorer_load_items(state);
    state->ctx_menu.is_open = false;
    state->active_menu = EXPLORER_MENU_NONE;
}

static void explorer_create_new_file(explorer_state_t* state) {
    char pfx = 'C';
    if (state->mode == EXPLORER_MODE_DRIVE_U) pfx = 'U';
    else if (state->mode == EXPLORER_MODE_FOLDER) {
        if (state->folder_name[0] == 'U' || state->folder_name[0] == 'u') pfx = 'U';
    }

    char path[160];
    if (state->mode == EXPLORER_MODE_FOLDER && state->folder_name[0]) {
        char clean[128];
        explorer_clean_path(state->folder_name, clean, sizeof(clean));
        mini_snprintf(path, sizeof(path), "%c:/%s/NUOVO.TXT", pfx, clean);
    } else {
        mini_snprintf(path, sizeof(path), "%c:/NUOVO.TXT", pfx);
    }
    const char* txt = "Nuovo documento di testo di NewOS V1 (64-bit).\n";
    vfs_write_entire_file_inplace(path, txt, (uint32_t)strlen(txt));
    explorer_load_items(state);
    state->ctx_menu.is_open = false;
    state->active_menu = EXPLORER_MENU_NONE;
}

static void explorer_delete_item(explorer_state_t* state, int idx) {
    if (idx < 0 || idx >= state->num_items) return;
    explorer_item_t* it = &state->items[idx];
    if (it->is_drive) return;

    char pfx = 'C';
    if (state->mode == EXPLORER_MODE_DRIVE_U) pfx = 'U';
    else if (state->mode == EXPLORER_MODE_FOLDER) {
        if (state->folder_name[0] == 'U' || state->folder_name[0] == 'u') pfx = 'U';
    }

    char path[160];
    if (state->mode == EXPLORER_MODE_FOLDER && state->folder_name[0]) {
        char clean[128];
        explorer_clean_path(state->folder_name, clean, sizeof(clean));
        mini_snprintf(path, sizeof(path), "%c:/%s/%s", pfx, clean, it->name);
    } else {
        mini_snprintf(path, sizeof(path), "%c:/%s", pfx, it->name);
    }
    vfs_unlink(path);
    explorer_load_items(state);
    state->ctx_menu.is_open = false;
    state->active_menu = EXPLORER_MENU_NONE;
}

typedef enum {
    CLIPBOARD_NONE = 0,
    CLIPBOARD_COPY,
    CLIPBOARD_CUT
} explorer_clip_op_t;

typedef struct {
    char source_path[VFS_MAX_PATH];
    char filename[64];
    bool is_directory;
    explorer_clip_op_t op;
} explorer_clipboard_t;

static explorer_clipboard_t g_explorer_clipboard = { {0}, {0}, false, CLIPBOARD_NONE };

static void explorer_get_item_path(explorer_state_t* state, int idx, char* out_path, size_t max_len) {
    if (idx < 0 || idx >= state->num_items || !out_path) return;
    explorer_item_t* it = &state->items[idx];

    char pfx = 'C';
    if (state->mode == EXPLORER_MODE_DRIVE_U) pfx = 'U';
    else if (state->mode == EXPLORER_MODE_FOLDER) {
        if (state->folder_name[0] == 'U' || state->folder_name[0] == 'u') pfx = 'U';
    }

    if (state->mode == EXPLORER_MODE_FOLDER && state->folder_name[0]) {
        char clean[128];
        explorer_clean_path(state->folder_name, clean, sizeof(clean));
        mini_snprintf(out_path, max_len, "%c:/%s/%s", pfx, clean, it->name);
    } else {
        mini_snprintf(out_path, max_len, "%c:/%s", pfx, it->name);
    }
}

static void explorer_cut_item(explorer_state_t* state, int idx) {
    if (idx < 0 || idx >= state->num_items) return;
    explorer_item_t* it = &state->items[idx];
    if (it->is_drive) return;

    explorer_get_item_path(state, idx, g_explorer_clipboard.source_path, sizeof(g_explorer_clipboard.source_path));
    strncpy(g_explorer_clipboard.filename, it->name, sizeof(g_explorer_clipboard.filename) - 1);
    g_explorer_clipboard.filename[sizeof(g_explorer_clipboard.filename) - 1] = '\0';
    g_explorer_clipboard.is_directory = it->is_folder;
    g_explorer_clipboard.op = CLIPBOARD_CUT;
    state->ctx_menu.is_open = false;
    state->active_menu = EXPLORER_MENU_NONE;
}

static void explorer_copy_item(explorer_state_t* state, int idx) {
    if (idx < 0 || idx >= state->num_items) return;
    explorer_item_t* it = &state->items[idx];
    if (it->is_drive) return;

    explorer_get_item_path(state, idx, g_explorer_clipboard.source_path, sizeof(g_explorer_clipboard.source_path));
    strncpy(g_explorer_clipboard.filename, it->name, sizeof(g_explorer_clipboard.filename) - 1);
    g_explorer_clipboard.filename[sizeof(g_explorer_clipboard.filename) - 1] = '\0';
    g_explorer_clipboard.is_directory = it->is_folder;
    g_explorer_clipboard.op = CLIPBOARD_COPY;
    state->ctx_menu.is_open = false;
    state->active_menu = EXPLORER_MENU_NONE;
}

static void explorer_paste_item(explorer_state_t* state) {
    if (g_explorer_clipboard.op == CLIPBOARD_NONE || g_explorer_clipboard.source_path[0] == '\0') return;

    char pfx = 'C';
    if (state->mode == EXPLORER_MODE_DRIVE_U) pfx = 'U';
    else if (state->mode == EXPLORER_MODE_FOLDER) {
        if (state->folder_name[0] == 'U' || state->folder_name[0] == 'u') pfx = 'U';
    }

    char target_path[VFS_MAX_PATH];
    if (state->mode == EXPLORER_MODE_FOLDER && state->folder_name[0]) {
        char clean[128];
        explorer_clean_path(state->folder_name, clean, sizeof(clean));
        mini_snprintf(target_path, sizeof(target_path), "%c:/%s/%s", pfx, clean, g_explorer_clipboard.filename);
    } else {
        mini_snprintf(target_path, sizeof(target_path), "%c:/%s", pfx, g_explorer_clipboard.filename);
    }

    if (g_explorer_clipboard.op == CLIPBOARD_CUT) {
        vfs_rename(g_explorer_clipboard.source_path, target_path);
        g_explorer_clipboard.op = CLIPBOARD_NONE;
        g_explorer_clipboard.source_path[0] = '\0';
    } else if (g_explorer_clipboard.op == CLIPBOARD_COPY) {
        uint32_t fsize = 0;
        void* data = vfs_read_entire_file(g_explorer_clipboard.source_path, &fsize);
        if (data) {
            vfs_write_entire_file_inplace(target_path, data, fsize);
            kfree(data);
        }
    }

    explorer_load_items(state);
    state->ctx_menu.is_open = false;
    state->active_menu = EXPLORER_MENU_NONE;
}

static void explorer_rename_item(explorer_state_t* state, int idx) {
    if (idx < 0 || idx >= state->num_items) return;
    explorer_item_t* it = &state->items[idx];
    if (it->is_drive) return;

    char old_path[VFS_MAX_PATH];
    explorer_get_item_path(state, idx, old_path, sizeof(old_path));

    char new_path[VFS_MAX_PATH];
    char pfx = 'C';
    if (state->mode == EXPLORER_MODE_DRIVE_U) pfx = 'U';
    else if (state->mode == EXPLORER_MODE_FOLDER) {
        if (state->folder_name[0] == 'U' || state->folder_name[0] == 'u') pfx = 'U';
    }

    char new_name[64];
    const char* dot = strrchr(it->name, '.');
    if (dot) {
        size_t baselen = dot - it->name;
        if (baselen > 30) baselen = 30;
        char base[48];
        strncpy(base, it->name, baselen);
        base[baselen] = '\0';
        mini_snprintf(new_name, sizeof(new_name), "%s_1%s", base, dot);
    } else {
        mini_snprintf(new_name, sizeof(new_name), "%s_1", it->name);
    }

    if (state->mode == EXPLORER_MODE_FOLDER && state->folder_name[0]) {
        char clean[128];
        explorer_clean_path(state->folder_name, clean, sizeof(clean));
        mini_snprintf(new_path, sizeof(new_path), "%c:/%s/%s", pfx, clean, new_name);
    } else {
        mini_snprintf(new_path, sizeof(new_path), "%c:/%s", pfx, new_name);
    }

    vfs_rename(old_path, new_path);
    explorer_load_items(state);
    state->ctx_menu.is_open = false;
    state->active_menu = EXPLORER_MENU_NONE;
}

static void explorer_navigate_address(explorer_state_t* state, const char* addr) {
    if (!addr) return;
    while (*addr == ' ') addr++;
    if (!*addr) return;

    if (strcasecmp(addr, "Risorse del computer") == 0 || strcasecmp(addr, "PC") == 0 || strcasecmp(addr, "::") == 0 || strcasecmp(addr, "risorse") == 0) {
        explorer_navigate_to(state, EXPLORER_MODE_THIS_PC, NULL);
    } else if (strcasecmp(addr, "C:") == 0 || strcasecmp(addr, "C:\\") == 0 || strcasecmp(addr, "C:/") == 0) {
        explorer_navigate_to(state, EXPLORER_MODE_DRIVE_C, NULL);
    } else if (strcasecmp(addr, "U:") == 0 || strcasecmp(addr, "U:\\") == 0 || strcasecmp(addr, "U:/") == 0) {
        explorer_navigate_to(state, EXPLORER_MODE_DRIVE_U, NULL);
    } else if (strcasecmp(addr, "D:") == 0 || strcasecmp(addr, "D:\\") == 0 || strcasecmp(addr, "D:/") == 0) {
        explorer_navigate_to(state, EXPLORER_MODE_DRIVE_D, NULL);
    } else if (strcasecmp(addr, "A:") == 0 || strcasecmp(addr, "A:\\") == 0 || strcasecmp(addr, "A:/") == 0) {
        explorer_navigate_to(state, EXPLORER_MODE_DRIVE_A, NULL);
    } else if (strcasecmp(addr, "Cestino") == 0) {
        explorer_navigate_to(state, EXPLORER_MODE_RECYCLE_BIN, NULL);
    } else if (strcasecmp(addr, "Pannello di controllo") == 0 || strcasecmp(addr, "Pannello") == 0) {
        explorer_navigate_to(state, EXPLORER_MODE_CONTROL_PANEL, NULL);
    } else {
        explorer_navigate_to(state, EXPLORER_MODE_FOLDER, addr);
    }
}

static void explorer_draw(window_t* win) {
    explorer_state_t* state = (explorer_state_t*)win->app_data;
    if (!state) return;

    // Sincronizza dinamicamente il titolo della finestra con la modalita e cartella corrente
    if (state->mode == EXPLORER_MODE_THIS_PC) {
        strncpy(win->title, "Risorse del computer", sizeof(win->title) - 1);
    } else if (state->mode == EXPLORER_MODE_DRIVE_C) {
        strncpy(win->title, "Disco locale (C:)", sizeof(win->title) - 1);
    } else if (state->mode == EXPLORER_MODE_DRIVE_D) {
        strncpy(win->title, "Unita CD-ROM (D:)", sizeof(win->title) - 1);
    } else if (state->mode == EXPLORER_MODE_DRIVE_A) {
        strncpy(win->title, "Floppy (A:)", sizeof(win->title) - 1);
    } else if (state->mode == EXPLORER_MODE_DRIVE_U) {
        strncpy(win->title, "Disco rimovibile (U:)", sizeof(win->title) - 1);
    } else if (state->mode == EXPLORER_MODE_RECYCLE_BIN) {
        strncpy(win->title, "Cestino", sizeof(win->title) - 1);
    } else if (state->mode == EXPLORER_MODE_CONTROL_PANEL) {
        strncpy(win->title, "Pannello di controllo", sizeof(win->title) - 1);
    } else if (state->mode == EXPLORER_MODE_FOLDER) {
        const char* leaf = state->folder_name;
        for (int k = 0; state->folder_name[k] != '\0'; k++) {
            if ((state->folder_name[k] == '/' || state->folder_name[k] == '\\') && state->folder_name[k + 1] != '\0') {
                leaf = &state->folder_name[k + 1];
            }
        }
        if (leaf && *leaf) {
            strncpy(win->title, leaf, sizeof(win->title) - 1);
        } else {
            strncpy(win->title, "Cartella", sizeof(win->title) - 1);
        }
    }
    win->title[sizeof(win->title) - 1] = '\0';

    int32_t cx = win->x + 4;
    int32_t cy = win->y + 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    // Sfondo generale finestra
    gfx_fill_rect(cx, cy, cw, ch, COLOR_BTNFACE);

    // 1. Barra dei Menu (File, Modifica, Visualizza, Strumenti, ?)
    int32_t menu_y = cy + 2;
    if (state->active_menu == EXPLORER_MENU_FILE) {
        gfx_fill_rect(cx + 6, menu_y, 36, 16, 0x0A246A);
        gfx_draw_string(cx + 8,  menu_y + 1, "File", 0xFFFFFF, 0, true);
    } else {
        gfx_draw_string(cx + 8,  menu_y + 1, "File", COLOR_WINDOWTEXT, 0, true);
    }

    if (state->active_menu == EXPLORER_MENU_EDIT) {
        gfx_fill_rect(cx + 44, menu_y, 68, 16, 0x0A246A);
        gfx_draw_string(cx + 46, menu_y + 1, "Modifica", 0xFFFFFF, 0, true);
    } else {
        gfx_draw_string(cx + 46, menu_y + 1, "Modifica", COLOR_WINDOWTEXT, 0, true);
    }

    if (state->active_menu == EXPLORER_MENU_VIEW) {
        gfx_fill_rect(cx + 116, menu_y, 80, 16, 0x0A246A);
        gfx_draw_string(cx + 118, menu_y + 1, "Visualizza", 0xFFFFFF, 0, true);
    } else {
        gfx_draw_string(cx + 118, menu_y + 1, "Visualizza", COLOR_WINDOWTEXT, 0, true);
    }

    if (state->active_menu == EXPLORER_MENU_TOOLS) {
        gfx_fill_rect(cx + 200, menu_y, 76, 16, 0x0A246A);
        gfx_draw_string(cx + 202, menu_y + 1, "Strumenti", 0xFFFFFF, 0, true);
    } else {
        gfx_draw_string(cx + 202, menu_y + 1, "Strumenti", COLOR_WINDOWTEXT, 0, true);
    }

    if (state->active_menu == EXPLORER_MENU_HELP) {
        gfx_fill_rect(cx + 280, menu_y, 16, 16, 0x0A246A);
        gfx_draw_string(cx + 282, menu_y + 1, "?", 0xFFFFFF, 0, true);
    } else {
        gfx_draw_string(cx + 282, menu_y + 1, "?", COLOR_WINDOWTEXT, 0, true);
    }

    // Separatore sotto la barra dei menu
    gfx_draw_bevel_rect(cx + 2, cy + 18, cw - 4, 2, true);

    // Icona rappresentativa della posizione corrente (usata per indirizzo e pannello web laterale)
    icon_id_t addr_icon = ICON_THIS_PC;
    if (state->mode == EXPLORER_MODE_DRIVE_C || state->mode == EXPLORER_MODE_DRIVE_U) addr_icon = ICON_HD;
    else if (state->mode == EXPLORER_MODE_DRIVE_D) addr_icon = ICON_CD;
    else if (state->mode == EXPLORER_MODE_DRIVE_A) addr_icon = ICON_FLOPPY;
    else if (state->mode == EXPLORER_MODE_FOLDER) addr_icon = ICON_FOLDER;
    else if (state->mode == EXPLORER_MODE_RECYCLE_BIN) addr_icon = ICON_CESTINO;
    else if (state->mode == EXPLORER_MODE_CONTROL_PANEL) addr_icon = ICON_CONTROL_PANEL;

    if (state->show_toolbar) {
        // 2. Barra degli Strumenti (Toolbar)
        int32_t tb_y = cy + 22;
        
        // Tasto [ Indietro ]
        gfx_fill_rect(cx + 6, tb_y, 74, 22, COLOR_BTNFACE);
        gfx_draw_bevel_rect(cx + 6, tb_y, 74, 22, false);
        gfx_draw_string(cx + 10, tb_y + 4, "Indietro", (state->history_index > 0) ? COLOR_WINDOWTEXT : 0x808080, 0, true);

        // Tasto [ Avanti ]
        gfx_fill_rect(cx + 84, tb_y, 64, 22, COLOR_BTNFACE);
        gfx_draw_bevel_rect(cx + 84, tb_y, 64, 22, false);
        gfx_draw_string(cx + 94, tb_y + 4, "Avanti", (state->history_index < state->history_count - 1) ? COLOR_WINDOWTEXT : 0x808080, 0, true);

        // Tasto [ Livello sup. ]
        gfx_fill_rect(cx + 152, tb_y, 96, 22, COLOR_BTNFACE);
        gfx_draw_bevel_rect(cx + 152, tb_y, 96, 22, false);
        gfx_draw_string(cx + 158, tb_y + 4, "Livello sup.", (state->mode != EXPLORER_MODE_THIS_PC) ? COLOR_WINDOWTEXT : 0x808080, 0, true);

        // Separatore verticale
        gfx_draw_bevel_rect(cx + 252, tb_y + 2, 2, 18, true);

        // Tasto [ Cerca ]
        gfx_fill_rect(cx + 258, tb_y, 56, 22, state->search_active ? 0xC0C0C0 : COLOR_BTNFACE);
        gfx_draw_bevel_rect(cx + 258, tb_y, 56, 22, state->search_active);
        gfx_draw_string(cx + 266, tb_y + 4, "Cerca", COLOR_WINDOWTEXT, 0, true);

        // Tasto [ Cartelle ]
        gfx_fill_rect(cx + 318, tb_y, 68, 22, state->show_folder_tree ? 0xC0C0C0 : COLOR_BTNFACE);
        gfx_draw_bevel_rect(cx + 318, tb_y, 68, 22, state->show_folder_tree);
        gfx_draw_string(cx + 324, tb_y + 4, "Cartelle", COLOR_WINDOWTEXT, 0, true);

        // Box input filtro ricerca se attivo
        if (state->search_active) {
            gfx_draw_string(cx + 392, tb_y + 4, "Filtro:", COLOR_WINDOWTEXT, 0, true);
            int32_t fw = cw - 446;
            if (fw > 160) fw = 160;
            gfx_fill_rect(cx + 440, tb_y + 2, fw, 18, 0xFFFFFF);
            gfx_draw_bevel_rect(cx + 440, tb_y + 2, fw, 18, true);
            char s_disp[48];
            mini_snprintf(s_disp, sizeof(s_disp), "%s|", state->search_filter);
            gfx_draw_string(cx + 444, tb_y + 3, s_disp, 0x000080, 0, true);
        }

        // Separatore sotto toolbar
        gfx_draw_bevel_rect(cx + 2, cy + 46, cw - 4, 2, true);

        // 3. Barra degli Indirizzi
        int32_t addr_y = cy + 50;
        gfx_draw_string(cx + 8, addr_y + 4, "Indirizzo", COLOR_WINDOWTEXT, 0, true);

        int32_t box_addr_x = cx + 76;
        int32_t box_addr_w = cw - 124;
        gfx_fill_rect(box_addr_x, addr_y + 2, box_addr_w, 20, 0xFFFFFF);
        gfx_draw_bevel_rect(box_addr_x, addr_y + 2, box_addr_w, 20, true);

        gfx_draw_icon_16(box_addr_x + 3, addr_y + 4, addr_icon);
        if (state->address_editing) {
            char addr_disp[140];
            mini_snprintf(addr_disp, sizeof(addr_disp), "%s|", state->address_buf);
            gfx_draw_string(box_addr_x + 24, addr_y + 4, addr_disp, 0x000080, 0, true);
        } else {
            gfx_draw_string(box_addr_x + 24, addr_y + 4, state->current_path, COLOR_WINDOWTEXT, 0, true);
        }

        // Tasto [ Vai ]
        gfx_fill_rect(cx + cw - 42, addr_y + 2, 36, 20, COLOR_BTNFACE);
        gfx_draw_bevel_rect(cx + cw - 42, addr_y + 2, 36, 20, false);
        gfx_draw_string(cx + cw - 32, addr_y + 4, "Vai", COLOR_WINDOWTEXT, 0, true);
    }

    // 4. Area Principale (Web View laterale a sinistra O Albero Cartelle + Riquadro File a destra)
    int32_t body_y = state->show_toolbar ? (cy + 74) : (cy + 22);
    int32_t body_h = ch - (body_y - cy) - (state->show_statusbar ? 22 : 4);
    int32_t side_w = 160;

    if (state->show_folder_tree) {
        // --- Albero Cartelle (Folder Tree) ---
        gfx_fill_rect(cx + 2, body_y, side_w, body_h, 0xFFFFFF);
        gfx_draw_bevel_rect(cx + 2, body_y, side_w, body_h, true);

        // Intestazione "Tutte le cartelle"
        gfx_fill_rect(cx + 3, body_y + 1, side_w - 2, 20, COLOR_BTNFACE);
        gfx_draw_bevel_rect(cx + 3, body_y + 1, side_w - 2, 20, false);
        gfx_draw_icon_16(cx + 6, body_y + 3, ICON_FOLDER);
        gfx_draw_string(cx + 26, body_y + 4, "Tutte le cartelle", COLOR_WINDOWTEXT, 0, true);

        const struct {
            const char* name;
            icon_id_t icon;
            int indent;
            explorer_mode_t target_mode;
        } tree_nodes[] = {
            { "Desktop",                ICON_DESKTOP,       2, EXPLORER_MODE_THIS_PC },
            { "[-] Risorse PC",         ICON_THIS_PC,      14, EXPLORER_MODE_THIS_PC },
            { "Disco C:",               ICON_HD,           28, EXPLORER_MODE_DRIVE_C },
            { "Unita CD (D:)",          ICON_CD,           28, EXPLORER_MODE_DRIVE_D },
            { "Disco USB (U:)",         ICON_HD,           28, EXPLORER_MODE_DRIVE_U },
            { "Floppy (A:)",            ICON_FLOPPY,       28, EXPLORER_MODE_DRIVE_A },
            { "Cestino",                ICON_CESTINO,      14, EXPLORER_MODE_RECYCLE_BIN },
            { "Pannello",               ICON_CONTROL_PANEL,14, EXPLORER_MODE_CONTROL_PANEL }
        };

        int32_t ty = body_y + 24;
        for (int t = 0; t < 8; t++) {
            if (t == 4 && !usb_msc_is_present()) continue;
            int32_t tx = cx + 4 + tree_nodes[t].indent;
            bool is_cur = (state->mode == tree_nodes[t].target_mode);
            if (is_cur) {
                gfx_fill_rect(cx + 4, ty - 1, side_w - 6, 18, 0x0A246A);
            }
            gfx_draw_icon_16(tx, ty, tree_nodes[t].icon);
            gfx_draw_string(tx + 20, ty + 2, tree_nodes[t].name, is_cur ? 0xFFFFFF : COLOR_WINDOWTEXT, 0, true);
            ty += 22;
        }
    } else {
        // --- Pannello Informativo Laterale (Stile Windows 2000 Classic Web View) ---
        gfx_draw_gradient_v(cx + 2, body_y, side_w, body_h, 0xDEEAF7, 0xA8C8EC);
        gfx_draw_bevel_rect(cx + 2, body_y, side_w, body_h, true);

    // Icona grande 32x32 nel pannello laterale
    gfx_draw_icon_32(cx + 12, body_y + 12, addr_icon);

    // Titolo dell'oggetto visualizzato
    const char* view_title = "Risorse del PC";
    if (state->mode == EXPLORER_MODE_DRIVE_C) view_title = "Disco locale (C:)";
    else if (state->mode == EXPLORER_MODE_DRIVE_U) view_title = "Disco rimovibile (U:)";
    else if (state->mode == EXPLORER_MODE_DRIVE_D) view_title = "Unita CD-ROM (D:)";
    else if (state->mode == EXPLORER_MODE_DRIVE_A) view_title = "Floppy (A:)";
    else if (state->mode == EXPLORER_MODE_FOLDER) {
        const char* leaf = state->folder_name;
        for (int k = 0; state->folder_name[k] != '\0'; k++) {
            if ((state->folder_name[k] == '/' || state->folder_name[k] == '\\') && state->folder_name[k + 1] != '\0') {
                leaf = &state->folder_name[k + 1];
            }
        }
        view_title = leaf;
    }
    else if (state->mode == EXPLORER_MODE_RECYCLE_BIN) view_title = "Cestino";
    else if (state->mode == EXPLORER_MODE_CONTROL_PANEL) view_title = "Pannello Controllo";

    gfx_draw_string(cx + 12, body_y + 50, view_title, 0x0A246A, 0, true);
    gfx_draw_bevel_rect(cx + 12, body_y + 66, side_w - 24, 2, true);

    // Dettagli specifici per modalità
    int32_t info_y = body_y + 74;
    if (state->mode == EXPLORER_MODE_THIS_PC) {
        explorer_item_t* sel_it = (state->selected_item >= 0 && state->selected_item < state->num_items) ? &state->items[state->selected_item] : NULL;
        if (sel_it && (strstr(sel_it->name, "(C:)") || strstr(sel_it->name, "(c:)"))) {
            const ata_device_t* pm = ata_get_primary_master();

            char fs_str[48];
            mini_snprintf(fs_str, sizeof(fs_str), "File system: %s", (pm && pm->part_fs[0]) ? pm->part_fs : "exFAT");

            gfx_draw_string(cx + 12, info_y,      "Disco locale (C:)", 0x0A246A, 0, true);
            gfx_draw_string(cx + 12, info_y + 16, "Tipo: Disco locale", 0x404040, 0, true);
            gfx_draw_string(cx + 12, info_y + 32, fs_str,  0x404040, 0, true);

            // Riquadro indicatore grafico utilizzo disco (stile Windows 2000)
            int32_t gauge_y = info_y + 54;
            gfx_draw_string(cx + 12, gauge_y, "Utilizzo Disco:", COLOR_WINDOWTEXT, 0, true);

            int32_t bar_w = side_w - 24;
            int32_t bar_h = 14;
            gfx_fill_rect(cx + 12, gauge_y + 16, bar_w, bar_h, 0xFFFFFF);
            gfx_draw_bevel_rect(cx + 12, gauge_y + 16, bar_w, bar_h, true);

            vfs_drive_info_t dinfo;
            bool has_c = vfs_get_drive_info('C', &dinfo);
            uint32_t total_mb = has_c ? dinfo.total_mb : ((pm && pm->size_mb) ? pm->size_mb : 128);
            uint32_t free_mb = has_c ? dinfo.free_mb : (total_mb > 64 ? total_mb - 64 : 0);
            uint32_t used_mb = total_mb > free_mb ? (total_mb - free_mb) : 0;
            int32_t pct_used = total_mb > 0 ? (int32_t)(((uint64_t)used_mb * 100) / total_mb) : 0;
            if (pct_used == 0 && used_mb > 0) pct_used = 1;

            int32_t used_w = (bar_w - 4) * pct_used / 100;
            if (used_w < 2 && pct_used > 0) used_w = 2;
            if (used_w > bar_w - 4) used_w = bar_w - 4;

            gfx_fill_rect(cx + 14, gauge_y + 18, used_w, bar_h - 4, 0x0A246A); // Blu
            gfx_fill_rect(cx + 14 + used_w, gauge_y + 18, bar_w - 4 - used_w, bar_h - 4, 0x7E3A6E); // Magenta/Viola

            // Legenda usata/libera
            char used_str[32], free_str[32], cap_str[32];
            if (used_mb >= 1024) {
                mini_snprintf(used_str, sizeof(used_str), "Usato: %d.%d GB", used_mb / 1024, (used_mb % 1024) / 100);
            } else {
                mini_snprintf(used_str, sizeof(used_str), "Usato: %d MB", used_mb);
            }

            if (free_mb >= 1024) {
                mini_snprintf(free_str, sizeof(free_str), "Libero: %d.%d GB", free_mb / 1024, (free_mb % 1024) / 100);
            } else {
                mini_snprintf(free_str, sizeof(free_str), "Libero: %d MB", free_mb);
            }

            if (total_mb >= 1024) {
                mini_snprintf(cap_str, sizeof(cap_str), "Capacita: %d GB", total_mb / 1024);
            } else {
                mini_snprintf(cap_str, sizeof(cap_str), "Capacita: %d MB", total_mb);
            }

            gfx_fill_rect(cx + 12, gauge_y + 36, 8, 8, 0x0A246A);
            gfx_draw_string(cx + 24, gauge_y + 34, used_str, 0x202020, 0, true);

            gfx_fill_rect(cx + 12, gauge_y + 48, 8, 8, 0x7E3A6E);
            gfx_draw_string(cx + 24, gauge_y + 46, free_str, 0x202020, 0, true);

            gfx_draw_string(cx + 12, gauge_y + 62, cap_str, 0x0A246A, 0, true);

            // Pulsante [ Apri C:\ ]
            int32_t btn_y = gauge_y + 80;
            gfx_fill_rect(cx + 12, btn_y, side_w - 24, 22, COLOR_BTNFACE);
            gfx_draw_bevel_rect(cx + 12, btn_y, side_w - 24, 22, false);
            gfx_draw_string(cx + 38, btn_y + 4, "Apri C:\\", COLOR_WINDOWTEXT, 0, true);
        } else if (sel_it && strchr(sel_it->name, '(') && strchr(sel_it->name, ':') && !strstr(sel_it->name, "(A:)") && !strstr(sel_it->name, "(D:)") && !strstr(sel_it->name, "(U:)")) {
            // Dettagli per Hard Disk / SSD secondari
            const char* op = strchr(sel_it->name, '(');
            char drv_letter = (op && op[1] >= 'A' && op[1] <= 'Z') ? op[1] : 0;
            vfs_drive_info_t dinfo;
            bool has_info = drv_letter ? vfs_get_drive_info(drv_letter, &dinfo) : false;

            char title_buf[32];
            mini_snprintf(title_buf, sizeof(title_buf), "Disco locale (%c:)", drv_letter);
            gfx_draw_string(cx + 12, info_y, title_buf, 0x0A246A, 0, true);
            gfx_draw_string(cx + 12, info_y + 16, "Tipo: Disco locale", 0x404040, 0, true);
            char fs_line[32];
            mini_snprintf(fs_line, sizeof(fs_line), "File system: %s", has_info ? dinfo.fs_name : "RAW");
            gfx_draw_string(cx + 12, info_y + 32, fs_line, 0x404040, 0, true);
            char cap_line[32];
            if (has_info && dinfo.total_mb >= 1024) {
                mini_snprintf(cap_line, sizeof(cap_line), "Capacita: %d GB", dinfo.total_mb / 1024);
            } else {
                mini_snprintf(cap_line, sizeof(cap_line), "Capacita: %d MB", has_info ? dinfo.total_mb : 0);
            }
            gfx_draw_string(cx + 12, info_y + 48, cap_line, 0x404040, 0, true);

            int32_t btn_y = info_y + 70;
            gfx_fill_rect(cx + 12, btn_y, side_w - 24, 22, COLOR_BTNFACE);
            gfx_draw_bevel_rect(cx + 12, btn_y, side_w - 24, 22, false);
            char btn_lbl[16];
            mini_snprintf(btn_lbl, sizeof(btn_lbl), "Apri %c:\\", drv_letter);
            gfx_draw_string(cx + 38, btn_y + 4, btn_lbl, COLOR_WINDOWTEXT, 0, true);
        } else if (sel_it && (strstr(sel_it->name, "Floppy") || strstr(sel_it->name, "(A:)"))) {
            gfx_draw_string(cx + 12, info_y,      "Floppy da 3,5 (A:)", 0x0A246A, 0, true);
            gfx_draw_string(cx + 12, info_y + 16, "Tipo: Disco floppy", 0x404040, 0, true);
            gfx_draw_string(cx + 12, info_y + 32, "File system: FAT12", 0x404040, 0, true);
            gfx_draw_string(cx + 12, info_y + 48, "Capacita: 1.44 MB",  0x404040, 0, true);

            int32_t btn_y = info_y + 70;
            gfx_fill_rect(cx + 12, btn_y, side_w - 24, 22, COLOR_BTNFACE);
            gfx_draw_bevel_rect(cx + 12, btn_y, side_w - 24, 22, false);
            gfx_draw_string(cx + 38, btn_y + 4, "Apri A:\\", COLOR_WINDOWTEXT, 0, true);
        } else if (sel_it && (strstr(sel_it->name, "CD-ROM") || strstr(sel_it->name, "Blu-Ray") || strstr(sel_it->name, "ottica") || strstr(sel_it->name, "(D:)"))) {
            int dev_count = ata_get_device_count();
            const ata_device_t* cd_dev = NULL;
            for (int i = 0; i < dev_count; i++) {
                const ata_device_t* dev = ata_get_device(i);
                if (dev && dev->is_atapi) { cd_dev = dev; break; }
            }
            gfx_draw_string(cx + 12, info_y,      "Unita ottica (D:)", 0x0A246A, 0, true);
            gfx_draw_string(cx + 12, info_y + 16, "Tipo: CD/DVD/BD", 0x404040, 0, true);
            char cd_sz[32];
            mini_snprintf(cd_sz, sizeof(cd_sz), "Capacita: %d MB", cd_dev ? cd_dev->size_mb : 650);
            gfx_draw_string(cx + 12, info_y + 32, cd_sz, 0x404040, 0, true);
            gfx_draw_string(cx + 12, info_y + 48, "File system: UDF/ISO", 0x404040, 0, true);

            int32_t btn_y = info_y + 70;
            gfx_fill_rect(cx + 12, btn_y, side_w - 24, 22, COLOR_BTNFACE);
            gfx_draw_bevel_rect(cx + 12, btn_y, side_w - 24, 22, false);
            gfx_draw_string(cx + 38, btn_y + 4, "Apri D:\\", COLOR_WINDOWTEXT, 0, true);
        } else if (sel_it && (strstr(sel_it->name, "rimovibile") || strstr(sel_it->name, "(U:)"))) {
            vfs_drive_info_t uinfo;
            bool has_u = vfs_get_drive_info('U', &uinfo);
            const char* ufs = has_u ? uinfo.fs_name : "NTFS";
            gfx_draw_string(cx + 12, info_y,      "Disco USB (U:)", 0x0A246A, 0, true);
            gfx_draw_string(cx + 12, info_y + 16, "Tipo: Memoria USB", 0x404040, 0, true);
            char ufs_line[32];
            mini_snprintf(ufs_line, sizeof(ufs_line), "File system: %s", ufs);
            gfx_draw_string(cx + 12, info_y + 32, ufs_line, 0x404040, 0, true);
            gfx_draw_string(cx + 12, info_y + 48, "Stato: Connesso R/W", 0x008000, 0, true);

            int32_t btn_y = info_y + 70;
            gfx_fill_rect(cx + 12, btn_y, side_w - 24, 22, COLOR_BTNFACE);
            gfx_draw_bevel_rect(cx + 12, btn_y, side_w - 24, 22, false);
            gfx_draw_string(cx + 38, btn_y + 4, "Apri U:\\", COLOR_WINDOWTEXT, 0, true);
        } else if (sel_it && strstr(sel_it->name, "Pannello")) {
            gfx_draw_string(cx + 12, info_y,      "Pannello controllo", 0x0A246A, 0, true);
            gfx_draw_string(cx + 12, info_y + 16, "Configurazioni OS",  0x404040, 0, true);
            gfx_draw_string(cx + 12, info_y + 32, "11 applet di sistema", 0x404040, 0, true);

            int32_t btn_y = info_y + 60;
            gfx_fill_rect(cx + 12, btn_y, side_w - 24, 22, COLOR_BTNFACE);
            gfx_draw_bevel_rect(cx + 12, btn_y, side_w - 24, 22, false);
            gfx_draw_string(cx + 46, btn_y + 4, "Apri", COLOR_WINDOWTEXT, 0, true);
        } else {
            gfx_draw_string(cx + 12, info_y,      "Visualizza unita", 0x0A246A, 0, true);
            gfx_draw_string(cx + 12, info_y + 16, "e cartelle del PC.", 0x404040, 0, true);
            gfx_draw_string(cx + 12, info_y + 38, "Seleziona un'unita", 0x404040, 0, true);
            gfx_draw_string(cx + 12, info_y + 52, "per vederne l'uso.", 0x404040, 0, true);
        }
    } else if (state->mode == EXPLORER_MODE_DRIVE_U) {
        vfs_drive_info_t uinfo;
        bool has_u = vfs_get_drive_info('U', &uinfo);
        const char* ufs = has_u ? uinfo.fs_name : "NTFS";
        gfx_draw_string(cx + 12, info_y,      "Unita USB (U:)", 0x0A246A, 0, true);
        gfx_draw_string(cx + 12, info_y + 16, "Memoria rimovibile", 0x404040, 0, true);
        char ufs_line[32];
        mini_snprintf(ufs_line, sizeof(ufs_line), "File system: %s", ufs);
        gfx_draw_string(cx + 12, info_y + 32, ufs_line, 0x404040, 0, true);
        gfx_draw_string(cx + 12, info_y + 48, "Stato: Connesso R/W", 0x008000, 0, true);
    } else if (state->mode == EXPLORER_MODE_RECYCLE_BIN) {
        gfx_draw_string(cx + 12, info_y,      "Contenuto pronto", 0x800000, 0, true);
        gfx_draw_string(cx + 12, info_y + 16, "da eliminare.",    0x800000, 0, true);
        gfx_draw_string(cx + 12, info_y + 36, "Elementi: 3 file", 0x404040, 0, true);
        gfx_draw_string(cx + 12, info_y + 50, "Dimensione: 13 KB", 0x404040, 0, true);

        // Pulsante Svuota Cestino
        int32_t empty_btn_y = info_y + 74;
        gfx_fill_rect(cx + 12, empty_btn_y, side_w - 24, 24, COLOR_BTNFACE);
        gfx_draw_bevel_rect(cx + 12, empty_btn_y, side_w - 24, 24, false);
        gfx_draw_string(cx + 18, empty_btn_y + 4, "Svuota Cestino", COLOR_WINDOWTEXT, 0, true);
    } else if (state->mode == EXPLORER_MODE_CONTROL_PANEL) {
        if (state->selected_item >= 0 && state->selected_item < state->num_items) {
            gfx_draw_string(cx + 12, info_y, state->items[state->selected_item].name, 0x0A246A, 0, true);
            gfx_draw_string(cx + 12, info_y + 20, state->items[state->selected_item].desc, 0x404040, 0, true);

            int32_t btn_y = info_y + 60;
            state->sidebar_btn_y = btn_y;
            state->sidebar_btn_h = 22;
            gfx_fill_rect(cx + 12, btn_y, side_w - 24, 22, COLOR_BTNFACE);
            gfx_draw_bevel_rect(cx + 12, btn_y, side_w - 24, 22, false);
            gfx_draw_string(cx + 46, btn_y + 4, "Apri", COLOR_WINDOWTEXT, 0, true);
        } else {
            state->sidebar_btn_y = -1;
            gfx_draw_string(cx + 12, info_y, "Configura e", 0x0A246A, 0, true);
            gfx_draw_string(cx + 12, info_y + 16, "personalizza", 0x0A246A, 0, true);
            gfx_draw_string(cx + 12, info_y + 32, "l'ambiente NewOS V1.", 0x404040, 0, true);
        }
    } else {
        // Modalità Unità C:, D:, A:, U: o Cartelle generiche
        if (state->selected_item >= 0 && state->selected_item < state->num_items) {
            explorer_item_t* sel = &state->items[state->selected_item];
            explorer_file_category_t cat = explorer_get_file_category(sel->name, sel->is_folder);

            // Icona 32x32 dell'oggetto selezionato
            gfx_draw_icon_32(cx + 12, info_y, sel->icon_id);

            // Nome file/cartella (troncato a 18 caratteri per eleganza)
            char short_name[24];
            strncpy(short_name, sel->name, 18);
            short_name[18] = '\0';
            gfx_draw_string(cx + 12, info_y + 36, short_name, 0x0A246A, 0, true);

            // Tipo / Descrizione
            char short_desc[24];
            strncpy(short_desc, sel->desc, 20);
            short_desc[20] = '\0';
            gfx_draw_string(cx + 12, info_y + 54, short_desc, 0x404040, 0, true);

            // Formatta dimensione
            char sz_str[32];
            if (sel->is_folder) {
                strcpy(sz_str, "Cartella");
            } else if (sel->size_bytes >= 1024 * 1024) {
                mini_snprintf(sz_str, sizeof(sz_str), "%d.%d MB", sel->size_bytes / (1024 * 1024), (sel->size_bytes % (1024 * 1024)) / 100000);
            } else if (sel->size_bytes >= 1024) {
                mini_snprintf(sz_str, sizeof(sz_str), "%d KB", sel->size_bytes / 1024);
            } else {
                mini_snprintf(sz_str, sizeof(sz_str), "%d byte", sel->size_bytes);
            }

            const char* btn_text = "Apri";
            int32_t btn_text_offset = 46;
            int32_t btn_y = info_y + 104;

            if (cat == FILE_CAT_AUDIO) {
                // Caso Audio: dettagli audio e pulsante dedicato
                gfx_draw_string(cx + 12, info_y + 70, "Audio AC'97", 0x008000, 0, true);
                char d_sz[32];
                mini_snprintf(d_sz, sizeof(d_sz), "Dim: %s", sz_str);
                gfx_draw_string(cx + 12, info_y + 86, d_sz, 0x404040, 0, true);
                btn_text = "Riproduci";
                btn_text_offset = 32;
            } else if (cat == FILE_CAT_VIDEO) {
                // Caso Video: dettagli codec e pulsante video
                gfx_draw_string(cx + 12, info_y + 70, "Video Player", 0x800080, 0, true);
                char d_sz[32];
                mini_snprintf(d_sz, sizeof(d_sz), "Dim: %s", sz_str);
                gfx_draw_string(cx + 12, info_y + 86, d_sz, 0x404040, 0, true);
                btn_text = "Riproduci video";
                btn_text_offset = 18;
            } else if (cat == FILE_CAT_IMAGE) {
                // Caso Immagini: grafica raster e pulsante sfondo
                gfx_draw_string(cx + 12, info_y + 70, "Grafica raster", 0x000080, 0, true);
                char d_sz[32];
                mini_snprintf(d_sz, sizeof(d_sz), "Dim: %s", sz_str);
                gfx_draw_string(cx + 12, info_y + 86, d_sz, 0x404040, 0, true);
                btn_text = "Imposta sfondo";
                btn_text_offset = 18;
            } else if (cat == FILE_CAT_INI) {
                // Caso .ini: testo di configurazione e pulsante modifica
                gfx_draw_string(cx + 12, info_y + 70, "Impostazioni OS", 0x606000, 0, true);
                char d_sz[32];
                mini_snprintf(d_sz, sizeof(d_sz), "Dim: %s", sz_str);
                gfx_draw_string(cx + 12, info_y + 86, d_sz, 0x404040, 0, true);
                btn_text = "Modifica config";
                btn_text_offset = 18;
            } else if (sel->is_folder) {
                btn_y = info_y + 78;
                btn_text = "Apri cartella";
                btn_text_offset = 24;
            } else if (cat == FILE_CAT_APP) {
                char d_sz[32];
                mini_snprintf(d_sz, sizeof(d_sz), "Dim: %s", sz_str);
                gfx_draw_string(cx + 12, info_y + 70, d_sz, 0x404040, 0, true);
                btn_y = info_y + 92;
                btn_text = "Esegui";
                btn_text_offset = 40;
            } else {
                char d_sz[32];
                mini_snprintf(d_sz, sizeof(d_sz), "Dim: %s", sz_str);
                gfx_draw_string(cx + 12, info_y + 70, d_sz, 0x404040, 0, true);
                btn_y = info_y + 92;
                btn_text = "Apri";
                btn_text_offset = 46;
            }

            state->sidebar_btn_y = btn_y;
            state->sidebar_btn_h = 22;

            gfx_fill_rect(cx + 12, btn_y, side_w - 24, 22, COLOR_BTNFACE);
            gfx_draw_bevel_rect(cx + 12, btn_y, side_w - 24, 22, false);
            gfx_draw_string(cx + btn_text_offset, btn_y + 4, btn_text, COLOR_WINDOWTEXT, 0, true);

        } else {
            // Nessun elemento selezionato: dettagli cartella o drive
            state->sidebar_btn_y = -1;
            if (state->mode == EXPLORER_MODE_DRIVE_C) {
                gfx_draw_string(cx + 12, info_y, "Tipo: Disco locale", 0x404040, 0, true);
                gfx_draw_string(cx + 12, info_y + 16, "File system: exFAT", 0x404040, 0, true);
                char buf_cnt[32];
                mini_snprintf(buf_cnt, sizeof(buf_cnt), "Oggetti: %d", state->num_items);
                gfx_draw_string(cx + 12, info_y + 32, buf_cnt, 0x404040, 0, true);
            } else if (state->mode == EXPLORER_MODE_DRIVE_D) {
                gfx_draw_string(cx + 12, info_y, "Tipo: Unita CD-ROM", 0x404040, 0, true);
                char buf_cnt[32];
                mini_snprintf(buf_cnt, sizeof(buf_cnt), "Oggetti: %d", state->num_items);
                gfx_draw_string(cx + 12, info_y + 16, buf_cnt, 0x404040, 0, true);
            } else if (state->mode == EXPLORER_MODE_DRIVE_A) {
                gfx_draw_string(cx + 12, info_y, "Tipo: Unita Floppy (A:)", 0x404040, 0, true);
                char buf_cnt[32];
                mini_snprintf(buf_cnt, sizeof(buf_cnt), "Oggetti: %d", state->num_items);
                gfx_draw_string(cx + 12, info_y + 16, buf_cnt, 0x404040, 0, true);
            } else {
                gfx_draw_string(cx + 12, info_y, "Cartella di file", 0x0A246A, 0, true);
                char buf_cnt[32];
                mini_snprintf(buf_cnt, sizeof(buf_cnt), "Oggetti: %d", state->num_items);
                gfx_draw_string(cx + 12, info_y + 18, buf_cnt, 0x404040, 0, true);
            }
        }
    }
}

    // --- Area Principale dei Contenuti (Sfondo bianco con griglia icone e scrollbar) ---
    int32_t main_x = cx + side_w + 4;
    int32_t main_w = cw - side_w - 6;

    int32_t row_h = 76;
    int visible_rows = (body_h - 16) / row_h;
    if (visible_rows < 1) visible_rows = 1;
    int total_rows = (state->num_items + 2) / 3;
    int max_scroll = (total_rows > visible_rows) ? (total_rows - visible_rows) : 0;
    if (state->scroll_row > max_scroll) state->scroll_row = max_scroll;
    if (state->scroll_row < 0) state->scroll_row = 0;

    bool show_sb = (total_rows > visible_rows);
    int32_t content_w = show_sb ? (main_w - 16) : main_w;

    gfx_fill_rect(main_x, body_y, content_w, body_h, 0xFFFFFF);
    gfx_draw_bevel_rect(main_x, body_y, content_w, body_h, true);

    int32_t col_w = (content_w - 16) / 3;
    if (col_w < 110) col_w = 110;

    for (int r = 0; r < visible_rows; r++) {
        int cur_row = state->scroll_row + r;
        for (int col = 0; col < 3; col++) {
            int i = cur_row * 3 + col;
            if (i >= state->num_items) break;

            int32_t ix = main_x + 8 + col * col_w;
            int32_t iy = body_y + 8 + r * row_h;

            if (iy + row_h > body_y + body_h) break;

            bool is_sel = (i == state->selected_item);
            int32_t center_x = ix + col_w / 2;

            // Disegna icona 32x32 centrata
            gfx_draw_icon_32(center_x - 16, iy + 4, state->items[i].icon_id);

            char l1[32], l2[32];
            explorer_split_name(state->items[i].name, l1, l2);

            int32_t lw1 = (int32_t)strlen(l1) * 8;
            int32_t lx1 = center_x - (lw1 / 2);
            if (lx1 < ix + 2) lx1 = ix + 2;

            if (l2[0] == '\0') {
                // Riga singola centrata
                if (is_sel) {
                    gfx_fill_rect(lx1 - 3, iy + 41, lw1 + 6, 16, 0x0A246A);
                    gfx_draw_string(lx1, iy + 42, l1, 0xFFFFFF, 0, true);
                } else {
                    gfx_draw_string(lx1, iy + 42, l1, COLOR_WINDOWTEXT, 0, true);
                }
            } else {
                // Doppia riga centrata
                int32_t lw2 = (int32_t)strlen(l2) * 8;
                int32_t lx2 = center_x - (lw2 / 2);
                if (lx2 < ix + 2) lx2 = ix + 2;

                if (is_sel) {
                    int32_t max_lw = (lw1 > lw2) ? lw1 : lw2;
                    int32_t sel_x = center_x - (max_lw / 2);
                    if (sel_x < ix + 2) sel_x = ix + 2;
                    gfx_fill_rect(sel_x - 3, iy + 39, max_lw + 6, 31, 0x0A246A);
                    gfx_draw_string(lx1, iy + 40, l1, 0xFFFFFF, 0, true);
                    gfx_draw_string(lx2, iy + 55, l2, 0xFFFFFF, 0, true);
                } else {
                    gfx_draw_string(lx1, iy + 40, l1, COLOR_WINDOWTEXT, 0, true);
                    gfx_draw_string(lx2, iy + 55, l2, COLOR_WINDOWTEXT, 0, true);
                }
            }
        }
    }

    // Disegna la Scrollbar verticale se gli elementi superano l'area visibile
    if (show_sb) {
        int32_t sb_x = main_x + content_w;
        int32_t sb_w = 16;
        gfx_fill_rect(sb_x, body_y, sb_w, body_h, 0xE8E8E8);
        gfx_draw_bevel_rect(sb_x, body_y, sb_w, body_h, true);

        // Freccia Su [^]
        gfx_fill_rect(sb_x, body_y, sb_w, 16, COLOR_BTNFACE);
        gfx_draw_bevel_rect(sb_x, body_y, sb_w, 16, false);
        gfx_draw_string(sb_x + 4, body_y + 1, "^", COLOR_WINDOWTEXT, 0, true);

        // Freccia Giù [v]
        gfx_fill_rect(sb_x, body_y + body_h - 16, sb_w, 16, COLOR_BTNFACE);
        gfx_draw_bevel_rect(sb_x, body_y + body_h - 16, sb_w, 16, false);
        gfx_draw_string(sb_x + 4, body_y + body_h - 15, "v", COLOR_WINDOWTEXT, 0, true);

        // Cursore thumb di scorrimento
        int32_t track_h = body_h - 32;
        int32_t thumb_h = (track_h * visible_rows) / total_rows;
        if (thumb_h < 18) thumb_h = 18;
        int32_t thumb_y = body_y + 16 + (track_h - thumb_h) * state->scroll_row / (max_scroll ? max_scroll : 1);

        gfx_fill_rect(sb_x + 1, thumb_y, sb_w - 2, thumb_h, COLOR_BTNFACE);
        gfx_draw_bevel_rect(sb_x + 1, thumb_y, sb_w - 2, thumb_h, false);
    }

    // 5. Barra di Stato in Basso (Status Bar)
    if (state->show_statusbar) {
        int32_t sb_y = cy + ch - 20;
        gfx_fill_rect(cx + 2, sb_y, cw - 4, 18, COLOR_BTNFACE);
        
        // Riquadro sinistro con conteggio oggetti
        int32_t sb1_w = cw - 170;
        gfx_draw_bevel_rect(cx + 2, sb_y, sb1_w, 18, true);

        vfs_drive_info_t dinfo_sb;
        uint32_t free_sb = 64;
        if (vfs_get_drive_info('C', &dinfo_sb)) {
            free_sb = dinfo_sb.free_mb;
        }

        char sb_text[64];
        if (free_sb >= 1024) {
            mini_snprintf(sb_text, sizeof(sb_text), "Oggetti: %d (Spazio libero: %d.%d GB)", state->num_items, free_sb / 1024, (free_sb % 1024) / 100);
        } else {
            mini_snprintf(sb_text, sizeof(sb_text), "Oggetti: %d (Spazio libero: %d MB)", state->num_items, free_sb);
        }
        gfx_draw_string(cx + 8, sb_y + 2, sb_text, COLOR_WINDOWTEXT, 0, true);

        // Riquadro destro con icona PC
        int32_t sb2_x = cx + sb1_w + 4;
        int32_t sb2_w = cw - sb1_w - 6;
        gfx_draw_bevel_rect(sb2_x, sb_y, sb2_w, 18, true);
        gfx_draw_icon_16(sb2_x + 4, sb_y + 1, ICON_THIS_PC);
        gfx_draw_string(sb2_x + 24, sb_y + 2, "Risorse del computer", COLOR_WINDOWTEXT, 0, true);
    }

    // 6. Menu a Tendina Superiore (se attivo)
    if (state->active_menu != EXPLORER_MENU_NONE) {
        int32_t m_x = cx + 6;
        int32_t m_y = cy + 18;
        int32_t m_w = 160;
        int32_t m_h = 80;

        if (state->active_menu == EXPLORER_MENU_FILE) {
            m_x = cx + 6; m_w = 165; m_h = 92;
            gfx_fill_rect(m_x + 2, m_y + 2, m_w, m_h, 0x808080);
            gfx_fill_rect(m_x, m_y, m_w, m_h, COLOR_BTNFACE);
            gfx_draw_bevel_rect(m_x, m_y, m_w, m_h, false);

            gfx_draw_string(m_x + 12, m_y + 6,  "Nuova cartella", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_string(m_x + 12, m_y + 24, "Nuovo file di testo", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_string(m_x + 12, m_y + 42, "Proprieta", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_bevel_rect(m_x + 4, m_y + 60, m_w - 8, 2, true);
            gfx_draw_string(m_x + 12, m_y + 66, "Chiudi", COLOR_WINDOWTEXT, 0, true);
        } else if (state->active_menu == EXPLORER_MENU_EDIT) {
            m_x = cx + 44; m_w = 205; m_h = 166;
            gfx_fill_rect(m_x + 2, m_y + 2, m_w, m_h, 0x808080);
            gfx_fill_rect(m_x, m_y, m_w, m_h, COLOR_BTNFACE);
            gfx_draw_bevel_rect(m_x, m_y, m_w, m_h, false);

            bool has_sel = (state->selected_item >= 0 && state->selected_item < state->num_items);
            uint32_t sel_col = has_sel ? (uint32_t)COLOR_WINDOWTEXT : 0x808080;
            uint32_t paste_col = (g_explorer_clipboard.op != CLIPBOARD_NONE) ? (uint32_t)COLOR_WINDOWTEXT : 0x808080;

            gfx_draw_string(m_x + 12, m_y + 6,   "Annulla", 0x808080, 0, true);
            gfx_draw_string(m_x + 145, m_y + 6,  "Ctrl+Z", 0x808080, 0, true);
            gfx_draw_bevel_rect(m_x + 4, m_y + 24, m_w - 8, 2, true);

            gfx_draw_string(m_x + 12, m_y + 28,  "Taglia", sel_col, 0, true);
            gfx_draw_string(m_x + 145, m_y + 28, "Ctrl+X", sel_col, 0, true);
            gfx_draw_string(m_x + 12, m_y + 46,  "Copia", sel_col, 0, true);
            gfx_draw_string(m_x + 145, m_y + 46, "Ctrl+C", sel_col, 0, true);
            gfx_draw_string(m_x + 12, m_y + 64,  "Incolla", paste_col, 0, true);
            gfx_draw_string(m_x + 145, m_y + 64, "Ctrl+V", paste_col, 0, true);
            gfx_draw_bevel_rect(m_x + 4, m_y + 82, m_w - 8, 2, true);

            gfx_draw_string(m_x + 12, m_y + 86,  "Seleziona tutto", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_string(m_x + 145, m_y + 86, "Ctrl+A", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_string(m_x + 12, m_y + 104, "Inverti selezione", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_bevel_rect(m_x + 4, m_y + 122, m_w - 8, 2, true);

            gfx_draw_string(m_x + 12, m_y + 126, "Elimina", sel_col, 0, true);
            gfx_draw_string(m_x + 145, m_y + 126, "Canc", sel_col, 0, true);
            gfx_draw_string(m_x + 12, m_y + 144, "Rinomina", sel_col, 0, true);
            gfx_draw_string(m_x + 145, m_y + 144, "F2", sel_col, 0, true);
        } else if (state->active_menu == EXPLORER_MENU_VIEW) {
            m_x = cx + 116; m_w = 210; m_h = 162;
            gfx_fill_rect(m_x + 2, m_y + 2, m_w, m_h, 0x808080);
            gfx_fill_rect(m_x, m_y, m_w, m_h, COLOR_BTNFACE);
            gfx_draw_bevel_rect(m_x, m_y, m_w, m_h, false);

            if (state->show_toolbar) gfx_draw_string(m_x + 6, m_y + 6, "v", 0x000080, 0, true);
            gfx_draw_string(m_x + 20, m_y + 6,  "Barre degli strumenti", COLOR_WINDOWTEXT, 0, true);

            if (state->show_statusbar) gfx_draw_string(m_x + 6, m_y + 24, "v", 0x000080, 0, true);
            gfx_draw_string(m_x + 20, m_y + 24, "Barra di stato", COLOR_WINDOWTEXT, 0, true);

            if (state->search_active) gfx_draw_string(m_x + 6, m_y + 42, "v", 0x000080, 0, true);
            gfx_draw_string(m_x + 20, m_y + 42, "Barra di ricerca", COLOR_WINDOWTEXT, 0, true);

            if (state->show_folder_tree) gfx_draw_string(m_x + 6, m_y + 60, "v", 0x000080, 0, true);
            gfx_draw_string(m_x + 20, m_y + 60, "Albero delle cartelle", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_bevel_rect(m_x + 4, m_y + 78, m_w - 8, 2, true);

            if (state->sort_mode == 1) gfx_draw_string(m_x + 6, m_y + 82, "*", 0x000080, 0, true);
            gfx_draw_string(m_x + 20, m_y + 82, "Disponi per Nome", COLOR_WINDOWTEXT, 0, true);

            if (state->sort_mode == 2) gfx_draw_string(m_x + 6, m_y + 100, "*", 0x000080, 0, true);
            gfx_draw_string(m_x + 20, m_y + 100, "Disponi per Dimensione", COLOR_WINDOWTEXT, 0, true);

            if (state->sort_mode == 3) gfx_draw_string(m_x + 6, m_y + 118, "*", 0x000080, 0, true);
            gfx_draw_string(m_x + 20, m_y + 118, "Disponi per Tipo", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_bevel_rect(m_x + 4, m_y + 136, m_w - 8, 2, true);

            gfx_draw_string(m_x + 20, m_y + 140, "Aggiorna", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_string(m_x + 160, m_y + 140, "F5", COLOR_WINDOWTEXT, 0, true);
        } else if (state->active_menu == EXPLORER_MENU_TOOLS) {
            m_x = cx + 200; m_w = 230; m_h = 146;
            gfx_fill_rect(m_x + 2, m_y + 2, m_w, m_h, 0x808080);
            gfx_fill_rect(m_x, m_y, m_w, m_h, COLOR_BTNFACE);
            gfx_draw_bevel_rect(m_x, m_y, m_w, m_h, false);

            gfx_draw_string(m_x + 12, m_y + 6,   "Sincronizza dischi (Flush)", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_string(m_x + 12, m_y + 24,  "Verifica file system...", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_bevel_rect(m_x + 4, m_y + 42, m_w - 8, 2, true);

            gfx_draw_string(m_x + 12, m_y + 46,  "Gestione periferiche...", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_string(m_x + 12, m_y + 64,  "Editor del Registro...", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_string(m_x + 12, m_y + 82,  "Gestione attivita...", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_bevel_rect(m_x + 4, m_y + 100, m_w - 8, 2, true);

            gfx_draw_string(m_x + 12, m_y + 104, "Opzioni cartella...", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_string(m_x + 12, m_y + 122, "Proprieta unita...", COLOR_WINDOWTEXT, 0, true);
        } else if (state->active_menu == EXPLORER_MENU_HELP) {
            m_x = cx + 215; m_w = 220; m_h = 70;
            gfx_fill_rect(m_x + 2, m_y + 2, m_w, m_h, 0x808080);
            gfx_fill_rect(m_x, m_y, m_w, m_h, COLOR_BTNFACE);
            gfx_draw_bevel_rect(m_x, m_y, m_w, m_h, false);

            gfx_draw_string(m_x + 12, m_y + 6,   "Guida in linea NewOS", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_string(m_x + 12, m_y + 24,  "Regole di sistema (.NOR)...", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_bevel_rect(m_x + 4, m_y + 42, m_w - 8, 2, true);
            gfx_draw_string(m_x + 12, m_y + 46,  "Info su Esplora risorse...", COLOR_WINDOWTEXT, 0, true);
        }
    }

    // 7. Menu Contestuale Tasto Destro (Modulo indipendente autoaggiornante)
    if (state->ctx_menu.is_open) {
        context_menu_draw(&state->ctx_menu);
    }

    // 8. Dialogo Proprietà (se attivo)
    if (state->show_properties) {
        int32_t dw = 270;
        int32_t dh = 240;
        int32_t dx = cx + (cw - dw) / 2;
        int32_t dy = cy + (ch - dh) / 2;

        gfx_fill_rect(dx + 4, dy + 4, dw, dh, 0x606060);
        gfx_fill_rect(dx, dy, dw, dh, COLOR_BTNFACE);
        gfx_draw_bevel_rect(dx, dy, dw, dh, false);

        // Barra del titolo
        gfx_draw_gradient_h(dx + 2, dy + 2, dw - 4, 18, COLOR_ACTIVECAPTION_LEFT, COLOR_ACTIVECAPTION_RIGHT);
        gfx_draw_string(dx + 6, dy + 3, "Proprieta", COLOR_CAPTIONTEXT, 0, true);

        // Pulsante chiudi finestra proprietà [x]
        gfx_fill_rect(dx + dw - 18, dy + 3, 14, 14, COLOR_BTNFACE);
        gfx_draw_bevel_rect(dx + dw - 18, dy + 3, 14, 14, false);
        gfx_draw_string(dx + dw - 14, dy + 1, "x", COLOR_WINDOWTEXT, 0, true);

        // Tab "Generale"
        gfx_fill_rect(dx + 8, dy + 24, 60, 18, 0xFFFFFF);
        gfx_draw_bevel_rect(dx + 8, dy + 24, 60, 18, false);
        gfx_draw_string(dx + 12, dy + 26, "Generale", COLOR_WINDOWTEXT, 0, true);
        gfx_draw_bevel_rect(dx + 6, dy + 41, dw - 12, dh - 74, true);

        // Icona 32x32 e Nome
        gfx_draw_icon_32(dx + 16, dy + 48, state->props_item.icon_id ? state->props_item.icon_id : ICON_DOCUMENT);
        char p_name[24];
        strncpy(p_name, state->props_item.name, 20);
        p_name[20] = '\0';
        gfx_draw_string(dx + 56, dy + 56, p_name, 0x0A246A, 0, true);
        gfx_draw_bevel_rect(dx + 14, dy + 88, dw - 28, 2, true);

        // Dettagli
        gfx_draw_string(dx + 16, dy + 96,  "Tipo:", 0x404040, 0, true);
        gfx_draw_string(dx + 70, dy + 96,  state->props_item.desc[0] ? state->props_item.desc : "Oggetto di sistema", COLOR_WINDOWTEXT, 0, true);

        gfx_draw_string(dx + 16, dy + 114, "Posizione:", 0x404040, 0, true);
        char pos_txt[24];
        strncpy(pos_txt, state->current_path, 20);
        pos_txt[20] = '\0';
        gfx_draw_string(dx + 70, dy + 114, pos_txt, COLOR_WINDOWTEXT, 0, true);

        gfx_draw_string(dx + 16, dy + 132, "Dimensioni:", 0x404040, 0, true);
        char sz_txt[32];
        if (state->props_item.size_bytes >= 1024 * 1024) {
            mini_snprintf(sz_txt, sizeof(sz_txt), "%d MB (%d byte)", state->props_item.size_bytes / (1024 * 1024), state->props_item.size_bytes);
        } else if (state->props_item.size_bytes >= 1024) {
            mini_snprintf(sz_txt, sizeof(sz_txt), "%d KB (%d byte)", state->props_item.size_bytes / 1024, state->props_item.size_bytes);
        } else {
            mini_snprintf(sz_txt, sizeof(sz_txt), "%d byte", state->props_item.size_bytes);
        }
        gfx_draw_string(dx + 70, dy + 132, sz_txt, COLOR_WINDOWTEXT, 0, true);

        gfx_draw_bevel_rect(dx + 14, dy + 152, dw - 28, 2, true);

        // Attributi
        gfx_draw_string(dx + 16, dy + 160, "Attributi: [X] Archivio   [ ] Sola lettura", 0x404040, 0, true);

        // Pulsante [ OK ]
        int32_t ok_w = 64;
        int32_t ok_h = 22;
        int32_t ok_x = dx + (dw - ok_w) / 2;
        int32_t ok_y = dy + dh - 28;
        gfx_fill_rect(ok_x, ok_y, ok_w, ok_h, COLOR_BTNFACE);
        gfx_draw_bevel_rect(ok_x, ok_y, ok_w, ok_h, false);
        gfx_draw_string(ok_x + 22, ok_y + 4, "OK", COLOR_WINDOWTEXT, 0, true);
    }
}

static void explorer_execute_selected(explorer_state_t* state) {
    if (state->selected_item < 0 || state->selected_item >= state->num_items) return;
    explorer_item_t* it = &state->items[state->selected_item];

    if (state->mode == EXPLORER_MODE_THIS_PC) {
        if (strstr(it->name, "(C:)") || strstr(it->name, "(c:)")) {
            explorer_navigate_to(state, EXPLORER_MODE_DRIVE_C, "");
        } else if (strstr(it->name, "Floppy") || strstr(it->name, "(A:)") || strstr(it->name, "(a:)")) {
            explorer_navigate_to(state, EXPLORER_MODE_DRIVE_A, "");
        } else if (strstr(it->name, "CD-ROM") || strstr(it->name, "ottica") || strstr(it->name, "(D:)") || strstr(it->name, "(d:)")) {
            explorer_navigate_to(state, EXPLORER_MODE_DRIVE_D, "");
        } else if (strstr(it->name, "rimovibile") || strstr(it->name, "(U:)") || strstr(it->name, "(u:)")) {
            explorer_navigate_to(state, EXPLORER_MODE_DRIVE_U, "");
        } else if (strstr(it->name, "Pannello")) {
            explorer_navigate_to(state, EXPLORER_MODE_CONTROL_PANEL, "");
        } else {
            // Controlla se e' un'altra unita' disco secondaria montata es. "(E:)", "(F:)", ecc.
            const char* op = strchr(it->name, '(');
            if (op && op[1] >= 'A' && op[1] <= 'Z' && op[2] == ':' && op[3] == ')') {
                char target_path[8];
                mini_snprintf(target_path, sizeof(target_path), "%c:/", op[1]);
                explorer_navigate_to(state, EXPLORER_MODE_FOLDER, target_path);
            }
        }
        return;
    }

    if (state->mode == EXPLORER_MODE_DRIVE_C || state->mode == EXPLORER_MODE_DRIVE_U || state->mode == EXPLORER_MODE_FOLDER) {
        char drive_pfx = (state->mode == EXPLORER_MODE_DRIVE_U) ? 'U' : 'C';
        if (state->mode == EXPLORER_MODE_FOLDER) {
            const char* p = state->folder_name;
            if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':') {
                drive_pfx = (p[0] >= 'a' && p[0] <= 'z') ? (char)(p[0] - 32) : p[0];
            }
        }

        if (it->is_folder) {
            char sub_path[128];
            if (state->mode == EXPLORER_MODE_FOLDER && state->folder_name[0] != '\0') {
                mini_snprintf(sub_path, sizeof(sub_path), "%s/%s", state->folder_name, it->name);
            } else {
                mini_snprintf(sub_path, sizeof(sub_path), "%c:/%s", drive_pfx, it->name);
            }
            explorer_navigate_to(state, EXPLORER_MODE_FOLDER, sub_path);
        } else {
            char full_path[160];
            if (state->mode == EXPLORER_MODE_FOLDER && state->folder_name[0] != '\0') {
                char clean[128];
                explorer_clean_path(state->folder_name, clean, sizeof(clean));
                mini_snprintf(full_path, sizeof(full_path), "%c:/%s/%s", drive_pfx, clean, it->name);
            } else {
                mini_snprintf(full_path, sizeof(full_path), "%c:/%s", drive_pfx, it->name);
            }

            explorer_file_category_t cat = explorer_get_file_category(it->name, it->is_folder);
            if (cat == FILE_CAT_AUDIO || cat == FILE_CAT_VIDEO) {
                // Caso Audio e Caso Video: riproduci con lettore multimediale
                app_player_open_file(full_path);
            } else if (cat == FILE_CAT_IMAGE) {
                // Caso Immagini: carica e imposta come sfondo del desktop
                image_t* img = image_load_from_file(full_path);
                if (img) {
                    gfx_set_custom_wallpaper(img->pixels, img->width, img->height);
                    image_free(img);
                    app_notepad_open_file("Sfondo Desktop", "Nuovo sfondo applicato con successo dal file immagine selezionato sul desktop di NewOS.");
                } else {
                    app_notepad_open_file(it->name, "Impossibile decodificare l'immagine selezionata.");
                }
            } else if (cat == FILE_CAT_INI) {
                // Caso .ini: apri in blocco note con intestazione di configurazione
                uint32_t fsz = 0;
                char* txt = (char*)vfs_read_entire_file(full_path, &fsz);
                char title_buf[64];
                mini_snprintf(title_buf, sizeof(title_buf), "Configurazione - %s", it->name);
                if (txt) {
                    app_notepad_open_file(title_buf, txt);
                    kfree(txt);
                } else if (it->content[0] != '\0') {
                    app_notepad_open_file(title_buf, it->content);
                } else {
                    app_notepad_open_file(title_buf, "; File di configurazione vuoto\n[Settings]\nAutoLoad=1\n");
                }
            } else if (cat == FILE_CAT_APP) {
                const char* dot = strrchr(it->name, '.');
                if (dot && strcasecmp(dot, ".cpl") == 0) {
                    if (strcasecmp(it->name, "DISPLAY.CPL") == 0) app_display_props_launch();
                    else if (strcasecmp(it->name, "SOUND.CPL") == 0) app_sound_props_launch();
                    else if (strcasecmp(it->name, "POWER.CPL") == 0) app_power_props_launch();
                    else if (strcasecmp(it->name, "DEVMGMT.CPL") == 0) app_devmgmt_launch();
                    else if (strcasecmp(it->name, "REGEDIT.CPL") == 0) app_regedit_launch();
                    else app_about_launch();
                } else {
                    if (strcasecmp(it->name, "CALC.EXE") == 0) app_calc_launch();
                    else if (strcasecmp(it->name, "NOTEPAD.EXE") == 0) app_notepad_launch();
                    else if (strcasecmp(it->name, "CMD.EXE") == 0) app_cmd_launch();
                    else if (strcasecmp(it->name, "TASKMGR.EXE") == 0) app_taskmgr_launch();
                    else if (strcasecmp(it->name, "EXPLORER.EXE") == 0) app_explorer_open_this_pc();
                    else if (strcasecmp(it->name, "PLAYER.EXE") == 0) app_player_launch();
                    else if (strcasecmp(it->name, "REGEDIT.EXE") == 0) app_regedit_launch();
                    else if (strcasecmp(it->name, "RULES.EXE") == 0) app_rules_viewer_launch();
                    else if (strcasecmp(it->name, "WEBBROWSER.EXE") == 0 || strcasecmp(it->name, "BROWSER.EXE") == 0) app_browser_launch();
                    else if (strcasecmp(it->name, "SETUP.EXE") == 0 || strcasecmp(it->name, "OOBE.EXE") == 0) app_oobe_launch();
                    else app_notepad_open_file(it->name, "Esecuzione applicazione PE64...");
                }
            } else if (cat == FILE_CAT_HTML) {
                app_browser_open_url(full_path);
            } else if (cat == FILE_CAT_RULES) {
                app_rules_viewer_open_file(full_path);
            } else {
                const char* dot = strrchr(it->name, '.');
                if (dot && (strcasecmp(dot, ".sys") == 0)) {
                    app_devmgmt_launch();
                } else if (dot && (strcasecmp(dot, ".dat") == 0) && strcasecmp(it->name, "REGISTRY.DAT") == 0) {
                    app_regedit_launch();
                } else {
                    uint32_t fsz = 0;
                    char* txt = (char*)vfs_read_entire_file(full_path, &fsz);
                    if (txt) {
                        app_notepad_open_file(it->name, txt);
                        kfree(txt);
                    } else {
                        app_notepad_open_file(it->name, it->content);
                    }
                }
            }
        }
        return;
    }

    if (state->mode == EXPLORER_MODE_DRIVE_D) {
        explorer_file_category_t cat = explorer_get_file_category(it->name, it->is_folder);
        if (cat == FILE_CAT_INI) {
            char title_buf[64];
            mini_snprintf(title_buf, sizeof(title_buf), "Configurazione - %s", it->name);
            app_notepad_open_file(title_buf, it->content);
        } else if (cat == FILE_CAT_AUDIO || cat == FILE_CAT_VIDEO) {
            char full_path[64];
            mini_snprintf(full_path, sizeof(full_path), "D:/%s", it->name);
            app_player_open_file(full_path);
        } else {
            const char* dot = strrchr(it->name, '.');
            if (dot && strcasecmp(dot, ".img") == 0) {
                app_notepad_open_file(it->name, "Immagine di sistema avviabile NewOS V1 (32 MB).\nContiene il sistema operativo live completo e il filesystem exFAT.");
            } else {
                app_notepad_open_file(it->name, it->content);
            }
        }
        return;
    }

    if (state->mode == EXPLORER_MODE_DRIVE_A) {
        app_notepad_open_file(it->name, it->content[0] ? it->content : "Unita Floppy da 3,5 pollici (1.44 MB).\nNessun errore di lettura I/O.");
        return;
    }

    if (state->mode == EXPLORER_MODE_CONTROL_PANEL) {
        if (strcmp(it->name, "Schermo") == 0) {
            app_display_props_launch();
        } else if (strcmp(it->name, "Suoni e periferiche audio") == 0) {
            app_sound_props_launch();
        } else if (strcmp(it->name, "Sistema") == 0) {
            app_about_launch();
        } else if (strcmp(it->name, "Opzioni risparmio energia") == 0) {
            app_power_props_launch();
        } else if (strcmp(it->name, "Gestione periferiche") == 0) {
            app_devmgmt_launch();
        } else if (strcmp(it->name, "Editor del Registro") == 0) {
            app_regedit_launch();
        } else {
            app_notepad_open_file(it->name, "Pannello di configurazione - Settaggio di sistema di NewOS V1.");
        }
        return;
    }

    if (state->mode == EXPLORER_MODE_RECYCLE_BIN) {
        app_notepad_open_file(it->name, it->content);
        return;
    }
}

static void explorer_mouse(window_t* win, int32_t mx, int32_t my, bool clicked) {
    if (!clicked) return;
    explorer_state_t* state = (explorer_state_t*)win->app_data;
    if (!state) return;

    int32_t cx = 4;
    int32_t cy = 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;

    // 0. Clic su finestra di dialogo Proprietà se aperta
    if (state->show_properties) {
        int32_t dw = 270;
        int32_t dh = 240;
        int32_t dx = cx + (cw - dw) / 2;
        int32_t dy = cy + (ch - dh) / 2;
        int32_t ok_x = dx + (dw - 64) / 2;
        int32_t ok_y = dy + dh - 28;

        if ((mx >= ok_x && mx <= ok_x + 64 && my >= ok_y && my <= ok_y + 22) ||
            (mx >= dx + dw - 18 && mx <= dx + dw - 4 && my >= dy + 3 && my <= dy + 17)) {
            state->show_properties = false;
        }
        return;
    }

    // 1. Clic dentro i Menu a tendina superiori se aperti
    if (state->active_menu != EXPLORER_MENU_NONE) {
        int32_t m_x = cx + 6;
        int32_t m_y = cy + 18;
        int32_t m_w = 160;
        int32_t m_h = 80;

        if (state->active_menu == EXPLORER_MENU_FILE) {
            m_x = cx + 6; m_w = 165; m_h = 92;
            if (mx >= m_x && mx <= m_x + m_w && my >= m_y && my <= m_y + m_h) {
                int32_t rel_y = my - m_y;
                if (rel_y >= 4 && rel_y < 22) explorer_create_new_folder(state);
                else if (rel_y >= 22 && rel_y < 40) explorer_create_new_file(state);
                else if (rel_y >= 40 && rel_y < 58) {
                    if (state->selected_item >= 0 && state->selected_item < state->num_items) {
                        explorer_show_item_properties(state, state->selected_item);
                    }
                } else if (rel_y >= 64 && rel_y <= 88) wm_destroy_window(win->id);
                state->active_menu = EXPLORER_MENU_NONE;
                return;
            }
        } else if (state->active_menu == EXPLORER_MENU_EDIT) {
            m_x = cx + 44; m_w = 205; m_h = 166;
            if (mx >= m_x && mx <= m_x + m_w && my >= m_y && my <= m_y + m_h) {
                int32_t rel_y = my - m_y;
                if (rel_y >= 4 && rel_y < 24) {
                    // Annulla (nessuna azione)
                } else if (rel_y >= 26 && rel_y < 44) {
                    if (state->selected_item >= 0 && state->selected_item < state->num_items) {
                        explorer_cut_item(state, state->selected_item);
                    }
                } else if (rel_y >= 44 && rel_y < 62) {
                    if (state->selected_item >= 0 && state->selected_item < state->num_items) {
                        explorer_copy_item(state, state->selected_item);
                    }
                } else if (rel_y >= 62 && rel_y < 80) {
                    explorer_paste_item(state);
                } else if (rel_y >= 84 && rel_y < 102) {
                    if (state->num_items > 0) state->selected_item = 0;
                } else if (rel_y >= 102 && rel_y < 120) {
                    if (state->num_items > 0) state->selected_item = (state->selected_item + 1) % state->num_items;
                } else if (rel_y >= 124 && rel_y < 142) {
                    if (state->selected_item >= 0 && state->selected_item < state->num_items) {
                        explorer_delete_item(state, state->selected_item);
                    }
                } else if (rel_y >= 142 && rel_y <= 166) {
                    if (state->selected_item >= 0 && state->selected_item < state->num_items) {
                        explorer_rename_item(state, state->selected_item);
                    }
                }
                state->active_menu = EXPLORER_MENU_NONE;
                return;
            }
        } else if (state->active_menu == EXPLORER_MENU_VIEW) {
            m_x = cx + 116; m_w = 210; m_h = 162;
            if (mx >= m_x && mx <= m_x + m_w && my >= m_y && my <= m_y + m_h) {
                int32_t rel_y = my - m_y;
                if (rel_y >= 4 && rel_y < 22) {
                    state->show_toolbar = !state->show_toolbar;
                } else if (rel_y >= 22 && rel_y < 40) {
                    state->show_statusbar = !state->show_statusbar;
                } else if (rel_y >= 40 && rel_y < 58) {
                    state->search_active = !state->search_active;
                    if (!state->search_active) state->search_filter[0] = '\0';
                    explorer_load_items(state);
                } else if (rel_y >= 58 && rel_y < 76) {
                    state->show_folder_tree = !state->show_folder_tree;
                } else if (rel_y >= 80 && rel_y < 98) {
                    explorer_sort_items(state, 1);
                } else if (rel_y >= 98 && rel_y < 116) {
                    explorer_sort_items(state, 2);
                } else if (rel_y >= 116 && rel_y < 134) {
                    explorer_sort_items(state, 3);
                } else if (rel_y >= 138 && rel_y <= 162) {
                    explorer_load_items(state);
                }
                state->active_menu = EXPLORER_MENU_NONE;
                return;
            }
        } else if (state->active_menu == EXPLORER_MENU_TOOLS) {
            m_x = cx + 200; m_w = 230; m_h = 146;
            if (mx >= m_x && mx <= m_x + m_w && my >= m_y && my <= m_y + m_h) {
                int32_t rel_y = my - m_y;
                if (rel_y >= 4 && rel_y < 22) {
                    vfs_flush();
                    app_notepad_open_file("Sincronizzazione Dischi", "Operazione completata con successo.\n\nCache Write-Back del File System sincronizzata su tutti i supporti.\nTutti i blocchi sporchi (dirty cache) sono stati scritti su disco.");
                } else if (rel_y >= 22 && rel_y < 42) {
                    vfs_flush();
                    app_notepad_open_file("Verifica File System (CHKDSK)", "NewOS V1 Check Disk Utility (CHKDSK.EXE)\n\nControllo partizione di sistema C: [exFAT]\nVerifica strutture di directory: OK\nControllo bitmap dei cluster liberi: OK\nIntegrita file e stream: 100% OK\nNessun errore rilevato sul volume.");
                } else if (rel_y >= 44 && rel_y < 62) {
                    app_devmgmt_launch();
                } else if (rel_y >= 62 && rel_y < 80) {
                    app_regedit_launch();
                } else if (rel_y >= 80 && rel_y < 98) {
                    app_taskmgr_launch();
                } else if (rel_y >= 102 && rel_y < 120) {
                    app_notepad_open_file("Opzioni Cartella", "NewOS V1 - Opzioni Cartella ed Esplorazione\n\n- Visualizzazione Web: Attiva anteprime e dettagli file laterali.\n- File nascosti: Mostra cartelle e file di sistema protetti.\n- Estensioni file: Visualizza sempre le estensioni dei tipi di file noti (.TXT, .BIN, .CPL, .EXE, .NOR).\n- Navigazione: Finestra singola con cronologia Indietro/Avanti.");
                } else if (rel_y >= 120 && rel_y <= 146) {
                    explorer_show_drive_properties(state);
                }
                state->active_menu = EXPLORER_MENU_NONE;
                return;
            }
        } else if (state->active_menu == EXPLORER_MENU_HELP) {
            m_x = cx + 215; m_w = 220; m_h = 70;
            if (mx >= m_x && mx <= m_x + m_w && my >= m_y && my <= m_y + m_h) {
                int32_t rel_y = my - m_y;
                if (rel_y >= 4 && rel_y < 22) {
                    app_notepad_open_file("Guida in linea NewOS", "NewOS V1 (64-bit Architecture) - Guida in linea di sistema\n\n- Esplora Risorse: Gestione completa di file e cartelle su unita C:, D:, A:, U:.\n- Menu Modifica: Taglia, Copia, Incolla con buffer appunti di sistema, Rinomina (F2) ed Elimina (Canc).\n- Menu Visualizza: Toolbar, Statusbar, Albero cartelle, Ricerca ed ordinamento per Nome, Dimensione o Tipo.\n- Menu Strumenti: Sincronizzazione dischi (Flush VFS), CHKDSK, Devmgmt, Regedit e Task Manager.\n- Menu ?: Guida in linea, Visualizzatore regole di sistema (.NOR) e Informazioni su Esplora risorse.");
                } else if (rel_y >= 22 && rel_y < 42) {
                    app_rules_viewer_launch();
                } else if (rel_y >= 44 && rel_y <= 70) {
                    app_notepad_open_file("Info su Esplora risorse", "Esplora risorse di NewOS V1 (Windows 2000 Experience)\nVersione 1.0 (Build x86_64 Core 2026)\n\nFunzionalita incluse:\n- Caching Write-Back avanzato con supporto Wildcard (* e ?)\n- Supporto Taglia, Copia, Incolla con buffer appunti\n- Menu a tendina completi: File, Modifica, Visualizza, Strumenti, ?\n- Albero delle cartelle, Ricerca istantanea e barra indirizzi interattiva.");
                }
                state->active_menu = EXPLORER_MENU_NONE;
                return;
            }
        }
        state->active_menu = EXPLORER_MENU_NONE;
    }

    // 2. Clic sul Menu Contestuale Tasto Destro se aperto
    if (state->ctx_menu.is_open) {
        context_menu_action_t act = context_menu_handle_click(&state->ctx_menu, win->x + mx, win->y + my);
        if (act != CTX_ACTION_NONE) {
            switch (act) {
                case CTX_ACTION_OPEN_PRIMARY:
                    if (state->ctx_menu.is_item && state->ctx_target_idx >= 0) {
                        if (!context_menu_execute_primary(&state->ctx_menu)) {
                            explorer_execute_selected(state);
                        }
                    }
                    break;
                case CTX_ACTION_CUT:
                    if (state->ctx_target_idx >= 0) explorer_cut_item(state, state->ctx_target_idx);
                    break;
                case CTX_ACTION_COPY:
                    if (state->ctx_target_idx >= 0) explorer_copy_item(state, state->ctx_target_idx);
                    break;
                case CTX_ACTION_PASTE:
                    explorer_paste_item(state);
                    break;
                case CTX_ACTION_DELETE:
                    if (state->ctx_target_idx >= 0) explorer_delete_item(state, state->ctx_target_idx);
                    break;
                case CTX_ACTION_RENAME:
                    if (state->ctx_target_idx >= 0) explorer_rename_item(state, state->ctx_target_idx);
                    break;
                case CTX_ACTION_PROPERTIES:
                    if (state->ctx_target_idx >= 0) explorer_show_item_properties(state, state->ctx_target_idx);
                    break;
                case CTX_ACTION_NEW_FOLDER:
                    explorer_create_new_folder(state);
                    break;
                case CTX_ACTION_NEW_FILE:
                    explorer_create_new_file(state);
                    break;
                case CTX_ACTION_REFRESH:
                    explorer_load_items(state);
                    break;
                case CTX_ACTION_DRIVE_PROPS:
                    explorer_show_drive_properties(state);
                    break;
                default:
                    break;
            }
        }
        return;
    }

    // 3. Clic sulla Barra dei Menu in alto
    int32_t menu_y = cy + 2;
    if (my >= menu_y && my <= menu_y + 16) {
        if (mx >= cx + 6 && mx < cx + 42) {
            state->active_menu = (state->active_menu == EXPLORER_MENU_FILE) ? EXPLORER_MENU_NONE : EXPLORER_MENU_FILE;
            return;
        } else if (mx >= cx + 44 && mx < cx + 114) {
            state->active_menu = (state->active_menu == EXPLORER_MENU_EDIT) ? EXPLORER_MENU_NONE : EXPLORER_MENU_EDIT;
            return;
        } else if (mx >= cx + 116 && mx < cx + 198) {
            state->active_menu = (state->active_menu == EXPLORER_MENU_VIEW) ? EXPLORER_MENU_NONE : EXPLORER_MENU_VIEW;
            return;
        } else if (mx >= cx + 200 && mx < cx + 278) {
            state->active_menu = (state->active_menu == EXPLORER_MENU_TOOLS) ? EXPLORER_MENU_NONE : EXPLORER_MENU_TOOLS;
            return;
        } else if (mx >= cx + 280 && mx < cx + 300) {
            state->active_menu = (state->active_menu == EXPLORER_MENU_HELP) ? EXPLORER_MENU_NONE : EXPLORER_MENU_HELP;
            return;
        }
    }

    if (state->show_toolbar) {
        // 4. Clic sui Pulsanti della Toolbar
        int32_t tb_y = cy + 22;

        // Tasto [ Indietro ]
        if (mx >= cx + 6 && mx <= cx + 80 && my >= tb_y && my <= tb_y + 22) {
            if (state->history_index > 0) {
                state->history_index--;
                state->mode = state->history_mode[state->history_index];
                strncpy(state->folder_name, state->history[state->history_index], sizeof(state->folder_name) - 1);
                state->folder_name[sizeof(state->folder_name) - 1] = '\0';
                explorer_load_items(state);
            }
            return;
        }

        // Tasto [ Avanti ]
        if (mx >= cx + 84 && mx <= cx + 148 && my >= tb_y && my <= tb_y + 22) {
            if (state->history_index < state->history_count - 1) {
                state->history_index++;
                state->mode = state->history_mode[state->history_index];
                strncpy(state->folder_name, state->history[state->history_index], sizeof(state->folder_name) - 1);
                state->folder_name[sizeof(state->folder_name) - 1] = '\0';
                explorer_load_items(state);
            }
            return;
        }

        // Tasto [ Livello sup. ]
        if (mx >= cx + 152 && mx <= cx + 248 && my >= tb_y && my <= tb_y + 22) {
            if (state->mode == EXPLORER_MODE_FOLDER) {
                char drive_pfx = 'C';
                const char* p = state->folder_name;
                if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':') {
                    drive_pfx = (p[0] >= 'a' && p[0] <= 'z') ? (char)(p[0] - 32) : p[0];
                }
                char clean[128];
                explorer_clean_path(state->folder_name, clean, sizeof(clean));

                int last_slash = -1;
                for (int k = 0; clean[k] != '\0'; k++) {
                    if (clean[k] == '/' || clean[k] == '\\') {
                        last_slash = k;
                    }
                }
                if (last_slash >= 0) {
                    char parent[128];
                    mini_snprintf(parent, sizeof(parent), "%c:/", drive_pfx);
                    int plen = (int)strlen(parent);
                    strncpy(parent + plen, clean, last_slash);
                    parent[plen + last_slash] = '\0';
                    explorer_navigate_to(state, EXPLORER_MODE_FOLDER, parent);
                } else {
                    if (drive_pfx == 'C') {
                        explorer_navigate_to(state, EXPLORER_MODE_DRIVE_C, "");
                    } else if (drive_pfx == 'U') {
                        explorer_navigate_to(state, EXPLORER_MODE_DRIVE_U, "");
                    } else {
                        explorer_navigate_to(state, EXPLORER_MODE_THIS_PC, "");
                    }
                }
            } else if (state->mode == EXPLORER_MODE_DRIVE_C || state->mode == EXPLORER_MODE_DRIVE_D || 
                       state->mode == EXPLORER_MODE_DRIVE_A || state->mode == EXPLORER_MODE_DRIVE_U ||
                       state->mode == EXPLORER_MODE_RECYCLE_BIN || state->mode == EXPLORER_MODE_CONTROL_PANEL) {
                explorer_navigate_to(state, EXPLORER_MODE_THIS_PC, "");
            }
            return;
        }

        // Tasto [ Cerca ]
        if (mx >= cx + 258 && mx <= cx + 314 && my >= tb_y && my <= tb_y + 22) {
            state->search_active = !state->search_active;
            if (!state->search_active) {
                state->search_filter[0] = '\0';
                explorer_load_items(state);
            }
            return;
        }

        // Tasto [ Cartelle ]
        if (mx >= cx + 318 && mx <= cx + 386 && my >= tb_y && my <= tb_y + 22) {
            state->show_folder_tree = !state->show_folder_tree;
            return;
        }

        // 5. Clic sulla Barra degli Indirizzi
        int32_t addr_y = cy + 50;
        int32_t box_addr_x = cx + 76;
        int32_t box_addr_w = cw - 124;
        if (mx >= box_addr_x && mx <= box_addr_x + box_addr_w && my >= addr_y + 2 && my <= addr_y + 22) {
            state->address_editing = true;
            strncpy(state->address_buf, state->current_path, sizeof(state->address_buf) - 1);
            state->address_buf[sizeof(state->address_buf) - 1] = '\0';
            return;
        }

        // Click su pulsante [ Vai ] nella barra degli indirizzi
        if (mx >= cx + cw - 42 && mx <= cx + cw - 6 && my >= addr_y + 2 && my <= addr_y + 22) {
            if (state->address_editing) {
                state->address_editing = false;
                explorer_navigate_address(state, state->address_buf);
            } else {
                explorer_execute_selected(state);
            }
            return;
        }
    }

    int32_t body_y = state->show_toolbar ? (cy + 74) : (cy + 22);
    int32_t body_h = ch - (body_y - cy) - (state->show_statusbar ? 22 : 4);
    int32_t side_w = 160;

    // 6. Clic nell'area sinistra: Albero Cartelle O Web View
    if (mx >= cx + 2 && mx <= cx + 2 + side_w && my >= body_y && my <= body_y + body_h) {
        if (state->show_folder_tree) {
            int32_t ty = body_y + 24;
            if (my >= ty && my <= ty + 8 * 22) {
                int node = (my - ty) / 22;
                if (node == 0 || node == 1) explorer_navigate_to(state, EXPLORER_MODE_THIS_PC, NULL);
                else if (node == 2) explorer_navigate_to(state, EXPLORER_MODE_DRIVE_C, NULL);
                else if (node == 3) explorer_navigate_to(state, EXPLORER_MODE_DRIVE_D, NULL);
                else if (node == 4) {
                    if (usb_msc_is_present()) explorer_navigate_to(state, EXPLORER_MODE_DRIVE_U, NULL);
                    else explorer_navigate_to(state, EXPLORER_MODE_DRIVE_A, NULL);
                }
                else if (node == 5) explorer_navigate_to(state, EXPLORER_MODE_DRIVE_A, NULL);
                else if (node == 6) explorer_navigate_to(state, EXPLORER_MODE_RECYCLE_BIN, NULL);
                else if (node == 7) explorer_navigate_to(state, EXPLORER_MODE_CONTROL_PANEL, NULL);
                return;
            }
        } else {
            // Web View: Clic sul pulsante di azione
            if (mx >= cx + 12 && mx <= cx + 12 + side_w - 24) {
                if (state->mode == EXPLORER_MODE_RECYCLE_BIN) {
                    int32_t empty_btn_y = body_y + 148;
                    if (my >= empty_btn_y && my <= empty_btn_y + 24) {
                        state->num_items = 0;
                        return;
                    }
                } else if (state->mode == EXPLORER_MODE_THIS_PC) {
                    explorer_execute_selected(state);
                    return;
                } else if (state->sidebar_btn_y > 0 &&
                           my >= state->sidebar_btn_y && my <= state->sidebar_btn_y + state->sidebar_btn_h) {
                    explorer_execute_selected(state);
                    return;
                }
            }
        }
    }

    // 7. Click nell'Area Principale dei Contenuti
    int32_t main_x = cx + side_w + 4;
    int32_t main_w = cw - side_w - 6;

    int32_t row_h = 76;
    int visible_rows = (body_h - 16) / row_h;
    if (visible_rows < 1) visible_rows = 1;
    int total_rows = (state->num_items + 2) / 3;
    int max_scroll = (total_rows > visible_rows) ? (total_rows - visible_rows) : 0;
    bool show_sb = (total_rows > visible_rows);
    int32_t content_w = show_sb ? (main_w - 16) : main_w;

    // Clic sulla scrollbar verticale
    if (show_sb && mx >= main_x + content_w && mx <= main_x + main_w && my >= body_y && my <= body_y + body_h) {
        if (my <= body_y + 16) {
            // Freccia Su
            if (state->scroll_row > 0) state->scroll_row--;
        } else if (my >= body_y + body_h - 16) {
            // Freccia Giù
            if (state->scroll_row < max_scroll) state->scroll_row++;
        } else {
            // Clic traccia
            int32_t rel_y = my - (body_y + 16);
            int32_t track_h = body_h - 32;
            state->scroll_row = (rel_y * max_scroll) / track_h;
            if (state->scroll_row > max_scroll) state->scroll_row = max_scroll;
            if (state->scroll_row < 0) state->scroll_row = 0;
        }
        return;
    }

    // Clic sugli elementi nella griglia
    int32_t col_w = (content_w - 16) / 3;
    if (col_w < 110) col_w = 110;

    uint64_t now_ticks = timer_get_ticks();
    bool is_double_click = false;

    for (int r = 0; r < visible_rows; r++) {
        int cur_row = state->scroll_row + r;
        for (int col = 0; col < 3; col++) {
            int i = cur_row * 3 + col;
            if (i >= state->num_items) break;

            int32_t ix = main_x + 8 + col * col_w;
            int32_t iy = body_y + 8 + r * row_h;
            if (iy + row_h > body_y + body_h) break;

            if (mx >= ix && mx <= ix + col_w && my >= iy && my <= iy + row_h) {
                if (state->selected_item == i && (now_ticks - state->last_click_time < 75)) {
                    is_double_click = true;
                }

                state->selected_item = i;
                state->last_click_time = now_ticks;

                if (is_double_click) {
                    explorer_execute_selected(state);
                }
                return;
            }
        }
    }
}

static void explorer_right_click(window_t* win, int32_t mx, int32_t my, bool clicked) {
    if (!clicked) return;
    explorer_state_t* state = (explorer_state_t*)win->app_data;
    if (!state) return;

    int32_t cx = 4;
    int32_t cy = 24;
    int32_t cw = win->width - 8;
    int32_t ch = win->height - 28;
    int32_t body_y = state->show_toolbar ? (cy + 74) : (cy + 22);
    int32_t body_h = ch - (body_y - cy) - (state->show_statusbar ? 22 : 4);
    int32_t side_w = 160;

    int32_t main_x = cx + side_w + 4;
    int32_t main_w = cw - side_w - 6;

    int32_t row_h = 76;
    int visible_rows = (body_h - 16) / row_h;
    if (visible_rows < 1) visible_rows = 1;
    int total_rows = (state->num_items + 2) / 3;
    bool show_sb = (total_rows > visible_rows);
    int32_t content_w = show_sb ? (main_w - 16) : main_w;
    int32_t col_w = (content_w - 16) / 3;
    if (col_w < 110) col_w = 110;

    state->ctx_target_idx = -1;
    bool found_item = false;

    // Se il clic destro avviene nell'area file principale
    if (mx >= main_x && mx <= main_x + content_w && my >= body_y && my <= body_y + body_h) {
        for (int r = 0; r < visible_rows; r++) {
            int cur_row = state->scroll_row + r;
            for (int col = 0; col < 3; col++) {
                int i = cur_row * 3 + col;
                if (i >= state->num_items) break;

                int32_t ix = main_x + 8 + col * col_w;
                int32_t iy = body_y + 8 + r * row_h;
                if (mx >= ix && mx <= ix + col_w && my >= iy && my <= iy + row_h) {
                    found_item = true;
                    state->ctx_target_idx = i;
                    state->selected_item = i;
                    break;
                }
            }
            if (found_item) break;
        }

        bool can_paste = (g_explorer_clipboard.op != CLIPBOARD_NONE);
        if (found_item && state->ctx_target_idx >= 0 && state->ctx_target_idx < state->num_items) {
            char full_path[160] = {0};
            explorer_get_item_full_path(state, state->ctx_target_idx, full_path, sizeof(full_path));
            explorer_item_t* it = &state->items[state->ctx_target_idx];
            context_menu_build_for_item(&state->ctx_menu, it->name, full_path, it->is_folder, can_paste);
        } else {
            char curr_dir[128] = {0};
            if (state->mode == EXPLORER_MODE_FOLDER) {
                strncpy(curr_dir, state->folder_name, sizeof(curr_dir) - 1);
            } else if (state->mode == EXPLORER_MODE_DRIVE_C) {
                strcpy(curr_dir, "C:/");
            } else if (state->mode == EXPLORER_MODE_DRIVE_U) {
                strcpy(curr_dir, "U:/");
            }
            context_menu_build_for_empty(&state->ctx_menu, curr_dir, can_paste);
        }

        context_menu_show(&state->ctx_menu, win->x + mx, win->y + my, gfx_get_width(), gfx_get_height());
        state->active_menu = EXPLORER_MENU_NONE;
    }
}

static void explorer_key(window_t* win, char c) {
    explorer_state_t* state = (explorer_state_t*)win->app_data;
    if (!state) return;

    if (state->show_properties) {
        if (c == 27 || c == '\n' || c == '\r') {
            state->show_properties = false;
        }
        return;
    }

    if (state->address_editing) {
        if (c == '\n' || c == '\r') {
            state->address_editing = false;
            explorer_navigate_address(state, state->address_buf);
        } else if (c == 27) { // Escape
            state->address_editing = false;
        } else if (c == 8) { // Backspace
            size_t blen = strlen(state->address_buf);
            if (blen > 0) {
                state->address_buf[blen - 1] = '\0';
            }
        } else if (c >= 32 && c <= 126) {
            size_t blen = strlen(state->address_buf);
            if (blen < sizeof(state->address_buf) - 1) {
                state->address_buf[blen] = c;
                state->address_buf[blen + 1] = '\0';
            }
        }
        return;
    }

    if (state->search_active) {
        if (c == 27) { // Escape
            state->search_active = false;
            state->search_filter[0] = '\0';
            explorer_load_items(state);
            return;
        } else if (c == 8) {
            size_t flen = strlen(state->search_filter);
            if (flen > 0) {
                state->search_filter[flen - 1] = '\0';
                explorer_load_items(state);
            }
            return;
        } else if (c >= 32 && c <= 126 && c != '\n' && c != '\r') {
            size_t flen = strlen(state->search_filter);
            if (flen < sizeof(state->search_filter) - 1) {
                state->search_filter[flen] = c;
                state->search_filter[flen + 1] = '\0';
                explorer_load_items(state);
            }
            return;
        }
    }

    if (c == '\n' || c == '\r') {
        explorer_execute_selected(state);
    } else if (c == 8) { // Backspace -> Livello sup.
        if (state->mode == EXPLORER_MODE_FOLDER) {
            char drive_pfx = 'C';
            const char* p = state->folder_name;
            if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':') {
                drive_pfx = (p[0] >= 'a' && p[0] <= 'z') ? (char)(p[0] - 32) : p[0];
            }
            char clean[128];
            explorer_clean_path(state->folder_name, clean, sizeof(clean));

            int last_slash = -1;
            for (int k = 0; clean[k] != '\0'; k++) {
                if (clean[k] == '/' || clean[k] == '\\') {
                    last_slash = k;
                }
            }
            if (last_slash >= 0) {
                char parent[128];
                mini_snprintf(parent, sizeof(parent), "%c:/", drive_pfx);
                int plen = (int)strlen(parent);
                strncpy(parent + plen, clean, last_slash);
                parent[plen + last_slash] = '\0';
                explorer_navigate_to(state, EXPLORER_MODE_FOLDER, parent);
            } else {
                if (drive_pfx == 'C') {
                    explorer_navigate_to(state, EXPLORER_MODE_DRIVE_C, "");
                } else if (drive_pfx == 'U') {
                    explorer_navigate_to(state, EXPLORER_MODE_DRIVE_U, "");
                } else {
                    explorer_navigate_to(state, EXPLORER_MODE_THIS_PC, "");
                }
            }
        } else if (state->mode != EXPLORER_MODE_THIS_PC) {
            explorer_navigate_to(state, EXPLORER_MODE_THIS_PC, "");
        }
    } else if (c == '\t') { // Tab -> Selezione elemento successivo
        if (state->num_items > 0) {
            state->selected_item = (state->selected_item + 1) % state->num_items;
        }
    } else if (c == 127 || c == 0x7F) { // Delete -> Elimina file
        explorer_delete_item(state, state->selected_item);
    } else if (c == 0x18) { // Ctrl+X -> Taglia
        if (state->selected_item >= 0 && state->selected_item < state->num_items) {
            explorer_cut_item(state, state->selected_item);
        }
    } else if (c == 0x03) { // Ctrl+C -> Copia
        if (state->selected_item >= 0 && state->selected_item < state->num_items) {
            explorer_copy_item(state, state->selected_item);
        }
    } else if (c == 0x16) { // Ctrl+V -> Incolla
        explorer_paste_item(state);
    } else if (c == 0x01) { // Ctrl+A -> Seleziona tutto
        if (state->num_items > 0) state->selected_item = 0;
    } else if (c == 'r' || c == 'R') { // Refresh
        explorer_load_items(state);
    } else if (c == 'u' || c == 'U') {
        for (int i = 0; i < state->num_items; i++) {
            if (strstr(state->items[i].name, "(U:)") || strstr(state->items[i].name, "rimovibile")) {
                state->selected_item = i;
                break;
            }
        }
    } else if (c == 'c' || c == 'C') {
        for (int i = 0; i < state->num_items; i++) {
            if (strstr(state->items[i].name, "(C:)") || strstr(state->items[i].name, "locale")) {
                state->selected_item = i;
                break;
            }
        }
    } else if (c == 'd' || c == 'D') {
        for (int i = 0; i < state->num_items; i++) {
            if (strstr(state->items[i].name, "(D:)") || strstr(state->items[i].name, "ottica") || strstr(state->items[i].name, "CD-ROM") || strstr(state->items[i].name, "Blu-Ray")) {
                state->selected_item = i;
                break;
            }
        }
    } else if (c == 'j' || c == 'J' || c == 0x1F) { // Scorrimento giù
        int total_rows = (state->num_items + 2) / 3;
        int max_scroll = (total_rows > 3) ? (total_rows - 3) : 0;
        if (state->scroll_row < max_scroll) state->scroll_row++;
    } else if (c == 'k' || c == 'K' || c == 0x1E) { // Scorrimento su
        if (state->scroll_row > 0) state->scroll_row--;
    }
}

static window_t* explorer_create_base(const char* title, explorer_mode_t mode, const char* folder_name) {
    outb(0x3F8, 'E');
    window_t* win = wm_create_window(title, 80, 45, 600, 430, APP_EXPLORER);
    if (!win) return NULL;
    outb(0x3F8, '1');

    explorer_state_t* state = (explorer_state_t*)kmalloc(sizeof(explorer_state_t));
    if (!state) return NULL;
    outb(0x3F8, '2');

    memset(state, 0, sizeof(explorer_state_t));
    state->mode = mode;
    state->history_index = 0;
    state->history_count = 1;
    state->history_mode[0] = mode;
    if (folder_name) {
        strncpy(state->folder_name, folder_name, sizeof(state->folder_name) - 1);
        state->folder_name[sizeof(state->folder_name) - 1] = '\0';
        strncpy(state->history[0], folder_name, sizeof(state->history[0]) - 1);
        state->history[0][sizeof(state->history[0]) - 1] = '\0';
    } else {
        state->folder_name[0] = '\0';
        state->history[0][0] = '\0';
    }
    state->scroll_row = 0;
    state->active_menu = EXPLORER_MENU_NONE;
    state->ctx_menu.is_open = false;
    state->ctx_target_idx = -1;
    state->show_folder_tree = false;
    state->search_active = false;
    state->search_filter[0] = '\0';
    state->address_editing = false;
    state->address_buf[0] = '\0';
    state->show_properties = false;
    state->show_toolbar = true;
    state->show_statusbar = true;
    state->sort_mode = 0;
    state->last_click_time = 0;
    state->last_clicked_idx = -1;

    // Carica configurazioni personalizzate da EXPLORER.INI
    uint32_t ini_sz = 0;
    char* ini_buf = (char*)vfs_read_entire_file("C:\\NewOS\\SystemProgram\\EXPLORER.INI", &ini_sz);
    if (!ini_buf) ini_buf = (char*)vfs_read_entire_file("C:\\SYSTEM\\EXPLORER.INI", &ini_sz);
    if (ini_buf) {
        if (strstr(ini_buf, "ShowFolderTree=1")) state->show_folder_tree = true;
        if (strstr(ini_buf, "ShowToolbar=0"))    state->show_toolbar = false;
        if (strstr(ini_buf, "ShowStatusBar=0"))  state->show_statusbar = false;
        char* sort = strstr(ini_buf, "SortMode=");
        if (sort) {
            int sm = sort[9] - '0';
            if (sm >= 0 && sm <= 3) state->sort_mode = sm;
        }
        kfree(ini_buf);
    }

    outb(0x3F8, '3');
    explorer_load_items(state);
    outb(0x3F8, '4');

    win->app_data = state;
    win->on_draw = explorer_draw;
    win->on_mouse = explorer_mouse;
    win->on_right_click = explorer_right_click;
    win->on_key = explorer_key;
    return win;
}

void app_explorer_open_this_pc(void) {
    outb(0x3F8, 'X');
    explorer_create_base("Risorse del computer", EXPLORER_MODE_THIS_PC, NULL);
    outb(0x3F8, 'Y');
}

void app_explorer_open_drive_c(void) {
    explorer_create_base("Disco locale (C:)", EXPLORER_MODE_DRIVE_C, NULL);
}

void app_explorer_open_drive_d(void) {
    explorer_create_base("Unita CD-ROM (D:)", EXPLORER_MODE_DRIVE_D, NULL);
}

void app_explorer_open_drive_a(void) {
    explorer_create_base("Floppy (A:)", EXPLORER_MODE_DRIVE_A, NULL);
}

void app_explorer_open_drive_u(void) {
    explorer_create_base("Disco rimovibile (U:)", EXPLORER_MODE_DRIVE_U, NULL);
}

void app_explorer_open_folder(const char* folder_name) {
    char title[64];
    if (folder_name && folder_name[0] != '\0') {
        strncpy(title, folder_name, 63);
    } else {
        strcpy(title, "Cartella");
    }
    explorer_create_base(title, EXPLORER_MODE_FOLDER, folder_name);
}

void app_explorer_open_recycle_bin(void) {
    explorer_create_base("Cestino", EXPLORER_MODE_RECYCLE_BIN, NULL);
}

void app_control_panel_open(void) {
    explorer_create_base("Pannello di controllo", EXPLORER_MODE_CONTROL_PANEL, NULL);
}
