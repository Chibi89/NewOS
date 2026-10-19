#include <fs/vfs_mount.h>
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

bool vfs_mount_drive(char drive_letter, const char* fs_name, const char* label, uint32_t dev_id, uint32_t lba, bool read_only, void* priv) {
    char letter = normalize_drive_letter(drive_letter);
    if (letter < 'A' || letter > 'Z') return false;

    const vfs_driver_t* drv = vfs_find_driver(fs_name);
    if (!drv) return false;

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
    if (label) {
        strncpy(target->label, label, sizeof(target->label) - 1);
    } else {
        mini_snprintf(target->label, sizeof(target->label), "Unita %c", letter);
    }
    target->total_mb = (letter == 'C') ? 128 : ((letter == 'D') ? 650 : ((letter == 'U') ? 128 : 16));
    target->free_mb = target->total_mb / 2;

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
