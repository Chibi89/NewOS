#ifndef FS_NTFS_H
#define FS_NTFS_H

#include <types.h>
#include <fs/vfs.h>
#include <fs/vfs_mount.h>

#define NTFS_SECTOR_SIZE             512
#define NTFS_OEM_ID                  "NTFS    "
#define NTFS_FILE_MAGIC              0x454C4946 // "FILE"
#define NTFS_INDX_MAGIC              0x58444E49 // "INDX"

// Identificatori Tipo Attributo MFT
#define NTFS_ATTR_STANDARD_INFO      0x10
#define NTFS_ATTR_ATTRIBUTE_LIST     0x20
#define NTFS_ATTR_FILE_NAME          0x30
#define NTFS_ATTR_OBJECT_ID          0x40
#define NTFS_ATTR_SECURITY           0x50
#define NTFS_ATTR_VOLUME_NAME        0x60
#define NTFS_ATTR_VOLUME_INFO        0x70
#define NTFS_ATTR_DATA               0x80
#define NTFS_ATTR_INDEX_ROOT         0x90
#define NTFS_ATTR_INDEX_ALLOCATION   0xA0
#define NTFS_ATTR_BITMAP             0xB0
#define NTFS_ATTR_REPARSE_POINT      0xC0
#define NTFS_ATTR_END                0xFFFFFFFF

// Indici MFT Record di Sistema standard
#define NTFS_REC_MFT                 0
#define NTFS_REC_MFT_MIRR            1
#define NTFS_REC_LOGFILE             2
#define NTFS_REC_VOLUME              3
#define NTFS_REC_ATTR_DEF            4
#define NTFS_REC_ROOT_DIR            5
#define NTFS_REC_BITMAP              6
#define NTFS_REC_BOOT                7
#define NTFS_REC_BAD_CLUST           8
#define NTFS_REC_SECURE              9
#define NTFS_REC_UPCASE              10
#define NTFS_REC_EXTEND              11

// Flag MFT Record
#define NTFS_REC_FLAG_IN_USE         0x0001
#define NTFS_REC_FLAG_DIRECTORY      0x0002

// Boot Sector NTFS (512 byte)
typedef struct __attribute__((packed)) {
    uint8_t  jump_boot[3];
    char     oem_id[8];              // "NTFS    "
    uint16_t bytes_per_sector;       // Standard: 512
    uint8_t  sectors_per_cluster;    // Tipicamente 8 (4096 byte per cluster)
    uint16_t reserved_sectors;       // 0
    uint8_t  fats_count;             // 0
    uint16_t root_entries_count;     // 0
    uint16_t total_sectors_16;       // 0
    uint8_t  media_descriptor;       // 0xF8
    uint16_t sectors_per_fat_16;     // 0
    uint16_t sectors_per_track;
    uint16_t heads_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;       // 0
    uint8_t  bios_drive_num;
    uint8_t  extended_boot_sig;
    uint64_t total_sectors_64;       // Numero totale di settori a 64-bit
    uint64_t mft_start_lcn;          // Cluster iniziale MFT ($MFT)
    uint64_t mft_mirr_start_lcn;     // Cluster iniziale copia speculare ($MFTMirr)
    int8_t   clusters_per_mft_record;// Se negativo: 2^(-valore) byte (es. -10 = 1024 byte)
    uint8_t  reserved1[3];
    int8_t   clusters_per_index_buf; // Se negativo: 2^(-valore) byte (es. -12 = 4096 byte)
    uint8_t  reserved2[3];
    uint64_t volume_serial_number;
    uint32_t checksum;
    uint8_t  bootstrap_code[426];
    uint16_t boot_signature;         // 0xAA55
} ntfs_boot_sector_t;

// Intestazione Record MFT (FILE Record)
typedef struct __attribute__((packed)) {
    uint32_t magic;                  // "FILE" (0x454C4946)
    uint16_t fixup_offset;
    uint16_t fixup_count;
    uint64_t lsn;                    // Logfile Sequence Number
    uint16_t sequence_number;
    uint16_t link_count;
    uint16_t first_attribute_offset; // Tipicamente 56 (0x38)
    uint16_t flags;                  // 0x01 = InUse, 0x02 = Directory
    uint32_t bytes_in_use;
    uint32_t bytes_allocated;
    uint64_t base_mft_record;
    uint16_t next_attribute_id;
    uint16_t reserved;
    uint32_t mft_record_number;
} ntfs_record_header_t;

// Intestazione Generica Attributo MFT
typedef struct __attribute__((packed)) {
    uint32_t type;                   // es. NTFS_ATTR_DATA (0x80)
    uint32_t total_length;
    uint8_t  non_resident;           // 0 = Residente, 1 = Non Residente
    uint8_t  name_length;
    uint16_t name_offset;
    uint16_t flags;
    uint16_t attribute_id;
    union {
        // Attributo Residente (dati contenuti internamente nel record MFT)
        struct __attribute__((packed)) {
            uint32_t value_length;
            uint16_t value_offset;
            uint8_t  indexed_flag;
            uint8_t  reserved;
        } res;
        // Attributo Non-Residente (dati mappati in cluster esterni tramite runlist)
        struct __attribute__((packed)) {
            uint64_t lowest_vcn;
            uint64_t highest_vcn;
            uint16_t mapping_pairs_offset;
            uint8_t  compression_unit;
            uint8_t  reserved[5];
            uint64_t allocated_size;
            uint64_t real_size;      // Dimensione reale file
            uint64_t initialized_size;
        } non_res;
    };
} ntfs_attr_header_t;

// Attributo $FILE_NAME (0x30)
typedef struct __attribute__((packed)) {
    uint64_t parent_dir_mft_ref;
    uint64_t creation_time;
    uint64_t modification_time;
    uint64_t mft_change_time;
    uint64_t read_time;
    uint64_t allocated_size;
    uint64_t real_size;
    uint32_t flags;                  // 0x10000000 = Directory, 0x20 = Archive
    uint32_t ea_reparse;
    uint8_t  filename_length;        // In caratteri UTF-16
    uint8_t  filename_namespace;
    uint16_t filename[1];            // Caratteri UTF-16LE
} ntfs_filename_attr_t;

// Intestazione $INDEX_ROOT (0x90)
typedef struct __attribute__((packed)) {
    uint32_t attr_type;              // Tipicamente NTFS_ATTR_FILE_NAME (0x30)
    uint32_t collation_rule;
    uint32_t bytes_per_index_record;
    uint8_t  clusters_per_index_record;
    uint8_t  reserved[3];
    // Index Header
    uint32_t entries_offset;
    uint32_t total_length;
    uint32_t allocated_length;
    uint8_t  flags;
    uint8_t  reserved2[3];
} ntfs_index_root_t;

// Voce di Indice Cartella NTFS
typedef struct __attribute__((packed)) {
    uint64_t mft_reference;
    uint16_t entry_length;
    uint16_t content_length;
    uint32_t flags;                  // 0x01 = Ha sub-nodo
    uint8_t  stream[1];              // Contiene $FILE_NAME
} ntfs_index_entry_t;

// Stato del Volume NTFS Montato
typedef struct {
    uint64_t partition_lba;
    uint16_t bytes_per_sec;
    uint8_t  sec_per_clust;
    uint32_t bytes_per_clust;
    uint32_t mft_record_size;
    uint64_t mft_start_lba;
    uint64_t total_sectors;
    char     volume_label[32];
    bool     is_usb;                 // true = USB MSC, false = ATA
    uint32_t device_id;
} ntfs_volume_t;

// Inizializza il driver NTFS-3G e lo registra nel VFS
void ntfs_init(void);

// Rileva se la partizione o unità specificata è formattata in NTFS
bool ntfs_probe_partition(bool is_usb, uint32_t dev_id, uint64_t part_lba, ntfs_boot_sector_t* out_bs);

// Restituisce il driver VFS per NTFS
const vfs_driver_t* ntfs_get_vfs_driver(void);

#endif // FS_NTFS_H
