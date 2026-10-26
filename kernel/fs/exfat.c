#include <fs/exfat.h>
#include <fs/vfs_cache.h>
#include <drivers/ata.h>
#include <arch/x86_64/io.h>
#include <mm/heap.h>
#include <string.h>

static bool        g_mounted = false;
static uint64_t    g_partition_lba = 0;
static exfat_vbr_t g_vbr;
static uint32_t    g_sectors_per_cluster = 8;
static uint32_t    g_cluster_size = 4096;
static uint64_t    g_cluster_heap_lba = 0;
static uint64_t    g_fat_lba = 0;
static uint32_t    g_allocated_clusters = 0;
static uint32_t    g_bm_cluster = 0;
static uint64_t    g_bm_length = 0;
static const ata_device_t* g_exfat_dev = NULL;

static bool exfat_read_sec(uint64_t lba, uint32_t count, void* buf) {
    if (g_exfat_dev) return ata_read_device_sectors(g_exfat_dev, lba, count, buf);
    return ata_read_sectors(lba, count, buf);
}

static bool exfat_write_sec(uint64_t lba, uint32_t count, const void* buf) {
    if (g_exfat_dev) return ata_write_device_sectors(g_exfat_dev, lba, count, buf);
    return ata_write_sectors(lba, count, buf);
}

uint64_t exfat_cluster_to_lba(uint32_t cluster) {
    if (cluster < 2) return 0;
    return g_cluster_heap_lba + (uint64_t)(cluster - 2) * (uint64_t)g_sectors_per_cluster;
}

uint32_t exfat_get_next_cluster(uint32_t cluster, bool no_fat_chain) {
    if (no_fat_chain) {
        return cluster + 1;
    }
    // Lettura da tabella FAT su disco (con verifica in cache per coerenza immediata)
    uint32_t fat_sector = g_fat_lba + (cluster * 4) / 512;
    uint32_t fat_offset = (cluster * 4) % 512;

    uint8_t sec_buf[512];
    if (!vfs_cache_read_sector(0, fat_sector, sec_buf)) {
        if (!exfat_read_sec(fat_sector, 1, sec_buf)) {
            return 0xFFFFFFFF;
        }
    }
    uint32_t next = *(uint32_t*)(sec_buf + fat_offset);
    return next;
}

void exfat_set_bitmap_bit(uint32_t cluster, bool allocated) {
    if (!g_mounted || g_bm_cluster < 2 || cluster < 2) return;
    uint32_t bit_idx = cluster - 2;
    uint32_t byte_off = bit_idx / 8;
    if (byte_off >= g_bm_length) return;

    uint32_t cl_idx = byte_off / g_cluster_size;
    uint32_t cl_rem = byte_off % g_cluster_size;
    uint32_t sec_in_cl = cl_rem / 512;
    uint32_t off_in_sec = cl_rem % 512;

    uint32_t cur_bm_cl = g_bm_cluster;
    for (uint32_t i = 0; i < cl_idx; i++) {
        cur_bm_cl = exfat_get_next_cluster(cur_bm_cl, false);
        if (cur_bm_cl < 2 || cur_bm_cl >= 0xFFFFFFF8) return;
    }

    uint32_t sec_lba = exfat_cluster_to_lba(cur_bm_cl) + sec_in_cl;
    uint8_t sec_buf[512];
    if (!vfs_cache_read_sector(0, sec_lba, sec_buf)) {
        if (!exfat_read_sec(sec_lba, 1, sec_buf)) return;
    }

    if (allocated) {
        sec_buf[off_in_sec] |= (uint8_t)(1 << (bit_idx % 8));
    } else {
        sec_buf[off_in_sec] &= (uint8_t)~(1 << (bit_idx % 8));
    }

    vfs_cache_write_sector(0, sec_lba, sec_buf);
}

bool exfat_mount_device(const ata_device_t* dev, uint64_t partition_lba) {
    g_exfat_dev = dev;
    return exfat_mount(partition_lba);
}

bool exfat_mount(uint64_t partition_lba) {
    if (!g_exfat_dev) {
        g_exfat_dev = ata_get_primary_master();
    }
    uint8_t sec_buf[512];
    bool primary_valid = false;

    if (exfat_read_sec(partition_lba, 1, sec_buf)) {
        if (memcmp(sec_buf + 3, "EXFAT   ", 8) == 0 && *(uint16_t*)(sec_buf + 510) == 0xAA55) {
            primary_valid = true;
        } else {
            outb(0x3F8, '['); outb(0x3F8, 'V'); outb(0x3F8, 'B'); outb(0x3F8, 'R'); outb(0x3F8, ':');
            for (int k = 3; k < 11; k++) outb(0x3F8, sec_buf[k] ? sec_buf[k] : '?');
            outb(0x3F8, ']'); outb(0x3F8, '\n');
        }
    } else {
        outb(0x3F8, '['); outb(0x3F8, 'R'); outb(0x3F8, 'D'); outb(0x3F8, 'E'); outb(0x3F8, 'R'); outb(0x3F8, 'R'); outb(0x3F8, ']'); outb(0x3F8, '\n');
    }

    // Se il Main VBR e' corrotto o non leggibile, tenta il ripristino dal Backup VBR (settore +12)
    if (!primary_valid) {
        if (exfat_read_sec(partition_lba + 12, 1, sec_buf)) {
            if (memcmp(sec_buf + 3, "EXFAT   ", 8) == 0 && *(uint16_t*)(sec_buf + 510) == 0xAA55) {
                // Auto-ripristino automatico del Main VBR dal Backup VBR
                for (uint32_t s = 0; s < 12; s++) {
                    uint8_t copy_sec[512];
                    if (exfat_read_sec(partition_lba + 12 + s, 1, copy_sec)) {
                        exfat_write_sec(partition_lba + s, 1, copy_sec);
                    }
                }
                primary_valid = true;
            }
        }
    }

    if (!primary_valid) {
        return false;
    }

    g_partition_lba = partition_lba;
    memcpy(&g_vbr, sec_buf, sizeof(exfat_vbr_t));

    g_sectors_per_cluster = 1 << g_vbr.sectors_per_cluster_shift;
    g_cluster_size = g_sectors_per_cluster * 512;
    g_cluster_heap_lba = partition_lba + g_vbr.cluster_heap_offset;
    g_fat_lba = partition_lba + g_vbr.fat_offset;

    g_mounted = true;

    // Scansione e calcolo reale dei cluster allocati dalla tabella Allocation Bitmap
    g_allocated_clusters = 0;
    uint32_t root_lba = exfat_cluster_to_lba(g_vbr.root_dir_cluster);
    uint8_t root_sec[512];
    uint32_t bm_cluster = 0;
    uint64_t bm_length = 0;

    if (exfat_read_sec(root_lba, 1, root_sec)) {
        for (int i = 0; i < 16; i++) {
            uint8_t* ent = root_sec + i * 32;
            if (ent[0] == EXFAT_ENTRY_BITMAP) {
                bm_cluster = *(uint32_t*)(ent + 20);
                bm_length = *(uint64_t*)(ent + 24);
                break;
            }
        }
    }

    g_bm_cluster = bm_cluster;
    g_bm_length = bm_length;

    if (bm_cluster >= 2 && bm_length > 0) {
        uint32_t count = 0;
        uint32_t rem_bytes = (uint32_t)bm_length;
        uint32_t cur_bm_cl = bm_cluster;
        uint8_t bm_sec[512];

        while (rem_bytes > 0 && cur_bm_cl >= 2 && cur_bm_cl < 0xFFFFFFF8) {
            uint32_t cl_lba = exfat_cluster_to_lba(cur_bm_cl);
            for (uint32_t s = 0; s < g_sectors_per_cluster && rem_bytes > 0; s++) {
                if (!exfat_read_sec(cl_lba + s, 1, bm_sec)) break;
                uint32_t b_in_sec = (rem_bytes > 512) ? 512 : rem_bytes;
                for (uint32_t b = 0; b < b_in_sec; b++) {
                    count += __builtin_popcount(bm_sec[b]);
                }
                rem_bytes -= b_in_sec;
            }
            cur_bm_cl = exfat_get_next_cluster(cur_bm_cl, false);
        }
        g_allocated_clusters = count;
    }

    // Fallback percentuale d'uso se la scansione bitmap non e' disponibile
    if (g_allocated_clusters == 0 && g_vbr.percent_in_use > 0) {
        g_allocated_clusters = (g_vbr.cluster_count * g_vbr.percent_in_use) / 100;
    }

    return true;
}

uint32_t exfat_get_allocated_clusters(void) {
    return g_allocated_clusters;
}

uint32_t exfat_get_used_mb(void) {
    if (!g_mounted) return 0;
    uint64_t used_bytes = (uint64_t)g_allocated_clusters * (uint64_t)g_cluster_size;
    uint32_t mb = (uint32_t)(used_bytes / (1024ULL * 1024ULL));
    if (mb == 0 && g_allocated_clusters > 0) mb = 1;
    return mb;
}

uint32_t exfat_get_free_clusters(void) {
    if (!g_mounted || g_vbr.cluster_count <= g_allocated_clusters) return 0;
    return g_vbr.cluster_count - g_allocated_clusters;
}

void exfat_notify_cluster_allocated(void) {
    g_allocated_clusters++;
}

void exfat_notify_cluster_freed(void) {
    if (g_allocated_clusters > 0) g_allocated_clusters--;
}

bool exfat_is_mounted(void) {
    return g_mounted;
}

const exfat_vbr_t* exfat_get_vbr(void) {
    if (!g_mounted) return NULL;
    return &g_vbr;
}

uint32_t exfat_get_root_cluster(void) {
    if (!g_mounted) return 0;
    return g_vbr.root_dir_cluster;
}

uint32_t exfat_cluster_size(void) {
    return g_cluster_size;
}

int32_t exfat_list_dir(uint32_t dir_cluster, exfat_item_info_t* out_items, int32_t max_items) {
    if (!g_mounted || dir_cluster < 2 || !out_items || max_items <= 0) return 0;

    int32_t count = 0;
    uint32_t cur_cl = dir_cluster;
    uint8_t* cl_buf = (uint8_t*)kmalloc(g_cluster_size);
    if (!cl_buf) return 0;

    while (cur_cl >= 2 && cur_cl < 0xFFFFFFF8 && count < max_items) {
        uint32_t lba = exfat_cluster_to_lba(cur_cl);
        if (!exfat_read_sec(lba, g_sectors_per_cluster, cl_buf)) {
            break;
        }

        uint32_t entries_per_cl = g_cluster_size / 32;
        uint32_t i = 0;

        while (i < entries_per_cl && count < max_items) {
            uint8_t* entry = cl_buf + i * 32;
            uint8_t entry_type = entry[0];

            if (entry_type == EXFAT_ENTRY_EOD) {
                // Fine cartella
                kfree(cl_buf);
                return count;
            }

            // Entry 0x85 (File/Directory)
            if (entry_type == EXFAT_ENTRY_FILE) {
                uint8_t secondary_count = entry[1];
                uint16_t attr = *(uint16_t*)(entry + 4);

                // Deve esserci almeno la Stream Extension entry
                if (secondary_count >= 1 && (i + 1 < entries_per_cl)) {
                    uint8_t* stream = cl_buf + (i + 1) * 32;
                    if (stream[0] == EXFAT_ENTRY_STREAM) {
                        uint8_t stream_flags = stream[1];
                        uint32_t first_cl = *(uint32_t*)(stream + 20);
                        uint64_t file_size = *(uint64_t*)(stream + 24);

                        // Leggi il nome dalle entry 0xC1 successive
                        char name[128];
                        int name_pos = 0;
                        name[0] = '\0';

                        for (uint8_t s = 2; s <= secondary_count && (i + s < entries_per_cl); s++) {
                            uint8_t* name_entry = cl_buf + (i + s) * 32;
                            if (name_entry[0] == EXFAT_ENTRY_NAME) {
                                for (int c = 0; c < 15 && name_pos < 127; c++) {
                                    uint16_t ch = *(uint16_t*)(name_entry + 2 + c * 2);
                                    if (ch == 0) break;
                                    name[name_pos++] = (ch < 128) ? (char)ch : '_';
                                }
                            }
                        }
                        name[name_pos] = '\0';

                        // Salva nel risultato se abbiamo un nome valido
                        if (name_pos > 0) {
                            strncpy(out_items[count].name, name, sizeof(out_items[count].name) - 1);
                            out_items[count].size = (uint32_t)file_size;
                            out_items[count].first_cluster = first_cl;
                            out_items[count].attributes = attr;
                            out_items[count].is_directory = (attr & EXFAT_ATTR_DIRECTORY) != 0;
                            out_items[count].no_fat_chain = (stream_flags & EXFAT_FLAG_NO_FAT_CHAIN) != 0;
                            count++;
                        }
                    }
                }
                i += 1 + secondary_count;
            } else {
                i++;
            }
        }

        // Passa al prossimo cluster della directory (le directory possono usare FAT)
        cur_cl = exfat_get_next_cluster(cur_cl, false);
    }

    kfree(cl_buf);
    return count;
}

bool exfat_find_in_dir(uint32_t dir_cluster, const char* name, exfat_item_info_t* out_info) {
    if (!name || !*name) return false;

    // Allocazione heap dinamica per proteggere lo stack kernel Ring 0
    exfat_item_info_t* items = (exfat_item_info_t*)kmalloc(sizeof(exfat_item_info_t) * 64);
    if (!items) return false;

    int32_t found = exfat_list_dir(dir_cluster, items, 64);
    bool res = false;

    for (int32_t i = 0; i < found; i++) {
        if (strcasecmp(items[i].name, name) == 0) {
            if (out_info) {
                memcpy(out_info, &items[i], sizeof(exfat_item_info_t));
            }
            res = true;
            break;
        }
    }

    kfree(items);
    return res;
}

int32_t exfat_read_file(const exfat_item_info_t* file_info, uint32_t offset, void* buf, uint32_t count) {
    if (!g_mounted || !file_info || !buf || count == 0) return 0;
    if (offset >= file_info->size) return 0;

    if (offset + count > file_info->size) {
        count = file_info->size - offset;
    }

    uint32_t cluster_idx = offset / g_cluster_size;
    uint32_t cluster_offset = offset % g_cluster_size;

    uint32_t cur_cl = file_info->first_cluster;

    // Se no_fat_chain è vero, il cluster è contiguo
    if (file_info->no_fat_chain) {
        cur_cl += cluster_idx;
    } else {
        // Altrimenti scorre la FAT
        for (uint32_t s = 0; s < cluster_idx; s++) {
            cur_cl = exfat_get_next_cluster(cur_cl, false);
            if (cur_cl < 2 || cur_cl >= 0xFFFFFFF8) return 0;
        }
    }

    uint8_t* cl_buf = (uint8_t*)kmalloc(g_cluster_size);
    if (!cl_buf) return 0;

    uint32_t bytes_read = 0;
    uint8_t* dest = (uint8_t*)buf;

    while (bytes_read < count && cur_cl >= 2 && cur_cl < 0xFFFFFFF8) {
        uint32_t lba = exfat_cluster_to_lba(cur_cl);
        if (!exfat_read_sec(lba, g_sectors_per_cluster, cl_buf)) {
            break;
        }

        uint32_t can_copy = g_cluster_size - cluster_offset;
        uint32_t needed = count - bytes_read;
        uint32_t chunk = (needed < can_copy) ? needed : can_copy;

        memcpy(dest + bytes_read, cl_buf + cluster_offset, chunk);
        bytes_read += chunk;

        cluster_offset = 0; // Solo il primo cluster può avere offset parziale
        cur_cl = exfat_get_next_cluster(cur_cl, file_info->no_fat_chain);
    }

    kfree(cl_buf);
    return (int32_t)bytes_read;
}

int32_t exfat_write_file_inplace(const exfat_item_info_t* file_info, uint32_t offset, const void* buf, uint32_t count) {
    if (!g_mounted || !file_info || file_info->first_cluster < 2 || !buf || count == 0) return 0;
    if (offset >= file_info->size) return 0;

    if (offset + count > file_info->size) {
        count = file_info->size - offset;
    }

    uint32_t cluster_idx = offset / g_cluster_size;
    uint32_t cluster_offset = offset % g_cluster_size;

    uint32_t cur_cl = file_info->first_cluster;

    if (file_info->no_fat_chain) {
        cur_cl += cluster_idx;
    } else {
        for (uint32_t s = 0; s < cluster_idx; s++) {
            cur_cl = exfat_get_next_cluster(cur_cl, false);
            if (cur_cl < 2 || cur_cl >= 0xFFFFFFF8) return 0;
        }
    }

    uint8_t* cl_buf = (uint8_t*)kmalloc(g_cluster_size);
    if (!cl_buf) return 0;

    uint32_t bytes_written = 0;
    const uint8_t* src = (const uint8_t*)buf;

    while (bytes_written < count && cur_cl >= 2 && cur_cl < 0xFFFFFFF8) {
        uint32_t lba = exfat_cluster_to_lba(cur_cl);
        // Legge il cluster corrente per preservare i byte non modificati
        if (!exfat_read_sec(lba, g_sectors_per_cluster, cl_buf)) {
            break;
        }

        uint32_t can_copy = g_cluster_size - cluster_offset;
        uint32_t needed = count - bytes_written;
        uint32_t chunk = (needed < can_copy) ? needed : can_copy;

        memcpy(cl_buf + cluster_offset, src + bytes_written, chunk);

        // Scrive il cluster modificato fisicamente sul disco ATA
        if (!exfat_write_sec(lba, g_sectors_per_cluster, cl_buf)) {
            break;
        }

        bytes_written += chunk;
        cluster_offset = 0;
        cur_cl = exfat_get_next_cluster(cur_cl, file_info->no_fat_chain);
    }

    kfree(cl_buf);
    return (int32_t)bytes_written;
}

bool exfat_sync_backup_vbr(void) {
    if (!g_mounted) return false;
    uint8_t sec[512];
    for (uint32_t s = 0; s < 12; s++) {
        if (!exfat_read_sec(g_partition_lba + s, 1, sec)) return false;
        if (!exfat_write_sec(g_partition_lba + 12 + s, 1, sec)) return false;
    }
    return true;
}

bool exfat_check_boot_integrity(exfat_boot_integrity_t* out_status) {
    if (!g_mounted) return false;

    exfat_boot_integrity_t st;
    memset(&st, 0, sizeof(st));

    // 1. Calcola Checksum sui primi 11 settori della Primary Boot Region
    uint32_t primary_csum = 0;
    uint8_t sec[512];
    bool read_all_primary = true;

    for (uint32_t s = 0; s < 11; s++) {
        if (!exfat_read_sec(g_partition_lba + s, 1, sec)) {
            read_all_primary = false;
            break;
        }
        if (s == 0) {
            if (memcmp(sec + 3, "EXFAT   ", 8) == 0 && *(uint16_t*)(sec + 510) == 0xAA55) {
                st.primary_vbr_valid = true;
            }
        }
        for (uint32_t b = 0; b < 512; b++) {
            uint32_t off = s * 512 + b;
            if (off == 106 || off == 107 || off == 112) continue; // Skip VolumeFlags & PercentInUse
            primary_csum = (((primary_csum >> 1) | ((primary_csum & 1) << 31)) + sec[b]);
        }
    }

    st.computed_checksum = primary_csum;

    if (read_all_primary && exfat_read_sec(g_partition_lba + 11, 1, sec)) {
        st.stored_checksum = *(uint32_t*)sec;
        if (st.stored_checksum == primary_csum) {
            st.checksum_matched = true;
        }
    }

    // 2. Ispezione Backup Boot Region a LBA + 12
    if (exfat_read_sec(g_partition_lba + 12, 1, sec)) {
        if (memcmp(sec + 3, "EXFAT   ", 8) == 0 && *(uint16_t*)(sec + 510) == 0xAA55) {
            st.backup_vbr_valid = true;
        }
    }

    // 3. Sincronizzazione automatica se il Backup VBR era assente o corrotto
    if (st.primary_vbr_valid && !st.backup_vbr_valid) {
        exfat_sync_backup_vbr();
        st.backup_vbr_valid = true;
        st.self_healed = true;
    }

    if (out_status) *out_status = st;
    return st.primary_vbr_valid;
}
