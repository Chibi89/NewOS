#include <fs/iso9660.h>
#include <drivers/ata.h>
#include <mm/heap.h>
#include <string.h>

typedef struct {
    const ata_device_t* dev;
    uint32_t            root_lba;
    uint32_t            root_size;
    bool                is_joliet;
} iso_volume_info_t;

static iso_volume_info_t g_iso_vol;

static const ata_device_t* find_atapi_cdrom(void) {
    int dev_count = ata_get_device_count();
    for (int i = 0; i < dev_count; i++) {
        const ata_device_t* dev = ata_get_device(i);
        if (dev && dev->is_atapi && dev->present) {
            return dev;
        }
    }
    return NULL;
}

static void decode_iso_name(const uint8_t* raw_name, uint8_t len, bool is_joliet, char* out_name, size_t max_out) {
    if (!raw_name || len == 0 || !out_name || max_out < 2) {
        if (out_name && max_out > 0) out_name[0] = '\0';
        return;
    }

    if (len == 1 && raw_name[0] == 0) {
        strcpy(out_name, ".");
        return;
    }
    if (len == 1 && raw_name[0] == 1) {
        strcpy(out_name, "..");
        return;
    }

    size_t out_pos = 0;

    if (is_joliet) {
        // Joliet utilizza caratteri a 16 bit Big-Endian (UCS-2BE)
        for (uint8_t i = 0; i + 1 < len && out_pos < max_out - 1; i += 2) {
            uint16_t ch = ((uint16_t)raw_name[i] << 8) | raw_name[i + 1];
            if (ch == ';') break; // Rimuove numero di versione ";1"
            out_name[out_pos++] = (ch < 128 && ch >= 32) ? (char)ch : '_';
        }
    } else {
        // Standard ISO 9660 ASCII
        for (uint8_t i = 0; i < len && out_pos < max_out - 1; i++) {
            char ch = (char)raw_name[i];
            if (ch == ';') break; // Rimuove ";1"
            out_name[out_pos++] = ch;
        }
    }

    // Rimuove eventuale punto finale solitario (es. "LEGGIMI." -> "LEGGIMI")
    if (out_pos > 0 && out_name[out_pos - 1] == '.') {
        out_pos--;
    }

    out_name[out_pos] = '\0';
}

static bool iso9660_vfs_mount(vfs_mount_t* mnt) {
    if (!mnt) return false;

    const ata_device_t* dev = find_atapi_cdrom();
    if (!dev) {
        mnt->read_only = true;
        mnt->total_mb = 650;
        mnt->free_mb = 0;
        mnt->priv_data = &g_iso_vol;
        return true;
    }

    memset(&g_iso_vol, 0, sizeof(g_iso_vol));
    g_iso_vol.dev = dev;

    uint8_t* sector_buf = (uint8_t*)kmalloc(ISO9660_SECTOR_SIZE);
    if (!sector_buf) return false;

    // 1. Legge il Primary Volume Descriptor (LBA 16)
    if (!atapi_read_cd_sector(dev, ISO9660_PVD_LBA, sector_buf)) {
        mnt->read_only = true;
        mnt->total_mb = dev->size_mb ? dev->size_mb : 650;
        mnt->free_mb = 0;
        mnt->priv_data = &g_iso_vol;
        kfree(sector_buf);
        return true;
    }

    iso9660_pvd_t* pvd = (iso9660_pvd_t*)sector_buf;
    if (memcmp(pvd->id, "CD001", 5) != 0 || pvd->type != 1) {
        mnt->read_only = true;
        mnt->total_mb = dev->size_mb ? dev->size_mb : 650;
        mnt->free_mb = 0;
        mnt->priv_data = &g_iso_vol;
        kfree(sector_buf);
        return true;
    }

    // Estrai Root Directory Record dal PVD
    iso9660_dirent_raw_t* root_dr = (iso9660_dirent_raw_t*)pvd->root_directory_record;
    g_iso_vol.root_lba = root_dr->extent_lba_lsb;
    g_iso_vol.root_size = root_dr->data_length_lsb;
    g_iso_vol.is_joliet = false;

    // 2. Verifica se presente Supplementary Volume Descriptor (Joliet UCS-2) al settore 17 o 18
    for (uint32_t s = 17; s < 24; s++) {
        if (!atapi_read_cd_sector(dev, s, sector_buf)) break;
        if (sector_buf[0] == 255) break; // Terminator
        if (sector_buf[0] == 2 && memcmp(&sector_buf[1], "CD001", 5) == 0) {
            // Trovato Joliet SVD!
            iso9660_pvd_t* svd = (iso9660_pvd_t*)sector_buf;
            iso9660_dirent_raw_t* jroot = (iso9660_dirent_raw_t*)svd->root_directory_record;
            g_iso_vol.root_lba = jroot->extent_lba_lsb;
            g_iso_vol.root_size = jroot->data_length_lsb;
            g_iso_vol.is_joliet = true;
            break;
        }
    }

    mnt->read_only = true;
    mnt->total_mb = dev->size_mb ? dev->size_mb : 650;
    mnt->free_mb = 0;
    mnt->priv_data = &g_iso_vol;

    kfree(sector_buf);
    return true;
}

static int32_t iso9660_list_dir_lba(const ata_device_t* dev, uint32_t dir_lba, uint32_t dir_size, bool is_joliet, vfs_dirent_t* entries, int32_t max_entries) {
    if (!dev || dir_lba == 0 || !entries || max_entries <= 0) return 0;

    uint8_t* sec_buf = (uint8_t*)kmalloc(ISO9660_SECTOR_SIZE);
    if (!sec_buf) return 0;

    int32_t count = 0;
    uint32_t bytes_read = 0;
    uint32_t cur_lba = dir_lba;

    while (bytes_read < dir_size && count < max_entries) {
        if (!atapi_read_cd_sector(dev, cur_lba, sec_buf)) {
            break;
        }

        uint32_t sec_offset = 0;
        while (sec_offset < ISO9660_SECTOR_SIZE && bytes_read < dir_size && count < max_entries) {
            iso9660_dirent_raw_t* dr = (iso9660_dirent_raw_t*)(sec_buf + sec_offset);
            if (dr->length == 0) {
                // Nessun altro record nel settore corrente, passa al settore successivo
                break;
            }

            char name[VFS_MAX_FILENAME];
            decode_iso_name((const uint8_t*)dr->name, dr->name_length, is_joliet, name, sizeof(name));

            // Salta "." e ".."
            if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0 && name[0] != '\0') {
                strncpy(entries[count].name, name, sizeof(entries[count].name) - 1);
                entries[count].name[sizeof(entries[count].name) - 1] = '\0';
                entries[count].size = dr->data_length_lsb;
                entries[count].is_directory = (dr->flags & 0x02) != 0;
                entries[count].attributes = entries[count].is_directory ? VFS_ATTR_DIRECTORY : (VFS_ATTR_READONLY | VFS_ATTR_ARCHIVE);
                count++;
            }

            sec_offset += dr->length;
            bytes_read += dr->length;
        }

        cur_lba++;
        bytes_read = (cur_lba - dir_lba) * ISO9660_SECTOR_SIZE;
    }

    kfree(sec_buf);
    return count;
}

static bool iso9660_find_item(const ata_device_t* dev, uint32_t dir_lba, uint32_t dir_size, bool is_joliet, const char* target_name, uint32_t* out_lba, uint32_t* out_size, bool* out_is_dir) {
    if (!dev || !target_name || dir_lba == 0) return false;

    uint8_t* sec_buf = (uint8_t*)kmalloc(ISO9660_SECTOR_SIZE);
    if (!sec_buf) return false;

    uint32_t bytes_read = 0;
    uint32_t cur_lba = dir_lba;

    while (bytes_read < dir_size) {
        if (!atapi_read_cd_sector(dev, cur_lba, sec_buf)) {
            break;
        }

        uint32_t sec_offset = 0;
        while (sec_offset < ISO9660_SECTOR_SIZE && bytes_read < dir_size) {
            iso9660_dirent_raw_t* dr = (iso9660_dirent_raw_t*)(sec_buf + sec_offset);
            if (dr->length == 0) break;

            char name[VFS_MAX_FILENAME];
            decode_iso_name((const uint8_t*)dr->name, dr->name_length, is_joliet, name, sizeof(name));

            if (strcasecmp(name, target_name) == 0) {
                if (out_lba) *out_lba = dr->extent_lba_lsb;
                if (out_size) *out_size = dr->data_length_lsb;
                if (out_is_dir) *out_is_dir = (dr->flags & 0x02) != 0;
                kfree(sec_buf);
                return true;
            }

            sec_offset += dr->length;
            bytes_read += dr->length;
        }

        cur_lba++;
        bytes_read = (cur_lba - dir_lba) * ISO9660_SECTOR_SIZE;
    }

    kfree(sec_buf);
    return false;
}

static bool iso9660_vfs_open(vfs_mount_t* mnt, const char* rel_path, vfs_file_t* file) {
    (void)mnt;
    if (!file) return false;
    if (!g_iso_vol.dev) return false;

    if (!rel_path || rel_path[0] == '\0') {
        // Apertura cartella radice
        strcpy(file->name, "/");
        file->size = g_iso_vol.root_size;
        file->first_cluster = g_iso_vol.root_lba;
        file->current_pos = 0;
        file->is_directory = true;
        file->is_open = true;
        return true;
    }

    char temp_path[VFS_MAX_PATH];
    strncpy(temp_path, rel_path, sizeof(temp_path) - 1);
    temp_path[sizeof(temp_path) - 1] = '\0';

    uint32_t cur_dir_lba = g_iso_vol.root_lba;
    uint32_t cur_dir_size = g_iso_vol.root_size;

    char* token = temp_path;
    char* next = NULL;

    while (token && *token) {
        next = strstr(token, "/");
        if (next) {
            *next = '\0';
            next++;
        }

        uint32_t item_lba = 0;
        uint32_t item_size = 0;
        bool is_dir = false;

        if (!iso9660_find_item(g_iso_vol.dev, cur_dir_lba, cur_dir_size, g_iso_vol.is_joliet, token, &item_lba, &item_size, &is_dir)) {
            return false;
        }

        if (next && *next) {
            if (!is_dir) return false;
            cur_dir_lba = item_lba;
            cur_dir_size = item_size;
        } else {
            strncpy(file->name, token, sizeof(file->name) - 1);
            file->name[sizeof(file->name) - 1] = '\0';
            file->size = item_size;
            file->first_cluster = item_lba; // Usiamo first_cluster per memorizzare LBA CD
            file->current_pos = 0;
            file->is_directory = is_dir;
            file->no_fat_chain = true; // Nei CD i settori sono contigui
            file->is_open = true;
            return true;
        }

        token = next;
    }

    return false;
}

static int32_t iso9660_vfs_read(vfs_mount_t* mnt, vfs_file_t* file, void* buf, uint32_t count) {
    (void)mnt;
    if (!file || !buf || count == 0 || !g_iso_vol.dev) return 0;
    if (file->current_pos >= file->size) return 0;

    if (file->current_pos + count > file->size) {
        count = file->size - file->current_pos;
    }

    uint8_t* sec_buf = (uint8_t*)kmalloc(ISO9660_SECTOR_SIZE);
    if (!sec_buf) return 0;

    uint32_t bytes_read = 0;
    uint8_t* dst = (uint8_t*)buf;

    while (bytes_read < count) {
        uint32_t cur_file_offset = file->current_pos + bytes_read;
        uint32_t sec_idx = cur_file_offset / ISO9660_SECTOR_SIZE;
        uint32_t sec_offset = cur_file_offset % ISO9660_SECTOR_SIZE;

        uint32_t cd_lba = file->first_cluster + sec_idx;
        if (!atapi_read_cd_sector(g_iso_vol.dev, cd_lba, sec_buf)) {
            break;
        }

        uint32_t available = ISO9660_SECTOR_SIZE - sec_offset;
        uint32_t needed = count - bytes_read;
        uint32_t chunk = (needed < available) ? needed : available;

        memcpy(dst + bytes_read, sec_buf + sec_offset, chunk);
        bytes_read += chunk;
    }

    kfree(sec_buf);
    file->current_pos += bytes_read;
    return (int32_t)bytes_read;
}

static void iso9660_vfs_close(vfs_mount_t* mnt, vfs_file_t* file) {
    (void)mnt;
    if (file) file->is_open = false;
}

static int32_t iso9660_vfs_seek(vfs_mount_t* mnt, vfs_file_t* file, int32_t offset, int whence) {
    (void)mnt;
    if (!file) return -1;

    int32_t new_pos = (int32_t)file->current_pos;
    if (whence == VFS_SEEK_SET) new_pos = offset;
    else if (whence == VFS_SEEK_CUR) new_pos += offset;
    else if (whence == VFS_SEEK_END) new_pos = (int32_t)file->size + offset;

    if (new_pos < 0) new_pos = 0;
    if ((uint32_t)new_pos > file->size) new_pos = (int32_t)file->size;

    file->current_pos = (uint32_t)new_pos;
    return new_pos;
}

static int32_t iso9660_vfs_readdir(vfs_mount_t* mnt, const char* rel_path, vfs_dirent_t* entries, int32_t max_entries) {
    (void)mnt;
    if (!g_iso_vol.dev || !entries || max_entries <= 0) return 0;

    uint32_t dir_lba = g_iso_vol.root_lba;
    uint32_t dir_size = g_iso_vol.root_size;

    if (rel_path && rel_path[0] != '\0') {
        vfs_file_t df;
        if (!iso9660_vfs_open(mnt, rel_path, &df) || !df.is_directory) {
            return 0;
        }
        dir_lba = df.first_cluster;
        dir_size = df.size;
    }

    return iso9660_list_dir_lba(g_iso_vol.dev, dir_lba, dir_size, g_iso_vol.is_joliet, entries, max_entries);
}

static bool iso9660_vfs_stat(vfs_mount_t* mnt, const char* rel_path, vfs_dirent_t* out_stat) {
    if (!rel_path || !out_stat) return false;
    vfs_file_t f;
    if (!iso9660_vfs_open(mnt, rel_path, &f)) return false;

    strncpy(out_stat->name, f.name, sizeof(out_stat->name) - 1);
    out_stat->name[sizeof(out_stat->name) - 1] = '\0';
    out_stat->size = f.size;
    out_stat->is_directory = f.is_directory;
    out_stat->attributes = f.is_directory ? VFS_ATTR_DIRECTORY : (VFS_ATTR_READONLY | VFS_ATTR_ARCHIVE);
    return true;
}

static vfs_driver_t g_iso9660_driver = {
    .name = "ISO9660",
    .mount = iso9660_vfs_mount,
    .unmount = NULL,
    .open = iso9660_vfs_open,
    .read = iso9660_vfs_read,
    .write = NULL, // CD-ROM è sola lettura
    .close = iso9660_vfs_close,
    .seek = iso9660_vfs_seek,
    .readdir = iso9660_vfs_readdir,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .stat = iso9660_vfs_stat
};

void iso9660_init(void) {
    vfs_register_driver(&g_iso9660_driver);
}

const vfs_driver_t* iso9660_get_vfs_driver(void) {
    return &g_iso9660_driver;
}
