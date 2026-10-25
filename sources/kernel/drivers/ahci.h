#ifndef DRIVERS_AHCI_H
#define DRIVERS_AHCI_H

#include <types.h>
#include <drivers/pci.h>

#define AHCI_PCI_CLASS          0x01
#define AHCI_PCI_SUBCLASS       0x06
#define AHCI_PCI_PROGIF         0x01

#define AHCI_MAX_PORTS          32
#define AHCI_SUPPORTED_PORTS    4

// Firme dei tipi di dispositivo (SIG register)
#define AHCI_DEV_NULL           0x00000000
#define AHCI_DEV_SATA           0x00000101 // Hard Disk / SSD SATA
#define AHCI_DEV_SATAPI         0xEB140101 // Unità ottica CD/DVD SATAPI
#define AHCI_DEV_SEMB           0xC33C0101 // Enclosure Management Bridge
#define AHCI_DEV_PM             0x96690101 // Port Multiplier

// FIS Types standard Serial ATA
#define FIS_TYPE_REG_H2D        0x27 // Register FIS - Host to Device
#define FIS_TYPE_REG_D2H        0x34 // Register FIS - Device to Host
#define FIS_TYPE_DMA_ACT        0x39 // DMA Activate FIS
#define FIS_TYPE_DMA_SETUP      0x41 // DMA Setup FIS
#define FIS_TYPE_DATA           0x46 // Data FIS
#define FIS_TYPE_PIO_SETUP      0x5F // PIO Setup FIS

// Comandi ATA/ATAPI standard
#define ATA_CMD_READ_DMA_EXT    0x25
#define ATA_CMD_WRITE_DMA_EXT   0x35
#define ATA_CMD_READ_DMA        0xC8
#define ATA_CMD_WRITE_DMA       0xCA
#define ATA_CMD_IDENTIFY        0xEC
#define ATA_CMD_IDENTIFY_PACKET 0xA1
#define ATA_CMD_PACKET          0xA0

// Command Header AHCI (32 byte per slot)
typedef struct __attribute__((packed)) {
    uint8_t  cfl:5;         // Command FIS Length in DWORDS (5 per H2D)
    uint8_t  a:1;           // ATAPI
    uint8_t  w:1;           // Write: 1=Host to Device, 0=Device to Host
    uint8_t  p:1;           // Prefetchable
    uint8_t  r:1;           // Reset
    uint8_t  b:1;           // BIST
    uint8_t  c:1;           // Clear busy upon R_OK
    uint8_t  reserved0:1;
    uint8_t  pmp:4;         // Port Multiplier Port
    uint16_t prdtl;         // Physical Region Descriptor Table Length
    volatile uint32_t prdbc;// PRD Byte Count trasferito
    uint32_t ctba;          // Command Table Descriptor Base Address Low
    uint32_t ctbau;         // Command Table Descriptor Base Address High
    uint32_t reserved1[4];
} ahci_cmd_header_t;

// PRDT Entry (16 byte)
typedef struct __attribute__((packed)) {
    uint32_t dba;           // Data Base Address Low
    uint32_t dbau;          // Data Base Address High
    uint32_t reserved0;
    uint32_t dbc:22;        // Data Byte Count (0-based: byte - 1)
    uint32_t reserved1:9;
    uint32_t i:1;           // Interrupt on completion
} ahci_prdt_entry_t;

// Command Table AHCI (128-byte aligned)
typedef struct __attribute__((packed)) {
    uint8_t  cfis[64];      // Command FIS
    uint8_t  acmd[16];      // ATAPI Command (12 o 16 byte)
    uint8_t  reserved[48];
    ahci_prdt_entry_t prdt_entry[4]; // Fino a 4 entry PRDT (sufficienti per 16 MB per comando)
} ahci_cmd_table_t;

// FIS Host to Device (20 byte)
typedef struct __attribute__((packed)) {
    uint8_t  fis_type;      // FIS_TYPE_REG_H2D (0x27)
    uint8_t  pmport:4;      // Port multiplier
    uint8_t  reserved0:3;
    uint8_t  c:1;           // 1: Command, 0: Control
    uint8_t  command;       // ATA Command code
    uint8_t  featurel;      // Feature low
    uint8_t  lba0;          // LBA 0..7
    uint8_t  lba1;          // LBA 8..15
    uint8_t  lba2;          // LBA 16..23
    uint8_t  device;        // Device register (1<<6 per LBA)
    uint8_t  lba3;          // LBA 24..31
    uint8_t  lba4;          // LBA 32..39
    uint8_t  lba5;          // LBA 40..47
    uint8_t  featureh;      // Feature high
    uint8_t  countl;        // Sector count 0..7
    uint8_t  counth;        // Sector count 8..15
    uint8_t  icc;           // Isochronous command completion
    uint8_t  control;       // Control register
    uint8_t  reserved1[4];
} fis_reg_h2d_t;

// Registri della porta AHCI (128 byte per porta)
typedef volatile struct __attribute__((packed)) {
    uint32_t clb;           // 0x00: Command List Base Address (1K-aligned)
    uint32_t clbu;          // 0x04: Command List Base Address High
    uint32_t fb;            // 0x08: FIS Base Address (256-byte aligned)
    uint32_t fbu;           // 0x0C: FIS Base Address High
    uint32_t is;            // 0x10: Interrupt Status
    uint32_t ie;            // 0x14: Interrupt Enable
    uint32_t cmd;           // 0x18: Command and Status
    uint32_t reserved0;     // 0x1C
    uint32_t tfd;           // 0x20: Task File Data
    uint32_t sig;           // 0x24: Signature
    uint32_t ssts;          // 0x28: SATA Status (SCR0)
    uint32_t sctl;          // 0x2C: SATA Control (SCR1)
    uint32_t serr;          // 0x30: SATA Error (SCR2)
    uint32_t sact;          // 0x34: SATA Active (SCR3)
    uint32_t ci;            // 0x38: Command Issue
    uint32_t sntf;          // 0x3C: SATA Notification (SCR4)
    uint32_t fbs;           // 0x40: FIS-based Switching Control
    uint32_t reserved1[11];
    uint32_t vendor[4];
} ahci_port_regs_t;

// Generic Host Control (HBA Memory Registers)
typedef volatile struct __attribute__((packed)) {
    uint32_t cap;           // 0x00: Host Capabilities
    uint32_t ghc;           // 0x04: Global Host Control
    uint32_t is;            // 0x08: Interrupt Status
    uint32_t pi;            // 0x0C: Ports Implemented (bitmask)
    uint32_t vs;            // 0x10: Version
    uint32_t ccc_ctl;       // 0x14: Command Completion Coalescing Control
    uint32_t ccc_pts;       // 0x18: Command Completion Coalescing Ports
    uint32_t em_loc;        // 0x1C: Enclosure Management Location
    uint32_t em_ctl;        // 0x20: Enclosure Management Control
    uint32_t cap2;          // 0x24: Host Capabilities Extended
    uint32_t bohc;          // 0x28: BIOS/OS Handoff Control and Status
    uint8_t  reserved[0x74];
    uint8_t  vendor[0x60];
    ahci_port_regs_t ports[32]; // 0x100..0x10FF
} ahci_hba_mem_t;

// Struttura descrittiva dispositivo SATA AHCI
typedef struct {
    bool     present;
    bool     is_atapi;
    uint8_t  port_num;
    char     model[48];
    char     serial[24];
    char     firmware[12];
    uint64_t total_sectors;
    uint64_t size_bytes;
    uint32_t size_mb;
    uint32_t size_gb;
    bool     has_mbr;
    uint8_t  part_type;
    uint32_t part_lba;
    uint32_t part_sectors;
    uint32_t boot_lba;
    char     part_fs[16];
} ahci_device_t;

// Inizializzazione e controllo stato
bool ahci_init(void);
bool ahci_is_active(void);
int  ahci_get_device_count(void);
const ahci_device_t* ahci_get_device(int index);
const ahci_device_t* ahci_get_primary_device(void);

// I/O a blocchi ad alte prestazioni (DMA)
bool ahci_read_sectors(uint32_t port_num, uint64_t lba, uint32_t count, void* buffer);
bool ahci_write_sectors(uint32_t port_num, uint64_t lba, uint32_t count, const void* buffer);
bool ahci_read_primary_sectors(uint64_t lba, uint32_t count, void* buffer);
bool ahci_write_primary_sectors(uint64_t lba, uint32_t count, const void* buffer);
bool ahci_read_cd_sector(uint32_t port_num, uint32_t cd_lba, void* buf2048);

// Gestione Asincrona I/O tramite Interrupt
void ahci_enable_interrupts(void);
bool ahci_is_interrupt_active(void);

#endif // DRIVERS_AHCI_H
