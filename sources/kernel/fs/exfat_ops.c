#include <fs/exfat_ops.h>
#include <fs/exfat.h>
#include <fs/vfs_cache.h>
#include <drivers/ata.h>
#include <arch/x86_64/io.h>
#include <mm/heap.h>
#include <string.h>

static uint16_t exfat_calc_name_hash(const char* name) {
    uint16_t hash = 0;
    while (*name) {
        char ch = *name++;
        if (ch >= 'a' && ch <= 'z') ch -= 32; // Uppercase
        uint16_t u16 = (uint16_t)ch;
        uint8_t b0 = (uint8_t)(u16 & 0xFF);
        uint8_t b1 = (uint8_t)(u16 >> 8);
        hash = (((hash >> 1) | ((hash & 1) << 15)) + b0) & 0xFFFF;
        hash = (((hash >> 1) | ((hash & 1) << 15)) + b1) & 0xFFFF;
    }
    return hash;
}

static uint16_t exfat_entry_set_checksum(const uint8_t* entries, int num_entries) {
    uint16_t checksum = 0;
    for (int e = 0; e < num_entries; e++) {
        for (int b = 0; b < 32; b++) {
            if (e == 0 && (b == 2 || b == 3)) continue;
            uint8_t byte = entries[e * 32 + b];
            checksum = (((checksum >> 1) | ((checksum & 1) << 15)) + byte) & 0xFFFF;
        }
    }
    return checksum;
}

uint32_t exfat_ops_allocate_cluster(void) {
    const exfat_vbr_t* vbr = exfat_get_vbr();
    if (!vbr) return 0;

    uint32_t total_clusters = vbr->cluster_count;
    uint32_t fat_lba = EXFAT_PARTITION_LBA + vbr->fat_offset;

    uint8_t sec_buf[512];

    // Scansiona la FAT per trovare il primo cluster libero (valore 0x00000000)
    for (uint32_t cl = 2; cl < total_clusters + 2; cl++) {
        uint32_t sec = fat_lba + (cl * 4) / 512;
        uint32_t off = (cl * 4) % 512;

        if (!vfs_cache_read_sector(0, sec, sec_buf)) {
            continue;
        }

        uint32_t val = *(uint32_t*)(sec_buf + off);
        if (val == 0) {
            // Trovato cluster libero! Marcalo come ultimo cluster (0xFFFFFFFF)
            *(uint32_t*)(sec_buf + off) = 0xFFFFFFFF;
            vfs_cache_write_sector(0, sec, sec_buf);

            // Inizializza a zero il cluster allocato
            uint32_t spc = 1 << vbr->sectors_per_cluster_shift;
            uint32_t cl_lba = EXFAT_PARTITION_LBA + vbr->cluster_heap_offset + (cl - 2) * spc;
            uint8_t zero_sec[512];
            memset(zero_sec, 0, 512);
            for (uint32_t s = 0; s < spc; s++) {
                vfs_cache_write_sector(0, cl_lba + s, zero_sec);
            }

            exfat_notify_cluster_allocated();
            return cl;
        }
    }

    return 0;
}

bool exfat_ops_create_entry(uint32_t dir_cluster, const char* name, uint32_t first_cluster, uint32_t initial_size, uint16_t attributes) {
    if (!name || !*name || dir_cluster < 2) return false;

    const exfat_vbr_t* vbr = exfat_get_vbr();
    if (!vbr) return false;

    uint32_t spc = 1 << vbr->sectors_per_cluster_shift;
    uint32_t cl_size = spc * 512;
    uint8_t* cl_buf = (uint8_t*)kmalloc(cl_size);
    if (!cl_buf) return false;

    uint32_t lba = EXFAT_PARTITION_LBA + vbr->cluster_heap_offset + (dir_cluster - 2) * spc;

    for (uint32_t s = 0; s < spc; s++) {
        vfs_cache_read_sector(0, lba + s, cl_buf + s * 512);
    }

    uint32_t entries_count = cl_size / 32;
    int free_slot = -1;

    // Trova 3 entry consecutive libere (0x00 o non critiche/cancellate < 0x80)
    for (uint32_t i = 0; i <= entries_count - 3; i++) {
        bool ok = true;
        for (int k = 0; k < 3; k++) {
            uint8_t type = cl_buf[(i + k) * 32];
            if (type != EXFAT_ENTRY_EOD && (type & 0x80) != 0) {
                ok = false;
                break;
            }
        }
        if (ok) {
            free_slot = (int)i;
            break;
        }
    }

    if (free_slot < 0) {
        kfree(cl_buf);
        return false;
    }

    // Costruisci il Set di 3 Directory Entries (96 byte)
    uint8_t entry_set[96];
    memset(entry_set, 0, sizeof(entry_set));

    // 1. Entry 0x85 (File/Directory)
    entry_set[0] = EXFAT_ENTRY_FILE;        // 0x85
    entry_set[1] = 2;                       // secondary count: 1 Stream + 1 Name
    *(uint16_t*)(entry_set + 4) = attributes;

    // 2. Entry 0xC0 (Stream Extension)
    uint8_t* stream = entry_set + 32;
    stream[0] = EXFAT_ENTRY_STREAM;         // 0xC0
    stream[1] = EXFAT_FLAG_ALLOC_POSSIBLE | EXFAT_FLAG_NO_FAT_CHAIN; // 0x01 | 0x02
    size_t name_len = strlen(name);
    if (name_len > 15) name_len = 15;
    stream[3] = (uint8_t)name_len;
    *(uint16_t*)(stream + 4) = exfat_calc_name_hash(name);
    *(uint32_t*)(stream + 20) = first_cluster;
    *(uint64_t*)(stream + 24) = (uint64_t)initial_size;

    // 3. Entry 0xC1 (File Name)
    uint8_t* nentry = entry_set + 64;
    nentry[0] = EXFAT_ENTRY_NAME;           // 0xC1
    for (size_t c = 0; c < name_len; c++) {
        *(uint16_t*)(nentry + 2 + c * 2) = (uint16_t)(uint8_t)name[c];
    }

    // Calcola e assegna Checksum del Set su entry 0x85 offset 2..3
    uint16_t chk = exfat_entry_set_checksum(entry_set, 3);
    *(uint16_t*)(entry_set + 2) = chk;

    // Copia il set nel buffer del cluster della directory
    memcpy(cl_buf + free_slot * 32, entry_set, 96);

    // Se il record non è alla fine del cluster, assicurati che il successivo sia EOD se era vuoto
    if ((uint32_t)(free_slot + 3) < entries_count) {
        if (cl_buf[(free_slot + 3) * 32] == 0) {
            cl_buf[(free_slot + 3) * 32] = EXFAT_ENTRY_EOD;
        }
    }

    // Scrivi i settori aggiornati
    for (uint32_t s = 0; s < spc; s++) {
        vfs_cache_write_sector(0, lba + s, cl_buf + s * 512);
    }

    kfree(cl_buf);
    return true;
}

bool exfat_ops_delete_entry(uint32_t dir_cluster, const char* name) {
    if (!name || !*name || dir_cluster < 2) return false;

    const exfat_vbr_t* vbr = exfat_get_vbr();
    if (!vbr) return false;

    uint32_t spc = 1 << vbr->sectors_per_cluster_shift;
    uint32_t cl_size = spc * 512;
    uint8_t* cl_buf = (uint8_t*)kmalloc(cl_size);
    if (!cl_buf) return false;

    uint32_t lba = EXFAT_PARTITION_LBA + vbr->cluster_heap_offset + (dir_cluster - 2) * spc;

    for (uint32_t s = 0; s < spc; s++) {
        vfs_cache_read_sector(0, lba + s, cl_buf + s * 512);
    }

    uint32_t entries_count = cl_size / 32;

    for (uint32_t i = 0; i < entries_count; i++) {
        uint8_t* entry = cl_buf + i * 32;
        if (entry[0] == EXFAT_ENTRY_EOD) break;

        if (entry[0] == EXFAT_ENTRY_FILE) {
            uint8_t sec_cnt = entry[1];
            if (sec_cnt >= 2 && i + sec_cnt < entries_count) {
                // Leggi il nome
                char cur_name[64];
                int npos = 0;
                uint8_t* nentry = cl_buf + (i + 2) * 32;
                if (nentry[0] == EXFAT_ENTRY_NAME) {
                    for (int c = 0; c < 15; c++) {
                        uint16_t ch = *(uint16_t*)(nentry + 2 + c * 2);
                        if (ch == 0) break;
                        cur_name[npos++] = (char)(ch & 0x7F);
                    }
                    cur_name[npos] = '\0';
                }

                if (strcasecmp(cur_name, name) == 0) {
                    // Trovato! Marca come cancellato azzerando il bit 7
                    entry[0] = 0x05; // Deleted file
                    cl_buf[(i + 1) * 32] = 0x40; // Deleted stream
                    cl_buf[(i + 2) * 32] = 0x41; // Deleted name

                    for (uint32_t s = 0; s < spc; s++) {
                        vfs_cache_write_sector(0, lba + s, cl_buf + s * 512);
                    }
                    kfree(cl_buf);
                    exfat_notify_cluster_freed();
                    return true;
                }
            }
            i += sec_cnt;
        }
    }

    kfree(cl_buf);
    return false;
}

// -------------------------------------------------------------
// Driver VFS Dinamico per exFAT
// -------------------------------------------------------------

static bool exfat_vfs_mount(vfs_mount_t* mnt) {
    if (!mnt) return false;
    if (mnt->drive_letter == 'C') {
        if (!exfat_is_mounted()) {
            exfat_mount(mnt->partition_lba);
        }
    } else if (mnt->partition_lba > 0) {
        exfat_mount(mnt->partition_lba);
    }

    // Risoluzione periferica fisica reale dal chip (ATA / AHCI)
    const ata_device_t* dev = (const ata_device_t*)mnt->priv_data;
    if (!dev) {
        if (mnt->drive_letter == 'C') {
            dev = ata_get_primary_master();
            mnt->priv_data = (void*)dev;
        } else if (mnt->device_id < (uint32_t)ata_get_device_count()) {
            dev = ata_get_device((int)mnt->device_id);
            mnt->priv_data = (void*)dev;
        }
    }

    const exfat_vbr_t* vbr = exfat_get_vbr();
    uint32_t used_mb = exfat_get_used_mb();

    // 1. Lettura dal chip del disco HD / SSD del valore reale totale
    if (dev && dev->present && dev->size_mb > 0) {
        mnt->total_mb = dev->size_mb;
    } else if (vbr && vbr->volume_length > 0) {
        mnt->total_mb = (uint32_t)((vbr->volume_length * 512ULL) / (1024ULL * 1024ULL));
    } else {
        mnt->total_mb = 128;
    }

    // 2. Lettura e calcolo memoria disponibile reale dal chip
    mnt->free_mb = mnt->total_mb > used_mb ? (mnt->total_mb - used_mb) : 0;
    return true;
}

static bool exfat_vfs_open(vfs_mount_t* mnt, const char* rel_path, vfs_file_t* file) {
    (void)mnt;
    if (!rel_path || !file) return false;

    // Ricerca tramite VFS root cluster
    uint32_t cur_cl = exfat_get_root_cluster();

    char temp_path[VFS_MAX_PATH];
    strncpy(temp_path, rel_path, sizeof(temp_path) - 1);
    temp_path[sizeof(temp_path) - 1] = '\0';
    for (int k = 0; temp_path[k] != '\0'; k++) {
        if (temp_path[k] == '\\') temp_path[k] = '/';
    }

    char* token = temp_path;
    char* next = NULL;
    exfat_item_info_t item;

    while (token && *token) {
        next = strstr(token, "/");
        if (next) {
            *next = '\0';
            next++;
        }

        if (!exfat_find_in_dir(cur_cl, token, &item)) {
            return false;
        }

        if (next && *next) {
            if (!item.is_directory) return false;
            cur_cl = item.first_cluster;
        } else {
            strncpy(file->name, item.name, sizeof(file->name) - 1);
            file->name[sizeof(file->name) - 1] = '\0';
            file->size = item.size;
            file->first_cluster = item.first_cluster;
            file->current_pos = 0;
            file->is_directory = item.is_directory;
            file->no_fat_chain = item.no_fat_chain;
            file->is_open = true;
            return true;
        }
        token = next;
    }

    return false;
}

static int32_t exfat_vfs_read(vfs_mount_t* mnt, vfs_file_t* file, void* buf, uint32_t count) {
    (void)mnt;
    if (!file || !buf || count == 0) return 0;

    exfat_item_info_t info;
    info.size = file->size;
    info.first_cluster = file->first_cluster;
    info.no_fat_chain = file->no_fat_chain;

    int32_t r = exfat_read_file(&info, file->current_pos, buf, count);
    if (r > 0) file->current_pos += r;
    return r;
}

static int32_t exfat_vfs_write(vfs_mount_t* mnt, vfs_file_t* file, const void* buf, uint32_t count) {
    (void)mnt;
    if (!file || !buf || count == 0) return 0;

    exfat_item_info_t info;
    info.size = file->size;
    info.first_cluster = file->first_cluster;
    info.no_fat_chain = file->no_fat_chain;

    int32_t w = exfat_write_file_inplace(&info, file->current_pos, buf, count);
    if (w > 0) file->current_pos += w;
    return w;
}

static void exfat_vfs_close(vfs_mount_t* mnt, vfs_file_t* file) {
    (void)mnt;
    if (file) file->is_open = false;
}

static int32_t exfat_vfs_seek(vfs_mount_t* mnt, vfs_file_t* file, int32_t offset, int whence) {
    (void)mnt;
    if (!file) return -1;

    int32_t new_pos = (int32_t)file->current_pos;
    if (whence == VFS_SEEK_SET) {
        new_pos = offset;
    } else if (whence == VFS_SEEK_CUR) {
        new_pos += offset;
    } else if (whence == VFS_SEEK_END) {
        new_pos = (int32_t)file->size + offset;
    }

    if (new_pos < 0) new_pos = 0;
    if ((uint32_t)new_pos > file->size) new_pos = (int32_t)file->size;

    file->current_pos = (uint32_t)new_pos;
    return new_pos;
}

static int32_t exfat_vfs_readdir(vfs_mount_t* mnt, const char* rel_path, vfs_dirent_t* entries, int32_t max_entries) {
    (void)mnt;
    if (!entries || max_entries <= 0) return 0;

    uint32_t dir_cl = exfat_get_root_cluster();
    if (rel_path && rel_path[0] != '\0') {
        vfs_file_t df;
        if (!exfat_vfs_open(mnt, rel_path, &df) || !df.is_directory) {
            return 0;
        }
        dir_cl = df.first_cluster;
    }

    exfat_item_info_t raw_items[64];
    int32_t count = exfat_list_dir(dir_cl, raw_items, (max_entries < 64) ? max_entries : 64);

    for (int32_t i = 0; i < count; i++) {
        strncpy(entries[i].name, raw_items[i].name, sizeof(entries[i].name) - 1);
        entries[i].name[sizeof(entries[i].name) - 1] = '\0';
        entries[i].size = raw_items[i].size;
        entries[i].attributes = raw_items[i].attributes;
        entries[i].is_directory = raw_items[i].is_directory;
    }
    return count;
}

static bool exfat_vfs_create(vfs_mount_t* mnt, const char* rel_path, uint16_t attributes) {
    (void)mnt;
    if (!rel_path || !*rel_path) return false;

    // Separa cartella da nome file
    const char* last_slash = strrchr(rel_path, '/');
    uint32_t dir_cl = exfat_get_root_cluster();
    const char* fname = rel_path;

    if (last_slash) {
        char dir_part[VFS_MAX_PATH];
        size_t dlen = last_slash - rel_path;
        strncpy(dir_part, rel_path, dlen);
        dir_part[dlen] = '\0';
        vfs_file_t df;
        if (!exfat_vfs_open(mnt, dir_part, &df) || !df.is_directory) {
            return false;
        }
        dir_cl = df.first_cluster;
        fname = last_slash + 1;
    }

    uint32_t cl = exfat_ops_allocate_cluster();
    if (cl == 0) return false;

    return exfat_ops_create_entry(dir_cl, fname, cl, 0, attributes | EXFAT_ATTR_ARCHIVE);
}

static bool exfat_vfs_mkdir(vfs_mount_t* mnt, const char* rel_path) {
    (void)mnt;
    if (!rel_path || !*rel_path) return false;

    const char* last_slash = strrchr(rel_path, '/');
    uint32_t dir_cl = exfat_get_root_cluster();
    const char* dname = rel_path;

    if (last_slash) {
        char dir_part[VFS_MAX_PATH];
        size_t dlen = last_slash - rel_path;
        strncpy(dir_part, rel_path, dlen);
        dir_part[dlen] = '\0';
        vfs_file_t df;
        if (!exfat_vfs_open(mnt, dir_part, &df) || !df.is_directory) {
            return false;
        }
        dir_cl = df.first_cluster;
        dname = last_slash + 1;
    }

    uint32_t cl = exfat_ops_allocate_cluster();
    if (cl == 0) return false;

    return exfat_ops_create_entry(dir_cl, dname, cl, 0, EXFAT_ATTR_DIRECTORY);
}

static bool exfat_vfs_unlink(vfs_mount_t* mnt, const char* rel_path) {
    (void)mnt;
    if (!rel_path || !*rel_path) return false;

    const char* last_slash = strrchr(rel_path, '/');
    uint32_t dir_cl = exfat_get_root_cluster();
    const char* fname = rel_path;

    if (last_slash) {
        char dir_part[VFS_MAX_PATH];
        size_t dlen = last_slash - rel_path;
        strncpy(dir_part, rel_path, dlen);
        dir_part[dlen] = '\0';
        vfs_file_t df;
        if (!exfat_vfs_open(mnt, dir_part, &df)) return false;
        dir_cl = df.first_cluster;
        fname = last_slash + 1;
    }

    return exfat_ops_delete_entry(dir_cl, fname);
}

static bool exfat_vfs_stat(vfs_mount_t* mnt, const char* rel_path, vfs_dirent_t* out_stat) {
    if (!rel_path || !out_stat) return false;
    vfs_file_t f;
    if (!exfat_vfs_open(mnt, rel_path, &f)) return false;

    strncpy(out_stat->name, f.name, sizeof(out_stat->name) - 1);
    out_stat->name[sizeof(out_stat->name) - 1] = '\0';
    out_stat->size = f.size;
    out_stat->is_directory = f.is_directory;
    out_stat->attributes = f.is_directory ? EXFAT_ATTR_DIRECTORY : EXFAT_ATTR_ARCHIVE;
    return true;
}

static vfs_driver_t g_exfat_driver = {
    .name = "exFAT",
    .mount = exfat_vfs_mount,
    .unmount = NULL,
    .open = exfat_vfs_open,
    .read = exfat_vfs_read,
    .write = exfat_vfs_write,
    .close = exfat_vfs_close,
    .seek = exfat_vfs_seek,
    .readdir = exfat_vfs_readdir,
    .create = exfat_vfs_create,
    .mkdir = exfat_vfs_mkdir,
    .unlink = exfat_vfs_unlink,
    .stat = exfat_vfs_stat
};

void exfat_ops_init(void) {
    vfs_register_driver(&g_exfat_driver);
}

const vfs_driver_t* exfat_get_vfs_driver(void) {
    return &g_exfat_driver;
}
