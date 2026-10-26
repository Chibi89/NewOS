#ifndef DRIVERS_ATA_H
#define DRIVERS_ATA_H

#include <types.h>

#define MAX_ATA_DEVICES 4

typedef struct ata_device_s {
    bool     present;
    bool     is_atapi;      // true = CD/DVD drive (ATAPI), false = Hard Disk (ATA)
    uint8_t  channel;       // 0 = Primary (0x1F0), 1 = Secondary (0x170)
    uint8_t  drive;         // 0 = Master, 1 = Slave
    uint16_t io_base;       // 0x1F0 o 0x170
    char     model[48];     // Modello reale (es. "VBOX HARDDISK", "QEMU HARDDISK")
    char     serial[24];    // Numero di serie reale
    char     firmware[12];  // Versione firmware
    uint64_t total_sectors; // Numero reale totale di settori a 512 byte
    uint64_t size_bytes;    // Dimensione reale in byte
    uint32_t size_mb;       // Dimensione reale in MB
    uint32_t size_gb;       // Dimensione reale in GB
    bool     has_mbr;       // Tabella MBR con firma 0xAA55 presente
    bool     has_gpt;       // Tabella GPT (GUID Partition Table) a 64-bit presente
    uint8_t  part_type;     // Tipo partizione primaria (es. 0x07 exFAT, 0xEE GPT)
    uint64_t part_lba;      // LBA iniziale della partizione 1 (64-bit per GPT e LBA48)
    uint64_t part_sectors;  // Numero di settori della partizione 1 (64-bit)
    uint32_t boot_lba;      // Settore CD LBA di avvio El Torito (es. 31138 o 32)
    char     part_fs[16];   // "exFAT", "FAT32", "NTFS", "RAW"
    bool     is_ahci;       // true = Dispositivo controllato tramite Serial ATA AHCI (prioritario)
    uint8_t  ahci_port;     // Numero porta AHCI (0..31)
} ata_device_t;

// Verifica se il controller AHCI Serial ATA è attivo come driver primario prioritario
bool storage_is_ahci_active(void);

// Inizializza il sottosistema ATA ed esegue la scansione reale delle periferiche collegate (priorità AHCI)
void ata_init(void);

// Restituisce il numero di unità disco/CD rilevate (0..4)
int ata_get_device_count(void);

// Restituisce le informazioni di una specifica periferica rilevata (0..3)
const ata_device_t* ata_get_device(int index);

// Restituisce il disco fisso primario di sistema (Primary Master 0x1F0)
const ata_device_t* ata_get_primary_master(void);

// Lettura e scrittura a basso livello (sul disco primario di boot con supporto 64-bit LBA)
bool ata_read_sectors(uint64_t lba, uint32_t count, void* buffer);
bool ata_write_sectors(uint64_t lba, uint32_t count, const void* buffer);

// Lettura e scrittura da una specifica unità con supporto 64-bit LBA
bool ata_read_device_sectors(const ata_device_t* dev, uint64_t lba, uint32_t count, void* buffer);
bool ata_write_device_sectors(const ata_device_t* dev, uint64_t lba, uint32_t count, const void* buffer);

// Lettura diretta blocco da 2048 byte da unità ottica ATAPI (CD-ROM / DVD / Blu-Ray)
bool atapi_read_cd_sector(const ata_device_t* dev, uint32_t cd_lba, void* buf2048);
bool atapi_read_cd_sectors(const ata_device_t* dev, uint32_t cd_lba, uint32_t count, void* buffer);

typedef enum {
    OPTICAL_DISC_NONE = 0,
    OPTICAL_DISC_CD,       // CD-ROM / CD-R (≤ 900 MB)
    OPTICAL_DISC_DVD,      // DVD-ROM / DVD±R / DVD-RW (4.7 GB - 8.5 GB)
    OPTICAL_DISC_BLURAY    // Blu-Ray BD-ROM / BD-R / BDXL (25 GB - 128 GB)
} optical_disc_type_t;

// Lettura capacità reale disco ottico tramite SCSI READ CAPACITY (10)
bool atapi_read_capacity(const ata_device_t* dev, uint32_t* total_sectors, uint32_t* sector_size);

// Restituisce la tipologia di supporto inserito (CD, DVD, Blu-Ray)
optical_disc_type_t atapi_get_disc_type(const ata_device_t* dev);
const char*         atapi_get_disc_type_name(const ata_device_t* dev);

#endif // DRIVERS_ATA_H
