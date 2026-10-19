#include <fs/exfat.h>
#include <drivers/ata.h>
#include <mm/heap.h>
#include <string.h>

static bool        g_mounted = false;
static uint32_t    g_partition_lba = 0;
static exfat_vbr_t g_vbr;
static uint32_t    g_sectors_per_cluster = 8;
static uint32_t    g_cluster_size = 4096;
static uint32_t    g_cluster_heap_lba = 0;
static uint32_t    g_fat_lba = 0;

static uint32_t cluster_to_lba(uint32_t cluster) {
    if (cluster < 2) return 0;
    return g_cluster_heap_lba + (cluster - 2) * g_sectors_per_cluster;
}

static uint32_t get_next_cluster(uint32_t cluster, bool no_fat_chain) {
    if (no_fat_chain) {
        return cluster + 1;
    }
    // Lettura da tabella FAT su disco
    uint32_t fat_sector = g_fat_lba + (cluster * 4) / 512;
    uint32_t fat_offset = (cluster * 4) % 512;

    uint8_t sec_buf[512];
    if (!ata_read_sectors(fat_sector, 1, sec_buf)) {
        return 0xFFFFFFFF;
    }
    uint32_t next = *(uint32_t*)(sec_buf + fat_offset);
    return next;
}

bool exfat_mount(uint32_t partition_lba) {
    g_partition_lba = partition_lba;

    uint8_t sec_buf[512];
    if (!ata_read_sectors(partition_lba, 1, sec_buf)) {
        return false;
    }

    memcpy(&g_vbr, sec_buf, sizeof(exfat_vbr_t));

    // Verifica firma "EXFAT   " e 0xAA55
    if (memcmp(g_vbr.fs_name, "EXFAT   ", 8) != 0 || g_vbr.boot_signature != 0xAA55) {
        return false;
    }

    g_sectors_per_cluster = 1 << g_vbr.sectors_per_cluster_shift;
    g_cluster_size = g_sectors_per_cluster * 512;
    g_cluster_heap_lba = partition_lba + g_vbr.cluster_heap_offset;
    g_fat_lba = partition_lba + g_vbr.fat_offset;

    g_mounted = true;
    return true;
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
        uint32_t lba = cluster_to_lba(cur_cl);
        if (!ata_read_sectors(lba, g_sectors_per_cluster, cl_buf)) {
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
        cur_cl = get_next_cluster(cur_cl, false);
    }

    kfree(cl_buf);
    return count;
}

bool exfat_find_in_dir(uint32_t dir_cluster, const char* name, exfat_item_info_t* out_info) {
    if (!name || !*name) return false;

    // Buffer locale per scorrere gli elementi
    exfat_item_info_t items[64];
    int32_t found = exfat_list_dir(dir_cluster, items, 64);

    for (int32_t i = 0; i < found; i++) {
        if (strcasecmp(items[i].name, name) == 0) {
            if (out_info) {
                memcpy(out_info, &items[i], sizeof(exfat_item_info_t));
            }
            return true;
        }
    }
    return false;
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
            cur_cl = get_next_cluster(cur_cl, false);
            if (cur_cl < 2 || cur_cl >= 0xFFFFFFF8) return 0;
        }
    }

    uint8_t* cl_buf = (uint8_t*)kmalloc(g_cluster_size);
    if (!cl_buf) return 0;

    uint32_t bytes_read = 0;
    uint8_t* dest = (uint8_t*)buf;

    while (bytes_read < count && cur_cl >= 2 && cur_cl < 0xFFFFFFF8) {
        uint32_t lba = cluster_to_lba(cur_cl);
        if (!ata_read_sectors(lba, g_sectors_per_cluster, cl_buf)) {
            break;
        }

        uint32_t can_copy = g_cluster_size - cluster_offset;
        uint32_t needed = count - bytes_read;
        uint32_t chunk = (needed < can_copy) ? needed : can_copy;

        memcpy(dest + bytes_read, cl_buf + cluster_offset, chunk);
        bytes_read += chunk;

        cluster_offset = 0; // Solo il primo cluster può avere offset parziale
        cur_cl = get_next_cluster(cur_cl, file_info->no_fat_chain);
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
            cur_cl = get_next_cluster(cur_cl, false);
            if (cur_cl < 2 || cur_cl >= 0xFFFFFFF8) return 0;
        }
    }

    uint8_t* cl_buf = (uint8_t*)kmalloc(g_cluster_size);
    if (!cl_buf) return 0;

    uint32_t bytes_written = 0;
    const uint8_t* src = (const uint8_t*)buf;

    while (bytes_written < count && cur_cl >= 2 && cur_cl < 0xFFFFFFF8) {
        uint32_t lba = cluster_to_lba(cur_cl);
        // Legge il cluster corrente per preservare i byte non modificati
        if (!ata_read_sectors(lba, g_sectors_per_cluster, cl_buf)) {
            break;
        }

        uint32_t can_copy = g_cluster_size - cluster_offset;
        uint32_t needed = count - bytes_written;
        uint32_t chunk = (needed < can_copy) ? needed : can_copy;

        memcpy(cl_buf + cluster_offset, src + bytes_written, chunk);

        // Scrive il cluster modificato fisicamente sul disco ATA
        if (!ata_write_sectors(lba, g_sectors_per_cluster, cl_buf)) {
            break;
        }

        bytes_written += chunk;
        cluster_offset = 0;
        cur_cl = get_next_cluster(cur_cl, file_info->no_fat_chain);
    }

    kfree(cl_buf);
    return (int32_t)bytes_written;
}
