#ifndef FS_UDF_H
#define FS_UDF_H

#include <types.h>
#include <fs/vfs.h>
#include <fs/vfs_mount.h>
#include <drivers/ata.h>

#define UDF_SECTOR_SIZE          2048
#define UDF_AVDP_LBA_PRIMARY     256
#define UDF_VRS_START_LBA        16

// Tag Identifiers ECMA-167 / UDF
#define UDF_TAG_PVD              0x0001
#define UDF_TAG_AVDP             0x0002
#define UDF_TAG_VDP              0x0003
#define UDF_TAG_IUVD             0x0004
#define UDF_TAG_PD               0x0005
#define UDF_TAG_LVD              0x0006
#define UDF_TAG_USD              0x0007
#define UDF_TAG_TD               0x0008
#define UDF_TAG_LVID             0x0009
#define UDF_TAG_FSD              0x0100
#define UDF_TAG_FID              0x0101
#define UDF_TAG_AED              0x0102
#define UDF_TAG_FE               0x0105  // UDF 1.02 / 1.50 File Entry
#define UDF_TAG_EFE              0x010A  // UDF 2.50 Extended File Entry (Blu-Ray)

// Caratteristiche file in File Identifier Descriptor (FID)
#define UDF_FID_CHAR_EXISTENCE   0x01
#define UDF_FID_CHAR_DIRECTORY   0x02
#define UDF_FID_CHAR_DELETED     0x04
#define UDF_FID_CHAR_PARENT      0x08

typedef enum {
    UDF_VER_UNKNOWN = 0,
    UDF_VER_102,        // DVD-Video, DVD-ROM standard
    UDF_VER_150,        // CD-R/RW, DVD-RAM
    UDF_VER_201,        // UDF 2.01
    UDF_VER_250,        // Blu-Ray BD-ROM, BDMV video, AVCHD
    UDF_VER_BRIDGE      // ISO 9660 / UDF-Bridge
} udf_version_t;

// Tag Descriptor (16 byte)
typedef struct __attribute__((packed)) {
    uint16_t tag_id;
    uint16_t tag_version;
    uint8_t  tag_checksum;
    uint8_t  reserved;
    uint16_t tag_serial_number;
    uint16_t descriptor_crc;
    uint16_t descriptor_crc_len;
    uint32_t tag_location;
} udf_tag_t;

// Extent Address
typedef struct __attribute__((packed)) {
    uint32_t len;
    uint32_t loc;
} udf_extent_ad_t;

// Long Allocation Descriptor
typedef struct __attribute__((packed)) {
    uint32_t len;
    uint32_t lba;
    uint16_t partition_ref;
    uint8_t  impl_use[6];
} udf_long_ad_t;

// Short Allocation Descriptor
typedef struct __attribute__((packed)) {
    uint32_t len;
    uint32_t pos;
} udf_short_ad_t;

// Anchor Volume Descriptor Pointer (AVDP)
typedef struct __attribute__((packed)) {
    udf_tag_t       tag;
    udf_extent_ad_t main_vds_extent;
    udf_extent_ad_t reserve_vds_extent;
    uint8_t         reserved[480];
} udf_avdp_t;

// Logical Volume Descriptor (LVD)
typedef struct __attribute__((packed)) {
    udf_tag_t     tag;
    uint32_t      vds_number;
    uint8_t       char_set_list[128];
    char          logical_vol_id[128];
    uint32_t      logical_block_size;
    uint8_t       domain_id[32];
    udf_long_ad_t logical_vol_content_use; // Points to FSD
    uint32_t      map_table_length;
    uint32_t      num_partition_maps;
    uint8_t       impl_id[32];
    uint8_t       impl_use[128];
    udf_extent_ad_t integrity_seq_extent;
    uint8_t       partition_maps[1];
} udf_lvd_t;

// File Set Descriptor (FSD)
typedef struct __attribute__((packed)) {
    udf_tag_t     tag;
    uint8_t       recording_date_and_time[12];
    uint16_t      interchange_level;
    uint16_t      max_interchange_level;
    uint32_t      char_set_list;
    uint32_t      max_char_set_list;
    uint32_t      file_set_number;
    uint32_t      file_set_descriptor_number;
    uint8_t       logical_vol_id_char_set[128];
    char          file_set_id[32];
    uint8_t       copyright_file_id[32];
    uint8_t       abstract_file_id[32];
    udf_long_ad_t root_directory_icb;       // Root Directory File Entry (FE/EFE)
    uint8_t       domain_id[32];
    udf_long_ad_t next_extent;
    udf_long_ad_t system_stream_dir_icb;
    uint8_t       reserved[32];
} udf_fsd_t;

// File Entry (FE - UDF 1.02 / 1.50)
typedef struct __attribute__((packed)) {
    udf_tag_t tag;
    uint8_t   icb_tag[20];
    uint32_t  uid;
    uint32_t  gid;
    uint32_t  permissions;
    uint16_t  file_link_count;
    uint8_t   record_format;
    uint8_t   record_display_attributes;
    uint32_t  record_length;
    uint64_t  information_length;           // Dimensione file in byte
    uint64_t  logical_blocks_recorded;
    uint8_t   access_time[12];
    uint8_t   modification_time[12];
    uint8_t   attribute_time[12];
    uint32_t  checkpoint;
    udf_long_ad_t extended_attribute_icb;
    uint8_t   impl_id[32];
    uint64_t  unique_id;
    uint32_t  length_of_extended_attributes;
    uint32_t  length_of_allocation_descriptors;
    uint8_t   data[1]; // Extended attributes + Allocation Descriptors
} udf_fe_t;

// Extended File Entry (EFE - UDF 2.50 per Blu-Ray)
typedef struct __attribute__((packed)) {
    udf_tag_t tag;
    uint8_t   icb_tag[20];
    uint32_t  uid;
    uint32_t  gid;
    uint32_t  permissions;
    uint16_t  file_link_count;
    uint8_t   record_format;
    uint8_t   record_display_attributes;
    uint32_t  record_length;
    uint64_t  information_length;           // Dimensione file in byte
    uint64_t  object_size;
    uint64_t  logical_blocks_recorded;
    uint8_t   access_time[12];
    uint8_t   modification_time[12];
    uint8_t   attribute_time[12];
    uint32_t  checkpoint;
    uint32_t  reserved;
    udf_long_ad_t extended_attribute_icb;
    udf_long_ad_t stream_directory_icb;
    uint8_t   impl_id[32];
    uint64_t  unique_id;
    uint32_t  length_of_extended_attributes;
    uint32_t  length_of_allocation_descriptors;
    uint8_t   data[1];
} udf_efe_t;

// File Identifier Descriptor (FID)
typedef struct __attribute__((packed)) {
    udf_tag_t     tag;
    uint16_t      file_version_number;
    uint8_t       file_characteristics;
    uint8_t       length_of_file_identifier;
    udf_long_ad_t icb;                      // Punta al FE del file
    uint16_t      length_of_impl_use;
    uint8_t       data[1];                  // Implementation Use + File Identifier
} udf_fid_t;

// Stato volume UDF montato
typedef struct {
    const ata_device_t* dev;
    bool                is_bridge;          // true = ISO 9660 / UDF-Bridge
    udf_version_t       version;            // 1.02, 1.50, 2.50, Bridge
    uint32_t            partition_start_lba;
    uint32_t            root_dir_lba;
    uint64_t            root_dir_size;
    char                vol_label[64];
} udf_volume_t;

// Inizializza il driver UDF e lo registra nel VFS
void udf_init(void);

// Rileva se il disco ottico o immagine è UDF / UDF-Bridge e ne determina la versione
bool udf_probe_device(const ata_device_t* dev, udf_version_t* out_ver, bool* out_is_bridge);

// Restituisce il driver VFS per UDF
const vfs_driver_t* udf_get_vfs_driver(void);

#endif // FS_UDF_H
