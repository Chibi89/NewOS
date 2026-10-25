#include <fs/vfs_cache.h>
#include <drivers/ata.h>
#include <string.h>

static vfs_sector_cache_entry_t g_sector_cache[VFS_CACHE_SECTORS_MAX];
static uint32_t                 g_sector_clock = 1;

static vfs_dentry_cache_entry_t g_dentry_cache[VFS_DENTRY_CACHE_MAX];
static uint32_t                 g_dentry_clock = 1;

void vfs_cache_init(void) {
    memset(g_sector_cache, 0, sizeof(g_sector_cache));
    g_sector_clock = 1;

    memset(g_dentry_cache, 0, sizeof(g_dentry_cache));
    g_dentry_clock = 1;
}

bool vfs_cache_read_sector(uint32_t dev_id, uint64_t lba, void* buffer) {
    if (!buffer) return false;

    // 1. Cerca nella cache
    for (int i = 0; i < VFS_CACHE_SECTORS_MAX; i++) {
        if (g_sector_cache[i].valid &&
            g_sector_cache[i].dev_id == dev_id &&
            g_sector_cache[i].lba == lba) {
            g_sector_cache[i].age = ++g_sector_clock;
            memcpy(buffer, g_sector_cache[i].data, 512);
            return true;
        }
    }

    // 2. Cache miss: leggi fisicamente tramite driver ATA
    const ata_device_t* dev = (dev_id == 0) ? ata_get_primary_master() : ata_get_device((int)dev_id);
    if (!dev) return false;

    uint8_t temp[512];
    if (!ata_read_device_sectors(dev, lba, 1, temp)) {
        return false;
    }

    memcpy(buffer, temp, 512);

    // 3. Trova slot LRU per inserimento
    int lru_idx = 0;
    uint32_t oldest_age = 0xFFFFFFFF;

    for (int i = 0; i < VFS_CACHE_SECTORS_MAX; i++) {
        if (!g_sector_cache[i].valid) {
            lru_idx = i;
            break;
        }
        if (g_sector_cache[i].age < oldest_age) {
            oldest_age = g_sector_cache[i].age;
            lru_idx = i;
        }
    }

    // Se il blocco sostituito era sporco, andrebbe fatto il flush (in questo caso facciamo write-through)
    g_sector_cache[lru_idx].dev_id = dev_id;
    g_sector_cache[lru_idx].lba = lba;
    g_sector_cache[lru_idx].valid = true;
    g_sector_cache[lru_idx].dirty = false;
    g_sector_cache[lru_idx].age = ++g_sector_clock;
    memcpy(g_sector_cache[lru_idx].data, temp, 512);

    return true;
}

bool vfs_cache_write_sector(uint32_t dev_id, uint64_t lba, const void* buffer) {
    if (!buffer) return false;

    // Write-through: aggiorna fisicamente sul disco ATA
    const ata_device_t* dev = (dev_id == 0) ? ata_get_primary_master() : ata_get_device((int)dev_id);
    if (!dev) return false;

    if (!ata_write_sectors(lba, 1, buffer)) {
        return false;
    }

    // Aggiorna o inserisci in cache
    int target_idx = -1;
    uint32_t oldest_age = 0xFFFFFFFF;

    for (int i = 0; i < VFS_CACHE_SECTORS_MAX; i++) {
        if (g_sector_cache[i].valid && g_sector_cache[i].dev_id == dev_id && g_sector_cache[i].lba == lba) {
            target_idx = i;
            break;
        }
        if (!g_sector_cache[i].valid) {
            target_idx = i;
            break;
        }
        if (g_sector_cache[i].age < oldest_age) {
            oldest_age = g_sector_cache[i].age;
            target_idx = i;
        }
    }

    if (target_idx >= 0) {
        g_sector_cache[target_idx].dev_id = dev_id;
        g_sector_cache[target_idx].lba = lba;
        g_sector_cache[target_idx].valid = true;
        g_sector_cache[target_idx].dirty = false;
        g_sector_cache[target_idx].age = ++g_sector_clock;
        memcpy(g_sector_cache[target_idx].data, buffer, 512);
    }

    return true;
}

void vfs_cache_flush(void) {
    for (int i = 0; i < VFS_CACHE_SECTORS_MAX; i++) {
        if (g_sector_cache[i].valid && g_sector_cache[i].dirty) {
            ata_write_sectors(g_sector_cache[i].lba, 1, g_sector_cache[i].data);
            g_sector_cache[i].dirty = false;
        }
    }
}

bool vfs_dentry_lookup(char drive_letter, const char* clean_path, uint32_t* out_cluster, uint32_t* out_size, uint16_t* out_attr, bool* out_is_dir) {
    if (!clean_path) return false;

    for (int i = 0; i < VFS_DENTRY_CACHE_MAX; i++) {
        if (g_dentry_cache[i].valid &&
            g_dentry_cache[i].drive_letter == drive_letter &&
            strcasecmp(g_dentry_cache[i].path, clean_path) == 0) {
            g_dentry_cache[i].age = ++g_dentry_clock;
            if (out_cluster) *out_cluster = g_dentry_cache[i].cluster;
            if (out_size) *out_size = g_dentry_cache[i].size;
            if (out_attr) *out_attr = g_dentry_cache[i].attributes;
            if (out_is_dir) *out_is_dir = g_dentry_cache[i].is_dir;
            return true;
        }
    }
    return false;
}

void vfs_dentry_insert(char drive_letter, const char* clean_path, uint32_t cluster, uint32_t size, uint16_t attr, bool is_dir) {
    if (!clean_path) return;

    int target_idx = -1;
    uint32_t oldest_age = 0xFFFFFFFF;

    for (int i = 0; i < VFS_DENTRY_CACHE_MAX; i++) {
        if (g_dentry_cache[i].valid &&
            g_dentry_cache[i].drive_letter == drive_letter &&
            strcasecmp(g_dentry_cache[i].path, clean_path) == 0) {
            target_idx = i;
            break;
        }
        if (!g_dentry_cache[i].valid) {
            target_idx = i;
            break;
        }
        if (g_dentry_cache[i].age < oldest_age) {
            oldest_age = g_dentry_cache[i].age;
            target_idx = i;
        }
    }

    if (target_idx >= 0) {
        g_dentry_cache[target_idx].drive_letter = drive_letter;
        strncpy(g_dentry_cache[target_idx].path, clean_path, sizeof(g_dentry_cache[target_idx].path) - 1);
        g_dentry_cache[target_idx].path[sizeof(g_dentry_cache[target_idx].path) - 1] = '\0';
        g_dentry_cache[target_idx].cluster = cluster;
        g_dentry_cache[target_idx].size = size;
        g_dentry_cache[target_idx].attributes = attr;
        g_dentry_cache[target_idx].is_dir = is_dir;
        g_dentry_cache[target_idx].valid = true;
        g_dentry_cache[target_idx].age = ++g_dentry_clock;
    }
}

void vfs_dentry_invalidate(char drive_letter, const char* clean_path) {
    for (int i = 0; i < VFS_DENTRY_CACHE_MAX; i++) {
        if (g_dentry_cache[i].valid && g_dentry_cache[i].drive_letter == drive_letter) {
            if (!clean_path || strcasecmp(g_dentry_cache[i].path, clean_path) == 0) {
                g_dentry_cache[i].valid = false;
            }
        }
    }
}
