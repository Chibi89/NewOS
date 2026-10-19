#ifndef FS_ISO9660_H
#define FS_ISO9660_H

#include <types.h>
#include <fs/vfs.h>
#include <fs/vfs_mount.h>

#define ISO9660_SECTOR_SIZE 2048
#define ISO9660_PVD_LBA     16

typedef struct __attribute__((packed)) {
    uint8_t  type;                      // 1 = PVD, 2 = SVD (Joliet), 255 = Terminator
    char     id[5];                     // "CD001"
    uint8_t  version;                   // 1
    uint8_t  unused1;
    char     system_id[32];
    char     volume_id[32];
    uint8_t  unused2[8];
    uint32_t volume_space_size_lsb;
    uint32_t volume_space_size_msb;
    uint8_t  unused3[32];
    uint16_t volume_set_size_lsb;
    uint16_t volume_set_size_msb;
    uint16_t volume_seq_number_lsb;
    uint16_t volume_seq_number_msb;
    uint16_t logical_block_size_lsb;
    uint16_t logical_block_size_msb;
    uint32_t path_table_size_lsb;
    uint32_t path_table_size_msb;
    uint32_t path_table_lba_lsb;
    uint32_t opt_path_table_lba_lsb;
    uint32_t path_table_lba_msb;
    uint32_t opt_path_table_lba_msb;
    uint8_t  root_directory_record[34];
    char     volume_set_id[128];
    char     publisher_id[128];
    char     data_preparer_id[128];
    char     application_id[128];
    char     copyright_file_id[37];
    char     abstract_file_id[37];
    char     bibliographic_file_id[37];
    char     creation_date[17];
    char     modification_date[17];
    char     expiration_date[17];
    char     effective_date[17];
    uint8_t  file_structure_version;
    uint8_t  unused4;
    uint8_t  application_data[512];
    uint8_t  reserved[653];
} iso9660_pvd_t;

typedef struct __attribute__((packed)) {
    uint8_t  length;
    uint8_t  ext_attr_length;
    uint32_t extent_lba_lsb;
    uint32_t extent_lba_msb;
    uint32_t data_length_lsb;
    uint32_t data_length_msb;
    uint8_t  recording_date[7];
    uint8_t  flags;
    uint8_t  file_unit_size;
    uint8_t  interleave_gap_size;
    uint16_t volume_seq_number_lsb;
    uint16_t volume_seq_number_msb;
    uint8_t  name_length;
    char     name[1]; // Variabile
} iso9660_dirent_raw_t;

// Inizializza il driver ISO 9660 e lo registra nel VFS
void iso9660_init(void);

// Restituisce il driver VFS per ISO 9660
const vfs_driver_t* iso9660_get_vfs_driver(void);

#endif // FS_ISO9660_H
