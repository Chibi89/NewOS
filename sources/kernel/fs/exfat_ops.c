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

bool exfat_ops_link_clusters(uint32_t prev_cluster, uint32_t next_cluster) {
    if (prev_cluster < 2) return false;
    const exfat_vbr_t* vbr = exfat_get_vbr();
    if (!vbr) return false;

    uint32_t fat_lba = EXFAT_PARTITION_LBA + vbr->fat_offset;
    uint32_t sec = fat_lba + (prev_cluster * 4) / 512;
    uint32_t off = (prev_cluster * 4) % 512;

    uint8_t sec_buf[512];
    if (!vfs_cache_read_sector(0, sec, sec_buf)) {
        return false;
    }

    *(uint32_t*)(sec_buf + off) = next_cluster;
    return vfs_cache_write_sector(0, sec, sec_buf);
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
            uint32_t cl_lba = exfat_cluster_to_lba(cl);
            uint8_t zero_sec[512];
            memset(zero_sec, 0, 512);
            for (uint32_t s = 0; s < spc; s++) {
                vfs_cache_write_sector(0, cl_lba + s, zero_sec);
            }

            exfat_set_bitmap_bit(cl, true);
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

    uint32_t lba = exfat_cluster_to_lba(dir_cluster);

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
    *(uint64_t*)(stream + 8) = (uint64_t)initial_size;
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

bool exfat_ops_update_file_entry(uint32_t dir_cluster, const char* name, uint32_t new_size, uint32_t first_cluster, bool no_fat_chain) {
    if (!name || !*name || dir_cluster < 2) return false;

    const exfat_vbr_t* vbr = exfat_get_vbr();
    if (!vbr) return false;

    uint32_t spc = 1 << vbr->sectors_per_cluster_shift;
    uint32_t cl_size = spc * 512;
    uint8_t* cl_buf = (uint8_t*)kmalloc(cl_size);
    if (!cl_buf) return false;

    uint32_t cur_dir_cl = dir_cluster;
    bool updated = false;

    while (cur_dir_cl >= 2 && cur_dir_cl < 0xFFFFFFF8 && !updated) {
        uint32_t lba = exfat_cluster_to_lba(cur_dir_cl);

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
                    char cur_name[128];
                    int npos = 0;
                    cur_name[0] = '\0';

                    for (uint8_t s = 2; s <= sec_cnt && (i + s < entries_count); s++) {
                        uint8_t* nentry = cl_buf + (i + s) * 32;
                        if (nentry[0] == EXFAT_ENTRY_NAME) {
                            for (int c = 0; c < 15 && npos < 127; c++) {
                                uint16_t ch = *(uint16_t*)(nentry + 2 + c * 2);
                                if (ch == 0) break;
                                cur_name[npos++] = (ch < 128) ? (char)ch : '_';
                            }
                        }
                    }
                    cur_name[npos] = '\0';

                    if (strcasecmp(cur_name, name) == 0) {
                        uint8_t* stream = cl_buf + (i + 1) * 32;
                        if (stream[0] == EXFAT_ENTRY_STREAM) {
                            stream[1] = EXFAT_FLAG_ALLOC_POSSIBLE | (no_fat_chain ? EXFAT_FLAG_NO_FAT_CHAIN : 0);
                            *(uint64_t*)(stream + 8) = (uint64_t)new_size;  // ValidDataLength
                            if (first_cluster >= 2) {
                                *(uint32_t*)(stream + 20) = first_cluster;  // FirstCluster
                            }
                            *(uint64_t*)(stream + 24) = (uint64_t)new_size; // DataLength

                            // Ricalcola il Checksum del Set di directory entries
                            uint16_t chk = exfat_entry_set_checksum(entry, 1 + sec_cnt);
                            *(uint16_t*)(entry + 2) = chk;

                            // Scrivi i settori aggiornati su disco
                            for (uint32_t s = 0; s < spc; s++) {
                                vfs_cache_write_sector(0, lba + s, cl_buf + s * 512);
                            }
                            updated = true;
                            break;
                        }
                    }
                }
                i += sec_cnt;
            }
        }

        if (!updated) {
            cur_dir_cl = exfat_get_next_cluster(cur_dir_cl, false);
        }
    }

    kfree(cl_buf);
    return updated;
}

bool exfat_ops_update_file_size(uint32_t dir_cluster, const char* name, uint32_t new_size) {
    return exfat_ops_update_file_entry(dir_cluster, name, new_size, 0, false);
}

bool exfat_ops_delete_entry(uint32_t dir_cluster, const char* name) {
    if (!name || !*name || dir_cluster < 2) return false;

    const exfat_vbr_t* vbr = exfat_get_vbr();
    if (!vbr) return false;

    uint32_t spc = 1 << vbr->sectors_per_cluster_shift;
    uint32_t cl_size = spc * 512;
    uint8_t* cl_buf = (uint8_t*)kmalloc(cl_size);
    if (!cl_buf) return false;

    uint32_t cur_dir_cl = dir_cluster;
    bool deleted = false;

    while (cur_dir_cl >= 2 && cur_dir_cl < 0xFFFFFFF8 && !deleted) {
        uint32_t lba = exfat_cluster_to_lba(cur_dir_cl);

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
                    char cur_name[128];
                    int npos = 0;
                    cur_name[0] = '\0';

                    for (uint8_t s = 2; s <= sec_cnt && (i + s < entries_count); s++) {
                        uint8_t* nentry = cl_buf + (i + s) * 32;
                        if (nentry[0] == EXFAT_ENTRY_NAME) {
                            for (int c = 0; c < 15 && npos < 127; c++) {
                                uint16_t ch = *(uint16_t*)(nentry + 2 + c * 2);
                                if (ch == 0) break;
                                cur_name[npos++] = (ch < 128) ? (char)ch : '_';
                            }
                        }
                    }
                    cur_name[npos] = '\0';

                    if (strcasecmp(cur_name, name) == 0) {
                        // Libera la catena di cluster allocati dal file se presenti
                        uint8_t* stream = cl_buf + (i + 1) * 32;
                        if (stream[0] == EXFAT_ENTRY_STREAM) {
                            uint32_t cl = *(uint32_t*)(stream + 20);
                            bool no_chain = (stream[1] & EXFAT_FLAG_NO_FAT_CHAIN) != 0;
                            uint64_t file_sz = *(uint64_t*)(stream + 24);
                            uint32_t cl_cnt = (uint32_t)((file_sz + cl_size - 1) / cl_size);
                            if (cl_cnt == 0 && cl >= 2) cl_cnt = 1;

                            uint32_t fat_lba = EXFAT_PARTITION_LBA + vbr->fat_offset;
                            while (cl >= 2 && cl < 0xFFFFFFF8 && cl_cnt > 0) {
                                uint32_t next = exfat_get_next_cluster(cl, no_chain);
                                uint32_t fat_sec = fat_lba + (cl * 4) / 512;
                                uint32_t fat_off = (cl * 4) % 512;
                                uint8_t fbuf[512];
                                if (vfs_cache_read_sector(0, fat_sec, fbuf)) {
                                    *(uint32_t*)(fbuf + fat_off) = 0x00000000;
                                    vfs_cache_write_sector(0, fat_sec, fbuf);
                                }
                                exfat_set_bitmap_bit(cl, false);
                                exfat_notify_cluster_freed();
                                cl = next;
                                cl_cnt--;
                            }
                        }

                        // Marca come cancellato azzerando il bit 7
                        entry[0] = 0x05; // Deleted file
                        for (uint8_t s = 1; s <= sec_cnt; s++) {
                            cl_buf[(i + s) * 32] &= 0x7F;
                        }

                        for (uint32_t s = 0; s < spc; s++) {
                            vfs_cache_write_sector(0, lba + s, cl_buf + s * 512);
                        }
                        deleted = true;
                        break;
                    }
                }
                i += sec_cnt;
            }
        }

        if (!deleted) {
            cur_dir_cl = exfat_get_next_cluster(cur_dir_cl, false);
        }
    }

    kfree(cl_buf);
    return deleted;
}

bool exfat_ops_rename_entry(uint32_t dir_cluster, const char* old_name, const char* new_name) {
    if (!old_name || !*old_name || !new_name || !*new_name || dir_cluster < 2) return false;

    const exfat_vbr_t* vbr = exfat_get_vbr();
    if (!vbr) return false;

    uint32_t spc = 1 << vbr->sectors_per_cluster_shift;
    uint32_t cl_size = spc * 512;
    uint8_t* cl_buf = (uint8_t*)kmalloc(cl_size);
    if (!cl_buf) return false;

    uint32_t cur_dir_cl = dir_cluster;
    bool renamed = false;

    while (cur_dir_cl >= 2 && cur_dir_cl < 0xFFFFFFF8 && !renamed) {
        uint32_t lba = exfat_cluster_to_lba(cur_dir_cl);

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
                    char cur_name[128];
                    int npos = 0;
                    cur_name[0] = '\0';

                    for (uint8_t s = 2; s <= sec_cnt && (i + s < entries_count); s++) {
                        uint8_t* nentry = cl_buf + (i + s) * 32;
                        if (nentry[0] == EXFAT_ENTRY_NAME) {
                            for (int c = 0; c < 15 && npos < 127; c++) {
                                uint16_t ch = *(uint16_t*)(nentry + 2 + c * 2);
                                if (ch == 0) break;
                                cur_name[npos++] = (ch < 128) ? (char)ch : '_';
                            }
                        }
                    }
                    cur_name[npos] = '\0';

                    if (strcasecmp(cur_name, old_name) == 0) {
                        uint8_t* stream = cl_buf + (i + 1) * 32;
                        if (stream[0] == EXFAT_ENTRY_STREAM) {
                            uint8_t nlen = (uint8_t)strlen(new_name);
                            if (nlen > 15) nlen = 15;
                            stream[3] = nlen;
                            *(uint16_t*)(stream + 4) = exfat_calc_name_hash(new_name);

                            uint8_t* name_ent = cl_buf + (i + 2) * 32;
                            if (name_ent[0] == EXFAT_ENTRY_NAME) {
                                memset(name_ent + 2, 0, 30);
                                for (int c = 0; c < nlen; c++) {
                                    *(uint16_t*)(name_ent + 2 + c * 2) = (uint8_t)new_name[c];
                                }
                            }

                            uint16_t chk = exfat_entry_set_checksum(entry, 1 + sec_cnt);
                            *(uint16_t*)(entry + 2) = chk;

                            for (uint32_t s = 0; s < spc; s++) {
                                vfs_cache_write_sector(0, lba + s, cl_buf + s * 512);
                            }
                            renamed = true;
                            break;
                        }
                    }
                }
                i += sec_cnt;
            }
        }

        if (!renamed) {
            cur_dir_cl = exfat_get_next_cluster(cur_dir_cl, false);
        }
    }

    kfree(cl_buf);
    return renamed;
}

// -------------------------------------------------------------
// Driver VFS Dinamico per exFAT
// -------------------------------------------------------------

static bool exfat_vfs_mount(vfs_mount_t* mnt) {
    if (!mnt) return false;

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

    if (!exfat_mount_device(dev, mnt->partition_lba)) {
        return false;
    }
    if (!exfat_is_mounted()) {
        return false;
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
    if (!rel_path || !file) return false;

    if (mnt && mnt->priv_data) {
        exfat_set_device((const ata_device_t*)mnt->priv_data, mnt->partition_lba);
    }

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
            file->dir_cluster = cur_cl;
            file->is_open = true;
            return true;
        }
        token = next;
    }

    return false;
}

static int32_t exfat_vfs_read(vfs_mount_t* mnt, vfs_file_t* file, void* buf, uint32_t count) {
    if (!file || !buf || count == 0) return 0;

    if (mnt && mnt->priv_data) {
        exfat_set_device((const ata_device_t*)mnt->priv_data, mnt->partition_lba);
    }

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
    if (file->is_directory) return -1;

    const exfat_vbr_t* vbr = exfat_get_vbr();
    if (!vbr) return 0;

    uint32_t cluster_size = exfat_cluster_size();
    if (cluster_size == 0) return 0;

    uint32_t spc = 1 << vbr->sectors_per_cluster_shift;
    uint32_t target_end = file->current_pos + count;

    // Se la scrittura supera la dimensione attuale o il file non ha ancora cluster allocati
    if (target_end > file->size || file->first_cluster < 2) {
        uint32_t needed_clusters = (target_end + cluster_size - 1) / cluster_size;
        if (needed_clusters == 0) needed_clusters = 1;

        uint32_t current_clusters = 0;
        if (file->first_cluster >= 2) {
            current_clusters = (file->size + cluster_size - 1) / cluster_size;
            if (current_clusters == 0) current_clusters = 1;
        }

        // Se non abbiamo ancora un primo cluster valido, allochiamolo
        if (file->first_cluster < 2) {
            uint32_t first_cl = exfat_ops_allocate_cluster();
            if (first_cl == 0) return 0; // Disco pieno
            file->first_cluster = first_cl;
            file->no_fat_chain = true;
            current_clusters = 1;
        }

        // Se servono più cluster di quelli attuali, allochiamo ed estendiamo la catena FAT
        if (needed_clusters > current_clusters) {
            // Se il file era senza catena FAT contigua, converti i cluster esistenti in catena FAT
            if (file->no_fat_chain && current_clusters > 1) {
                for (uint32_t i = 0; i < current_clusters - 1; i++) {
                    exfat_ops_link_clusters(file->first_cluster + i, file->first_cluster + i + 1);
                }
            }

            // Trova l'ultimo cluster attuale
            uint32_t last_cl = file->first_cluster;
            if (file->no_fat_chain) {
                last_cl = file->first_cluster + current_clusters - 1;
            } else {
                for (uint32_t i = 1; i < current_clusters; i++) {
                    uint32_t nxt = exfat_get_next_cluster(last_cl, false);
                    if (nxt < 2 || nxt >= 0xFFFFFFF8) break;
                    last_cl = nxt;
                }
            }

            // Alloca e collega i nuovi cluster uno a uno
            for (uint32_t i = current_clusters; i < needed_clusters; i++) {
                uint32_t new_cl = exfat_ops_allocate_cluster();
                if (new_cl == 0) {
                    // Disco esaurito! Continua con quanto possibile
                    break;
                }
                exfat_ops_link_clusters(last_cl, new_cl);
                last_cl = new_cl;
            }

            // Da ora in avanti il file ha una catena FAT esplicita
            file->no_fat_chain = false;
        }
    }

    // Effettua la scrittura fisica dei dati nei cluster del file
    uint32_t cluster_idx = file->current_pos / cluster_size;
    uint32_t cluster_offset = file->current_pos % cluster_size;

    uint32_t cur_cl = file->first_cluster;
    if (file->no_fat_chain) {
        cur_cl += cluster_idx;
    } else {
        for (uint32_t s = 0; s < cluster_idx; s++) {
            cur_cl = exfat_get_next_cluster(cur_cl, false);
            if (cur_cl < 2 || cur_cl >= 0xFFFFFFF8) return 0;
        }
    }

    uint8_t* cl_buf = (uint8_t*)kmalloc(cluster_size);
    if (!cl_buf) return 0;

    uint32_t bytes_written = 0;
    const uint8_t* src = (const uint8_t*)buf;

    while (bytes_written < count && cur_cl >= 2 && cur_cl < 0xFFFFFFF8) {
        uint32_t lba = exfat_cluster_to_lba(cur_cl);

        uint32_t can_copy = cluster_size - cluster_offset;
        uint32_t needed = count - bytes_written;
        uint32_t chunk = (needed < can_copy) ? needed : can_copy;

        // Se scriviamo solo parzialmente il cluster, leggiamo prima il contenuto esistente per non sovrascrivere byte adiacenti
        if (cluster_offset > 0 || chunk < cluster_size) {
            for (uint32_t s = 0; s < spc; s++) {
                vfs_cache_read_sector(0, lba + s, cl_buf + s * 512);
            }
        }

        memcpy(cl_buf + cluster_offset, src + bytes_written, chunk);

        for (uint32_t s = 0; s < spc; s++) {
            vfs_cache_write_sector(0, lba + s, cl_buf + s * 512);
        }

        bytes_written += chunk;
        cluster_offset = 0;
        cur_cl = file->no_fat_chain ? (cur_cl + 1) : exfat_get_next_cluster(cur_cl, false);
    }

    kfree(cl_buf);

    if (bytes_written > 0) {
        file->current_pos += bytes_written;
        if (file->current_pos > file->size) {
            file->size = file->current_pos;
        }

        // Aggiorna l'entry nel catalogo cartella
        if (file->dir_cluster >= 2) {
            exfat_ops_update_file_entry(file->dir_cluster, file->name, file->size, file->first_cluster, file->no_fat_chain);
        }

        vfs_cache_flush();
    }

    return (int32_t)bytes_written;
}

static void exfat_vfs_close(vfs_mount_t* mnt, vfs_file_t* file) {
    (void)mnt;
    if (file) {
        if (file->is_open && file->dir_cluster >= 2 && !file->is_directory) {
            exfat_ops_update_file_entry(file->dir_cluster, file->name, file->size, file->first_cluster, file->no_fat_chain);
        }
        file->is_open = false;
        vfs_cache_flush();
    }
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

    int32_t limit = (max_entries > 256) ? 256 : max_entries;
    exfat_item_info_t* raw_items = (exfat_item_info_t*)kmalloc(sizeof(exfat_item_info_t) * limit);
    if (!raw_items) return 0;

    int32_t count = exfat_list_dir(dir_cl, raw_items, limit);

    for (int32_t i = 0; i < count; i++) {
        strncpy(entries[i].name, raw_items[i].name, sizeof(entries[i].name) - 1);
        entries[i].name[sizeof(entries[i].name) - 1] = '\0';
        entries[i].size = raw_items[i].size;
        entries[i].attributes = raw_items[i].attributes;
        entries[i].is_directory = raw_items[i].is_directory;
    }
    kfree(raw_items);
    return count;
}

static bool exfat_vfs_create(vfs_mount_t* mnt, const char* rel_path, uint16_t attributes) {
    (void)mnt;
    if (!rel_path || !*rel_path) return false;

    char norm[VFS_MAX_PATH];
    strncpy(norm, rel_path, sizeof(norm) - 1);
    norm[sizeof(norm) - 1] = '\0';
    for (int k = 0; norm[k] != '\0'; k++) {
        if (norm[k] == '\\') norm[k] = '/';
    }

    // Separa cartella da nome file
    const char* last_slash = strrchr(norm, '/');
    uint32_t dir_cl = exfat_get_root_cluster();
    const char* fname = norm;

    if (last_slash) {
        char dir_part[VFS_MAX_PATH];
        size_t dlen = last_slash - norm;
        strncpy(dir_part, norm, dlen);
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

    char norm[VFS_MAX_PATH];
    strncpy(norm, rel_path, sizeof(norm) - 1);
    norm[sizeof(norm) - 1] = '\0';
    for (int k = 0; norm[k] != '\0'; k++) {
        if (norm[k] == '\\') norm[k] = '/';
    }

    const char* last_slash = strrchr(norm, '/');
    uint32_t dir_cl = exfat_get_root_cluster();
    const char* dname = norm;

    if (last_slash) {
        char dir_part[VFS_MAX_PATH];
        size_t dlen = last_slash - norm;
        strncpy(dir_part, norm, dlen);
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

    char norm[VFS_MAX_PATH];
    strncpy(norm, rel_path, sizeof(norm) - 1);
    norm[sizeof(norm) - 1] = '\0';
    for (int k = 0; norm[k] != '\0'; k++) {
        if (norm[k] == '\\') norm[k] = '/';
    }

    const char* last_slash = strrchr(norm, '/');
    uint32_t dir_cl = exfat_get_root_cluster();
    const char* fname = norm;

    if (last_slash) {
        char dir_part[VFS_MAX_PATH];
        size_t dlen = last_slash - norm;
        strncpy(dir_part, norm, dlen);
        dir_part[dlen] = '\0';
        vfs_file_t df;
        if (!exfat_vfs_open(mnt, dir_part, &df)) return false;
        dir_cl = df.first_cluster;
        fname = last_slash + 1;
    }

    return exfat_ops_delete_entry(dir_cl, fname);
}

static bool exfat_vfs_rename(vfs_mount_t* mnt, const char* old_path, const char* new_path) {
    if (!old_path || !*old_path || !new_path || !*new_path) return false;

    char old_norm[VFS_MAX_PATH];
    strncpy(old_norm, old_path, sizeof(old_norm) - 1);
    old_norm[sizeof(old_norm) - 1] = '\0';
    for (int k = 0; old_norm[k] != '\0'; k++) {
        if (old_norm[k] == '\\') old_norm[k] = '/';
    }

    char new_norm[VFS_MAX_PATH];
    strncpy(new_norm, new_path, sizeof(new_norm) - 1);
    new_norm[sizeof(new_norm) - 1] = '\0';
    for (int k = 0; new_norm[k] != '\0'; k++) {
        if (new_norm[k] == '\\') new_norm[k] = '/';
    }

    const char* old_slash = strrchr(old_norm, '/');
    uint32_t dir_cl = exfat_get_root_cluster();
    const char* old_fname = old_norm;

    if (old_slash) {
        char dir_part[VFS_MAX_PATH];
        size_t dlen = old_slash - old_norm;
        strncpy(dir_part, old_norm, dlen);
        dir_part[dlen] = '\0';
        vfs_file_t df;
        if (!exfat_vfs_open(mnt, dir_part, &df)) return false;
        dir_cl = df.first_cluster;
        old_fname = old_slash + 1;
    }

    const char* new_slash = strrchr(new_norm, '/');
    const char* new_fname = new_slash ? (new_slash + 1) : new_norm;

    return exfat_ops_rename_entry(dir_cl, old_fname, new_fname);
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
    .rename = exfat_vfs_rename,
    .stat = exfat_vfs_stat
};

void exfat_ops_init(void) {
    vfs_register_driver(&g_exfat_driver);
}

const vfs_driver_t* exfat_get_vfs_driver(void) {
    return &g_exfat_driver;
}
