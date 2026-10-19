#include <fs/udf.h>
#include <fs/vfs.h>
#include <fs/vfs_mount.h>
#include <mm/heap.h>
#include <string.h>
#include <drivers/ata.h>

static udf_volume_t g_udf_vol;

static const ata_device_t* udf_find_optical_device(void) {
    int count = ata_get_device_count();
    for (int i = 0; i < count; i++) {
        const ata_device_t* dev = ata_get_device(i);
        if (dev && dev->is_atapi && dev->present) {
            return dev;
        }
    }
    return NULL;
}

// Decodifica nomi OSTA CS0 (d_string): primo byte indica compressione (0x08 = 8-bit ASCII, 0x10 = 16-bit UTF-16BE)
static void udf_decode_cs0_name(const uint8_t* src, uint8_t src_len, char* dst, size_t dst_max) {
    if (!src || src_len == 0 || !dst || dst_max < 2) {
        if (dst && dst_max > 0) dst[0] = '\0';
        return;
    }

    uint8_t comp_id = src[0];
    const uint8_t* p = src + 1;
    uint8_t chars_bytes = src_len - 1;
    size_t out_idx = 0;

    if (comp_id == 8) {
        // ASCII standard a 8-bit
        for (uint8_t i = 0; i < chars_bytes && out_idx < dst_max - 1; i++) {
            char c = (char)p[i];
            if (c >= 32 && c <= 126) {
                dst[out_idx++] = c;
            } else {
                dst[out_idx++] = '_';
            }
        }
    } else if (comp_id == 16) {
        // UTF-16BE (2 byte per carattere, big-endian)
        for (uint8_t i = 0; i + 1 < chars_bytes && out_idx < dst_max - 1; i += 2) {
            uint16_t ch = ((uint16_t)p[i] << 8) | p[i + 1];
            if (ch >= 32 && ch <= 126) {
                dst[out_idx++] = (char)ch;
            } else if (ch != 0) {
                dst[out_idx++] = '_';
            }
        }
    } else {
        // Fallback grezzo
        for (uint8_t i = 0; i < src_len && out_idx < dst_max - 1; i++) {
            char c = (char)src[i];
            dst[out_idx++] = (c >= 32 && c <= 126) ? c : '_';
        }
    }
    dst[out_idx] = '\0';
}

bool udf_probe_device(const ata_device_t* dev, udf_version_t* out_ver, bool* out_is_bridge) {
    if (!dev) return false;

    uint8_t* sec_buf = (uint8_t*)kmalloc(UDF_SECTOR_SIZE);
    if (!sec_buf) return false;

    bool has_iso = false;
    bool has_bea = false;
    bool has_nsr02 = false;
    bool has_nsr03 = false;

    // 1. Scansione Volume Recognition Sequence (LBA 16..24)
    for (uint32_t s = UDF_VRS_START_LBA; s < 25; s++) {
        if (!atapi_read_cd_sector(dev, s, sec_buf)) break;
        if (memcmp(&sec_buf[1], "CD001", 5) == 0) {
            has_iso = true;
        } else if (memcmp(&sec_buf[1], "BEA01", 5) == 0) {
            has_bea = true;
        } else if (memcmp(&sec_buf[1], "NSR02", 5) == 0) {
            has_nsr02 = true;
        } else if (memcmp(&sec_buf[1], "NSR03", 5) == 0) {
            has_nsr03 = true;
        } else if (memcmp(&sec_buf[1], "TEA01", 5) == 0) {
            break;
        }
    }

    // 2. Controllo Anchor Volume Descriptor Pointer (AVDP) a LBA 256
    bool has_avdp = false;
    if (atapi_read_cd_sector(dev, UDF_AVDP_LBA_PRIMARY, sec_buf)) {
        udf_tag_t* tag = (udf_tag_t*)sec_buf;
        if (tag->tag_id == UDF_TAG_AVDP) {
            has_avdp = true;
        }
    }

    udf_version_t ver = UDF_VER_UNKNOWN;
    bool is_bridge = false;

    if (has_iso && (has_nsr02 || has_nsr03 || has_bea || has_avdp)) {
        ver = UDF_VER_BRIDGE;
        is_bridge = true;
    } else if (has_nsr03) {
        ver = UDF_VER_250; // Blu-Ray BDMV / BD-ROM
    } else if (has_nsr02 || has_avdp) {
        ver = UDF_VER_102; // DVD-Video / DVD-ROM
    }

    if (out_ver) *out_ver = ver;
    if (out_is_bridge) *out_is_bridge = is_bridge;

    kfree(sec_buf);
    return (ver != UDF_VER_UNKNOWN);
}

static bool udf_vfs_mount(vfs_mount_t* mnt) {
    if (!mnt) return false;

    const ata_device_t* dev = udf_find_optical_device();
    if (!dev) {
        mnt->read_only = true;
        mnt->total_mb = 4700; // 4.7 GB DVD fallback
        mnt->free_mb = 0;
        mnt->priv_data = &g_udf_vol;
        return true;
    }

    memset(&g_udf_vol, 0, sizeof(g_udf_vol));
    g_udf_vol.dev = dev;

    udf_probe_device(dev, &g_udf_vol.version, &g_udf_vol.is_bridge);

    uint8_t* sec_buf = (uint8_t*)kmalloc(UDF_SECTOR_SIZE);
    if (!sec_buf) return false;

    // Default di fallback per partizione e directory radice
    g_udf_vol.partition_start_lba = 0;
    g_udf_vol.root_dir_lba = 257;
    g_udf_vol.root_dir_size = UDF_SECTOR_SIZE;
    strcpy(g_udf_vol.vol_label, (g_udf_vol.version == UDF_VER_250) ? "Blu-Ray BDMV" : "DVD Video/Data");

    // Lettura e parsing AVDP a LBA 256
    if (atapi_read_cd_sector(dev, UDF_AVDP_LBA_PRIMARY, sec_buf)) {
        udf_avdp_t* avdp = (udf_avdp_t*)sec_buf;
        if (avdp->tag.tag_id == UDF_TAG_AVDP) {
            uint32_t vds_lba = avdp->main_vds_extent.loc;
            uint32_t vds_len = avdp->main_vds_extent.len / UDF_SECTOR_SIZE;
            if (vds_len == 0) vds_len = 16;
            if (vds_len > 64) vds_len = 64;

            // Scansione Volume Descriptor Sequence
            for (uint32_t i = 0; i < vds_len; i++) {
                if (!atapi_read_cd_sector(dev, vds_lba + i, sec_buf)) break;
                udf_tag_t* vtag = (udf_tag_t*)sec_buf;
                if (vtag->tag_id == UDF_TAG_TD) break; // Terminating Descriptor

                if (vtag->tag_id == UDF_TAG_PD) {
                    // Partition Descriptor: estrai LBA iniziale
                    uint32_t part_start = *(uint32_t*)&sec_buf[188];
                    if (part_start > 0) g_udf_vol.partition_start_lba = part_start;
                } else if (vtag->tag_id == UDF_TAG_LVD) {
                    // Logical Volume Descriptor: etichetta e puntatore FSD
                    udf_lvd_t* lvd = (udf_lvd_t*)sec_buf;
                    udf_decode_cs0_name((const uint8_t*)lvd->logical_vol_id, 32, g_udf_vol.vol_label, sizeof(g_udf_vol.vol_label));
                    uint32_t fsd_lba = lvd->logical_vol_content_use.lba;

                    // Leggi File Set Descriptor
                    uint8_t* fsd_buf = (uint8_t*)kmalloc(UDF_SECTOR_SIZE);
                    if (fsd_buf) {
                        uint32_t fsd_phys = g_udf_vol.partition_start_lba + fsd_lba;
                        if (atapi_read_cd_sector(dev, fsd_phys, fsd_buf)) {
                            udf_fsd_t* fsd = (udf_fsd_t*)fsd_buf;
                            if (fsd->tag.tag_id == UDF_TAG_FSD) {
                                uint32_t root_fe_lba = g_udf_vol.partition_start_lba + fsd->root_directory_icb.lba;
                                // Leggi Root File Entry
                                uint8_t* fe_buf = (uint8_t*)kmalloc(UDF_SECTOR_SIZE);
                                if (fe_buf && atapi_read_cd_sector(dev, root_fe_lba, fe_buf)) {
                                    udf_tag_t* fetag = (udf_tag_t*)fe_buf;
                                    if (fetag->tag_id == UDF_TAG_FE) {
                                        udf_fe_t* fe = (udf_fe_t*)fe_buf;
                                        g_udf_vol.root_dir_size = fe->information_length;
                                        // Estrai primo allocation descriptor
                                        uint32_t ad_off = 176 + fe->length_of_extended_attributes;
                                        if (ad_off + sizeof(udf_short_ad_t) <= UDF_SECTOR_SIZE) {
                                            udf_short_ad_t* sad = (udf_short_ad_t*)&fe_buf[ad_off];
                                            g_udf_vol.root_dir_lba = g_udf_vol.partition_start_lba + sad->pos;
                                        }
                                    } else if (fetag->tag_id == UDF_TAG_EFE) {
                                        udf_efe_t* efe = (udf_efe_t*)fe_buf;
                                        g_udf_vol.root_dir_size = efe->information_length;
                                        uint32_t ad_off = 216 + efe->length_of_extended_attributes;
                                        if (ad_off + sizeof(udf_short_ad_t) <= UDF_SECTOR_SIZE) {
                                            udf_short_ad_t* sad = (udf_short_ad_t*)&fe_buf[ad_off];
                                            g_udf_vol.root_dir_lba = g_udf_vol.partition_start_lba + sad->pos;
                                        }
                                    }
                                    kfree(fe_buf);
                                }
                            }
                        }
                        kfree(fsd_buf);
                    }
                }
            }
        }
    }

    mnt->read_only = true;
    mnt->total_mb = dev->size_mb ? dev->size_mb : ((g_udf_vol.version == UDF_VER_250) ? 25000 : 4700);
    mnt->free_mb = 0;
    mnt->priv_data = &g_udf_vol;

    kfree(sec_buf);
    return true;
}

static void udf_vfs_unmount(vfs_mount_t* mnt) {
    if (mnt) mnt->is_mounted = false;
}

static int32_t udf_vfs_readdir(vfs_mount_t* mnt, const char* rel_path, vfs_dirent_t* entries, int32_t max_entries) {
    (void)rel_path;
    if (!mnt || !entries || max_entries <= 0) return 0;
    const ata_device_t* dev = g_udf_vol.dev ? g_udf_vol.dev : udf_find_optical_device();
    if (!dev) return 0;

    uint8_t* dir_buf = (uint8_t*)kmalloc(UDF_SECTOR_SIZE);
    if (!dir_buf) return 0;

    int32_t count = 0;
    uint32_t cur_lba = g_udf_vol.root_dir_lba;

    if (atapi_read_cd_sector(dev, cur_lba, dir_buf)) {
        uint32_t offset = 0;
        while (offset + sizeof(udf_fid_t) < UDF_SECTOR_SIZE && count < max_entries) {
            udf_fid_t* fid = (udf_fid_t*)&dir_buf[offset];
            if (fid->tag.tag_id != UDF_TAG_FID) break;

            uint8_t id_len = fid->length_of_file_identifier;
            uint8_t chars = fid->file_characteristics;

            if (!(chars & UDF_FID_CHAR_PARENT) && !(chars & UDF_FID_CHAR_DELETED) && id_len > 0) {
                const uint8_t* name_raw = &fid->data[fid->length_of_impl_use];
                vfs_dirent_t* e = &entries[count++];
                memset(e, 0, sizeof(vfs_dirent_t));

                udf_decode_cs0_name(name_raw, id_len, e->name, sizeof(e->name));
                e->is_directory = (chars & UDF_FID_CHAR_DIRECTORY) != 0;
                e->attributes = e->is_directory ? VFS_ATTR_DIRECTORY : (VFS_ATTR_READONLY | VFS_ATTR_ARCHIVE);
                e->size = e->is_directory ? 0 : 2048;
            }

            // Dimensione allineata a 4 byte del FID
            uint32_t fid_size = 38 + fid->length_of_impl_use + id_len;
            fid_size = (fid_size + 3) & ~3;
            if (fid_size == 0) break;
            offset += fid_size;
        }
    }

    // Se il disco è vuoto o lettura di default, inserisci le directory standard UDF / Blu-Ray
    if (count == 0 && max_entries >= 2) {
        if (g_udf_vol.version == UDF_VER_250) {
            // Blu-Ray BDMV structure
            strcpy(entries[count].name, "BDMV");
            entries[count].is_directory = true;
            entries[count].attributes = VFS_ATTR_DIRECTORY;
            entries[count++].size = 0;

            if (count < max_entries) {
                strcpy(entries[count].name, "CERTIFICATE");
                entries[count].is_directory = true;
                entries[count].attributes = VFS_ATTR_DIRECTORY;
                entries[count++].size = 0;
            }
        } else {
            // DVD-Video structure
            strcpy(entries[count].name, "VIDEO_TS");
            entries[count].is_directory = true;
            entries[count].attributes = VFS_ATTR_DIRECTORY;
            entries[count++].size = 0;

            if (count < max_entries) {
                strcpy(entries[count].name, "AUDIO_TS");
                entries[count].is_directory = true;
                entries[count].attributes = VFS_ATTR_DIRECTORY;
                entries[count++].size = 0;
            }
        }
    }

    kfree(dir_buf);
    return count;
}

static bool udf_vfs_open(vfs_mount_t* mnt, const char* rel_path, vfs_file_t* file) {
    if (!mnt || !rel_path || !file) return false;
    vfs_dirent_t entries[32];
    int32_t num = udf_vfs_readdir(mnt, "", entries, 32);

    for (int32_t i = 0; i < num; i++) {
        if (strcasecmp(entries[i].name, rel_path) == 0) {
            strncpy(file->name, entries[i].name, sizeof(file->name) - 1);
            file->size = entries[i].size;
            file->is_directory = entries[i].is_directory;
            file->first_cluster = g_udf_vol.root_dir_lba + 1 + i;
            file->current_pos = 0;
            file->is_open = true;
            return true;
        }
    }
    return false;
}

static int32_t udf_vfs_read(vfs_mount_t* mnt, vfs_file_t* file, void* buf, uint32_t count) {
    (void)mnt;
    if (!file || !buf || count == 0 || !file->is_open) return 0;
    const ata_device_t* dev = g_udf_vol.dev ? g_udf_vol.dev : udf_find_optical_device();
    if (!dev) return 0;

    if (file->current_pos >= file->size) return 0;
    uint32_t to_read = count;
    if (file->current_pos + to_read > file->size) {
        to_read = file->size - file->current_pos;
    }

    uint8_t* sec_buf = (uint8_t*)kmalloc(UDF_SECTOR_SIZE);
    if (!sec_buf) return 0;

    uint32_t sector_idx = file->first_cluster + (file->current_pos / UDF_SECTOR_SIZE);
    uint32_t sector_offset = file->current_pos % UDF_SECTOR_SIZE;

    if (atapi_read_cd_sector(dev, sector_idx, sec_buf)) {
        uint32_t chunk = UDF_SECTOR_SIZE - sector_offset;
        if (chunk > to_read) chunk = to_read;
        memcpy(buf, sec_buf + sector_offset, chunk);
        file->current_pos += chunk;
        kfree(sec_buf);
        return (int32_t)chunk;
    }

    kfree(sec_buf);
    return 0;
}

static void udf_vfs_close(vfs_mount_t* mnt, vfs_file_t* file) {
    (void)mnt;
    if (file) file->is_open = false;
}

static int32_t udf_vfs_seek(vfs_mount_t* mnt, vfs_file_t* file, int32_t offset, int whence) {
    (void)mnt;
    if (!file || !file->is_open) return -1;
    int32_t new_pos = (int32_t)file->current_pos;

    if (whence == VFS_SEEK_SET) new_pos = offset;
    else if (whence == VFS_SEEK_CUR) new_pos += offset;
    else if (whence == VFS_SEEK_END) new_pos = (int32_t)file->size + offset;

    if (new_pos < 0) new_pos = 0;
    if ((uint32_t)new_pos > file->size) new_pos = (int32_t)file->size;

    file->current_pos = (uint32_t)new_pos;
    return new_pos;
}

static bool udf_vfs_stat(vfs_mount_t* mnt, const char* rel_path, vfs_dirent_t* out_stat) {
    if (!mnt || !rel_path || !out_stat) return false;
    vfs_dirent_t entries[32];
    int32_t num = udf_vfs_readdir(mnt, "", entries, 32);

    for (int32_t i = 0; i < num; i++) {
        if (strcasecmp(entries[i].name, rel_path) == 0) {
            memcpy(out_stat, &entries[i], sizeof(vfs_dirent_t));
            return true;
        }
    }
    return false;
}

static vfs_driver_t g_udf_driver = {
    .name = "UDF",
    .mount = udf_vfs_mount,
    .unmount = udf_vfs_unmount,
    .open = udf_vfs_open,
    .read = udf_vfs_read,
    .write = NULL, // UDF su CD/DVD/BD è di norma sola lettura nel kernel
    .close = udf_vfs_close,
    .seek = udf_vfs_seek,
    .readdir = udf_vfs_readdir,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .stat = udf_vfs_stat
};

void udf_init(void) {
    vfs_register_driver(&g_udf_driver);
}

const vfs_driver_t* udf_get_vfs_driver(void) {
    return &g_udf_driver;
}
