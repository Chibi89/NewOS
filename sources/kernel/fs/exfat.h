#ifndef FS_EXFAT_H
#define FS_EXFAT_H

#include <types.h>

typedef struct ata_device_s ata_device_t;

#define EXFAT_PARTITION_LBA 2048

#define EXFAT_ENTRY_EOD      0x00 // End of directory
#define EXFAT_ENTRY_BITMAP   0x81
#define EXFAT_ENTRY_UPCASE   0x82
#define EXFAT_ENTRY_LABEL    0x83
#define EXFAT_ENTRY_FILE     0x85
#define EXFAT_ENTRY_STREAM   0xC0
#define EXFAT_ENTRY_NAME     0xC1

#define EXFAT_FLAG_ALLOC_POSSIBLE 0x01
#define EXFAT_FLAG_NO_FAT_CHAIN   0x02

#define EXFAT_ATTR_READONLY  0x0001
#define EXFAT_ATTR_HIDDEN    0x0002
#define EXFAT_ATTR_SYSTEM    0x0004
#define EXFAT_ATTR_DIRECTORY 0x0010
#define EXFAT_ATTR_ARCHIVE   0x0020

typedef struct __attribute__((packed)) {
    uint8_t  jump_boot[3];
    char     fs_name[8];             // "EXFAT   "
    uint8_t  must_be_zero[53];
    uint64_t partition_offset;       // LBA
    uint64_t volume_length;          // sectors
    uint32_t fat_offset;             // sector offset
    uint32_t fat_length;             // sectors
    uint32_t cluster_heap_offset;    // sector offset
    uint32_t cluster_count;
    uint32_t root_dir_cluster;
    uint32_t volume_serial;
    uint16_t fs_revision;
    uint16_t volume_flags;
    uint8_t  bytes_per_sector_shift; // 9
    uint8_t  sectors_per_cluster_shift; // 3
    uint8_t  number_of_fats;
    uint8_t  drive_select;
    uint8_t  percent_in_use;
    uint8_t  reserved[7];
    uint8_t  boot_code[390];
    uint16_t boot_signature;         // 0xAA55
} exfat_vbr_t;

typedef struct {
    char     name[128];
    uint32_t size;
    uint32_t first_cluster;
    uint16_t attributes;
    bool     is_directory;
    bool     no_fat_chain;
} exfat_item_info_t;

bool exfat_mount(uint64_t partition_lba);
bool exfat_mount_device(const ata_device_t* dev, uint64_t partition_lba);
void exfat_set_device(const ata_device_t* dev, uint64_t partition_lba);
bool exfat_is_mounted(void);
const exfat_vbr_t* exfat_get_vbr(void);
uint32_t exfat_get_root_cluster(void);
uint32_t exfat_cluster_size(void);
uint64_t exfat_cluster_to_lba(uint32_t cluster);
uint32_t exfat_get_next_cluster(uint32_t cluster, bool no_fat_chain);

// Ricerca un elemento all'interno di una directory (tramite il cluster di inizio della dir)
bool exfat_find_in_dir(uint32_t dir_cluster, const char* name, exfat_item_info_t* out_info);

// Elenca gli elementi presenti in una directory
int32_t exfat_list_dir(uint32_t dir_cluster, exfat_item_info_t* out_items, int32_t max_items);

// Legge dati da un file dato il suo exfat_item_info_t
int32_t exfat_read_file(const exfat_item_info_t* file_info, uint32_t offset, void* buf, uint32_t count);

// Scrive dati in-place su un file esistente preservando i cluster
int32_t exfat_write_file_inplace(const exfat_item_info_t* file_info, uint32_t offset, const void* buf, uint32_t count);

// Diagnostica e verifica integrita' regione di avvio exFAT (VBR primario e secondario)
typedef struct {
    bool     primary_vbr_valid;
    bool     backup_vbr_valid;
    bool     checksum_matched;
    bool     self_healed;
    uint32_t computed_checksum;
    uint32_t stored_checksum;
} exfat_boot_integrity_t;

// Verifica l'integrita' della regione di avvio e sincronizza Main e Backup VBR
bool exfat_check_boot_integrity(exfat_boot_integrity_t* out_status);

// Sincronizza i 12 settori del VBR Primario nel VBR di Backup
bool exfat_sync_backup_vbr(void);

// Interrogazione spazio e cluster allocati reali
uint32_t exfat_get_allocated_clusters(void);
uint32_t exfat_get_used_mb(void);
uint32_t exfat_get_free_clusters(void);
void     exfat_notify_cluster_allocated(void);
void     exfat_notify_cluster_freed(void);
void     exfat_set_bitmap_bit(uint32_t cluster, bool allocated);

#endif // FS_EXFAT_H
