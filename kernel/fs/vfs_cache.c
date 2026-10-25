#include <fs/vfs_cache.h>
#include <drivers/ata.h>
#include <string.h>

static vfs_sector_cache_entry_t g_sector_cache[VFS_CACHE_SECTORS_MAX];
static uint32_t                 g_sector_clock = 1;
static bool                     g_write_back_mode = true;

static vfs_dentry_cache_entry_t g_dentry_cache[VFS_DENTRY_CACHE_MAX];
static uint32_t                 g_dentry_clock = 1;

void vfs_cache_init(void) {
    memset(g_sector_cache, 0, sizeof(g_sector_cache));
    g_sector_clock = 1;
    g_write_back_mode = true;

    memset(g_dentry_cache, 0, sizeof(g_dentry_cache));
    g_dentry_clock = 1;
}

void vfs_cache_set_write_back(bool enable) {
    if (!enable && g_write_back_mode) {
        vfs_cache_flush();
    }
    g_write_back_mode = enable;
}

bool vfs_cache_is_write_back(void) {
    return g_write_back_mode;
}

static uint64_t g_last_read_lba = 0;
static uint32_t g_sequential_reads = 0;

static int vfs_cache_find_slot_lru(void) {
    int lru_idx = 0;
    uint32_t oldest_age = 0xFFFFFFFF;
    for (int i = 0; i < VFS_CACHE_SECTORS_MAX; i++) {
        if (!g_sector_cache[i].valid) return i;
        if (g_sector_cache[i].age < oldest_age) {
            oldest_age = g_sector_cache[i].age;
            lru_idx = i;
        }
    }
    return lru_idx;
}

static void vfs_cache_insert_entry(uint32_t dev_id, uint64_t lba, const void* data) {
    for (int i = 0; i < VFS_CACHE_SECTORS_MAX; i++) {
        if (g_sector_cache[i].valid && g_sector_cache[i].dev_id == dev_id && g_sector_cache[i].lba == lba) {
            g_sector_cache[i].age = ++g_sector_clock;
            memcpy(g_sector_cache[i].data, data, 512);
            return;
        }
    }
    int slot = vfs_cache_find_slot_lru();

    // Se lo slot da sovrascrivere contiene dati dirty, scarichiamolo fisicamente
    if (g_sector_cache[slot].valid && g_sector_cache[slot].dirty) {
        const ata_device_t* dev = (g_sector_cache[slot].dev_id == 0) ? ata_get_primary_master() : ata_get_device((int)g_sector_cache[slot].dev_id);
        if (dev) {
            ata_write_sectors(g_sector_cache[slot].lba, 1, g_sector_cache[slot].data);
        }
        g_sector_cache[slot].dirty = false;
    }

    g_sector_cache[slot].dev_id = dev_id;
    g_sector_cache[slot].lba = lba;
    g_sector_cache[slot].valid = true;
    g_sector_cache[slot].dirty = false;
    g_sector_cache[slot].age = ++g_sector_clock;
    memcpy(g_sector_cache[slot].data, data, 512);
}

void vfs_cache_prefetch(uint32_t dev_id, uint64_t lba, uint32_t count) {
    if (count == 0 || count > VFS_PREFETCH_BURST) count = VFS_PREFETCH_BURST;
    const ata_device_t* dev = (dev_id == 0) ? ata_get_primary_master() : ata_get_device((int)dev_id);
    if (!dev) return;

    static uint8_t burst_buf[VFS_PREFETCH_BURST * 512] __attribute__((aligned(16)));
    if (ata_read_device_sectors(dev, lba, count, burst_buf)) {
        for (uint32_t s = 0; s < count; s++) {
            vfs_cache_insert_entry(dev_id, lba + s, burst_buf + s * 512);
        }
    }
}

bool vfs_cache_read_sector(uint32_t dev_id, uint64_t lba, void* buffer) {
    if (!buffer) return false;

    // 1. Cerca nella cache LRU
    for (int i = 0; i < VFS_CACHE_SECTORS_MAX; i++) {
        if (g_sector_cache[i].valid &&
            g_sector_cache[i].dev_id == dev_id &&
            g_sector_cache[i].lba == lba) {
            g_sector_cache[i].age = ++g_sector_clock;
            memcpy(buffer, g_sector_cache[i].data, 512);

            // Rilevamento stream sequenziale su cache hit
            if (lba == g_last_read_lba + 1) {
                g_sequential_reads++;
                if (g_sequential_reads >= 2) {
                    vfs_cache_prefetch(dev_id, lba + 1, VFS_PREFETCH_BURST);
                    g_sequential_reads = 0;
                }
            } else {
                g_sequential_reads = 0;
            }
            g_last_read_lba = lba;
            return true;
        }
    }

    // 2. Cache miss: leggi fisicamente tramite driver ATA/AHCI
    const ata_device_t* dev = (dev_id == 0) ? ata_get_primary_master() : ata_get_device((int)dev_id);
    if (!dev) return false;

    uint8_t temp[512];
    if (!ata_read_device_sectors(dev, lba, 1, temp)) {
        return false;
    }

    memcpy(buffer, temp, 512);
    vfs_cache_insert_entry(dev_id, lba, temp);

    // Rilevamento stream sequenziale su cache miss: avvia prefetch burst
    if (lba == g_last_read_lba + 1) {
        g_sequential_reads++;
        if (g_sequential_reads >= 2) {
            vfs_cache_prefetch(dev_id, lba + 1, VFS_PREFETCH_BURST);
            g_sequential_reads = 0;
        }
    } else {
        g_sequential_reads = 0;
    }
    g_last_read_lba = lba;

    return true;
}

bool vfs_cache_write_sector(uint32_t dev_id, uint64_t lba, const void* buffer) {
    if (!buffer) return false;

    // 1. Cerca se il settore e' gia' presente in cache
    for (int i = 0; i < VFS_CACHE_SECTORS_MAX; i++) {
        if (g_sector_cache[i].valid && g_sector_cache[i].dev_id == dev_id && g_sector_cache[i].lba == lba) {
            memcpy(g_sector_cache[i].data, buffer, 512);
            g_sector_cache[i].age = ++g_sector_clock;

            if (g_write_back_mode) {
                // Modalita' Write-Back differita: marca come dirty ed esci all'istante
                g_sector_cache[i].dirty = true;
                return true;
            } else {
                // Modalita' Write-Through sincrona
                const ata_device_t* dev = (dev_id == 0) ? ata_get_primary_master() : ata_get_device((int)dev_id);
                if (dev && !ata_write_sectors(lba, 1, buffer)) return false;
                g_sector_cache[i].dirty = false;
                return true;
            }
        }
    }

    // 2. Settore non presente: alloca slot LRU
    int slot = vfs_cache_find_slot_lru();
    if (g_sector_cache[slot].valid && g_sector_cache[slot].dirty) {
        const ata_device_t* dev = (g_sector_cache[slot].dev_id == 0) ? ata_get_primary_master() : ata_get_device((int)g_sector_cache[slot].dev_id);
        if (dev) {
            ata_write_sectors(g_sector_cache[slot].lba, 1, g_sector_cache[slot].data);
        }
        g_sector_cache[slot].dirty = false;
    }

    g_sector_cache[slot].dev_id = dev_id;
    g_sector_cache[slot].lba = lba;
    g_sector_cache[slot].valid = true;
    g_sector_cache[slot].age = ++g_sector_clock;
    memcpy(g_sector_cache[slot].data, buffer, 512);

    if (g_write_back_mode) {
        g_sector_cache[slot].dirty = true;
        return true;
    } else {
        const ata_device_t* dev = (dev_id == 0) ? ata_get_primary_master() : ata_get_device((int)dev_id);
        if (dev && !ata_write_sectors(lba, 1, buffer)) return false;
        g_sector_cache[slot].dirty = false;
        return true;
    }
}

void vfs_cache_flush(void) {
    for (int i = 0; i < VFS_CACHE_SECTORS_MAX; i++) {
        if (g_sector_cache[i].valid && g_sector_cache[i].dirty) {
            const ata_device_t* dev = (g_sector_cache[i].dev_id == 0) ? ata_get_primary_master() : ata_get_device((int)g_sector_cache[i].dev_id);
            if (dev) {
                ata_write_sectors(g_sector_cache[i].lba, 1, g_sector_cache[i].data);
            }
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
