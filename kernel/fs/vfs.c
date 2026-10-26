#include <fs/vfs.h>
#include <fs/exfat.h>
#include <fs/exfat_ops.h>
#include <fs/vfs_mount.h>
#include <fs/vfs_cache.h>
#include <fs/iso9660.h>
#include <fs/udf.h>
#include <fs/ntfs.h>
#include <fs/fs_probe.h>
#include <fs/ramfs.h>
#include <drivers/ata.h>
#include <mm/heap.h>
#include <string.h>
#include <arch/x86_64/io.h>

#define MAX_OPEN_FILES 16
static vfs_file_t g_file_pool[MAX_OPEN_FILES];

static void vfs_serial(const char* s) {
    if (!s) return;
    while (*s) outb(0x3F8, *s++);
}

// Normalizza il percorso: rimuove "C:", "c:", converte '\' in '/' e rimuove slash iniziali
static const char* normalize_path(const char* path, char* clean_buf, size_t buf_size) {
    if (!path || !clean_buf || buf_size < 2) return "";

    const char* p = path;
    // Rimuove eventuale lettera di unità es. "C:" o "c:"
    if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':') {
        p += 2;
    }

    // Salta slash iniziali
    while (*p == '/' || *p == '\\') {
        p++;
    }

    size_t i = 0;
    while (*p && i < buf_size - 1) {
        clean_buf[i++] = (*p == '\\') ? '/' : *p;
        p++;
    }
    clean_buf[i] = '\0';
    return clean_buf;
}

// Risolve un percorso navigando di cartella in cartella su exFAT (fallback primario)
static bool resolve_path_exfat(const char* clean_path, exfat_item_info_t* out_info, uint32_t* out_parent_cluster) {
    if (!exfat_is_mounted()) return false;

    uint32_t cur_cluster = exfat_get_root_cluster();
    if (out_parent_cluster) *out_parent_cluster = cur_cluster;

    if (!clean_path || clean_path[0] == '\0') {
        if (out_info) {
            strcpy(out_info->name, "/");
            out_info->size = 0;
            out_info->first_cluster = cur_cluster;
            out_info->attributes = EXFAT_ATTR_DIRECTORY;
            out_info->is_directory = true;
            out_info->no_fat_chain = false;
        }
        return true;
    }

    char temp_path[VFS_MAX_PATH];
    strncpy(temp_path, clean_path, sizeof(temp_path) - 1);
    temp_path[sizeof(temp_path) - 1] = '\0';

    char* token = temp_path;
    char* next = NULL;
    exfat_item_info_t item;

    while (token && *token) {
        next = strstr(token, "/");
        if (next) {
            *next = '\0';
            next++;
        }

        if (out_parent_cluster) *out_parent_cluster = cur_cluster;

        if (!exfat_find_in_dir(cur_cluster, token, &item)) {
            return false;
        }

        if (next && *next) {
            if (!item.is_directory) return false;
            cur_cluster = item.first_cluster;
        } else {
            if (out_info) {
                memcpy(out_info, &item, sizeof(exfat_item_info_t));
            }
            return true;
        }

        token = next;
    }

    return false;
}

bool vfs_init(void) {
    memset(g_file_pool, 0, sizeof(g_file_pool));

    // 1. Inizializza i moduli estesi separati
    vfs_cache_init();
    vfs_mount_init();
    exfat_ops_init();
    iso9660_init();
    udf_init();
    ntfs_init();
    ramfs_init();

    // 2. Annoda dinamicamente i punti di montaggio ufficiali:
    // 2. Rileva l'unità ottica CD-ROM ATAPI (per Live CD e archivio di installazione)
    char opt_fs[16] = "ISO9660";
    char opt_lbl[32] = "Unita ottica (D:)";
    const ata_device_t* atapi_dev = NULL;
    uint32_t atapi_dev_id = 1;
    int dev_cnt = ata_get_device_count();
    for (int i = 0; i < dev_cnt; i++) {
        const ata_device_t* d = ata_get_device(i);
        if (d && d->is_atapi) {
            atapi_dev = d;
            atapi_dev_id = (uint32_t)i;
            break;
        }
    }
    if (atapi_dev) {
        fs_probe_optical_device(atapi_dev, opt_fs, sizeof(opt_fs), opt_lbl, sizeof(opt_lbl));
    }

    // C: -> Partizione primaria exFAT a LBA 2048 (SISTEMA IN ESECUZIONE - SEMPRE PRIORITARIO)
    const ata_device_t* pm = ata_get_primary_master();
    vfs_serial("[VFS] Montaggio C: da Primary Master...\n");
    bool c_ok = vfs_mount_drive('C', "exFAT", "Disco locale (C:)", 0, EXFAT_PARTITION_LBA, false, (void*)pm);
    if (!c_ok && atapi_dev) {
        vfs_serial("[VFS] Fallback: Montaggio C: da unita' ottica ATAPI...\n");
        c_ok = vfs_mount_drive('C', "exFAT", "Disco locale (C:)", atapi_dev_id, EXFAT_PARTITION_LBA, true, (void*)atapi_dev);
        if (c_ok) {
            vfs_serial("[VFS] Montaggio C: da unita' ottica RIUSCITO!\n");
        } else {
            vfs_serial("[VFS] Montaggio C: da unita' ottica FALLITO!\n");
        }
    }

    // D: -> Lettore Ottico CD-ROM / DVD / Blu-Ray (con rilevamento automatico UDF / ISO 9660 / Joliet / Bridge)
    vfs_mount_drive('D', opt_fs, opt_lbl, atapi_dev_id, 0, true, (void*)atapi_dev);

    // R: -> Disco RAM in memoria ad altissima velocità
    vfs_mount_drive('R', "RamFS", "Disco RAM (R:)", 0, 0, false, NULL);

    // 3. Montaggio di tutti gli altri Hard Disk / SSD secondari rilevati sulla scheda madre:
    // Il disco C: e' SEMPRE e RIGOROSAMENTE il sistema operativo in esecuzione.
    // Se altri dischi hanno partizioni con lettera C:, vengono riassegnati alla prima lettera non allocata.
    for (int i = 0; i < dev_cnt; i++) {
        const ata_device_t* dev = ata_get_device(i);
        if (!dev || !dev->present || dev->is_atapi) continue;
        if (dev == pm) continue; // Salta il disco di sistema C:

        // Trova la prima lettera di unità libera non allocata (E..Z, escluse R e U)
        char free_letter = 0;
        for (char cand = 'E'; cand <= 'Z'; cand++) {
            if (cand == 'R' || cand == 'U') continue;
            vfs_drive_info_t dummy;
            if (!vfs_get_drive_info(cand, &dummy)) {
                free_letter = cand;
                break;
            }
        }
        if (!free_letter) break; // Terminate lettere di unità disponibili

        const char* fs_name = "RAW";
        if (strcmp(dev->part_fs, "NTFS") == 0) {
            fs_name = "NTFS";
        } else if (strcmp(dev->part_fs, "exFAT") == 0) {
            fs_name = "exFAT";
        } else if (strcmp(dev->part_fs, "FAT32") == 0 || strcmp(dev->part_fs, "FAT16") == 0) {
            fs_name = "exFAT"; // Fallback compatibile FAT
        }

        char label[32];
        mini_snprintf(label, sizeof(label), "Disco locale (%c:)", free_letter);

        vfs_mount_drive(free_letter, fs_name, label, (uint32_t)i, dev->part_lba, false, (void*)dev);
    }

    return c_ok;
}

vfs_file_t* vfs_open(const char* path) {
    if (!path) return NULL;

    // Trova uno slot libero nel pool
    int free_slot = -1;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!g_file_pool[i].is_open) {
            free_slot = i;
            break;
        }
    }
    if (free_slot < 0) return NULL;

    vfs_file_t* f = &g_file_pool[free_slot];
    memset(f, 0, sizeof(vfs_file_t));

    // Risoluzione tramite Mount Manager e dispatch dinamico
    const char* rel_path = NULL;
    vfs_mount_t* mnt = vfs_resolve_path_mount(path, &rel_path);

    if (mnt && mnt->driver && mnt->driver->open) {
        if (mnt->driver->open(mnt, rel_path, f)) {
            f->drive_letter = mnt->drive_letter;
            f->driver_data = mnt;
            f->is_open = true;
            return f;
        }
    }

    // Fallback: prova risoluzione diretta su exFAT (C:\)
    char clean[VFS_MAX_PATH];
    normalize_path(path, clean, sizeof(clean));

    exfat_item_info_t info;
    uint32_t parent_cl = exfat_get_root_cluster();
    if (resolve_path_exfat(clean, &info, &parent_cl)) {
        strncpy(f->name, info.name, sizeof(f->name) - 1);
        f->name[sizeof(f->name) - 1] = '\0';
        f->size = info.size;
        f->first_cluster = info.first_cluster;
        f->current_pos = 0;
        f->is_directory = info.is_directory;
        f->no_fat_chain = info.no_fat_chain;
        f->drive_letter = 'C';
        f->driver_data = vfs_resolve_path_mount("C:", NULL);
        f->dir_cluster = parent_cl;
        f->is_open = true;
        return f;
    }

    return NULL;
}

int32_t vfs_read(vfs_file_t* file, void* buf, uint32_t count) {
    if (!file || !file->is_open || !buf || count == 0) return 0;

    if (file->driver_data) {
        vfs_mount_t* mnt = (vfs_mount_t*)file->driver_data;
        if (mnt->driver && mnt->driver->read) {
            return mnt->driver->read(mnt, file, buf, count);
        }
    }

    // Fallback exFAT
    if (file->current_pos >= file->size) return 0;
    exfat_item_info_t info;
    info.size = file->size;
    info.first_cluster = file->first_cluster;
    info.no_fat_chain = file->no_fat_chain;

    int32_t read_bytes = exfat_read_file(&info, file->current_pos, buf, count);
    if (read_bytes > 0) {
        file->current_pos += read_bytes;
    }
    return read_bytes;
}

int32_t vfs_write(vfs_file_t* file, const void* buf, uint32_t count) {
    if (!file || !file->is_open || !buf || count == 0) return 0;

    vfs_mount_t* mnt = (vfs_mount_t*)file->driver_data;
    if (!mnt) {
        mnt = vfs_resolve_path_mount("C:", NULL);
    }
    if (mnt) {
        if (mnt->read_only) return -1;
        if (mnt->driver && mnt->driver->write) {
            return mnt->driver->write(mnt, file, buf, count);
        }
    }

    // Fallback exFAT in-place
    exfat_item_info_t info;
    info.size = file->size;
    info.first_cluster = file->first_cluster;
    info.no_fat_chain = file->no_fat_chain;

    int32_t wb = exfat_write_file_inplace(&info, file->current_pos, buf, count);
    if (wb > 0) file->current_pos += wb;
    return wb;
}

int32_t vfs_seek(vfs_file_t* file, int32_t offset, int whence) {
    if (!file || !file->is_open) return -1;

    if (file->driver_data) {
        vfs_mount_t* mnt = (vfs_mount_t*)file->driver_data;
        if (mnt->driver && mnt->driver->seek) {
            return mnt->driver->seek(mnt, file, offset, whence);
        }
    }

    int32_t new_pos = (int32_t)file->current_pos;
    if (whence == VFS_SEEK_SET) new_pos = offset;
    else if (whence == VFS_SEEK_CUR) new_pos += offset;
    else if (whence == VFS_SEEK_END) new_pos = (int32_t)file->size + offset;

    if (new_pos < 0) new_pos = 0;
    if ((uint32_t)new_pos > file->size) new_pos = (int32_t)file->size;

    file->current_pos = (uint32_t)new_pos;
    return new_pos;
}

uint32_t vfs_tell(vfs_file_t* file) {
    if (!file || !file->is_open) return 0;
    return file->current_pos;
}

void vfs_close(vfs_file_t* file) {
    if (!file) return;

    if (file->driver_data) {
        vfs_mount_t* mnt = (vfs_mount_t*)file->driver_data;
        if (mnt->driver && mnt->driver->close) {
            mnt->driver->close(mnt, file);
        }
    }
    file->is_open = false;
}

int32_t vfs_readdir(const char* dir_path, vfs_dirent_t* entries, int32_t max_entries) {
    if (!entries || max_entries <= 0) return 0;

    const char* rel_path = NULL;
    vfs_mount_t* mnt = vfs_resolve_path_mount(dir_path, &rel_path);

    if (mnt && mnt->driver && mnt->driver->readdir) {
        return mnt->driver->readdir(mnt, rel_path, entries, max_entries);
    }

    // Fallback exFAT root
    char clean[VFS_MAX_PATH];
    normalize_path(dir_path, clean, sizeof(clean));

    uint32_t dir_cluster = 0;
    if (clean[0] == '\0') {
        dir_cluster = exfat_get_root_cluster();
    } else {
        exfat_item_info_t dir_info;
        if (!resolve_path_exfat(clean, &dir_info, NULL)) {
            return 0;
        }
        if (!dir_info.is_directory) return 0;
        dir_cluster = dir_info.first_cluster;
    }

    exfat_item_info_t* raw_items = (exfat_item_info_t*)kmalloc(sizeof(exfat_item_info_t) * 64);
    if (!raw_items) return 0;
    int32_t raw_count = exfat_list_dir(dir_cluster, raw_items, (max_entries < 64) ? max_entries : 64);

    for (int32_t i = 0; i < raw_count; i++) {
        strncpy(entries[i].name, raw_items[i].name, sizeof(entries[i].name) - 1);
        entries[i].name[sizeof(entries[i].name) - 1] = '\0';
        entries[i].size = raw_items[i].size;
        entries[i].attributes = raw_items[i].attributes;
        entries[i].is_directory = raw_items[i].is_directory;
    }

    kfree(raw_items);
    return raw_count;
}

bool vfs_exists(const char* path) {
    if (!path) return false;

    const char* rel_path = NULL;
    vfs_mount_t* mnt = vfs_resolve_path_mount(path, &rel_path);

    if (mnt && mnt->driver && mnt->driver->stat) {
        vfs_dirent_t st;
        return mnt->driver->stat(mnt, rel_path, &st);
    }

    char clean[VFS_MAX_PATH];
    normalize_path(path, clean, sizeof(clean));
    return resolve_path_exfat(clean, NULL, NULL);
}

uint32_t vfs_file_size(const char* path) {
    if (!path) return 0;

    const char* rel_path = NULL;
    vfs_mount_t* mnt = vfs_resolve_path_mount(path, &rel_path);

    if (mnt && mnt->driver && mnt->driver->stat) {
        vfs_dirent_t st;
        if (mnt->driver->stat(mnt, rel_path, &st)) {
            return st.size;
        }
    }

    char clean[VFS_MAX_PATH];
    normalize_path(path, clean, sizeof(clean));
    exfat_item_info_t info;
    if (resolve_path_exfat(clean, &info, NULL)) {
        return info.size;
    }
    return 0;
}

bool vfs_create(const char* path) {
    if (!path) return false;

    const char* rel_path = NULL;
    vfs_mount_t* mnt = vfs_resolve_path_mount(path, &rel_path);

    if (mnt && mnt->driver && mnt->driver->create) {
        return mnt->driver->create(mnt, rel_path, VFS_ATTR_ARCHIVE);
    }
    return false;
}

bool vfs_mkdir(const char* path) {
    if (!path) return false;

    const char* rel_path = NULL;
    vfs_mount_t* mnt = vfs_resolve_path_mount(path, &rel_path);

    if (mnt && mnt->driver && mnt->driver->mkdir) {
        return mnt->driver->mkdir(mnt, rel_path);
    }
    return false;
}

static bool path_starts_with_nocase(const char* str, const char* prefix) {
    while (*prefix) {
        char c1 = *str++;
        char c2 = *prefix++;
        if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
        if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
        if (c1 != c2) return false;
    }
    return true;
}

bool vfs_is_protected(const char* path) {
    if (!path) return false;

    // Rimuove eventuale lettera di unita' (es. "C:" o "C:\")
    const char* p = path;
    if ((p[0] >= 'A' && p[0] <= 'Z' && p[1] == ':') ||
        (p[0] >= 'a' && p[0] <= 'z' && p[1] == ':')) {
        p += 2;
    }
    while (*p == '\\' || *p == '/') p++;

    // 1. Binari critici del kernel, bootloader e archivio di sistema
    if (strcasecmp(p, "KERNEL.BIN") == 0 || strcasecmp(p, "KERNEL.ELF") == 0 ||
        strcasecmp(p, "STAGE2.BIN") == 0 || strcasecmp(p, "BOOTSECT.BIN") == 0 ||
        strcasecmp(p, "BOOTX64.EFI") == 0 || strcasecmp(p, "BOOTX64.BIN") == 0 ||
        strcasecmp(p, "SYSTEM.PAK") == 0 || strcasecmp(p, "BootSetup/SYSTEM.PAK") == 0 ||
        strcasecmp(p, "BootSetup\\SYSTEM.PAK") == 0) {
        return true;
    }

    // 2. Moduli protetti di boot e codec (.MOD e .CDC)
    if (strcasecmp(p, "BOOTSCR.MOD") == 0 || strcasecmp(p, "LOGIN.MOD") == 0 ||
        strcasecmp(p, "LOGOUT.MOD") == 0 || strcasecmp(p, "BOOTSETUP.MOD") == 0 ||
        strcasecmp(p, "UPDATE.MOD") == 0) {
        return true;
    }

    // 3. Cartelle radice di sistema inamovibili
    if (strcasecmp(p, "NewOS") == 0 || strcasecmp(p, "ProgramFile") == 0 ||
        strcasecmp(p, "User") == 0 || strcasecmp(p, "EFI") == 0 ||
        strcasecmp(p, "Boot") == 0) {
        return true;
    }

    return false;
}

bool vfs_is_program_file_path(const char* path) {
    if (!path) return false;
    const char* p = path;
    if ((p[0] >= 'A' && p[0] <= 'Z' && p[1] == ':') ||
        (p[0] >= 'a' && p[0] <= 'z' && p[1] == ':')) {
        p += 2;
    }
    while (*p == '\\' || *p == '/') p++;

    return (path_starts_with_nocase(p, "ProgramFile") || path_starts_with_nocase(p, "User"));
}

bool vfs_unlink(const char* path) {
    if (!path) return false;

    // Protezione WAF: rifiuta l'eliminazione di file critici o cartelle di sistema
    if (vfs_is_protected(path)) {
        return false;
    }

    const char* rel_path = NULL;
    vfs_mount_t* mnt = vfs_resolve_path_mount(path, &rel_path);

    if (mnt && mnt->driver && mnt->driver->unlink) {
        return mnt->driver->unlink(mnt, rel_path);
    }
    return false;
}

bool vfs_rename(const char* old_path, const char* new_path) {
    if (!old_path || !new_path) return false;

    // Protezione WAF: rifiuta di rinominare elementi protetti o sovrascrivere file protetti
    if (vfs_is_protected(old_path) || vfs_is_protected(new_path)) {
        return false;
    }

    const char* rel_old = NULL;
    vfs_mount_t* mnt_old = vfs_resolve_path_mount(old_path, &rel_old);

    const char* rel_new = NULL;
    vfs_mount_t* mnt_new = vfs_resolve_path_mount(new_path, &rel_new);

    if (!mnt_old || !mnt_new) return false;

    if (mnt_old == mnt_new && mnt_old->driver && mnt_old->driver->rename) {
        bool ok = mnt_old->driver->rename(mnt_old, rel_old, rel_new);
        if (ok) {
            vfs_dentry_invalidate(mnt_old->drive_letter, rel_old);
            vfs_dentry_invalidate(mnt_new->drive_letter, rel_new);
        }
        return ok;
    }

    // Fallback cross-device: copia e cancellazione atomica
    uint32_t fsize = 0;
    void* data = vfs_read_entire_file(old_path, &fsize);
    if (!data && vfs_file_size(old_path) > 0) return false;

    if (!vfs_create(new_path)) {
        if (data) kfree(data);
        return false;
    }

    if (data && fsize > 0) {
        vfs_write_entire_file_inplace(new_path, data, fsize);
        kfree(data);
    }

    vfs_unlink(old_path);
    return true;
}

void* vfs_read_entire_file(const char* path, uint32_t* out_size) {
    vfs_file_t* f = vfs_open(path);
    if (!f) return NULL;

    uint32_t sz = f->size;
    uint8_t* buf = (uint8_t*)kmalloc(sz + 1);
    if (!buf) {
        vfs_close(f);
        return NULL;
    }

    int32_t read_bytes = vfs_read(f, buf, sz);
    vfs_close(f);

    if (read_bytes < (int32_t)sz) {
        kfree(buf);
        return NULL;
    }

    buf[sz] = '\0';
    if (out_size) *out_size = sz;
    return buf;
}

int32_t vfs_write_entire_file_inplace(const char* path, const void* buf, uint32_t count) {
    if (!path || !buf || count == 0) return 0;
    vfs_file_t* f = vfs_open(path);
    if (!f) {
        // Se non esiste, crea il file
        if (!vfs_create(path)) return 0;
        f = vfs_open(path);
        if (!f) return 0;
    }

    int32_t written = vfs_write(f, buf, count);
    vfs_close(f);
    return written;
}

bool vfs_wildcard_match(const char* pattern, const char* str) {
    if (!pattern || !str) return false;

    while (*pattern) {
        if (*pattern == '*') {
            pattern++;
            if (*pattern == '\0') return true; // '*' finale matcha qualsiasi coda di stringa
            while (*str) {
                if (vfs_wildcard_match(pattern, str)) return true;
                str++;
            }
            return false;
        } else if (*pattern == '?') {
            if (*str == '\0') return false;
            pattern++;
            str++;
        } else {
            char c1 = *pattern;
            char c2 = *str;
            if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
            if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
            if (c1 != c2) return false;
            pattern++;
            str++;
        }
    }
    return (*str == '\0');
}

void vfs_flush(void) {
    vfs_cache_flush();
}
