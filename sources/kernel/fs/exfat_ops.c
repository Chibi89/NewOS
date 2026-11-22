#include <arch/x86_64/io.h>
#include <drivers/ata.h>
#include <drivers/rtc.h>
#include <fs/exfat.h>
#include <fs/exfat_ops.h>
#include <fs/vfs_cache.h>
#include <mm/heap.h>
#include <string.h>

static uint32_t exfat_now_timestamp(void) {
  rtc_time_t t;
  rtc_get_time(&t);
  uint32_t yr = (t.year >= 1980U) ? (t.year - 1980U) : 0U;
  return ((yr & 0x7FU) << 25) | (((uint32_t)t.month & 0x0FU) << 21) |
         (((uint32_t)t.day & 0x1FU) << 16) |
         (((uint32_t)t.hour & 0x1FU) << 11) |
         (((uint32_t)t.minute & 0x3FU) << 5) |
         (((uint32_t)t.second >> 1) & 0x1FU);
}

static uint16_t exfat_calc_name_hash_u16(const uint16_t *u16, int len) {
  uint16_t hash = 0;
  for (int i = 0; i < len; i++) {
    uint16_t uc = exfat_upcase_char(u16[i]);
    uint8_t b0 = (uint8_t)(uc & 0xFF);
    uint8_t b1 = (uint8_t)(uc >> 8);
    hash = (((hash >> 1) | ((hash & 1) << 15)) + b0) & 0xFFFF;
    hash = (((hash >> 1) | ((hash & 1) << 15)) + b1) & 0xFFFF;
  }
  return hash;
}

static uint16_t exfat_calc_name_hash(const char *name) {
  uint16_t u16[255];
  int len = exfat_utf8_to_utf16(name, u16, 255);
  return exfat_calc_name_hash_u16(u16, len);
}

static uint16_t exfat_entry_set_checksum(const uint8_t *entries,
                                         int num_entries) {
  uint16_t checksum = 0;
  for (int e = 0; e < num_entries; e++) {
    for (int b = 0; b < 32; b++) {
      if (e == 0 && (b == 2 || b == 3))
        continue;
      uint8_t byte = entries[e * 32 + b];
      checksum = (((checksum >> 1) | ((checksum & 1) << 15)) + byte) & 0xFFFF;
    }
  }
  return checksum;
}

bool exfat_ops_link_clusters(uint32_t prev_cluster, uint32_t next_cluster) {
  if (prev_cluster < 2)
    return false;
  const exfat_vbr_t *vbr = exfat_get_vbr();
  if (!vbr)
    return false;

  uint64_t part_lba =
      vbr->partition_offset ? vbr->partition_offset : EXFAT_PARTITION_LBA;
  uint64_t fat_lba = part_lba + (uint64_t)vbr->fat_offset;
  uint64_t sec = fat_lba + ((uint64_t)prev_cluster * 4ULL) / 512ULL;
  uint32_t off = (uint32_t)(((uint64_t)prev_cluster * 4ULL) % 512ULL);

  uint8_t sec_buf[512];
  uint32_t dev_id = exfat_get_dev_id();
  if (!vfs_cache_read_sector(dev_id, sec, sec_buf)) {
    return false;
  }

  *(uint32_t *)(sec_buf + off) = next_cluster;
  return vfs_cache_write_sector(dev_id, sec, sec_buf);
}

uint32_t exfat_ops_allocate_cluster(void) {
  const exfat_vbr_t *vbr = exfat_get_vbr();
  if (!vbr)
    return 0;

  uint32_t cl = exfat_find_free_cluster();
  if (cl < 2)
    return 0;

  uint32_t dev_id = exfat_get_dev_id();
  uint64_t part_lba =
      vbr->partition_offset ? vbr->partition_offset : EXFAT_PARTITION_LBA;
  uint64_t fat_lba = part_lba + (uint64_t)vbr->fat_offset;
  uint64_t sec = fat_lba + ((uint64_t)cl * 4ULL) / 512ULL;
  uint32_t off = (uint32_t)(((uint64_t)cl * 4ULL) % 512ULL);
  uint8_t sec_buf[512];
  if (vfs_cache_read_sector(dev_id, sec, sec_buf)) {
    *(uint32_t *)(sec_buf + off) = 0xFFFFFFFF;
    vfs_cache_write_sector(dev_id, sec, sec_buf);
  }

  // Inizializza a zero il cluster allocato
  uint32_t spc = 1 << vbr->sectors_per_cluster_shift;
  uint64_t cl_lba = exfat_cluster_to_lba(cl);
  uint8_t zero_sec[512];
  memset(zero_sec, 0, 512);
  for (uint32_t s = 0; s < spc; s++) {
    vfs_cache_write_sector(dev_id, cl_lba + s, zero_sec);
  }

  exfat_set_bitmap_bit(cl, true);
  exfat_notify_cluster_allocated();
  return cl;
}

bool exfat_ops_create_entry(uint32_t dir_cluster, const char *name,
                            uint32_t first_cluster, uint64_t initial_size,
                            uint16_t attributes) {
  if (!name || !*name || dir_cluster < 2)
    return false;

  const exfat_vbr_t *vbr = exfat_get_vbr();
  if (!vbr)
    return false;

  uint16_t u16_name[255];
  int u16_len = exfat_utf8_to_utf16(name, u16_name, 255);
  if (u16_len <= 0)
    return false;
  uint32_t name_entries = (uint32_t)((u16_len + 14) / 15);
  if (name_entries == 0)
    name_entries = 1;
  uint32_t total_entries = 2 + name_entries; // 0x85 + 0xC0 + N*0xC1 (max 19)

  uint32_t spc = 1 << vbr->sectors_per_cluster_shift;
  uint32_t cl_size = spc * 512;
  uint32_t entries_count = cl_size / 32;
  if (total_entries > entries_count)
    return false;

  uint8_t *cl_buf = (uint8_t *)kmalloc(cl_size);
  if (!cl_buf)
    return false;

  exfat_set_volume_dirty(true);

  uint32_t dev_id = exfat_get_dev_id();
  uint32_t cur_dir_cl = dir_cluster;
  uint32_t last_dir_cl = dir_cluster;
  uint64_t target_lba = 0;
  int free_slot = -1;

  // Cerca slot consecutivi lungo tutta la catena cluster della directory
  while (cur_dir_cl >= 2 && cur_dir_cl < 0xFFFFFFF8) {
    uint64_t lba = exfat_cluster_to_lba(cur_dir_cl);
    for (uint32_t s = 0; s < spc; s++) {
      vfs_cache_read_sector(dev_id, lba + s, cl_buf + s * 512);
    }

    for (uint32_t i = 0; i <= entries_count - total_entries; i++) {
      bool ok = true;
      for (uint32_t k = 0; k < total_entries; k++) {
        uint8_t type = cl_buf[(i + k) * 32];
        if (type != EXFAT_ENTRY_EOD && (type & 0x80) != 0) {
          ok = false;
          break;
        }
      }
      if (ok) {
        free_slot = (int)i;
        target_lba = lba;
        break;
      }
    }
    if (free_slot >= 0)
      break;

    last_dir_cl = cur_dir_cl;
    cur_dir_cl = exfat_get_next_cluster(cur_dir_cl, false);
  }

  // Se tutti i cluster della directory sono pieni, espandi la directory
  // allocandone uno nuovo
  if (free_slot < 0) {
    uint32_t new_dir_cl = exfat_ops_allocate_cluster();
    if (new_dir_cl < 2) {
      kfree(cl_buf);
      return false;
    }
    if (!exfat_ops_link_clusters(last_dir_cl, new_dir_cl)) {
      kfree(cl_buf);
      return false;
    }
    memset(cl_buf, 0, cl_size);
    target_lba = exfat_cluster_to_lba(new_dir_cl);
    free_slot = 0;
  }

  // Costruisci il Set di Directory Entries (max 19 * 32 = 608 byte)
  uint8_t entry_set[19 * 32];
  memset(entry_set, 0, total_entries * 32);

  // 1. Entry 0x85 (File/Directory) con timestamp reali RTC
  uint32_t now_ts = exfat_now_timestamp();
  entry_set[0] = EXFAT_ENTRY_FILE; // 0x85
  entry_set[1] =
      (uint8_t)(1 + name_entries); // secondary count: 1 Stream + N Name
  *(uint16_t *)(entry_set + 4) = attributes;
  *(uint32_t *)(entry_set + 8) = now_ts;
  *(uint32_t *)(entry_set + 12) = now_ts;
  *(uint32_t *)(entry_set + 16) = now_ts;

  // 2. Entry 0xC0 (Stream Extension)
  uint8_t *stream = entry_set + 32;
  stream[0] = EXFAT_ENTRY_STREAM; // 0xC0
  stream[1] = EXFAT_FLAG_ALLOC_POSSIBLE | EXFAT_FLAG_NO_FAT_CHAIN;
  stream[3] = (uint8_t)u16_len;
  *(uint16_t *)(stream + 4) = exfat_calc_name_hash_u16(u16_name, u16_len);
  *(uint64_t *)(stream + 8) = (uint64_t)initial_size;
  *(uint32_t *)(stream + 20) = first_cluster;
  *(uint64_t *)(stream + 24) = (uint64_t)initial_size;

  // 3. Entry 0xC1 (File Name, fino a 255 caratteri UTF-16LE)
  for (uint32_t ne = 0; ne < name_entries; ne++) {
    uint8_t *nentry = entry_set + (2 + ne) * 32;
    nentry[0] = EXFAT_ENTRY_NAME; // 0xC1
    for (uint32_t c = 0; c < 15; c++) {
      int idx = (int)ne * 15 + (int)c;
      if (idx >= u16_len)
        break;
      *(uint16_t *)(nentry + 2 + c * 2) = u16_name[idx];
    }
  }

  // Calcola e assegna Checksum del Set su entry 0x85 offset 2..3
  uint16_t chk = exfat_entry_set_checksum(entry_set, (uint8_t)total_entries);
  *(uint16_t *)(entry_set + 2) = chk;

  // Copia il set nel buffer del cluster della directory
  memcpy(cl_buf + (uint32_t)free_slot * 32, entry_set, total_entries * 32);

  // Se il record non è alla fine del cluster, assicurati che il successivo sia
  // EOD se era vuoto
  if ((uint32_t)free_slot + total_entries < entries_count) {
    if (cl_buf[((uint32_t)free_slot + total_entries) * 32] == 0) {
      cl_buf[((uint32_t)free_slot + total_entries) * 32] = EXFAT_ENTRY_EOD;
    }
  }

  // Scrivi i settori aggiornati
  for (uint32_t s = 0; s < spc; s++) {
    vfs_cache_write_sector(dev_id, target_lba + s, cl_buf + s * 512);
  }
  vfs_cache_flush();
  exfat_set_volume_dirty(false);

  kfree(cl_buf);
  return true;
}

bool exfat_ops_update_file_entry(uint32_t dir_cluster, const char *name,
                                 uint64_t new_size, uint32_t first_cluster,
                                 bool no_fat_chain) {
  if (!name || !*name || dir_cluster < 2)
    return false;

  const exfat_vbr_t *vbr = exfat_get_vbr();
  if (!vbr)
    return false;

  uint32_t spc = 1 << vbr->sectors_per_cluster_shift;
  uint32_t cl_size = spc * 512;
  uint8_t *cl_buf = (uint8_t *)kmalloc(cl_size);
  if (!cl_buf)
    return false;

  uint32_t cur_dir_cl = dir_cluster;
  bool updated = false;

  while (cur_dir_cl >= 2 && cur_dir_cl < 0xFFFFFFF8 && !updated) {
    uint64_t lba = exfat_cluster_to_lba(cur_dir_cl);

    for (uint32_t s = 0; s < spc; s++) {
      vfs_cache_read_sector(0, lba + s, cl_buf + s * 512);
    }

    uint32_t entries_count = cl_size / 32;

    for (uint32_t i = 0; i < entries_count; i++) {
      uint8_t *entry = cl_buf + i * 32;
      if (entry[0] == EXFAT_ENTRY_EOD)
        break;

      if (entry[0] == EXFAT_ENTRY_FILE) {
        uint8_t sec_cnt = entry[1];
        if (sec_cnt >= 2 && i + sec_cnt < entries_count) {
          char cur_name[128];
          int npos = 0;
          cur_name[0] = '\0';

          for (uint8_t s = 2; s <= sec_cnt && (i + s < entries_count); s++) {
            uint8_t *nentry = cl_buf + (i + s) * 32;
            if (nentry[0] == EXFAT_ENTRY_NAME) {
              for (int c = 0; c < 15 && npos < 127; c++) {
                uint16_t ch = *(uint16_t *)(nentry + 2 + c * 2);
                if (ch == 0)
                  break;
                cur_name[npos++] = (ch < 128) ? (char)ch : '_';
              }
            }
          }
          cur_name[npos] = '\0';

          if (strcasecmp(cur_name, name) == 0) {
            uint8_t *stream = cl_buf + (i + 1) * 32;
            if (stream[0] == EXFAT_ENTRY_STREAM) {
              stream[1] = EXFAT_FLAG_ALLOC_POSSIBLE |
                          (no_fat_chain ? EXFAT_FLAG_NO_FAT_CHAIN : 0);
              *(uint64_t *)(stream + 8) = (uint64_t)new_size; // ValidDataLength
              if (first_cluster >= 2) {
                *(uint32_t *)(stream + 20) = first_cluster; // FirstCluster
              }
              *(uint64_t *)(stream + 24) = (uint64_t)new_size; // DataLength

              // Ricalcola il Checksum del Set di directory entries
              uint16_t chk = exfat_entry_set_checksum(entry, 1 + sec_cnt);
              *(uint16_t *)(entry + 2) = chk;

              // Scrivi i settori aggiornati su disco
              for (uint32_t s = 0; s < spc; s++) {
                vfs_cache_write_sector(0, lba + s, cl_buf + s * 512);
              }
              vfs_cache_flush();
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

bool exfat_ops_update_file_size(uint32_t dir_cluster, const char *name,
                                uint64_t new_size) {
  return exfat_ops_update_file_entry(dir_cluster, name, new_size, 0, false);
}

bool exfat_ops_delete_entry(uint32_t dir_cluster, const char *name) {
  if (!name || !*name || dir_cluster < 2)
    return false;

  const exfat_vbr_t *vbr = exfat_get_vbr();
  if (!vbr)
    return false;

  uint32_t spc = 1 << vbr->sectors_per_cluster_shift;
  uint32_t cl_size = spc * 512;
  uint8_t *cl_buf = (uint8_t *)kmalloc(cl_size);
  if (!cl_buf)
    return false;

  uint32_t cur_dir_cl = dir_cluster;
  bool deleted = false;

  while (cur_dir_cl >= 2 && cur_dir_cl < 0xFFFFFFF8 && !deleted) {
    uint64_t lba = exfat_cluster_to_lba(cur_dir_cl);

    for (uint32_t s = 0; s < spc; s++) {
      vfs_cache_read_sector(0, lba + s, cl_buf + s * 512);
    }

    uint32_t entries_count = cl_size / 32;

    for (uint32_t i = 0; i < entries_count; i++) {
      uint8_t *entry = cl_buf + i * 32;
      if (entry[0] == EXFAT_ENTRY_EOD)
        break;

      if (entry[0] == EXFAT_ENTRY_FILE) {
        uint8_t sec_cnt = entry[1];
        if (sec_cnt >= 2 && i + sec_cnt < entries_count) {
          char cur_name[128];
          int npos = 0;
          cur_name[0] = '\0';

          for (uint8_t s = 2; s <= sec_cnt && (i + s < entries_count); s++) {
            uint8_t *nentry = cl_buf + (i + s) * 32;
            if (nentry[0] == EXFAT_ENTRY_NAME) {
              for (int c = 0; c < 15 && npos < 127; c++) {
                uint16_t ch = *(uint16_t *)(nentry + 2 + c * 2);
                if (ch == 0)
                  break;
                cur_name[npos++] = (ch < 128) ? (char)ch : '_';
              }
            }
          }
          cur_name[npos] = '\0';

          if (strcasecmp(cur_name, name) == 0) {
            exfat_set_volume_dirty(true);
            uint32_t dev_id = exfat_get_dev_id();
            const ata_device_t *hw_dev = exfat_get_device();
            // Libera la catena di cluster allocati e invia TRIM/Deallocate SSD/NVMe
            uint8_t *stream = cl_buf + (i + 1) * 32;
            if (stream[0] == EXFAT_ENTRY_STREAM) {
              uint32_t cl = *(uint32_t *)(stream + 20);
              bool no_chain = (stream[1] & EXFAT_FLAG_NO_FAT_CHAIN) != 0;
              uint64_t file_sz = *(uint64_t *)(stream + 24);
              uint32_t cl_cnt = (uint32_t)((file_sz + cl_size - 1) / cl_size);
              if (cl_cnt == 0 && cl >= 2)
                cl_cnt = 1;

              uint64_t part_lba = vbr->partition_offset ? vbr->partition_offset
                                                        : EXFAT_PARTITION_LBA;
              uint64_t fat_lba = part_lba + (uint64_t)vbr->fat_offset;
              uint32_t run_start = 0, run_len = 0;

              while (cl >= 2 && cl < 0xFFFFFFF8 && cl_cnt > 0) {
                uint32_t next = exfat_get_next_cluster(cl, no_chain);
                uint64_t fat_sec = fat_lba + ((uint64_t)cl * 4ULL) / 512ULL;
                uint32_t fat_off = (uint32_t)(((uint64_t)cl * 4ULL) % 512ULL);
                uint8_t fbuf[512];
                if (vfs_cache_read_sector(dev_id, fat_sec, fbuf)) {
                  *(uint32_t *)(fbuf + fat_off) = 0x00000000;
                  vfs_cache_write_sector(dev_id, fat_sec, fbuf);
                }
                exfat_set_bitmap_bit(cl, false);
                exfat_notify_cluster_freed();

                if (run_len == 0) {
                  run_start = cl;
                  run_len = 1;
                } else if (cl == run_start + run_len) {
                  run_len++;
                } else {
                  if (hw_dev)
                    ata_discard_device_sectors(hw_dev, exfat_cluster_to_lba(run_start), run_len * spc);
                  run_start = cl;
                  run_len = 1;
                }
                cl = next;
                cl_cnt--;
              }
              if (run_len > 0 && hw_dev) {
                ata_discard_device_sectors(hw_dev, exfat_cluster_to_lba(run_start), run_len * spc);
              }
            }

            // Marca come cancellato azzerando il bit 7
            entry[0] = 0x05; // Deleted file
            for (uint8_t s = 1; s <= sec_cnt; s++) {
              cl_buf[(i + s) * 32] &= 0x7F;
            }

            for (uint32_t s = 0; s < spc; s++) {
              vfs_cache_write_sector(dev_id, lba + s, cl_buf + s * 512);
            }
            vfs_cache_flush();
            exfat_set_volume_dirty(false);
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

bool exfat_ops_rename_entry(uint32_t dir_cluster, const char *old_name,
                            const char *new_name) {
  if (!old_name || !*old_name || !new_name || !*new_name || dir_cluster < 2)
    return false;

  const exfat_vbr_t *vbr = exfat_get_vbr();
  if (!vbr)
    return false;

  uint32_t spc = 1 << vbr->sectors_per_cluster_shift;
  uint32_t cl_size = spc * 512;
  uint8_t *cl_buf = (uint8_t *)kmalloc(cl_size);
  if (!cl_buf)
    return false;

  uint32_t cur_dir_cl = dir_cluster;
  bool renamed = false;

  while (cur_dir_cl >= 2 && cur_dir_cl < 0xFFFFFFF8 && !renamed) {
    uint32_t lba = exfat_cluster_to_lba(cur_dir_cl);

    for (uint32_t s = 0; s < spc; s++) {
      vfs_cache_read_sector(0, lba + s, cl_buf + s * 512);
    }

    uint32_t entries_count = cl_size / 32;

    for (uint32_t i = 0; i < entries_count; i++) {
      uint8_t *entry = cl_buf + i * 32;
      if (entry[0] == EXFAT_ENTRY_EOD)
        break;

      if (entry[0] == EXFAT_ENTRY_FILE) {
        uint8_t sec_cnt = entry[1];
        if (sec_cnt >= 2 && i + sec_cnt < entries_count) {
          char cur_name[128];
          int npos = 0;
          cur_name[0] = '\0';

          for (uint8_t s = 2; s <= sec_cnt && (i + s < entries_count); s++) {
            uint8_t *nentry = cl_buf + (i + s) * 32;
            if (nentry[0] == EXFAT_ENTRY_NAME) {
              for (int c = 0; c < 15 && npos < 127; c++) {
                uint16_t ch = *(uint16_t *)(nentry + 2 + c * 2);
                if (ch == 0)
                  break;
                cur_name[npos++] = (ch < 128) ? (char)ch : '_';
              }
            }
          }
          cur_name[npos] = '\0';

          if (strcasecmp(cur_name, old_name) == 0) {
            uint8_t *stream = cl_buf + (i + 1) * 32;
            if (stream[0] == EXFAT_ENTRY_STREAM) {
              uint8_t nlen = (uint8_t)strlen(new_name);
              if (nlen > 15)
                nlen = 15;
              stream[3] = nlen;
              *(uint16_t *)(stream + 4) = exfat_calc_name_hash(new_name);

              uint8_t *name_ent = cl_buf + (i + 2) * 32;
              if (name_ent[0] == EXFAT_ENTRY_NAME) {
                memset(name_ent + 2, 0, 30);
                for (int c = 0; c < nlen; c++) {
                  *(uint16_t *)(name_ent + 2 + c * 2) = (uint8_t)new_name[c];
                }
              }

              uint16_t chk = exfat_entry_set_checksum(entry, 1 + sec_cnt);
              *(uint16_t *)(entry + 2) = chk;

              for (uint32_t s = 0; s < spc; s++) {
                vfs_cache_write_sector(0, lba + s, cl_buf + s * 512);
              }
              vfs_cache_flush();
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

static bool exfat_vfs_mount(vfs_mount_t *mnt) {
  if (!mnt)
    return false;

  // Risoluzione periferica fisica reale dal chip (ATA / AHCI)
  const ata_device_t *dev = (const ata_device_t *)mnt->priv_data;
  if (!dev) {
    if (mnt->drive_letter == 'C') {
      dev = ata_get_primary_master();
      mnt->priv_data = (void *)dev;
    } else if (mnt->device_id < (uint32_t)ata_get_device_count()) {
      dev = ata_get_device((int)mnt->device_id);
      mnt->priv_data = (void *)dev;
    }
  }

  if (!exfat_mount_device(dev, mnt->partition_lba)) {
    return false;
  }
  if (!exfat_is_mounted()) {
    return false;
  }

  const exfat_vbr_t *vbr = exfat_get_vbr();
  uint32_t used_mb = exfat_get_used_mb();
  uint64_t used_bytes =
      (uint64_t)exfat_get_allocated_clusters() * (uint64_t)exfat_cluster_size();

  // 1. Lettura reale dal volume exFAT / chip disco (nessun limite fittizio)
  uint64_t total_bytes = 0;
  if (vbr && vbr->volume_length > 0) {
    uint32_t sec_sz =
        (vbr->bytes_per_sector_shift >= 9 && vbr->bytes_per_sector_shift <= 12)
            ? (1U << vbr->bytes_per_sector_shift)
            : 512U;
    total_bytes = vbr->volume_length * (uint64_t)sec_sz;
  } else if (dev && dev->present && dev->size_bytes > 0) {
    total_bytes = dev->size_bytes;
  }

  if (total_bytes > 0) {
    mnt->total_bytes = total_bytes;
    mnt->total_mb = (uint32_t)(total_bytes / (1024ULL * 1024ULL));
  } else if (dev && dev->present && dev->size_mb > 0) {
    mnt->total_mb = dev->size_mb;
    mnt->total_bytes = (uint64_t)dev->size_mb * 1024ULL * 1024ULL;
  }

  // 2. Calcolo spazio disponibile reale a 64-bit
  mnt->free_bytes =
      (mnt->total_bytes > used_bytes) ? (mnt->total_bytes - used_bytes) : 0;
  mnt->free_mb = mnt->total_mb > used_mb ? (mnt->total_mb - used_mb) : 0;
  return true;
}

static bool exfat_vfs_open(vfs_mount_t *mnt, const char *rel_path,
                           vfs_file_t *file) {
  if (!rel_path || !file)
    return false;

  if (mnt && mnt->priv_data) {
    exfat_set_device((const ata_device_t *)mnt->priv_data, mnt->partition_lba);
  }

  // Ricerca tramite VFS root cluster
  uint32_t cur_cl = exfat_get_root_cluster();

  char temp_path[VFS_MAX_PATH];
  strncpy(temp_path, rel_path, sizeof(temp_path) - 1);
  temp_path[sizeof(temp_path) - 1] = '\0';
  for (int k = 0; temp_path[k] != '\0'; k++) {
    if (temp_path[k] == '\\')
      temp_path[k] = '/';
  }

  char *token = temp_path;
  char *next = NULL;
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
      if (!item.is_directory)
        return false;
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

static int32_t exfat_vfs_read(vfs_mount_t *mnt, vfs_file_t *file, void *buf,
                              uint32_t count) {
  if (!file || !buf || count == 0)
    return 0;

  if (mnt && mnt->priv_data) {
    exfat_set_device((const ata_device_t *)mnt->priv_data, mnt->partition_lba);
  }

  exfat_item_info_t info;
  info.size = file->size;
  info.first_cluster = file->first_cluster;
  info.no_fat_chain = file->no_fat_chain;

  int32_t r = exfat_read_file(&info, file->current_pos, buf, count);
  if (r > 0)
    file->current_pos += r;
  return r;
}

static int32_t exfat_vfs_write(vfs_mount_t *mnt, vfs_file_t *file,
                               const void *buf, uint32_t count) {
  if (!file || !buf || count == 0)
    return 0;
  if (file->is_directory)
    return -1;

  if (mnt && mnt->priv_data) {
    exfat_set_device((const ata_device_t *)mnt->priv_data, mnt->partition_lba);
  }

  const exfat_vbr_t *vbr = exfat_get_vbr();
  if (!vbr)
    return 0;

  uint32_t cluster_size = exfat_cluster_size();
  if (cluster_size == 0)
    return 0;

  uint32_t dev_id = exfat_get_dev_id();
  uint32_t spc = 1 << vbr->sectors_per_cluster_shift;
  uint64_t target_end = file->current_pos + (uint64_t)count;

  // Se la scrittura supera la dimensione attuale o il file non ha ancora
  // cluster allocati
  if (target_end > file->size || file->first_cluster < 2) {
    uint32_t needed_clusters =
        (uint32_t)((target_end + cluster_size - 1ULL) / cluster_size);
    if (needed_clusters == 0)
      needed_clusters = 1;

    uint32_t current_clusters = 0;
    if (file->first_cluster >= 2) {
      current_clusters =
          (uint32_t)((file->size + cluster_size - 1ULL) / cluster_size);
      if (current_clusters == 0)
        current_clusters = 1;
    }

    // Se non abbiamo ancora un primo cluster valido, allochiamolo
    if (file->first_cluster < 2) {
      uint32_t first_cl = exfat_ops_allocate_cluster();
      if (first_cl == 0)
        return 0; // Disco pieno
      file->first_cluster = first_cl;
      file->no_fat_chain = true;
      current_clusters = 1;
    }

    // Estende l'allocazione preservando no_fat_chain se i nuovi cluster sono
    // contigui
    if (needed_clusters > current_clusters) {
      uint32_t last_cl = file->first_cluster;
      if (file->no_fat_chain) {
        last_cl = file->first_cluster + current_clusters - 1;
      } else {
        for (uint32_t i = 1; i < current_clusters; i++) {
          uint32_t nxt = exfat_get_next_cluster(last_cl, false);
          if (nxt < 2 || nxt >= 0xFFFFFFF8)
            break;
          last_cl = nxt;
        }
      }

      uint32_t prev_contig_end = current_clusters;
      for (uint32_t i = current_clusters; i < needed_clusters; i++) {
        uint32_t new_cl = exfat_ops_allocate_cluster();
        if (new_cl == 0)
          break;

        if (file->no_fat_chain && new_cl == last_cl + 1) {
          last_cl = new_cl;
          prev_contig_end = i + 1;
        } else {
          if (file->no_fat_chain) {
            for (uint32_t k = 0; k + 1 < prev_contig_end; k++) {
              exfat_ops_link_clusters(file->first_cluster + k,
                                      file->first_cluster + k + 1);
            }
            file->no_fat_chain = false;
          }
          exfat_ops_link_clusters(last_cl, new_cl);
          last_cl = new_cl;
        }
      }
    }
  }

  // Effettua la scrittura fisica dei dati nei cluster del file (indirizzamento
  // 64-bit)
  uint64_t cluster_idx = file->current_pos / (uint64_t)cluster_size;
  uint32_t cluster_offset =
      (uint32_t)(file->current_pos % (uint64_t)cluster_size);

  uint32_t cur_cl = file->first_cluster;
  if (file->no_fat_chain) {
    cur_cl += (uint32_t)cluster_idx;
  } else {
    for (uint64_t s = 0; s < cluster_idx; s++) {
      cur_cl = exfat_get_next_cluster(cur_cl, false);
      if (cur_cl < 2 || cur_cl >= 0xFFFFFFF8)
        return 0;
    }
  }

  uint8_t *cl_buf = (uint8_t *)kmalloc(cluster_size);
  if (!cl_buf)
    return 0;

  uint32_t bytes_written = 0;
  const uint8_t *src = (const uint8_t *)buf;

  while (bytes_written < count && cur_cl >= 2 && cur_cl < 0xFFFFFFF8) {
    uint32_t needed = count - bytes_written;
    // Fast-path burst zero-copy multi-cluster in scrittura (no_fat_chain)
    if (file->no_fat_chain && cluster_offset == 0 && needed >= cluster_size) {
      const ata_device_t *hw_dev = exfat_get_device();
      if (hw_dev) {
        uint32_t batch_cl = needed / cluster_size;
        if (batch_cl > 16)
          batch_cl = 16;
        uint64_t lba = exfat_cluster_to_lba(cur_cl);
        uint32_t secs = batch_cl * spc;
        if (ata_write_device_sectors(hw_dev, lba, secs, src + bytes_written)) {
          vfs_cache_invalidate_range(dev_id, lba, secs);
          bytes_written += batch_cl * cluster_size;
          cur_cl += batch_cl;
          continue;
        }
      }
    }

    uint64_t lba = exfat_cluster_to_lba(cur_cl);

    uint32_t can_copy = cluster_size - cluster_offset;
    uint32_t chunk = (needed < can_copy) ? needed : can_copy;

    // Se scriviamo solo parzialmente il cluster, leggiamo prima il contenuto
    // esistente per non sovrascrivere byte adiacenti
    if (cluster_offset > 0 || chunk < cluster_size) {
      for (uint32_t s = 0; s < spc; s++) {
        vfs_cache_read_sector(dev_id, lba + s, cl_buf + s * 512);
      }
    }

    memcpy(cl_buf + cluster_offset, src + bytes_written, chunk);

    for (uint32_t s = 0; s < spc; s++) {
      vfs_cache_write_sector(dev_id, lba + s, cl_buf + s * 512);
    }

    bytes_written += chunk;
    cluster_offset = 0;
    cur_cl = file->no_fat_chain ? (cur_cl + 1)
                                : exfat_get_next_cluster(cur_cl, false);
  }

  kfree(cl_buf);

  if (bytes_written > 0) {
    file->current_pos += bytes_written;
    if (file->current_pos > file->size) {
      file->size = file->current_pos;
    }

    // Aggiorna l'entry nel catalogo cartella
    if (file->dir_cluster >= 2) {
      exfat_ops_update_file_entry(file->dir_cluster, file->name, file->size,
                                  file->first_cluster, file->no_fat_chain);
    }

    vfs_cache_flush();
  }

  return (int32_t)bytes_written;
}

static void exfat_vfs_close(vfs_mount_t *mnt, vfs_file_t *file) {
  (void)mnt;
  if (file) {
    if (file->is_open && file->dir_cluster >= 2 && !file->is_directory) {
      exfat_ops_update_file_entry(file->dir_cluster, file->name, file->size,
                                  file->first_cluster, file->no_fat_chain);
    }
    file->is_open = false;
    vfs_cache_flush();
  }
}

static int64_t exfat_vfs_seek(vfs_mount_t *mnt, vfs_file_t *file,
                              int64_t offset, int whence) {
  (void)mnt;
  if (!file)
    return -1;

  int64_t new_pos = (int64_t)file->current_pos;
  if (whence == VFS_SEEK_SET) {
    new_pos = offset;
  } else if (whence == VFS_SEEK_CUR) {
    new_pos += offset;
  } else if (whence == VFS_SEEK_END) {
    new_pos = (int64_t)file->size + offset;
  }

  if (new_pos < 0)
    new_pos = 0;
  if ((uint64_t)new_pos > file->size)
    new_pos = (int64_t)file->size;

  file->current_pos = (uint64_t)new_pos;
  return new_pos;
}

static int32_t exfat_vfs_readdir(vfs_mount_t *mnt, const char *rel_path,
                                 vfs_dirent_t *entries, int32_t max_entries) {
  (void)mnt;
  if (!entries || max_entries <= 0)
    return 0;

  uint32_t dir_cl = exfat_get_root_cluster();
  if (rel_path && rel_path[0] != '\0') {
    vfs_file_t df;
    if (!exfat_vfs_open(mnt, rel_path, &df) || !df.is_directory) {
      return 0;
    }
    dir_cl = df.first_cluster;
  }

  int32_t limit = (max_entries > 256) ? 256 : max_entries;
  exfat_item_info_t *raw_items =
      (exfat_item_info_t *)kmalloc(sizeof(exfat_item_info_t) * limit);
  if (!raw_items)
    return 0;

  int32_t count = exfat_list_dir(dir_cl, raw_items, limit);

  for (int32_t i = 0; i < count; i++) {
    strncpy(entries[i].name, raw_items[i].name, sizeof(entries[i].name) - 1);
    entries[i].name[sizeof(entries[i].name) - 1] = '\0';
    entries[i].size = raw_items[i].size;
    entries[i].attributes = raw_items[i].attributes;
    entries[i].mtime = raw_items[i].mtime;
    entries[i].permissions = (raw_items[i].attributes & EXFAT_ATTR_READONLY)
                                 ? (VFS_PERM_READ | VFS_PERM_EXEC)
                                 : (VFS_PERM_READ | VFS_PERM_WRITE | VFS_PERM_EXEC);
    entries[i].is_directory = raw_items[i].is_directory;
  }
  kfree(raw_items);
  return count;
}

static bool exfat_vfs_create(vfs_mount_t *mnt, const char *rel_path,
                             uint16_t attributes) {
  (void)mnt;
  if (!rel_path || !*rel_path)
    return false;

  char norm[VFS_MAX_PATH];
  strncpy(norm, rel_path, sizeof(norm) - 1);
  norm[sizeof(norm) - 1] = '\0';
  for (int k = 0; norm[k] != '\0'; k++) {
    if (norm[k] == '\\')
      norm[k] = '/';
  }

  // Separa cartella da nome file
  const char *last_slash = strrchr(norm, '/');
  uint32_t dir_cl = exfat_get_root_cluster();
  const char *fname = norm;

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
  if (cl == 0)
    return false;

  return exfat_ops_create_entry(dir_cl, fname, cl, 0,
                                attributes | EXFAT_ATTR_ARCHIVE);
}

static bool exfat_vfs_mkdir(vfs_mount_t *mnt, const char *rel_path) {
  (void)mnt;
  if (!rel_path || !*rel_path)
    return false;

  char norm[VFS_MAX_PATH];
  strncpy(norm, rel_path, sizeof(norm) - 1);
  norm[sizeof(norm) - 1] = '\0';
  for (int k = 0; norm[k] != '\0'; k++) {
    if (norm[k] == '\\')
      norm[k] = '/';
  }

  const char *last_slash = strrchr(norm, '/');
  uint32_t dir_cl = exfat_get_root_cluster();
  const char *dname = norm;

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
  if (cl == 0)
    return false;

  return exfat_ops_create_entry(dir_cl, dname, cl, 0, EXFAT_ATTR_DIRECTORY);
}

static bool exfat_vfs_unlink(vfs_mount_t *mnt, const char *rel_path) {
  (void)mnt;
  if (!rel_path || !*rel_path)
    return false;

  char norm[VFS_MAX_PATH];
  strncpy(norm, rel_path, sizeof(norm) - 1);
  norm[sizeof(norm) - 1] = '\0';
  for (int k = 0; norm[k] != '\0'; k++) {
    if (norm[k] == '\\')
      norm[k] = '/';
  }

  const char *last_slash = strrchr(norm, '/');
  uint32_t dir_cl = exfat_get_root_cluster();
  const char *fname = norm;

  if (last_slash) {
    char dir_part[VFS_MAX_PATH];
    size_t dlen = last_slash - norm;
    strncpy(dir_part, norm, dlen);
    dir_part[dlen] = '\0';
    vfs_file_t df;
    if (!exfat_vfs_open(mnt, dir_part, &df))
      return false;
    dir_cl = df.first_cluster;
    fname = last_slash + 1;
  }

  return exfat_ops_delete_entry(dir_cl, fname);
}

static bool exfat_vfs_rename(vfs_mount_t *mnt, const char *old_path,
                             const char *new_path) {
  if (!old_path || !*old_path || !new_path || !*new_path)
    return false;

  char old_norm[VFS_MAX_PATH];
  strncpy(old_norm, old_path, sizeof(old_norm) - 1);
  old_norm[sizeof(old_norm) - 1] = '\0';
  for (int k = 0; old_norm[k] != '\0'; k++) {
    if (old_norm[k] == '\\')
      old_norm[k] = '/';
  }

  char new_norm[VFS_MAX_PATH];
  strncpy(new_norm, new_path, sizeof(new_norm) - 1);
  new_norm[sizeof(new_norm) - 1] = '\0';
  for (int k = 0; new_norm[k] != '\0'; k++) {
    if (new_norm[k] == '\\')
      new_norm[k] = '/';
  }

  const char *old_slash = strrchr(old_norm, '/');
  uint32_t dir_cl = exfat_get_root_cluster();
  const char *old_fname = old_norm;

  if (old_slash) {
    char dir_part[VFS_MAX_PATH];
    size_t dlen = old_slash - old_norm;
    strncpy(dir_part, old_norm, dlen);
    dir_part[dlen] = '\0';
    vfs_file_t df;
    if (!exfat_vfs_open(mnt, dir_part, &df))
      return false;
    dir_cl = df.first_cluster;
    old_fname = old_slash + 1;
  }

  const char *new_slash = strrchr(new_norm, '/');
  const char *new_fname = new_slash ? (new_slash + 1) : new_norm;

  return exfat_ops_rename_entry(dir_cl, old_fname, new_fname);
}

static bool exfat_vfs_stat(vfs_mount_t *mnt, const char *rel_path,
                           vfs_dirent_t *out_stat) {
  if (!rel_path || !out_stat)
    return false;
  vfs_file_t f;
  if (!exfat_vfs_open(mnt, rel_path, &f))
    return false;

  strncpy(out_stat->name, f.name, sizeof(out_stat->name) - 1);
  out_stat->name[sizeof(out_stat->name) - 1] = '\0';
  out_stat->size = f.size;
  out_stat->is_directory = f.is_directory;
  out_stat->attributes =
      f.is_directory ? EXFAT_ATTR_DIRECTORY : EXFAT_ATTR_ARCHIVE;
  out_stat->permissions = VFS_PERM_READ | VFS_PERM_WRITE | VFS_PERM_EXEC;
  out_stat->mtime = 0;
  return true;
}

static bool exfat_vfs_truncate(vfs_mount_t *mnt, vfs_file_t *file, uint64_t new_size) {
  if (!file || file->is_directory)
    return false;
  if (mnt && mnt->priv_data) {
    exfat_set_device((const ata_device_t *)mnt->priv_data, mnt->partition_lba);
  }
  const exfat_vbr_t *vbr = exfat_get_vbr();
  uint32_t cl_size = exfat_cluster_size();
  if (vbr && cl_size > 0 && new_size < file->size && file->first_cluster >= 2) {
    exfat_set_volume_dirty(true);
    uint32_t dev_id = exfat_get_dev_id();
    const ata_device_t *hw_dev = exfat_get_device();
    uint32_t spc = 1 << vbr->sectors_per_cluster_shift;
    uint32_t old_cl_cnt = (uint32_t)((file->size + cl_size - 1ULL) / cl_size);
    uint32_t keep_cl_cnt = (new_size == 0) ? 1U : (uint32_t)((new_size + cl_size - 1ULL) / cl_size);
    if (keep_cl_cnt < old_cl_cnt) {
      uint64_t part_lba = vbr->partition_offset ? vbr->partition_offset : EXFAT_PARTITION_LBA;
      uint64_t fat_lba = part_lba + (uint64_t)vbr->fat_offset;
      uint32_t cur_cl = file->first_cluster;
      for (uint32_t idx = 0; idx < keep_cl_cnt - 1 && cur_cl >= 2 && cur_cl < 0xFFFFFFF8; idx++) {
        cur_cl = exfat_get_next_cluster(cur_cl, file->no_fat_chain);
      }
      uint32_t drop_cl = (cur_cl >= 2 && cur_cl < 0xFFFFFFF8)
                             ? exfat_get_next_cluster(cur_cl, file->no_fat_chain)
                             : 0;
      if (!file->no_fat_chain && cur_cl >= 2 && cur_cl < 0xFFFFFFF8) {
        exfat_ops_link_clusters(cur_cl, 0xFFFFFFFF);
      }
      uint32_t rem = old_cl_cnt - keep_cl_cnt;
      while (drop_cl >= 2 && drop_cl < 0xFFFFFFF8 && rem > 0) {
        uint32_t nxt = exfat_get_next_cluster(drop_cl, file->no_fat_chain);
        if (!file->no_fat_chain) {
          uint64_t fat_sec = fat_lba + ((uint64_t)drop_cl * 4ULL) / 512ULL;
          uint32_t fat_off = (uint32_t)(((uint64_t)drop_cl * 4ULL) % 512ULL);
          uint8_t fbuf[512];
          if (vfs_cache_read_sector(dev_id, fat_sec, fbuf)) {
            *(uint32_t *)(fbuf + fat_off) = 0x00000000;
            vfs_cache_write_sector(dev_id, fat_sec, fbuf);
          }
        }
        exfat_set_bitmap_bit(drop_cl, false);
        exfat_notify_cluster_freed();
        if (hw_dev) {
          ata_discard_device_sectors(hw_dev, exfat_cluster_to_lba(drop_cl), spc);
        }
        drop_cl = nxt;
        rem--;
      }
    }
  }
  file->size = new_size;
  if (file->current_pos > new_size)
    file->current_pos = new_size;
  if (file->dir_cluster >= 2) {
    exfat_ops_update_file_entry(file->dir_cluster, file->name, file->size,
                                file->first_cluster, file->no_fat_chain);
  }
  vfs_cache_flush();
  exfat_set_volume_dirty(false);
  return true;
}

static bool exfat_vfs_fsync(vfs_mount_t *mnt, vfs_file_t *file) {
  if (mnt && mnt->priv_data) {
    exfat_set_device((const ata_device_t *)mnt->priv_data, mnt->partition_lba);
  }
  if (file && file->dir_cluster >= 2 && !file->is_directory) {
    exfat_ops_update_file_entry(file->dir_cluster, file->name, file->size,
                                file->first_cluster, file->no_fat_chain);
  }
  vfs_cache_flush();
  exfat_set_volume_dirty(false);
  return true;
}

static vfs_driver_t g_exfat_driver = {.name = "exFAT",
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
                                      .stat = exfat_vfs_stat,
                                      .truncate = exfat_vfs_truncate,
                                      .fsync = exfat_vfs_fsync};

void exfat_ops_init(void) { vfs_register_driver(&g_exfat_driver); }

const vfs_driver_t *exfat_get_vfs_driver(void) { return &g_exfat_driver; }
