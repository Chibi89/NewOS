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

fs_type_t fs_probe_usb_device(uint32_t* out_part_lba, char* out_fs_name, size_t fs_name_len, char* out_label, size_t label_len) {
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

    uint32_t part_lba = 0;
    uint8_t part_type = 0;

    // Ispezione tabella partizioni MBR (offset 0x1BE = 446)
    if (mbr[510] == 0x55 && mbr[511] == 0xAA) {
        part_type = mbr[446 + 4];
        part_lba = *(uint32_t*)&mbr[446 + 8];
    }

    // Se MBR non partizionato (Superfloppy) o LBA nullo, prova a leggere LBA 0 direttamente
    uint8_t vbr[512];
    if (!usb_msc_read_sector(part_lba > 0 ? part_lba : 0, vbr)) {
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
        if (usb_msc_read_sector(part_lba + 2, sb)) {
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
