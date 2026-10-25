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

// Callback per lettura settori con indirizzamento LBA a 64-bit
typedef bool (*sector_reader_fn)(void* ctx, uint64_t lba, uint32_t count, void* buf);

// Header GPT standard (LBA 1, 92 byte minimi)
typedef struct __attribute__((packed)) {
    char     signature[8];           // "EFI PART" (0x5452415020494645ULL)
    uint32_t revision;              // es. 0x00010000 (1.0)
    uint32_t header_size;           // Tipicamente 92 byte
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t current_lba;          // LBA 1
    uint64_t backup_lba;           // Ultimo settore del disco
    uint64_t first_usable_lba;     // Tipicamente 34
    uint64_t last_usable_lba;
    uint8_t  disk_guid[16];
    uint64_t partition_entries_lba;// Tipicamente 2
    uint32_t num_partition_entries;// Tipicamente 128
    uint32_t partition_entry_size; // Tipicamente 128 byte
    uint32_t partition_entries_crc32;
} gpt_header_t;

// Voce di partizione GPT standard (128 byte)
typedef struct __attribute__((packed)) {
    uint8_t  type_guid[16];        // GUID tipo di partizione (Basic Data, ESP, ecc.)
    uint8_t  unique_guid[16];      // GUID univoco di partizione
    uint64_t starting_lba;         // Settore iniziale (64-bit)
    uint64_t ending_lba;           // Settore finale inclusivo (64-bit)
    uint64_t attributes;           // Flag attributi
    uint16_t name[36];             // Nome partizione UTF-16LE
} gpt_entry_t;

// Esegue la scansione a 64-bit della tabella GPT su disco fisico
bool gpt_probe_table(sector_reader_fn reader, void* ctx, uint64_t* out_lba, uint64_t* out_sectors, char* out_fs_name, size_t fs_len);

// Esegue il rilevamento automatico del filesystem nativo presente su un supporto USB Mass Storage
fs_type_t fs_probe_usb_device(uint64_t* out_part_lba, char* out_fs_name, size_t fs_name_len, char* out_label, size_t label_len);

// Esegue il rilevamento automatico del filesystem nativo presente su un'unità ottica ATAPI (CD/DVD/Blu-Ray)
fs_type_t fs_probe_optical_device(const ata_device_t* dev, char* out_fs_name, size_t fs_name_len, char* out_label, size_t label_len);

// Converte l'enumeratore del filesystem in stringa leggibile
const char* fs_type_to_str(fs_type_t type);

#endif // FS_PROBE_H
