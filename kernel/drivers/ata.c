#include <drivers/ata.h>
#include <drivers/ahci.h>
#include <fs/fs_probe.h>
#include <arch/x86_64/io.h>
#include <string.h>

bool storage_is_ahci_active(void) {
    return ahci_is_active();
}

bool ata_read_device_sectors(const ata_device_t* dev, uint64_t lba, uint32_t count, void* buffer);
static bool ata_probe_sector_reader(void* ctx, uint64_t lba, uint32_t count, void* buf) {
    return ata_read_device_sectors((const ata_device_t*)ctx, lba, count, buf);
}

#define ATA_PRIMARY_IO      0x1F0
#define ATA_SECONDARY_IO    0x170

#define ATA_REG_DATA        0x00
#define ATA_REG_ERROR       0x01
#define ATA_REG_SEC_COUNT   0x02
#define ATA_REG_LBA_LO      0x03
#define ATA_REG_LBA_MID     0x04
#define ATA_REG_LBA_HI      0x05
#define ATA_REG_DRIVE_SEL   0x06
#define ATA_REG_STATUS      0x07
#define ATA_REG_COMMAND     0x07

#define ATA_CMD_READ_PIO    0x20
#define ATA_CMD_WRITE_PIO   0x30
#define ATA_CMD_IDENTIFY    0xEC
#define ATA_CMD_IDENTIFY_PKT 0xA1
#define ATA_CMD_FLUSH       0xE7

static ata_device_t g_devices[MAX_ATA_DEVICES];
static int g_device_count = 0;
static bool g_ata_initialized = false;

static void ata_wait_bsy_port(uint16_t io_base) {
    uint32_t timeout = 100000;
    while ((inb(io_base + ATA_REG_STATUS) & 0x80) && timeout--) {
        io_wait();
    }
}

static bool ata_wait_drq_port(uint16_t io_base) {
    uint32_t timeout = 500000;
    while (timeout--) {
        uint8_t status = inb(io_base + ATA_REG_STATUS);
        if (status & 0x01) return false; // ERR bit
        if (status & 0x08) return true;  // DRQ bit
        io_wait();
    }
    return false;
}

static void ata_clean_string(char* dst, const uint16_t* src, int word_count) {
    int pos = 0;
    for (int i = 0; i < word_count; i++) {
        uint16_t w = src[i];
        dst[pos++] = (char)(w >> 8);
        dst[pos++] = (char)(w & 0xFF);
    }
    dst[pos] = '\0';

    // Rimuove spazi bianchi finali
    while (pos > 0 && dst[pos - 1] == ' ') {
        dst[--pos] = '\0';
    }
}

bool atapi_read_cd_sector(const ata_device_t* dev, uint32_t cd_lba, void* buf2048) {
    if (!dev || !buf2048) return false;
    if (dev->is_ahci) {
        return ahci_read_cd_sector(dev->ahci_port, cd_lba, buf2048);
    }
    uint16_t io = dev->io_base;
    uint8_t sel = (dev->drive == 0) ? 0xA0 : 0xB0;

    outb(io + ATA_REG_DRIVE_SEL, sel);
    for (int i = 0; i < 4; i++) inb(io + ATA_REG_STATUS);
    ata_wait_bsy_port(io);

    outb(io + ATA_REG_ERROR, 0); // PIO mode
    outb(io + ATA_REG_SEC_COUNT, 0);
    outb(io + ATA_REG_LBA_LO, 0);
    outb(io + ATA_REG_LBA_MID, 2048 & 0xFF);
    outb(io + ATA_REG_LBA_HI, (2048 >> 8) & 0xFF);

    outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PKT); // 0xA0 (ATAPI PACKET)
    for (int i = 0; i < 4; i++) inb(io + ATA_REG_STATUS);

    if (!ata_wait_drq_port(io)) return false;

    // Pacchetto SCSI READ (10) di 12 byte (6 word)
    uint8_t pkt[12];
    memset(pkt, 0, 12);
    pkt[0] = 0x28; // SCSI READ (10)
    pkt[2] = (uint8_t)((cd_lba >> 24) & 0xFF);
    pkt[3] = (uint8_t)((cd_lba >> 16) & 0xFF);
    pkt[4] = (uint8_t)((cd_lba >> 8) & 0xFF);
    pkt[5] = (uint8_t)(cd_lba & 0xFF);
    pkt[7] = 0;
    pkt[8] = 1; // 1 settore CD (2048 byte)

    uint16_t* p16 = (uint16_t*)pkt;
    for (int i = 0; i < 6; i++) {
        outw(io + ATA_REG_DATA, p16[i]);
    }

    ata_wait_bsy_port(io);
    if (!ata_wait_drq_port(io)) return false;

    uint16_t word_count = (inb(io + ATA_REG_LBA_MID) | (inb(io + ATA_REG_LBA_HI) << 8)) / 2;
    if (word_count == 0) word_count = 1024;

    uint16_t* dst = (uint16_t*)buf2048;
    for (uint16_t i = 0; i < word_count; i++) {
        dst[i] = inw(io + ATA_REG_DATA);
    }
    return true;
}

bool atapi_read_capacity(const ata_device_t* dev, uint32_t* total_sectors, uint32_t* sector_size) {
    if (!dev || !dev->is_atapi) return false;
    uint16_t io = dev->io_base;
    uint8_t sel = (dev->drive == 0) ? 0xA0 : 0xB0;

    outb(io + ATA_REG_DRIVE_SEL, sel);
    for (int i = 0; i < 4; i++) inb(io + ATA_REG_STATUS);
    ata_wait_bsy_port(io);

    outb(io + ATA_REG_ERROR, 0); // PIO mode
    outb(io + ATA_REG_SEC_COUNT, 0);
    outb(io + ATA_REG_LBA_LO, 0);
    outb(io + ATA_REG_LBA_MID, 8 & 0xFF);
    outb(io + ATA_REG_LBA_HI, (8 >> 8) & 0xFF);

    outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PKT); // 0xA0 (ATAPI PACKET)
    for (int i = 0; i < 4; i++) inb(io + ATA_REG_STATUS);

    if (!ata_wait_drq_port(io)) return false;

    // Pacchetto SCSI READ CAPACITY (10) di 12 byte
    uint8_t pkt[12];
    memset(pkt, 0, 12);
    pkt[0] = 0x25; // SCSI READ CAPACITY (10)

    uint16_t* p16 = (uint16_t*)pkt;
    for (int i = 0; i < 6; i++) {
        outw(io + ATA_REG_DATA, p16[i]);
    }

    ata_wait_bsy_port(io);
    if (!ata_wait_drq_port(io)) return false;

    uint16_t cap_raw[4];
    for (int i = 0; i < 4; i++) {
        cap_raw[i] = inw(io + ATA_REG_DATA);
    }

    uint8_t* b = (uint8_t*)cap_raw;
    uint32_t last_lba = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
    uint32_t blk_size = ((uint32_t)b[4] << 24) | ((uint32_t)b[5] << 16) | ((uint32_t)b[6] << 8) | (uint32_t)b[7];
    if (blk_size == 0) blk_size = 2048;

    if (total_sectors) *total_sectors = last_lba + 1;
    if (sector_size) *sector_size = blk_size;
    return true;
}

optical_disc_type_t atapi_get_disc_type(const ata_device_t* dev) {
    if (!dev || !dev->is_atapi) return OPTICAL_DISC_NONE;
    if (dev->size_mb > 9000) return OPTICAL_DISC_BLURAY;
    if (dev->size_mb > 900)  return OPTICAL_DISC_DVD;
    return OPTICAL_DISC_CD;
}

const char* atapi_get_disc_type_name(const ata_device_t* dev) {
    optical_disc_type_t t = atapi_get_disc_type(dev);
    switch (t) {
        case OPTICAL_DISC_BLURAY: return "Blu-Ray Disc (BD-ROM)";
        case OPTICAL_DISC_DVD:    return "DVD-ROM / DVD+-R";
        case OPTICAL_DISC_CD:     return "CD-ROM / CD-R";
        default:                  return "Nessun disco";
    }
}

static bool ata_identify_drive(uint8_t channel, uint8_t drive, ata_device_t* dev) {
    uint16_t io = (channel == 0) ? ATA_PRIMARY_IO : ATA_SECONDARY_IO;
    uint8_t sel = (drive == 0) ? 0xA0 : 0xB0;

    memset(dev, 0, sizeof(ata_device_t));
    dev->channel = channel;
    dev->drive = drive;
    dev->io_base = io;

    // 1. Seleziona unità
    outb(io + ATA_REG_DRIVE_SEL, sel);
    for (int i = 0; i < 4; i++) inb(io + ATA_REG_STATUS); // 400ns delay

    // Se bus fluttuante (0xFF), non c'è controller o linea aperta
    uint8_t st = inb(io + ATA_REG_STATUS);
    if (st == 0xFF) return false;

    // 2. Azzera registri prima del comando IDENTIFY
    outb(io + ATA_REG_SEC_COUNT, 0);
    outb(io + ATA_REG_LBA_LO, 0);
    outb(io + ATA_REG_LBA_MID, 0);
    outb(io + ATA_REG_LBA_HI, 0);

    // 3. Invia comando ATA IDENTIFY (0xEC)
    outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    for (int i = 0; i < 4; i++) inb(io + ATA_REG_STATUS);

    st = inb(io + ATA_REG_STATUS);
    if (st == 0) return false; // Unità non presente

    ata_wait_bsy_port(io);

    // Controlla se periferica ATAPI (CD-ROM / DVD)
    uint8_t mid = inb(io + ATA_REG_LBA_MID);
    uint8_t hi  = inb(io + ATA_REG_LBA_HI);
    bool is_atapi = false;

    if ((mid == 0x14 && hi == 0xEB) || (mid == 0x69 && hi == 0x96)) {
        is_atapi = true;
        // Invia IDENTIFY PACKET DEVICE (0xA1) per ATAPI
        outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PKT);
        for (int i = 0; i < 4; i++) inb(io + ATA_REG_STATUS);
        ata_wait_bsy_port(io);
    }

    if (!ata_wait_drq_port(io)) {
        return false;
    }

    // 4. Legge il blocco dati IDENTIFY (256 word = 512 byte)
    uint16_t id_buf[256];
    for (int i = 0; i < 256; i++) {
        id_buf[i] = inw(io + ATA_REG_DATA);
    }

    dev->present = true;
    dev->is_atapi = is_atapi;

    // Decodifica Serial (word 10..19), Firmware (word 23..26), Model (word 27..46)
    ata_clean_string(dev->serial, &id_buf[10], 10);
    ata_clean_string(dev->firmware, &id_buf[23], 4);
    ata_clean_string(dev->model, &id_buf[27], 20);

    if (dev->model[0] == '\0') {
        strcpy(dev->model, is_atapi ? "Unita' CD-ROM/DVD" : "Hard Disk ATA");
    }

    // Calcolo capacità totale
    if (!is_atapi) {
        bool lba48 = (id_buf[83] & (1 << 10)) != 0;
        if (lba48) {
            uint64_t w100 = id_buf[100];
            uint64_t w101 = id_buf[101];
            uint64_t w102 = id_buf[102];
            uint64_t w103 = id_buf[103];
            dev->total_sectors = w100 | (w101 << 16) | (w102 << 32) | (w103 << 48);
        } else {
            dev->total_sectors = (uint32_t)id_buf[60] | ((uint32_t)id_buf[61] << 16);
        }

        dev->size_bytes = dev->total_sectors * 512ULL;
        dev->size_mb = (uint32_t)(dev->size_bytes / (1024ULL * 1024ULL));
        dev->size_gb = (uint32_t)(dev->size_mb / 1024);

        // 5. Ispezione Partizioni MBR reali su LBA 0
        uint8_t mbr[512];
        if (ata_read_device_sectors(dev, 0, 1, mbr)) {
            if (mbr[510] == 0x55 && mbr[511] == 0xAA) {
                dev->has_mbr = true;
                // Entry partizione 1 (offset 446 / 0x1BE)
                dev->part_type = mbr[446 + 4];
                dev->part_lba = *(uint32_t*)&mbr[446 + 8];
                dev->part_sectors = *(uint32_t*)&mbr[446 + 12];

                if (dev->part_type == 0xEE) {
                    // Partizione GPT Protettiva (UEFI / Dischi > 2TB)
                    uint64_t gpt_lba = 0, gpt_sec = 0;
                    char gpt_fs[16] = "exFAT";
                    if (gpt_probe_table(ata_probe_sector_reader, dev, &gpt_lba, &gpt_sec, gpt_fs, sizeof(gpt_fs))) {
                        dev->has_gpt = true;
                        dev->part_lba = gpt_lba;
                        dev->part_sectors = gpt_sec;
                        strncpy(dev->part_fs, gpt_fs, sizeof(dev->part_fs) - 1);
                    }
                } else if (dev->part_type == 0x07) {
                    // Controlla se exFAT verificando VBR a part_lba
                    uint8_t vbr[512];
                    if (ata_read_device_sectors(dev, dev->part_lba, 1, vbr)) {
                        if (memcmp(&vbr[3], "EXFAT   ", 8) == 0) {
                            strcpy(dev->part_fs, "exFAT");
                        } else if (memcmp(&vbr[3], "NTFS    ", 8) == 0) {
                            strcpy(dev->part_fs, "NTFS");
                        } else {
                            strcpy(dev->part_fs, "exFAT/NTFS");
                        }
                    } else {
                        strcpy(dev->part_fs, "exFAT");
                    }
                } else if (dev->part_type == 0x0B || dev->part_type == 0x0C) {
                    strcpy(dev->part_fs, "FAT32");
                } else if (dev->part_type == 0x04 || dev->part_type == 0x06 || dev->part_type == 0x0E) {
                    strcpy(dev->part_fs, "FAT16");
                } else if (dev->part_type != 0) {
                    strcpy(dev->part_fs, "RAW");
                }
            }
        }
    } else {
        // Unità ottica ATAPI (CD-ROM / DVD / Blu-Ray)
        uint32_t opt_sec = 0;
        uint32_t opt_bsz = 2048;
        if (atapi_read_capacity(dev, &opt_sec, &opt_bsz) && opt_sec > 0) {
            dev->total_sectors = opt_sec;
            dev->size_bytes = (uint64_t)opt_sec * (uint64_t)opt_bsz;
            dev->size_mb = (uint32_t)(dev->size_bytes / (1024ULL * 1024ULL));
            dev->size_gb = (uint32_t)(dev->size_mb / 1024);
        } else {
            dev->total_sectors = 332800; // 650 MB default CD-ROM
            dev->size_mb = 650;
            dev->size_bytes = 650ULL * 1024ULL * 1024ULL;
        }

        strcpy(dev->part_fs, "ISO 9660");

        // 1. Rileva il BOOT_LBA dal catalogo El Torito al settore 20
        uint32_t boot_lba = 32;
        uint8_t cat_sec[2048];
        if (atapi_read_cd_sector(dev, 20, cat_sec)) {
            uint32_t cand = *(uint32_t*)&cat_sec[40];
            if (cand > 0 && cand < 200000) {
                boot_lba = cand;
            }
        }
        dev->boot_lba = boot_lba;

        // 2. Verifica se presente disco ibrido el-torito con MBR/exFAT a boot_lba
        uint8_t cd_sector[2048];
        if (atapi_read_cd_sector(dev, boot_lba, cd_sector)) {
            if (cd_sector[510] == 0x55 && cd_sector[511] == 0xAA) {
                dev->has_mbr = true;
                dev->part_type = cd_sector[446 + 4];
                dev->part_lba = *(uint32_t*)&cd_sector[446 + 8];
                if (dev->part_type == 0x07) {
                    strcpy(dev->part_fs, "exFAT");
                }
            }
        }
    }

    return true;
}

void ata_init(void) {
    if (g_ata_initialized) return;
    memset(g_devices, 0, sizeof(g_devices));
    g_device_count = 0;

    // 1. TENTATIVO PRIORITARIO CONTROLLER SERIAL ATA AHCI 1.0 (DMA AD ALTE PRESTAZIONI)
    if (ahci_init()) {
        int ahci_cnt = ahci_get_device_count();
        for (int i = 0; i < ahci_cnt && g_device_count < MAX_ATA_DEVICES; i++) {
            const ahci_device_t* ad = ahci_get_device(i);
            if (!ad || !ad->present) continue;
            ata_device_t* dev = &g_devices[g_device_count++];
            memset(dev, 0, sizeof(ata_device_t));
            dev->present = true;
            dev->is_atapi = ad->is_atapi;
            dev->is_ahci = true;
            dev->ahci_port = ad->port_num;
            dev->channel = 0;
            dev->drive = (uint8_t)i;
            dev->io_base = 0;
            strncpy(dev->model, ad->model, sizeof(dev->model) - 1);
            strncpy(dev->serial, ad->serial, sizeof(dev->serial) - 1);
            strncpy(dev->firmware, ad->firmware, sizeof(dev->firmware) - 1);
            dev->total_sectors = ad->total_sectors;
            dev->size_bytes = ad->size_bytes;
            dev->size_mb = ad->size_mb;
            dev->size_gb = ad->size_gb;
            dev->has_mbr = ad->has_mbr;
            dev->has_gpt = ad->has_gpt;
            dev->part_type = ad->part_type;
            dev->part_lba = ad->part_lba;
            dev->part_sectors = ad->part_sectors;
            dev->boot_lba = ad->boot_lba;
            strncpy(dev->part_fs, ad->part_fs, sizeof(dev->part_fs) - 1);
        }
    }

    // 2. SCANSIONE FALLBACK CANALI LEGACY ATA PIO (se rimangono slot liberi)
    if (g_device_count < MAX_ATA_DEVICES) {
        for (uint8_t ch = 0; ch < 2; ch++) {
            for (uint8_t drv = 0; drv < 2; drv++) {
                ata_device_t tmp;
                if (ata_identify_drive(ch, drv, &tmp)) {
                    if (g_device_count < MAX_ATA_DEVICES) {
                        memcpy(&g_devices[g_device_count++], &tmp, sizeof(ata_device_t));
                    }
                }
            }
        }
    }

    g_ata_initialized = true;
}

int ata_get_device_count(void) {
    if (!g_ata_initialized) ata_init();
    return g_device_count;
}

const ata_device_t* ata_get_device(int index) {
    if (!g_ata_initialized) ata_init();
    if (index >= 0 && index < g_device_count) {
        return &g_devices[index];
    }
    return NULL;
}

const ata_device_t* ata_get_primary_master(void) {
    if (!g_ata_initialized) ata_init();
    // 1. Cercare prima un Hard Disk non-ATAPI presente sul canale primario master
    for (int i = 0; i < g_device_count; i++) {
        if (g_devices[i].channel == 0 && g_devices[i].drive == 0 && !g_devices[i].is_atapi && g_devices[i].present) {
            return &g_devices[i];
        }
    }
    // 2. Cercare QUALSIASI Hard Disk reale non-ATAPI presente (Primary Slave, Secondary Master/Slave)
    for (int i = 0; i < g_device_count; i++) {
        if (!g_devices[i].is_atapi && g_devices[i].present) {
            return &g_devices[i];
        }
    }
    // 3. Fallback all'unita' CD-ROM ATAPI (Live CD avviato in VirtualBox/VM)
    for (int i = 0; i < g_device_count; i++) {
        if (g_devices[i].present) return &g_devices[i];
    }
    if (g_device_count > 0) return &g_devices[0];
    return NULL;
}

bool ata_read_device_sectors(const ata_device_t* dev, uint64_t lba, uint32_t count, void* buffer) {
    if (!dev || !buffer || count == 0) return false;

    if (dev->is_ahci) {
        if (dev->is_atapi) {
            uint32_t base_sec = dev->boot_lba ? dev->boot_lba : 32;
            uint8_t cd_buf[2048];
            uint8_t* dst = (uint8_t*)buffer;
            for (uint32_t i = 0; i < count; i++) {
                uint64_t cur_lba = lba + i;
                uint32_t cd_sec = base_sec + (uint32_t)(cur_lba / 4);
                uint32_t sub_offset = (uint32_t)(cur_lba % 4) * 512;
                if (!ahci_read_cd_sector(dev->ahci_port, cd_sec, cd_buf)) {
                    return false;
                }
                memcpy(dst + i * 512, cd_buf + sub_offset, 512);
            }
            return true;
        }
        return ahci_read_sectors(dev->ahci_port, lba, count, buffer);
    }

    if (dev->is_atapi) {
        // Unità ATAPI CD-ROM (settori da 2048 byte, immagine disco a boot_lba)
        uint32_t base_sec = dev->boot_lba ? dev->boot_lba : 32;
        uint8_t cd_buf[2048];
        uint8_t* dst = (uint8_t*)buffer;
        for (uint32_t i = 0; i < count; i++) {
            uint64_t cur_lba = lba + i;
            uint32_t cd_sec = base_sec + (uint32_t)(cur_lba / 4);
            uint32_t sub_offset = (uint32_t)(cur_lba % 4) * 512;
            if (!atapi_read_cd_sector(dev, cd_sec, cd_buf)) {
                return false;
            }
            memcpy(dst + i * 512, cd_buf + sub_offset, 512);
        }
        return true;
    }

    uint16_t io = dev->io_base;
    uint8_t drive_sel = (dev->drive == 0) ? 0xE0 : 0xF0;
    uint16_t* ptr = (uint16_t*)buffer;

    while (count > 0) {
        uint32_t batch = (count > 255) ? 255 : count;

        ata_wait_bsy_port(io);
        if (lba > 0x0FFFFFFF) {
            // LBA48 PIO: invia byte alti prima dei byte bassi (Standard ATA-6+)
            outb(io + ATA_REG_DRIVE_SEL, drive_sel);
            outb(io + ATA_REG_SEC_COUNT, 0);
            outb(io + ATA_REG_SEC_COUNT, (uint8_t)batch);
            outb(io + ATA_REG_LBA_LO, (uint8_t)(lba >> 24));
            outb(io + ATA_REG_LBA_LO, (uint8_t)lba);
            outb(io + ATA_REG_LBA_MID, (uint8_t)(lba >> 32));
            outb(io + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
            outb(io + ATA_REG_LBA_HI, (uint8_t)(lba >> 40));
            outb(io + ATA_REG_LBA_HI, (uint8_t)(lba >> 16));
            outb(io + ATA_REG_COMMAND, 0x24); // ATA_CMD_READ_PIO_EXT
        } else {
            outb(io + ATA_REG_DRIVE_SEL, drive_sel | ((lba >> 24) & 0x0F));
            for (int i = 0; i < 4; i++) inb(io + ATA_REG_STATUS);
            outb(io + ATA_REG_SEC_COUNT, (uint8_t)batch);
            outb(io + ATA_REG_LBA_LO, (uint8_t)lba);
            outb(io + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
            outb(io + ATA_REG_LBA_HI, (uint8_t)(lba >> 16));
            outb(io + ATA_REG_COMMAND, ATA_CMD_READ_PIO);
        }
        for (int i = 0; i < 4; i++) inb(io + ATA_REG_STATUS);

        for (uint32_t s = 0; s < batch; s++) {
            ata_wait_bsy_port(io);
            if (!ata_wait_drq_port(io)) {
                return false;
            }
            for (int i = 0; i < 256; i++) {
                *ptr++ = inw(io + ATA_REG_DATA);
            }
        }

        lba += batch;
        count -= batch;
    }
    return true;
}

bool ata_read_sectors(uint64_t lba, uint32_t count, void* buffer) {
    const ata_device_t* dev = ata_get_primary_master();
    if (dev) {
        return ata_read_device_sectors(dev, lba, count, buffer);
    }
    if (ahci_is_active()) {
        return ahci_read_primary_sectors(lba, count, buffer);
    }
    // Fallback standard su porta 0x1F0 Primary Master
    ata_device_t def;
    memset(&def, 0, sizeof(def));
    def.io_base = ATA_PRIMARY_IO;
    def.drive = 0;
    return ata_read_device_sectors(&def, lba, count, buffer);
}

bool ata_write_sectors(uint64_t lba, uint32_t count, const void* buffer) {
    const ata_device_t* dev = ata_get_primary_master();
    if (dev && dev->is_atapi) return false;
    if (dev && dev->is_ahci) {
        return ahci_write_sectors(dev->ahci_port, lba, count, buffer);
    }
    if (ahci_is_active()) {
        return ahci_write_primary_sectors(lba, count, buffer);
    }

    uint16_t io = dev ? dev->io_base : ATA_PRIMARY_IO;
    uint8_t drive_sel = (dev && dev->drive == 1) ? 0xF0 : 0xE0;
    const uint16_t* ptr = (const uint16_t*)buffer;

    while (count > 0) {
        uint32_t batch = (count > 255) ? 255 : count;

        ata_wait_bsy_port(io);
        if (lba > 0x0FFFFFFF) {
            // LBA48 PIO: invia byte alti prima dei byte bassi (Standard ATA-6+)
            outb(io + ATA_REG_DRIVE_SEL, drive_sel);
            outb(io + ATA_REG_SEC_COUNT, 0);
            outb(io + ATA_REG_SEC_COUNT, (uint8_t)batch);
            outb(io + ATA_REG_LBA_LO, (uint8_t)(lba >> 24));
            outb(io + ATA_REG_LBA_LO, (uint8_t)lba);
            outb(io + ATA_REG_LBA_MID, (uint8_t)(lba >> 32));
            outb(io + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
            outb(io + ATA_REG_LBA_HI, (uint8_t)(lba >> 40));
            outb(io + ATA_REG_LBA_HI, (uint8_t)(lba >> 16));
            outb(io + ATA_REG_COMMAND, 0x34); // ATA_CMD_WRITE_PIO_EXT
        } else {
            outb(io + ATA_REG_DRIVE_SEL, drive_sel | ((lba >> 24) & 0x0F));
            outb(io + ATA_REG_SEC_COUNT, (uint8_t)batch);
            outb(io + ATA_REG_LBA_LO, (uint8_t)lba);
            outb(io + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
            outb(io + ATA_REG_LBA_HI, (uint8_t)(lba >> 16));
            outb(io + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
        }

        for (uint32_t s = 0; s < batch; s++) {
            ata_wait_bsy_port(io);
            if (!ata_wait_drq_port(io)) return false;
            for (int i = 0; i < 256; i++) {
                outw(io + ATA_REG_DATA, *ptr++);
            }
            for (int i = 0; i < 4; i++) inb(io + ATA_REG_STATUS);
        }

        lba += batch;
        count -= batch;
    }
    return true;
}
