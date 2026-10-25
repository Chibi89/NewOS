#include <fs/vfs_mount.h>
#include <drivers/ata.h>
#include <fs/exfat.h>
#include <string.h>

static vfs_driver_t g_registered_drivers[VFS_MAX_FS_DRIVERS];
static int          g_num_registered_drivers = 0;

static vfs_mount_t  g_mounts[VFS_MAX_DRIVES];
static int          g_num_mounts = 0;

void vfs_mount_init(void) {
    memset(g_registered_drivers, 0, sizeof(g_registered_drivers));
    g_num_registered_drivers = 0;

    memset(g_mounts, 0, sizeof(g_mounts));
    g_num_mounts = 0;
}

bool vfs_register_driver(const vfs_driver_t* driver) {
    if (!driver || !driver->name[0]) return false;
    if (g_num_registered_drivers >= VFS_MAX_FS_DRIVERS) return false;

    // Controlla se esiste già
    for (int i = 0; i < g_num_registered_drivers; i++) {
        if (strcasecmp(g_registered_drivers[i].name, driver->name) == 0) {
            // Aggiorna puntatori driver
            memcpy(&g_registered_drivers[i], driver, sizeof(vfs_driver_t));
            return true;
        }
    }

    memcpy(&g_registered_drivers[g_num_registered_drivers++], driver, sizeof(vfs_driver_t));
    return true;
}

const vfs_driver_t* vfs_find_driver(const char* name) {
    if (!name || !*name) return NULL;
    for (int i = 0; i < g_num_registered_drivers; i++) {
        if (strcasecmp(g_registered_drivers[i].name, name) == 0) {
            return &g_registered_drivers[i];
        }
    }
    return NULL;
}

static char normalize_drive_letter(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - 32);
    return c;
}

bool vfs_is_drive_mounted(char drive_letter) {
    char letter = normalize_drive_letter(drive_letter);
    for (int i = 0; i < g_num_mounts; i++) {
        if (g_mounts[i].is_mounted && g_mounts[i].drive_letter == letter) {
            return true;
        }
    }
    return false;
}

char vfs_get_first_free_letter(void) {
    // Cerchiamo la prima lettera non allocata nell'intervallo E..Z (riservando R e U se non necessarie)
    for (char cand = 'E'; cand <= 'Z'; cand++) {
        if (cand == 'R' || cand == 'U') continue;
        if (!vfs_is_drive_mounted(cand)) {
            return cand;
        }
    }
    // Fallback prioritario se le altre lettere sono occupate
    for (char cand = 'B'; cand <= 'Z'; cand++) {
        if (cand == 'C') continue; // C: e' riservato esclusivamente al sistema
        if (!vfs_is_drive_mounted(cand)) {
            return cand;
        }
    }
    return 0;
}

static bool raw_open(vfs_mount_t* mnt, const char* path, vfs_file_t* out_file) {
    (void)mnt; (void)path; (void)out_file;
    return false;
}

static int32_t raw_readdir(vfs_mount_t* mnt, const char* path, vfs_dirent_t* entries, int32_t max_entries) {
    (void)mnt; (void)path; (void)entries; (void)max_entries;
    return 0; // Unità RAW o non formattata: 0 elementi
}

static const vfs_driver_t g_raw_driver = {
    .name = "RAW",
    .mount = NULL,
    .unmount = NULL,
    .open = raw_open,
    .read = NULL,
    .write = NULL,
    .close = NULL,
    .seek = NULL,
    .readdir = raw_readdir,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .stat = NULL
};

bool vfs_mount_drive(char drive_letter, const char* fs_name, const char* label, uint32_t dev_id, uint64_t lba, bool read_only, void* priv) {
    char letter = normalize_drive_letter(drive_letter);
    if (letter < 'A' || letter > 'Z') return false;

    // Buffer locale per l'etichetta dell'unità
    char final_label[32];
    if (label && label[0]) {
        strncpy(final_label, label, sizeof(final_label) - 1);
        final_label[sizeof(final_label) - 1] = '\0';
    } else {
        mini_snprintf(final_label, sizeof(final_label), "Disco locale (%c:)", letter);
    }

    // REGOLA TASSATIVA: Il disco locale C: e' SEMPRE e RIGOROSAMENTE il sistema in esecuzione.
    // Se C: e' gia' montato e vengono rilevati altri dischi con tale lettera,
    // la lettera viene automaticamente passata alla prima lettera non allocata.
    if (letter == 'C' && vfs_is_drive_mounted('C')) {
        char free_letter = vfs_get_first_free_letter();
        if (!free_letter) return false;
        letter = free_letter;

        // Se l'etichetta conteneva "(C:)" o "(c:)", riallinea il testo con la nuova lettera
        char* c_tag = strstr(final_label, "(C:)");
        if (!c_tag) c_tag = strstr(final_label, "(c:)");
        if (c_tag) {
            c_tag[1] = letter;
        } else if (strcmp(final_label, "Disco locale (C:)") == 0) {
            mini_snprintf(final_label, sizeof(final_label), "Disco locale (%c:)", letter);
        }
    }

    const vfs_driver_t* drv = vfs_find_driver(fs_name);
    if (!drv) {
        drv = &g_raw_driver; // Fallback per filesystem non riconosciuti o partizioni RAW
    }

    // Trova slot esistente per la lettera o allocane uno nuovo
    vfs_mount_t* target = NULL;
    for (int i = 0; i < g_num_mounts; i++) {
        if (g_mounts[i].drive_letter == letter) {
            target = &g_mounts[i];
            break;
        }
    }

    if (!target) {
        if (g_num_mounts >= VFS_MAX_DRIVES) return false;
        target = &g_mounts[g_num_mounts++];
    }

    memset(target, 0, sizeof(vfs_mount_t));
    target->drive_letter = letter;
    target->is_mounted = true;
    target->read_only = read_only;
    target->device_id = dev_id;
    target->partition_lba = lba;
    target->driver = drv;
    target->priv_data = priv;
    strncpy(target->label, final_label, sizeof(target->label) - 1);

    // Risoluzione dispositivo hardware reale se non esplicitamente passato
    if (!priv) {
        if (letter == 'C') {
            priv = (void*)ata_get_primary_master();
        } else if (dev_id < (uint32_t)ata_get_device_count()) {
            priv = (void*)ata_get_device((int)dev_id);
        }
    }
    target->priv_data = priv;

    // Calcolo capacità reale dal chip del dispositivo collegato (HD / SSD)
    if (priv) {
        const ata_device_t* d = (const ata_device_t*)priv;
        if (d->size_mb > 0) {
            target->total_mb = d->size_mb;
            target->free_mb = d->size_mb > 64 ? d->size_mb - 64 : 0;
        } else {
            target->total_mb = 128;
            target->free_mb = 64;
        }
    } else {
        target->total_mb = (letter == 'C') ? 128 : ((letter == 'D') ? 650 : ((letter == 'U') ? 128 : 16));
        target->free_mb = target->total_mb / 2;
    }

    // Chiama callback mount del driver se implementata
    if (drv->mount) {
        if (!drv->mount(target)) {
            target->is_mounted = false;
            return false;
        }
    }

    return true;
}

bool vfs_unmount_drive(char drive_letter) {
    char letter = normalize_drive_letter(drive_letter);
    for (int i = 0; i < g_num_mounts; i++) {
        if (g_mounts[i].drive_letter == letter && g_mounts[i].is_mounted) {
            if (g_mounts[i].driver && g_mounts[i].driver->unmount) {
                g_mounts[i].driver->unmount(&g_mounts[i]);
            }
            g_mounts[i].is_mounted = false;
            return true;
        }
    }
    return false;
}

vfs_mount_t* vfs_resolve_path_mount(const char* path, const char** out_rel_path) {
    if (!path) return NULL;

    char target_letter = 'C'; // Default drive C:
    const char* p = path;

    // Verifica se è presente prefisso di unità es. "C:" o "D:" o "R:"
    if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':') {
        target_letter = normalize_drive_letter(p[0]);
        p += 2;
    }

    // Salta eventuali slash iniziali
    while (*p == '/' || *p == '\\') {
        p++;
    }

    if (out_rel_path) {
        *out_rel_path = p;
    }

    for (int i = 0; i < g_num_mounts; i++) {
        if (g_mounts[i].is_mounted && g_mounts[i].drive_letter == target_letter) {
            return &g_mounts[i];
        }
    }

    return NULL;
}

bool vfs_get_drive_info(char drive_letter, vfs_drive_info_t* out_info) {
    if (!out_info) return false;
    char letter = normalize_drive_letter(drive_letter);

    for (int i = 0; i < g_num_mounts; i++) {
        if (g_mounts[i].is_mounted && g_mounts[i].drive_letter == letter) {
            out_info->drive_letter = letter;
            strncpy(out_info->fs_name, g_mounts[i].driver ? g_mounts[i].driver->name : "RAW", sizeof(out_info->fs_name) - 1);
            strncpy(out_info->label, g_mounts[i].label, sizeof(out_info->label) - 1);
            out_info->read_only = g_mounts[i].read_only;

            // Aggiorna dinamicamente la capacità e lo spazio disponibile dal chip hardware e dal filesystem
            const ata_device_t* dev = (const ata_device_t*)g_mounts[i].priv_data;
            if (!dev && letter == 'C') {
                dev = ata_get_primary_master();
                g_mounts[i].priv_data = (void*)dev;
            }
            if (dev && dev->present && dev->size_mb > 0) {
                g_mounts[i].total_mb = dev->size_mb;
            }

            if (g_mounts[i].driver && strcmp(g_mounts[i].driver->name, "exFAT") == 0) {
                uint32_t used_mb = exfat_get_used_mb();
                g_mounts[i].free_mb = g_mounts[i].total_mb > used_mb ? (g_mounts[i].total_mb - used_mb) : 0;
            }

            out_info->total_mb = g_mounts[i].total_mb;
            out_info->free_mb = g_mounts[i].free_mb;
            return true;
        }
    }
    return false;
}

int vfs_list_drives(char* out_letters, int max_drives) {
    if (!out_letters || max_drives <= 0) return 0;
    int count = 0;
    for (int i = 0; i < g_num_mounts && count < max_drives; i++) {
        if (g_mounts[i].is_mounted) {
            out_letters[count++] = g_mounts[i].drive_letter;
        }
    }
    return count;
}
