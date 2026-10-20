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

static void explorer_load_items(explorer_state_t* state) {
    state->num_items = 0;
    state->selected_item = 0;
    state->scroll_row = 0;
    state->ctx_open = false;
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
            int32_t count = vfs_readdir("/", entries, MAX_EXPLORER_ITEMS);
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

                    if (it->is_folder) {
                        strcpy(it->desc, "Cartella di file");
                        it->icon_id = ICON_FOLDER;
                    } else {
                        const char* dot = strstr(it->name, ".");
                        if (dot && (strcasecmp(dot, ".exe") == 0)) {
                            strcpy(it->desc, "Applicazione");
                            it->icon_id = ICON_RUN;
                        } else if (dot && (strcasecmp(dot, ".nor") == 0)) {
                            strcpy(it->desc, "Regola Ufficiale NewOS");
                            it->icon_id = ICON_DOCUMENT;
                        } else if (dot && (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".bmp") == 0 || strcasecmp(dot, ".png") == 0)) {
                            strcpy(it->desc, "Immagine grafica");
                            it->icon_id = ICON_IMAGE;
                        } else if (dot && (strcasecmp(dot, ".wav") == 0)) {
                            strcpy(it->desc, "File audio Wave");
                            it->icon_id = ICON_SOUND;
                        } else {
                            strcpy(it->desc, "Documento di testo");
                            it->icon_id = ICON_DOCUMENT;
                        }
                    }
                }
            } else {
                explorer_item_t* p = &state->items[state->num_items++];
                strcpy(p->name, "SYSTEM");
                strcpy(p->desc, "Cartella di file");
                p->icon_id = ICON_FOLDER;
                p->is_drive = false;
                p->is_folder = true;
                p->size_bytes = 0;
                p->content[0] = '\0';

                explorer_item_t* doc = &state->items[state->num_items++];
                strcpy(doc->name, "DOCS");
                strcpy(doc->desc, "Cartella di file");
                doc->icon_id = ICON_FOLDER;
                doc->is_drive = false;
                doc->is_folder = true;
                doc->size_bytes = 0;
                doc->content[0] = '\0';

                explorer_item_t* app = &state->items[state->num_items++];
                strcpy(app->name, "APPS");
                strcpy(app->desc, "Cartella di file");
                app->icon_id = ICON_FOLDER;
                app->is_drive = false;
                app->is_folder = true;
                app->size_bytes = 0;
                app->content[0] = '\0';

                explorer_item_t* usr = &state->items[state->num_items++];
                strcpy(usr->name, "USER");
                strcpy(usr->desc, "Cartella di file");
                usr->icon_id = ICON_FOLDER;
                usr->is_drive = false;
                usr->is_folder = true;
                usr->size_bytes = 0;
                usr->content[0] = '\0';

                explorer_item_t* src = &state->items[state->num_items++];
                strcpy(src->name, "SOURCES");
                strcpy(src->desc, "Cartella di file");
                src->icon_id = ICON_FOLDER;
                src->is_drive = false;
                src->is_folder = true;
                src->size_bytes = 0;
                src->content[0] = '\0';
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

                    if (it->is_folder) {
                        strcpy(it->desc, "Cartella di file");
                        it->icon_id = ICON_FOLDER;
                    } else {
                        const char* dot = strstr(it->name, ".");
                        if (dot && (strcasecmp(dot, ".exe") == 0)) {
                            strcpy(it->desc, "Applicazione");
                            it->icon_id = ICON_RUN;
                        } else if (dot && (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".bmp") == 0 || strcasecmp(dot, ".png") == 0)) {
                            strcpy(it->desc, "Immagine grafica");
                            it->icon_id = ICON_IMAGE;
                        } else if (dot && (strcasecmp(dot, ".wav") == 0)) {
                            strcpy(it->desc, "File audio Wave");
                            it->icon_id = ICON_SOUND;
                        } else {
                            strcpy(it->desc, "Documento di testo");
                            it->icon_id = ICON_DOCUMENT;
                        }
                    }
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

                    if (it->is_folder) {
                        strcpy(it->desc, "Cartella di file");
                        it->icon_id = ICON_FOLDER;
                    } else {
                        const char* dot = strstr(it->name, ".");
                        if (dot && (strcasecmp(dot, ".exe") == 0)) {
                            strcpy(it->desc, "Applicazione");
                            it->icon_id = ICON_RUN;
                        } else if (dot && (strcasecmp(dot, ".cpl") == 0)) {
                            strcpy(it->desc, "Applet di controllo");
                            it->icon_id = ICON_RUN;
                        } else if (dot && (strcasecmp(dot, ".nor") == 0)) {
                            strcpy(it->desc, "Regola Ufficiale NewOS");
                            it->icon_id = ICON_DOCUMENT;
                        } else if (dot && (strcasecmp(dot, ".sys") == 0 || strstr(it->name, ".SYS"))) {
                            strcpy(it->desc, "Driver di periferica");
                            it->icon_id = ICON_DOCUMENT;
                        } else if (dot && (strcasecmp(dot, ".ini") == 0 || strcasecmp(dot, ".cfg") == 0)) {
                            strcpy(it->desc, "File di configurazione");
                            it->icon_id = ICON_DOCUMENT;
                        } else if (dot && (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".bmp") == 0 || strcasecmp(dot, ".png") == 0)) {
                            strcpy(it->desc, "Immagine grafica");
                            it->icon_id = ICON_IMAGE;
                        } else if (dot && (strcasecmp(dot, ".wav") == 0)) {
                            strcpy(it->desc, "File audio Wave");
                            it->icon_id = ICON_SOUND;
                        } else {
                            strcpy(it->desc, "Documento di testo");
                            it->icon_id = ICON_DOCUMENT;
                        }
                    }
                }
            } else {
                // Navigazione gerarchica fallback (garantita a qualunque profondità)
                if (strcasecmp(clean, "SYSTEM") == 0) {
                    explorer_add_item(state, "DRIVERS", "Cartella di file", ICON_FOLDER, true, 0, NULL);
                    explorer_add_item(state, "DEV", "Cartella di file", ICON_FOLDER, true, 0, NULL);
                    explorer_add_item(state, "WALLPAPERS", "Cartella di file", ICON_FOLDER, true, 0, NULL);
                    explorer_add_item(state, "SOUNDS", "Cartella di file", ICON_FOLDER, true, 0, NULL);
                    explorer_add_item(state, "REGEDIT.EXE", "Applicazione", ICON_RUN, false, 49152, NULL);
                    explorer_add_item(state, "REGEDIT.CPL", "Applet di controllo", ICON_RUN, false, 12288, NULL);
                    explorer_add_item(state, "DEVMGMT.CPL", "Applet di controllo", ICON_RUN, false, 16384, NULL);
                    explorer_add_item(state, "REGISTRY.DAT", "Database di sistema", ICON_DOCUMENT, false, 8192, NULL);
                    explorer_add_item(state, "LEGGIMI.TXT", "Documento di testo", ICON_DOCUMENT, false, 512, "File e cartelle di sistema fondamentali di NewOS V1 (64-bit).\n");
                } else if (strcasecmp(clean, "SYSTEM/DRIVERS") == 0) {
                    explorer_add_item(state, "NET", "Cartella di file", ICON_FOLDER, true, 0, NULL);
                    explorer_add_item(state, "PCI.SYS", "Driver di periferica", ICON_DOCUMENT, false, 8192, NULL);
                    explorer_add_item(state, "PCI.INI", "Configurazione driver", ICON_DOCUMENT, false, 256, "[Driver]\nName=PCI Bus Driver\nStatus=Active\n");
                    explorer_add_item(state, "ATA.SYS", "Driver di periferica", ICON_DOCUMENT, false, 12288, NULL);
                    explorer_add_item(state, "ATA.INI", "Configurazione driver", ICON_DOCUMENT, false, 256, "[Driver]\nName=IDE ATA Controller\nStatus=Active\n");
                    explorer_add_item(state, "VGA.SYS", "Driver di periferica", ICON_DOCUMENT, false, 8192, NULL);
                    explorer_add_item(state, "VGA.INI", "Configurazione driver", ICON_DOCUMENT, false, 256, "[Driver]\nName=VGA/BGA Graphics\nStatus=Active\n");
                    explorer_add_item(state, "AC97.SYS", "Driver di periferica", ICON_DOCUMENT, false, 8192, NULL);
                    explorer_add_item(state, "AC97.INI", "Configurazione driver", ICON_DOCUMENT, false, 256, "[Driver]\nName=Intel AC97 Audio\nStatus=Active\n");
                    explorer_add_item(state, "PS2MOUSE.SYS", "Driver di periferica", ICON_DOCUMENT, false, 4096, NULL);
                    explorer_add_item(state, "PS2MOUSE.INI", "Configurazione driver", ICON_DOCUMENT, false, 256, "[Driver]\nName=PS/2 Mouse\nStatus=Active\n");
                    explorer_add_item(state, "PS2KBD.SYS", "Driver di periferica", ICON_DOCUMENT, false, 4096, NULL);
                    explorer_add_item(state, "PS2KBD.INI", "Configurazione driver", ICON_DOCUMENT, false, 256, "[Driver]\nName=PS/2 Keyboard\nStatus=Active\n");
                    explorer_add_item(state, "RTC.SYS", "Driver di periferica", ICON_DOCUMENT, false, 2048, NULL);
                    explorer_add_item(state, "POWER.SYS", "Driver di periferica", ICON_DOCUMENT, false, 4096, NULL);
                    explorer_add_item(state, "E1000.SYS", "Driver di periferica", ICON_DOCUMENT, false, 16384, NULL);
                    explorer_add_item(state, "RTL8139.SYS", "Driver di periferica", ICON_DOCUMENT, false, 16384, NULL);
                    explorer_add_item(state, "LEGGIMI.TXT", "Documento di testo", ICON_DOCUMENT, false, 512, "Sottosistema driver modulare (.SYS + .INI) per periferiche hardware.\n");
                } else if (strcasecmp(clean, "SYSTEM/DRIVERS/NET") == 0) {
                    explorer_add_item(state, "E1000.SYS", "Driver di periferica", ICON_DOCUMENT, false, 16384, NULL);
                    explorer_add_item(state, "E1000.INI", "Configurazione driver", ICON_DOCUMENT, false, 256, "[Driver]\nName=Intel PRO/1000 MT\nStatus=Active\n");
                    explorer_add_item(state, "RTL8139.SYS", "Driver di periferica", ICON_DOCUMENT, false, 16384, NULL);
                    explorer_add_item(state, "RTL8139.INI", "Configurazione driver", ICON_DOCUMENT, false, 256, "[Driver]\nName=Realtek RTL8139\nStatus=Active\n");
                    explorer_add_item(state, "ETH.SYS.NET", "Driver protocollo rete", ICON_DOCUMENT, false, 8192, NULL);
                    explorer_add_item(state, "IP.SYS.NET", "Driver protocollo rete", ICON_DOCUMENT, false, 8192, NULL);
                    explorer_add_item(state, "UDP.SYS.NET", "Driver protocollo rete", ICON_DOCUMENT, false, 4096, NULL);
                    explorer_add_item(state, "TCP.SYS.NET", "Driver protocollo rete", ICON_DOCUMENT, false, 8192, NULL);
                    explorer_add_item(state, "DHCP.SYS.NET", "Driver protocollo rete", ICON_DOCUMENT, false, 4096, NULL);
                    explorer_add_item(state, "TLS.SYS.NET", "Driver protocollo rete", ICON_DOCUMENT, false, 8192, NULL);
                    explorer_add_item(state, "LEGGIMI.TXT", "Documento di testo", ICON_DOCUMENT, false, 512, "Stack di rete nativo: schede fisiche e protocolli TCP/IP.\n");
                } else if (strcasecmp(clean, "SYSTEM/DEV") == 0) {
                    explorer_add_item(state, "TCC", "Cartella di file", ICON_FOLDER, true, 0, NULL);
                    explorer_add_item(state, "LEGGIMI.TXT", "Documento di testo", ICON_DOCUMENT, false, 256, "Ambiente di sviluppo software per NewOS V1 64-bit.\n");
                } else if (strcasecmp(clean, "SYSTEM/DEV/TCC") == 0) {
                    explorer_add_item(state, "CONFIG", "Cartella di file", ICON_FOLDER, true, 0, NULL);
                    explorer_add_item(state, "INCLUDE", "Cartella di file", ICON_FOLDER, true, 0, NULL);
                    explorer_add_item(state, "TEMP", "Cartella di file", ICON_FOLDER, true, 0, NULL);
                    explorer_add_item(state, "TMP", "Cartella di file", ICON_FOLDER, true, 0, NULL);
                    explorer_add_item(state, "TCC.EXE", "Applicazione", ICON_RUN, false, 131072, NULL);
                    explorer_add_item(state, "LEGGIMI.TXT", "Documento di testo", ICON_DOCUMENT, false, 256, "Compilatore C nativo Tiny C Compiler (TCC 64-bit).\n");
                } else if (strcasecmp(clean, "SYSTEM/DEV/TCC/CONFIG") == 0) {
                    explorer_add_item(state, "TCC.CFG", "File di configurazione", ICON_DOCUMENT, false, 256, "[Compiler]\nTarget=x86_64-newos\nOptimization=O2\nSysInclude=C:\\SYSTEM\\DEV\\TCC\\INCLUDE\\\n");
                } else if (strcasecmp(clean, "SYSTEM/DEV/TCC/INCLUDE") == 0) {
                    explorer_add_item(state, "NEWOS.H", "Header C", ICON_DOCUMENT, false, 1024, "/**\n * newos.h - NewOS V1 Software Development Kit (SDK)\n */\n#ifndef _NEWOS_H_\n#define _NEWOS_H_\ntypedef unsigned long long uint64_t;\ntypedef unsigned int uint32_t;\nvoid sys_print(const char* str);\nvoid sys_exit(int code);\n#endif\n");
                } else if (strcasecmp(clean, "SYSTEM/DEV/TCC/TEMP") == 0) {
                    explorer_add_item(state, "TEMP.TXT", "Documento di testo", ICON_DOCUMENT, false, 64, "Directory per file temporanei di compilazione.\n");
                } else if (strcasecmp(clean, "SYSTEM/DEV/TCC/TMP") == 0) {
                    explorer_add_item(state, "TMP.TXT", "Documento di testo", ICON_DOCUMENT, false, 64, "Directory temporanea TMP per il compilatore TCC.\n");
                } else if (strcasecmp(clean, "SYSTEM/WALLPAPERS") == 0) {
                    explorer_add_item(state, "BLISS.BMP", "Immagine grafica", ICON_IMAGE, false, 150000, NULL);
                    explorer_add_item(state, "FLOW.BMP", "Immagine grafica", ICON_IMAGE, false, 150000, NULL);
                    explorer_add_item(state, "AUTUMN.JPG", "Immagine grafica", ICON_IMAGE, false, 120000, NULL);
                    explorer_add_item(state, "OCEAN.JPG", "Immagine grafica", ICON_IMAGE, false, 120000, NULL);
                    explorer_add_item(state, "SUNSET.JPG", "Immagine grafica", ICON_IMAGE, false, 120000, NULL);
                    explorer_add_item(state, "LEGGIMI.TXT", "Documento di testo", ICON_DOCUMENT, false, 256, "Sfondi per il desktop di NewOS V1.\nFai doppio clic su un'immagine per impostarla come sfondo!\n");
                } else if (strcasecmp(clean, "SYSTEM/SOUNDS") == 0) {
                    explorer_add_item(state, "STARTUP.WAV", "File audio Wave", ICON_SOUND, false, 48000, NULL);
                    explorer_add_item(state, "CHORD.WAV", "File audio Wave", ICON_SOUND, false, 24000, NULL);
                    explorer_add_item(state, "DING.WAV", "File audio Wave", ICON_SOUND, false, 16000, NULL);
                    explorer_add_item(state, "NOTIFY.WAV", "File audio Wave", ICON_SOUND, false, 20000, NULL);
                    explorer_add_item(state, "SHUTDOWN.WAV", "File audio Wave", ICON_SOUND, false, 48000, NULL);
                    explorer_add_item(state, "LEGGIMI.TXT", "Documento di testo", ICON_DOCUMENT, false, 256, "Suoni di sistema PCM Wave per schede audio AC97 e altoparlante PC.\n");
                } else if (strcasecmp(clean, "DOCS") == 0) {
                    explorer_add_item(state, "LEGGIMI.TXT", "Documento di testo", ICON_DOCUMENT, false, 1024, "================================================================\nBenvenuto in NewOS V1 (64-bit)!\nSistema Operativo con File System exFAT nativo e interfaccia GUI.\n================================================================\n");
                    explorer_add_item(state, "BENVENUTO.TXT", "Documento di testo", ICON_DOCUMENT, false, 256, "NewOS V1 64-bit Edition.\nBuon divertimento!\n");
                } else if (strcasecmp(clean, "APPS") == 0) {
                    explorer_add_item(state, "LEGGIMI.TXT", "Documento di testo", ICON_DOCUMENT, false, 256, "Cartella riservata alle applicazioni utente e terze parti.\n");
                } else if (strcasecmp(clean, "SOURCES") == 0) {
                    explorer_add_item(state, "LEGGIMI.TXT", "Documento di testo", ICON_DOCUMENT, false, 512, "Sorgenti di sistema di NewOS V1 per la massima portabilita'.\n");
                    explorer_add_item(state, "KERNEL.C", "Sorgente C", ICON_DOCUMENT, false, 16384, "/* Punto di ingresso principale del kernel 64-bit */\n");
                    explorer_add_item(state, "REGEDIT.C", "Sorgente C", ICON_DOCUMENT, false, 8192, "/* Editor grafico del registro */\n");
                    explorer_add_item(state, "REGISTRY.H", "Header C", ICON_DOCUMENT, false, 4096, "/* Header del registro */\n");
                    explorer_add_item(state, "BOOTSECT.ASM", "Sorgente Assembly", ICON_DOCUMENT, false, 4096, "; Settore di avvio MBR per NewOS V1\n");
                } else if (strcasecmp(clean, "USER") == 0) {
                    explorer_add_item(state, "MORGAN", "Cartella di file", ICON_FOLDER, true, 0, NULL);
                } else if (strcasecmp(clean, "USER/MORGAN") == 0) {
                    explorer_add_item(state, "DESKTOP", "Cartella di file", ICON_FOLDER, true, 0, NULL);
                    explorer_add_item(state, "USER.CFG", "File di configurazione", ICON_DOCUMENT, false, 128, "USER=Morgan\nPASS=\nHOST=NEWOS-PC\nTHEME=0\nPOWER=0\nINSTALLED=0\n");
                } else if (strcasecmp(clean, "USER/MORGAN/DESKTOP") == 0) {
                    explorer_add_item(state, "PROGETTI", "Cartella di file", ICON_FOLDER, true, 0, NULL);
                    explorer_add_item(state, "NOTE.TXT", "Documento di testo", ICON_DOCUMENT, false, 128, "Note e appunti personali memorizzati in C:\\USER\\MORGAN\\DESKTOP\\\nFile System: exFAT Microsoft nativo.\n");
                    explorer_add_item(state, "BENVENUTO.TXT", "Documento di testo", ICON_DOCUMENT, false, 128, "Benvenuto nel tuo Desktop personale di NewOS V1 64-bit!\nPercorso fisico: C:\\USER\\MORGAN\\DESKTOP\\\n");
                    explorer_add_item(state, "SFONDO_BLISS.BMP", "Immagine grafica", ICON_IMAGE, false, 150000, NULL);
                } else if (strcasecmp(clean, "USER/MORGAN/DESKTOP/PROGETTI") == 0) {
                    explorer_add_item(state, "PROGETTO1.TXT", "Documento di testo", ICON_DOCUMENT, false, 128, "Bozza progetto software in C per NewOS V1 64-bit.\n");
                } else {
                    // Cartella vuota o personalizzata
                    explorer_add_item(state, "LEGGIMI.TXT", "Documento di testo", ICON_DOCUMENT, false, 64, "Cartella vuota.\n");
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

                    if (it->is_folder) {
                        strcpy(it->desc, "Cartella di file");
                        it->icon_id = ICON_FOLDER;
                    } else {
                        const char* dot = strstr(it->name, ".");
                        if (dot && (strcasecmp(dot, ".exe") == 0)) {
                            if (strcasecmp(it->name, "WEBBROWSER.EXE") == 0 || strcasecmp(it->name, "BROWSER.EXE") == 0) {
                                strcpy(it->desc, "Browser Web");
                                it->icon_id = ICON_NET;
                            } else {
                                strcpy(it->desc, "Applicazione");
                                it->icon_id = ICON_RUN;
                            }
                        } else if (dot && (strcasecmp(dot, ".htm") == 0 || strcasecmp(dot, ".html") == 0)) {
                            strcpy(it->desc, "Documento HTML");
                            it->icon_id = ICON_NET;
                        } else if (dot && (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".bmp") == 0 || strcasecmp(dot, ".png") == 0)) {
                            strcpy(it->desc, "Immagine grafica");
                            it->icon_id = ICON_IMAGE;
                        } else if (dot && (strcasecmp(dot, ".wav") == 0)) {
                            strcpy(it->desc, "File audio Wave");
                            it->icon_id = ICON_SOUND;
                        } else {
                            strcpy(it->desc, "Documento di testo");
                            it->icon_id = ICON_DOCUMENT;
                        }
                    }
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
    state->ctx_open = false;
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
    state->ctx_open = false;
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
    state->ctx_open = false;
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
    state->ctx_open = false;
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
    state->ctx_open = false;
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
    state->ctx_open = false;
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

    // Icona 16x16 nella barra degli indirizzi
    icon_id_t addr_icon = ICON_THIS_PC;
    if (state->mode == EXPLORER_MODE_DRIVE_C || state->mode == EXPLORER_MODE_DRIVE_U) addr_icon = ICON_HD;
    else if (state->mode == EXPLORER_MODE_DRIVE_D) addr_icon = ICON_CD;
    else if (state->mode == EXPLORER_MODE_DRIVE_A) addr_icon = ICON_FLOPPY;
    else if (state->mode == EXPLORER_MODE_FOLDER) addr_icon = ICON_FOLDER;
    else if (state->mode == EXPLORER_MODE_RECYCLE_BIN) addr_icon = ICON_CESTINO;
    else if (state->mode == EXPLORER_MODE_CONTROL_PANEL) addr_icon = ICON_CONTROL_PANEL;

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

    // 4. Area Principale (Web View laterale a sinistra O Albero Cartelle + Riquadro File a destra)
    int32_t body_y = cy + 74;
    int32_t body_h = ch - 74 - 22; // Lascia 22px per la barra di stato
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
        if (sel_it && (strstr(sel_it->name, "Disco locale") || strstr(sel_it->name, "(C:)"))) {
            const ata_device_t* pm = ata_get_primary_master();
            const exfat_vbr_t* vbr = exfat_get_vbr();

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

            uint32_t total_mb = (pm && pm->size_mb) ? pm->size_mb : 32;
            uint32_t pct_used = (vbr && vbr->percent_in_use) ? vbr->percent_in_use : 23;
            uint32_t used_mb = (total_mb * pct_used) / 100;
            if (used_mb == 0 && pct_used > 0) used_mb = 1;
            uint32_t free_mb = total_mb > used_mb ? (total_mb - used_mb) : 0;

            int32_t used_w = (bar_w - 4) * pct_used / 100;
            if (used_w < 2 && pct_used > 0) used_w = 2;
            if (used_w > bar_w - 4) used_w = bar_w - 4;

            gfx_fill_rect(cx + 14, gauge_y + 18, used_w, bar_h - 4, 0x0A246A); // Blu
            gfx_fill_rect(cx + 14 + used_w, gauge_y + 18, bar_w - 4 - used_w, bar_h - 4, 0x7E3A6E); // Magenta/Viola

            // Legenda usata/libera
            char used_str[32], free_str[32], cap_str[32];
            mini_snprintf(used_str, sizeof(used_str), "Usato: %d MB", used_mb);
            mini_snprintf(free_str, sizeof(free_str), "Libero: %d MB", free_mb);
            if (pm && pm->size_gb > 0) {
                mini_snprintf(cap_str, sizeof(cap_str), "Capacita: %d GB", pm->size_gb);
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
            gfx_fill_rect(cx + 12, btn_y, side_w - 24, 22, COLOR_BTNFACE);
            gfx_draw_bevel_rect(cx + 12, btn_y, side_w - 24, 22, false);
            gfx_draw_string(cx + 46, btn_y + 4, "Apri", COLOR_WINDOWTEXT, 0, true);
        } else {
            gfx_draw_string(cx + 12, info_y, "Configura e", 0x0A246A, 0, true);
            gfx_draw_string(cx + 12, info_y + 16, "personalizza", 0x0A246A, 0, true);
            gfx_draw_string(cx + 12, info_y + 32, "l'ambiente NewOS V1.", 0x404040, 0, true);
        }
    } else {
        // Cartella o C: o D: o A:
        const char* drive_or_folder = "Cartella di file";
        if (state->mode == EXPLORER_MODE_DRIVE_C) drive_or_folder = "Disco locale (C:)";
        else if (state->mode == EXPLORER_MODE_DRIVE_D) drive_or_folder = "CD-ROM (D:)";
        else if (state->mode == EXPLORER_MODE_DRIVE_A) drive_or_folder = "Floppy (A:)";

        gfx_draw_string(cx + 12, info_y, drive_or_folder, 0x0A246A, 0, true);
        char buf_cnt[32];
        mini_snprintf(buf_cnt, sizeof(buf_cnt), "Oggetti: %d", state->num_items);
        gfx_draw_string(cx + 12, info_y + 18, buf_cnt, 0x404040, 0, true);

        if (state->selected_item >= 0 && state->selected_item < state->num_items) {
            int32_t btn_y = info_y + 44;
            gfx_fill_rect(cx + 12, btn_y, side_w - 24, 22, COLOR_BTNFACE);
            gfx_draw_bevel_rect(cx + 12, btn_y, side_w - 24, 22, false);
            gfx_draw_string(cx + 46, btn_y + 4, "Apri", COLOR_WINDOWTEXT, 0, true);
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
    int32_t sb_y = cy + ch - 20;
    gfx_fill_rect(cx + 2, sb_y, cw - 4, 18, COLOR_BTNFACE);
    
    // Riquadro sinistro con conteggio oggetti
    int32_t sb1_w = cw - 170;
    gfx_draw_bevel_rect(cx + 2, sb_y, sb1_w, 18, true);

    const ata_device_t* pm_sb = ata_get_primary_master();
    const exfat_vbr_t* vbr_sb = exfat_get_vbr();
    uint32_t tot_sb = (pm_sb && pm_sb->size_mb) ? pm_sb->size_mb : 32;
    uint32_t pct_sb = (vbr_sb && vbr_sb->percent_in_use) ? vbr_sb->percent_in_use : 23;
    uint32_t used_sb = (tot_sb * pct_sb) / 100;
    if (used_sb == 0 && pct_sb > 0) used_sb = 1;
    uint32_t free_sb = tot_sb > used_sb ? (tot_sb - used_sb) : 0;

    char sb_text[64];
    mini_snprintf(sb_text, sizeof(sb_text), "Oggetti: %d (Spazio libero: %d MB)", state->num_items, free_sb);
    gfx_draw_string(cx + 8, sb_y + 2, sb_text, COLOR_WINDOWTEXT, 0, true);

    // Riquadro destro con icona PC
    int32_t sb2_x = cx + sb1_w + 4;
    int32_t sb2_w = cw - sb1_w - 6;
    gfx_draw_bevel_rect(sb2_x, sb_y, sb2_w, 18, true);
    gfx_draw_icon_16(sb2_x + 4, sb_y + 1, ICON_THIS_PC);
    gfx_draw_string(sb2_x + 24, sb_y + 2, "Risorse del computer", COLOR_WINDOWTEXT, 0, true);

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
            m_x = cx + 44; m_w = 145; m_h = 72;
            gfx_fill_rect(m_x + 2, m_y + 2, m_w, m_h, 0x808080);
            gfx_fill_rect(m_x, m_y, m_w, m_h, COLOR_BTNFACE);
            gfx_draw_bevel_rect(m_x, m_y, m_w, m_h, false);

            gfx_draw_string(m_x + 12, m_y + 6,  "Seleziona tutto", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_string(m_x + 12, m_y + 24, "Inverti selezione", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_bevel_rect(m_x + 4, m_y + 42, m_w - 8, 2, true);
            gfx_draw_string(m_x + 12, m_y + 48, "Elimina", COLOR_WINDOWTEXT, 0, true);
        } else if (state->active_menu == EXPLORER_MENU_VIEW) {
            m_x = cx + 116; m_w = 150; m_h = 52;
            gfx_fill_rect(m_x + 2, m_y + 2, m_w, m_h, 0x808080);
            gfx_fill_rect(m_x, m_y, m_w, m_h, COLOR_BTNFACE);
            gfx_draw_bevel_rect(m_x, m_y, m_w, m_h, false);

            gfx_draw_string(m_x + 12, m_y + 6,  "Aggiorna", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_string(m_x + 12, m_y + 26, "Albero cartelle", COLOR_WINDOWTEXT, 0, true);
        } else if (state->active_menu == EXPLORER_MENU_TOOLS) {
            m_x = cx + 200; m_w = 180; m_h = 52;
            gfx_fill_rect(m_x + 2, m_y + 2, m_w, m_h, 0x808080);
            gfx_fill_rect(m_x, m_y, m_w, m_h, COLOR_BTNFACE);
            gfx_draw_bevel_rect(m_x, m_y, m_w, m_h, false);

            gfx_draw_string(m_x + 12, m_y + 6,  "Opzioni cartella...", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_string(m_x + 12, m_y + 26, "Gestione periferiche...", COLOR_WINDOWTEXT, 0, true);
        } else if (state->active_menu == EXPLORER_MENU_HELP) {
            m_x = cx + 270; m_w = 170; m_h = 32;
            gfx_fill_rect(m_x + 2, m_y + 2, m_w, m_h, 0x808080);
            gfx_fill_rect(m_x, m_y, m_w, m_h, COLOR_BTNFACE);
            gfx_draw_bevel_rect(m_x, m_y, m_w, m_h, false);

            gfx_draw_string(m_x + 12, m_y + 6,  "Info su Esplora risorse", COLOR_WINDOWTEXT, 0, true);
        }
    }

    // 7. Menu Contestuale Tasto Destro (se aperto)
    if (state->ctx_open) {
        int32_t px = win->x + state->ctx_x;
        int32_t py = win->y + state->ctx_y;
        int32_t pw = 150;
        int32_t ph = state->ctx_is_item ? 72 : 92;

        if (px + pw > cx + cw) px = cx + cw - pw - 4;
        if (py + ph > cy + ch) py = cy + ch - ph - 4;

        gfx_fill_rect(px + 3, py + 3, pw, ph, 0x808080);
        gfx_fill_rect(px, py, pw, ph, COLOR_BTNFACE);
        gfx_draw_bevel_rect(px, py, pw, ph, false);

        if (state->ctx_is_item) {
            gfx_draw_string(px + 12, py + 6,  "Apri", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_bevel_rect(px + 4, py + 24, pw - 8, 2, true);
            gfx_draw_string(px + 12, py + 30, "Elimina", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_bevel_rect(px + 4, py + 48, pw - 8, 2, true);
            gfx_draw_string(px + 12, py + 52, "Proprieta", COLOR_WINDOWTEXT, 0, true);
        } else {
            gfx_draw_string(px + 12, py + 6,  "Nuova cartella", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_string(px + 12, py + 26, "Nuovo testo", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_bevel_rect(px + 4, py + 44, pw - 8, 2, true);
            gfx_draw_string(px + 12, py + 50, "Aggiorna", COLOR_WINDOWTEXT, 0, true);
            gfx_draw_bevel_rect(px + 4, py + 68, pw - 8, 2, true);
            gfx_draw_string(px + 12, py + 72, "Proprieta unita", COLOR_WINDOWTEXT, 0, true);
        }
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
        if (strstr(it->name, "Disco locale") || strstr(it->name, "(C:)")) {
            explorer_navigate_to(state, EXPLORER_MODE_DRIVE_C, "");
        } else if (strstr(it->name, "Floppy") || strstr(it->name, "(A:)")) {
            explorer_navigate_to(state, EXPLORER_MODE_DRIVE_A, "");
        } else if (strstr(it->name, "CD-ROM") || strstr(it->name, "ottica") || strstr(it->name, "(D:)")) {
            explorer_navigate_to(state, EXPLORER_MODE_DRIVE_D, "");
        } else if (strstr(it->name, "rimovibile") || strstr(it->name, "(U:)")) {
            explorer_navigate_to(state, EXPLORER_MODE_DRIVE_U, "");
        } else if (strstr(it->name, "Pannello")) {
            explorer_navigate_to(state, EXPLORER_MODE_CONTROL_PANEL, "");
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

            const char* dot = strstr(it->name, ".");
            if (dot && (strcasecmp(dot, ".wav") == 0)) {
                sound_play_wav_file(full_path);
            } else if (dot && (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".bmp") == 0)) {
                image_t* img = image_load_from_file(full_path);
                if (img) {
                    gfx_set_custom_wallpaper(img->pixels, img->width, img->height);
                    image_free(img);
                }
            } else if (dot && (strcasecmp(dot, ".cpl") == 0)) {
                if (strcasecmp(it->name, "DISPLAY.CPL") == 0) app_display_props_launch();
                else if (strcasecmp(it->name, "SOUND.CPL") == 0) app_sound_props_launch();
                else if (strcasecmp(it->name, "POWER.CPL") == 0) app_power_props_launch();
                else if (strcasecmp(it->name, "DEVMGMT.CPL") == 0) app_devmgmt_launch();
                else if (strcasecmp(it->name, "REGEDIT.CPL") == 0) app_regedit_launch();
                else app_about_launch();
            } else if (dot && (strcasecmp(dot, ".nor") == 0)) {
                app_rules_viewer_open_file(full_path);
            } else if (dot && (strcasecmp(dot, ".sys") == 0)) {
                app_devmgmt_launch();
            } else if (dot && (strcasecmp(dot, ".exe") == 0)) {
                if (strcasecmp(it->name, "CALC.EXE") == 0) app_calc_launch();
                else if (strcasecmp(it->name, "NOTEPAD.EXE") == 0) app_notepad_launch();
                else if (strcasecmp(it->name, "CMD.EXE") == 0) app_cmd_launch();
                else if (strcasecmp(it->name, "EXPLORER.EXE") == 0) app_explorer_open_this_pc();
                else if (strcasecmp(it->name, "REGEDIT.EXE") == 0) app_regedit_launch();
                else if (strcasecmp(it->name, "RULES.EXE") == 0) app_rules_viewer_launch();
                else if (strcasecmp(it->name, "WEBBROWSER.EXE") == 0 || strcasecmp(it->name, "BROWSER.EXE") == 0) app_browser_launch();
                else if (strcasecmp(it->name, "SETUP.EXE") == 0 || strcasecmp(it->name, "OOBE.EXE") == 0) app_oobe_launch();
                else app_notepad_open_file(it->name, "Esecuzione applicazione PE64...");
            } else if (dot && (strcasecmp(dot, ".htm") == 0 || strcasecmp(dot, ".html") == 0)) {
                app_browser_open_url(full_path);
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
        return;
    }

    if (state->mode == EXPLORER_MODE_DRIVE_D) {
        const char* dot = strstr(it->name, ".");
        if (dot && (strcasecmp(dot, ".inf") == 0 || strcasecmp(dot, ".txt") == 0)) {
            app_notepad_open_file(it->name, it->content);
        } else if (dot && strcasecmp(dot, ".img") == 0) {
            app_notepad_open_file(it->name, "Immagine di sistema avviabile NewOS V1 (32 MB).\nContiene il sistema operativo live completo e il filesystem exFAT.");
        } else {
            app_notepad_open_file(it->name, it->content);
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
            m_x = cx + 44; m_w = 145; m_h = 72;
            if (mx >= m_x && mx <= m_x + m_w && my >= m_y && my <= m_y + m_h) {
                int32_t rel_y = my - m_y;
                if (rel_y >= 4 && rel_y < 22) state->selected_item = 0;
                else if (rel_y >= 44 && rel_y <= 68) {
                    if (state->selected_item >= 0 && state->selected_item < state->num_items) {
                        explorer_delete_item(state, state->selected_item);
                    }
                }
                state->active_menu = EXPLORER_MENU_NONE;
                return;
            }
        } else if (state->active_menu == EXPLORER_MENU_VIEW) {
            m_x = cx + 116; m_w = 150; m_h = 52;
            if (mx >= m_x && mx <= m_x + m_w && my >= m_y && my <= m_y + m_h) {
                int32_t rel_y = my - m_y;
                if (rel_y >= 4 && rel_y < 24) explorer_load_items(state);
                else if (rel_y >= 24 && rel_y <= 48) state->show_folder_tree = !state->show_folder_tree;
                state->active_menu = EXPLORER_MENU_NONE;
                return;
            }
        } else if (state->active_menu == EXPLORER_MENU_TOOLS) {
            m_x = cx + 200; m_w = 180; m_h = 52;
            if (mx >= m_x && mx <= m_x + m_w && my >= m_y && my <= m_y + m_h) {
                int32_t rel_y = my - m_y;
                if (rel_y >= 4 && rel_y < 24) {
                    app_notepad_open_file("Opzioni cartella", "Impostazioni visualizzazione e cartelle di sistema NewOS V1.");
                } else if (rel_y >= 24 && rel_y <= 48) app_devmgmt_launch();
                state->active_menu = EXPLORER_MENU_NONE;
                return;
            }
        } else if (state->active_menu == EXPLORER_MENU_HELP) {
            m_x = cx + 270; m_w = 170; m_h = 32;
            if (mx >= m_x && mx <= m_x + m_w && my >= m_y && my <= m_y + m_h) {
                app_notepad_open_file("Info su Esplora risorse", "NewOS V1 - Esplora Risorse (Windows 2000 Explorer)\nVersione 1.0 (Build 2026)\nSupporto exFAT, NTFS-3G, UDF, ISO 9660, USB.");
                state->active_menu = EXPLORER_MENU_NONE;
                return;
            }
        }
        state->active_menu = EXPLORER_MENU_NONE;
    }

    // 2. Clic sul Menu Contestuale Tasto Destro se aperto
    if (state->ctx_open) {
        int32_t px = state->ctx_x;
        int32_t py = state->ctx_y;
        int32_t pw = 150;
        int32_t ph = state->ctx_is_item ? 72 : 92;

        if (px + pw > cx + cw) px = cx + cw - pw - 4;
        if (py + ph > cy + ch) py = cy + ch - ph - 4;

        if (mx >= px && mx <= px + pw && my >= py && my <= py + ph) {
            int32_t rel_y = my - py;
            if (state->ctx_is_item) {
                if (rel_y >= 4 && rel_y < 24) {
                    explorer_execute_selected(state);
                } else if (rel_y >= 26 && rel_y < 48) {
                    explorer_delete_item(state, state->ctx_target_idx);
                } else if (rel_y >= 50 && rel_y <= 70) {
                    explorer_show_item_properties(state, state->ctx_target_idx);
                }
            } else {
                if (rel_y >= 4 && rel_y < 24) {
                    explorer_create_new_folder(state);
                } else if (rel_y >= 24 && rel_y < 44) {
                    explorer_create_new_file(state);
                } else if (rel_y >= 46 && rel_y < 68) {
                    explorer_load_items(state);
                } else if (rel_y >= 70 && rel_y <= 90) {
                    explorer_show_drive_properties(state);
                }
            }
            state->ctx_open = false;
            return;
        }
        state->ctx_open = false;
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
                explorer_navigate_to(state, (drive_pfx == 'U') ? EXPLORER_MODE_DRIVE_U : EXPLORER_MODE_DRIVE_C, "");
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

    int32_t body_y = cy + 74;
    int32_t body_h = ch - 74 - 22;
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
                } else if (state->mode != EXPLORER_MODE_CONTROL_PANEL) {
                    int32_t btn_y = body_y + 118;
                    if (my >= btn_y && my <= btn_y + 24) {
                        explorer_execute_selected(state);
                        return;
                    }
                } else if (state->mode == EXPLORER_MODE_CONTROL_PANEL) {
                    int32_t btn_y = body_y + 134;
                    if (my >= btn_y && my <= btn_y + 24) {
                        explorer_execute_selected(state);
                        return;
                    }
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
    int32_t body_y = cy + 74;
    int32_t body_h = ch - 74 - 22;
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

    state->ctx_is_item = false;
    state->ctx_target_idx = -1;

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
                    state->ctx_is_item = true;
                    state->ctx_target_idx = i;
                    state->selected_item = i;
                    break;
                }
            }
            if (state->ctx_is_item) break;
        }

        state->ctx_open = true;
        state->ctx_x = mx;
        state->ctx_y = my;
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
                explorer_navigate_to(state, (drive_pfx == 'U') ? EXPLORER_MODE_DRIVE_U : EXPLORER_MODE_DRIVE_C, "");
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
    state->ctx_open = false;
    state->ctx_is_item = false;
    state->ctx_target_idx = -1;
    state->show_folder_tree = false;
    state->search_active = false;
    state->search_filter[0] = '\0';
    state->address_editing = false;
    state->address_buf[0] = '\0';
    state->show_properties = false;
    state->last_click_time = 0;
    state->last_clicked_idx = -1;

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
