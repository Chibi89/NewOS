#include <fs/fs_probe.h>
#include <drivers/usb/usb_msc.h>
#include <drivers/ata.h>
#include <fs/udf.h>
#include <string.h>

const char* fs_type_to_str(fs_type_t type) {
    switch (type) {
        case FS_TYPE_NTFS:    return "NTFS";
        case FS_TYPE_EXFAT:   return "exFAT";
        case FS_TYPE_FAT32:   return "FAT32";
        case FS_TYPE_FAT16:   return "FAT16";
        case FS_TYPE_ISO9660: return "ISO 9660 / Joliet";
        case FS_TYPE_UDF:     return "UDF (1.02/2.50/Bridge)";
        case FS_TYPE_EXT4:    return "Linux EXT4";
        default:              return "Sconosciuto";
    }
}

// GUID standard GPT
static const uint8_t GPT_GUID_UNUSED[16] = {0};
static const uint8_t GPT_GUID_BASIC_DATA[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44, 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};
static const uint8_t GPT_GUID_ESP[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};

bool gpt_probe_table(sector_reader_fn reader, void* ctx, uint64_t* out_lba, uint64_t* out_sectors, char* out_fs_name, size_t fs_len) {
    if (!reader) return false;

    // 1. Lettura Header GPT su LBA 1
    uint8_t sec_buf[512];
    if (!reader(ctx, 1, 1, sec_buf)) return false;

    const gpt_header_t* hdr = (const gpt_header_t*)sec_buf;
    if (memcmp(hdr->signature, "EFI PART", 8) != 0) return false;

    uint32_t num_entries = hdr->num_partition_entries;
    if (num_entries == 0 || num_entries > 128) num_entries = 128; // Limite di sicurezza
    uint32_t entry_size = hdr->partition_entry_size;
    if (entry_size < sizeof(gpt_entry_t)) entry_size = sizeof(gpt_entry_t);

    uint64_t entries_lba = hdr->partition_entries_lba;
    if (entries_lba < 2) entries_lba = 2;

    uint32_t entries_per_sector = 512 / entry_size;
    if (entries_per_sector == 0) entries_per_sector = 4;

    uint32_t sectors_to_read = (num_entries + entries_per_sector - 1) / entries_per_sector;
    if (sectors_to_read > 32) sectors_to_read = 32;

    uint64_t best_lba = 0;
    uint64_t best_sectors = 0;
    char best_fs[16] = "exFAT";
    bool found_partition = false;

    for (uint32_t s = 0; s < sectors_to_read; s++) {
        if (!reader(ctx, entries_lba + s, 1, sec_buf)) break;

        for (uint32_t e = 0; e < entries_per_sector; e++) {
            uint32_t entry_idx = s * entries_per_sector + e;
            if (entry_idx >= num_entries) break;

            const gpt_entry_t* entry = (const gpt_entry_t*)(sec_buf + e * entry_size);
            if (memcmp(entry->type_guid, GPT_GUID_UNUSED, 16) == 0) continue;

            uint64_t start = entry->starting_lba;
            uint64_t end = entry->ending_lba;
            if (end < start || start == 0) continue;
            uint64_t count = end - start + 1;

            // Riconoscimento filesystem all'LBA iniziale
            uint8_t vbr[512];
            char detected_fs[16] = "RAW";
            if (reader(ctx, start, 1, vbr)) {
                if (memcmp(&vbr[3], "EXFAT   ", 8) == 0) {
                    strncpy(detected_fs, "exFAT", sizeof(detected_fs) - 1);
                } else if (memcmp(&vbr[3], "NTFS    ", 8) == 0) {
                    strncpy(detected_fs, "NTFS", sizeof(detected_fs) - 1);
                } else if (memcmp(&vbr[82], "FAT32   ", 8) == 0) {
                    strncpy(detected_fs, "FAT32", sizeof(detected_fs) - 1);
                } else if (memcmp(&vbr[54], "FAT16   ", 8) == 0) {
                    strncpy(detected_fs, "FAT16", sizeof(detected_fs) - 1);
                } else {
                    uint8_t sb[512];
                    if (reader(ctx, start + 2, 1, sb) && *(uint16_t*)&sb[56] == 0xEF53) {
                        strncpy(detected_fs, "EXT4", sizeof(detected_fs) - 1);
                    }
                }
            }

            // Preferenza partizioni Basic Data o ESP con filesystem riconosciuto
            bool is_basic = (memcmp(entry->type_guid, GPT_GUID_BASIC_DATA, 16) == 0);
            bool is_esp = (memcmp(entry->type_guid, GPT_GUID_ESP, 16) == 0);
            if (!found_partition || is_basic || (is_esp && strcmp(best_fs, "RAW") == 0)) {
                best_lba = start;
                best_sectors = count;
                strncpy(best_fs, detected_fs, sizeof(best_fs) - 1);
                found_partition = true;
                if (is_basic && strcmp(detected_fs, "RAW") != 0) break;
            }
        }
        if (found_partition && strcmp(best_fs, "RAW") != 0) break;
    }

    if (found_partition) {
        if (out_lba) *out_lba = best_lba;
        if (out_sectors) *out_sectors = best_sectors;
        if (out_fs_name) strncpy(out_fs_name, best_fs, fs_len - 1);
        return true;
    }
    return false;
}

static bool usb_sector_reader(void* ctx, uint64_t lba, uint32_t count, void* buf) {
    (void)ctx;
    uint8_t* p = (uint8_t*)buf;
    for (uint32_t i = 0; i < count; i++) {
        if (!usb_msc_read_sector((uint32_t)(lba + i), p + i * 512)) return false;
    }
    return true;
}

fs_type_t fs_probe_usb_device(uint64_t* out_part_lba, char* out_fs_name, size_t fs_name_len, char* out_label, size_t label_len) {
    const usb_msc_device_t* udev = usb_msc_get_device();
    if (!udev || !udev->is_attached) {
        if (out_part_lba) *out_part_lba = 0;
        if (out_fs_name) strncpy(out_fs_name, "exFAT", fs_name_len);
        if (out_label) strncpy(out_label, "Disco USB", label_len);
        return FS_TYPE_UNKNOWN;
    }

    uint8_t mbr[512];
    if (!usb_msc_read_sector(0, mbr)) {
        if (out_part_lba) *out_part_lba = 0;
        if (out_fs_name) strncpy(out_fs_name, "exFAT", fs_name_len);
        return FS_TYPE_UNKNOWN;
    }

    uint64_t part_lba = 0;
    uint8_t part_type = 0;

    // Ispezione tabella partizioni MBR (offset 0x1BE = 446)
    if (mbr[510] == 0x55 && mbr[511] == 0xAA) {
        part_type = mbr[446 + 4];
        part_lba = *(uint32_t*)&mbr[446 + 8];
    }

    // Se MBR protettivo GPT (0xEE), interroga direttamente la tabella GPT a 64-bit
    if (part_type == 0xEE) {
        uint64_t gpt_lba = 0, gpt_sec = 0;
        char gpt_fs[16] = "exFAT";
        if (gpt_probe_table(usb_sector_reader, NULL, &gpt_lba, &gpt_sec, gpt_fs, sizeof(gpt_fs))) {
            if (out_part_lba) *out_part_lba = gpt_lba;
            if (out_fs_name) strncpy(out_fs_name, gpt_fs, fs_name_len - 1);
            if (out_label) {
                mini_snprintf(out_label, label_len, "Disco USB (%s GPT)", gpt_fs);
            }
            if (strcmp(gpt_fs, "NTFS") == 0) return FS_TYPE_NTFS;
            if (strcmp(gpt_fs, "FAT32") == 0) return FS_TYPE_FAT32;
            if (strcmp(gpt_fs, "FAT16") == 0) return FS_TYPE_FAT16;
            if (strcmp(gpt_fs, "EXT4") == 0) return FS_TYPE_EXT4;
            return FS_TYPE_EXFAT;
        }
    }

    // Se MBR non partizionato (Superfloppy) o LBA nullo, prova a leggere LBA 0 direttamente
    uint8_t vbr[512];
    if (!usb_msc_read_sector((uint32_t)(part_lba > 0 ? part_lba : 0), vbr)) {
        part_lba = 0;
        memcpy(vbr, mbr, 512);
    }

    fs_type_t detected = FS_TYPE_UNKNOWN;
    const char* fs_driver = "exFAT";
    char label_buf[32] = "Disco USB";

    // 1. Rilevamento NTFS (Firma "NTFS    " all'offset 3)
    if (memcmp(&vbr[3], "NTFS    ", 8) == 0) {
        detected = FS_TYPE_NTFS;
        fs_driver = "NTFS";
        strcpy(label_buf, "Disco USB (NTFS)");
    }
    // 2. Rilevamento exFAT (Firma "EXFAT   " all'offset 3)
    else if (memcmp(&vbr[3], "EXFAT   ", 8) == 0) {
        detected = FS_TYPE_EXFAT;
        fs_driver = "exFAT";
        strcpy(label_buf, "Disco USB (exFAT)");
    }
    // 3. Rilevamento FAT32 (Firma "FAT32   " all'offset 82)
    else if (memcmp(&vbr[82], "FAT32   ", 8) == 0 || part_type == 0x0B || part_type == 0x0C) {
        detected = FS_TYPE_FAT32;
        fs_driver = "exFAT"; // Il driver exFAT supporta anche catene compatibili FAT
        strcpy(label_buf, "Disco USB (FAT32)");
    }
    // 4. Rilevamento FAT16 (Firma "FAT16   " all'offset 54)
    else if (memcmp(&vbr[54], "FAT16   ", 8) == 0 || part_type == 0x04 || part_type == 0x06 || part_type == 0x0E) {
        detected = FS_TYPE_FAT16;
        fs_driver = "exFAT";
        strcpy(label_buf, "Disco USB (FAT16)");
    }
    // 5. Rilevamento Linux EXT4 (Magic 0xEF53 all'offset 1080 del filesystem)
    else {
        uint8_t sb[512];
        if (usb_msc_read_sector((uint32_t)(part_lba + 2), sb)) {
            uint16_t ext_magic = *(uint16_t*)&sb[56];
            if (ext_magic == 0xEF53) {
                detected = FS_TYPE_EXT4;
                fs_driver = "NTFS";
                strcpy(label_buf, "Disco USB (Linux EXT4)");
            }
        }
    }

    // Se non identificato con certezza, usa exFAT nativo come default moderno
    if (detected == FS_TYPE_UNKNOWN) {
        detected = FS_TYPE_EXFAT;
        fs_driver = "exFAT";
        strcpy(label_buf, "Disco USB (exFAT)");
    }

    if (out_part_lba) *out_part_lba = part_lba;
    if (out_fs_name) strncpy(out_fs_name, fs_driver, fs_name_len - 1);
    if (out_label) strncpy(out_label, label_buf, label_len - 1);

    return detected;
}

fs_type_t fs_probe_optical_device(const ata_device_t* dev, char* out_fs_name, size_t fs_name_len, char* out_label, size_t label_len) {
    if (!dev || !dev->is_atapi) {
        if (out_fs_name) strncpy(out_fs_name, "ISO9660", fs_name_len);
        if (out_label) strncpy(out_label, "Unita ottica", label_len);
        return FS_TYPE_UNKNOWN;
    }

    udf_version_t uver = UDF_VER_UNKNOWN;
    bool is_bridge = false;
    bool is_udf = udf_probe_device(dev, &uver, &is_bridge);

    fs_type_t detected = FS_TYPE_ISO9660;
    const char* fs_driver = "ISO9660";
    char label_buf[32] = "CD-ROM (ISO 9660)";

    if (is_udf) {
        detected = FS_TYPE_UDF;
        fs_driver = "UDF";
        if (is_bridge) {
            strcpy(label_buf, "DVD/BD (ISO-UDF Bridge)");
        } else if (uver == UDF_VER_250) {
            strcpy(label_buf, "Blu-Ray BDMV (UDF 2.50)");
        } else {
            strcpy(label_buf, "DVD-Video (UDF 1.02)");
        }
    } else {
        if (dev->size_mb > 900) {
            strcpy(label_buf, "DVD Dati (ISO 9660/Joliet)");
        } else {
            strcpy(label_buf, "CD-ROM (ISO 9660/Joliet)");
        }
    }

    if (out_fs_name) strncpy(out_fs_name, fs_driver, fs_name_len - 1);
    if (out_label) strncpy(out_label, label_buf, label_len - 1);

    return detected;
}
