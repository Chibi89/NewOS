#ifndef FS_PROBE_H
#define FS_PROBE_H

#include <types.h>
#include <drivers/ata.h>

typedef enum {
    FS_TYPE_UNKNOWN = 0,
    FS_TYPE_NTFS,
    FS_TYPE_EXFAT,
    FS_TYPE_FAT32,
    FS_TYPE_FAT16,
    FS_TYPE_ISO9660,
    FS_TYPE_UDF,
    FS_TYPE_EXT4
} fs_type_t;

// Esegue il rilevamento automatico del filesystem nativo presente su un supporto USB Mass Storage
fs_type_t fs_probe_usb_device(uint32_t* out_part_lba, char* out_fs_name, size_t fs_name_len, char* out_label, size_t label_len);

// Esegue il rilevamento automatico del filesystem nativo presente su un'unità ottica ATAPI (CD/DVD/Blu-Ray)
fs_type_t fs_probe_optical_device(const ata_device_t* dev, char* out_fs_name, size_t fs_name_len, char* out_label, size_t label_len);

// Converte l'enumeratore del filesystem in stringa leggibile
const char* fs_type_to_str(fs_type_t type);

#endif // FS_PROBE_H
