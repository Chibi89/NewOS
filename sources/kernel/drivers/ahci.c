#include <drivers/ahci.h>
#include <drivers/pci.h>
#include <fs/fs_probe.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/pic.h>
#include <drivers/timer.h>
#include <string.h>

static void serial_puts(const char* s) {
    if (!s) return;
    while (*s) {
        outb(0x3F8, *s++);
    }
}

static bool ahci_probe_sector_reader(void* ctx, uint64_t lba, uint32_t count, void* buf) {
    uint32_t port = (uint32_t)(uintptr_t)ctx;
    return ahci_read_sectors(port, lba, count, buf);
}

static ahci_hba_mem_t* g_hba = NULL;
static ahci_device_t   g_ahci_devices[AHCI_SUPPORTED_PORTS];
static int             g_ahci_device_count = 0;
static bool            g_ahci_active = false;
static uint8_t         g_primary_port = 0;

static pci_device_t    g_ahci_pci_dev;
static uint8_t         g_ahci_irq = 0;
static volatile bool   g_ahci_irq_enabled = false;

// Strutture di memoria fisica allineate per ciascuna porta supportata (tutti i 32 slot)
static ahci_cmd_header_t g_cmd_list[AHCI_SUPPORTED_PORTS][32] __attribute__((aligned(1024)));
static uint8_t           g_rx_fis[AHCI_SUPPORTED_PORTS][256]  __attribute__((aligned(256)));
static ahci_cmd_table_t  g_cmd_table[AHCI_SUPPORTED_PORTS][32] __attribute__((aligned(128)));

// Internal Kernel Bounce Buffer di sicurezza (64 KB = 128 settori * 512 byte)
// Utilizzato come fallback per buffer non allineati a livello di parola o che violano i limiti DMA
static uint8_t g_ahci_bounce_buf[128 * 512] __attribute__((aligned(4096)));

// Lock atomico di porta per garantire il multithreading e la concorrenza senza race condition
static volatile int g_port_lock[AHCI_SUPPORTED_PORTS] = {0};

// Rilevamento dello stato degli interrupt della CPU (flag IF in RFLAGS)
static inline bool are_interrupts_enabled(void) {
    uint64_t rflags;
    __asm__ volatile ("pushfq; popq %0" : "=r"(rflags));
    return (rflags & (1U << 9)) != 0; // IF flag (bit 9)
}

// Verifica se un buffer in memoria e' allineato e sicuro per trasferimento DMA Zero-Copy diretto
static inline bool is_dma_safe_aligned(const void* ptr) {
    uintptr_t addr = (uintptr_t)ptr;
    // PRDT Data Base Address richiede bit 0 = 0 (allineamento minimo a 2 byte / WORD)
    return (addr != 0) && ((addr & 1) == 0);
}

// Lettura hardware reale del Time Stamp Counter (TSC x86_64)
static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static void ahci_lock_port(int port_idx) {
    if (port_idx < 0 || port_idx >= AHCI_SUPPORTED_PORTS) return;
    while (__atomic_test_and_set(&g_port_lock[port_idx], __ATOMIC_ACQUIRE)) {
        if (are_interrupts_enabled()) {
            hlt();
        } else {
            io_wait();
        }
    }
}

static void ahci_unlock_port(int port_idx) {
    if (port_idx < 0 || port_idx >= AHCI_SUPPORTED_PORTS) return;
    __atomic_clear(&g_port_lock[port_idx], __ATOMIC_RELEASE);
}

// Allocatore dinamico dello slot di comando per sfruttare la coda fino a 32 comandi
static int ahci_find_free_slot(ahci_port_regs_t* port) {
    uint32_t slots = (port->ci | port->sact);
    for (int i = 0; i < 32; i++) {
        if ((slots & (1U << i)) == 0) {
            return i;
        }
    }
    return -1;
}

static int ahci_alloc_slot(ahci_port_regs_t* port) {
    uint64_t start = rdtsc();
    // Timeout massimo di attesa slot libero (~1 secondo)
    while (rdtsc() - start < 4000000000ULL) {
        int slot = ahci_find_free_slot(port);
        if (slot >= 0) return slot;
        if (are_interrupts_enabled()) {
            hlt();
        } else {
            io_wait();
        }
    }
    return -1;
}

static void ahci_clean_string(char* dst, const uint16_t* src, int word_count) {
    int pos = 0;
    for (int i = 0; i < word_count; i++) {
        uint16_t w = src[i];
        dst[pos++] = (char)(w >> 8);
        dst[pos++] = (char)(w & 0xFF);
    }
    dst[pos] = '\0';
    while (pos > 0 && dst[pos - 1] == ' ') {
        dst[--pos] = '\0';
    }
}

static void ahci_stop_port(ahci_port_regs_t* port) {
    port->cmd &= ~0x01; // Clear ST (bit 0)
    uint64_t start = rdtsc();
    while ((port->cmd & 0x8000) && (rdtsc() - start < 2000000000ULL)) {
        io_wait();
    }

    port->cmd &= ~0x10; // Clear FRE (bit 4)
    start = rdtsc();
    while ((port->cmd & 0x4000) && (rdtsc() - start < 2000000000ULL)) {
        io_wait();
    }
}

static void ahci_start_port(ahci_port_regs_t* port) {
    uint64_t start = rdtsc();
    while ((port->cmd & 0x8000) && (rdtsc() - start < 2000000000ULL)) {
        io_wait();
    }

    port->cmd |= 0x10; // FRE = 1
    port->cmd |= 0x01; // ST = 1
}

static void ahci_rebase_port(ahci_port_regs_t* port, int port_index) {
    ahci_stop_port(port);

    // Imposta gli indirizzi fisici delle strutture allocate nel BSS
    uint64_t cl_addr = (uint64_t)&g_cmd_list[port_index][0];
    port->clb  = (uint32_t)(cl_addr & 0xFFFFFFFF);
    port->clbu = (uint32_t)(cl_addr >> 32);
    memset((void*)cl_addr, 0, sizeof(ahci_cmd_header_t) * 32);

    uint64_t fis_addr = (uint64_t)&g_rx_fis[port_index][0];
    port->fb  = (uint32_t)(fis_addr & 0xFFFFFFFF);
    port->fbu = (uint32_t)(fis_addr >> 32);
    memset((void*)fis_addr, 0, 256);

    // Inizializza tutti i 32 slot di comando collegando ciascuno alla sua tabella dedicata
    for (int s = 0; s < 32; s++) {
        ahci_cmd_header_t* cmd_hdr = &g_cmd_list[port_index][s];
        uint64_t tbl_addr = (uint64_t)&g_cmd_table[port_index][s];
        cmd_hdr->ctba  = (uint32_t)(tbl_addr & 0xFFFFFFFF);
        cmd_hdr->ctbau = (uint32_t)(tbl_addr >> 32);
        memset((void*)tbl_addr, 0, sizeof(ahci_cmd_table_t));
    }

    // Pulisce interrupt e registri di errore
    port->serr = 0xFFFFFFFF;
    port->is   = 0xFFFFFFFF;

    ahci_start_port(port);
}

// Interrupt Service Routine (ISR) per il controller AHCI con protezione da IRQ Storm
static void ahci_irq_handler(interrupt_frame_t* frame) {
    (void)frame;
    if (!g_hba) return;

    uint32_t is = g_hba->is;
    if (!is) return;

    // Disabilita e pulisce PxIS e PxSERR su tutte le porte attive che hanno generato evento
    for (int p = 0; p < 32; p++) {
        if (is & (1U << p)) {
            ahci_port_regs_t* port = &g_hba->ports[p];
            port->ie = 0;
            port->is = port->is; // Write-1-to-clear del registro IS della porta
            port->serr = port->serr; // Pulisce errori SERR
        }
    }

    // Write-1-to-clear del registro globale IS dell'HBA (fondamentale per de-asserire la linea PCI)
    g_hba->is = is;
}

void ahci_enable_interrupts(void) {
    if (!g_ahci_active || !g_hba) return;
    if (g_ahci_irq == 0 || g_ahci_irq >= 16) return;

    // 1. Pulisce preventivamente registri IS e SERR di tutte le 32 porte e disabilita PxIE
    for (int p = 0; p < 32; p++) {
        ahci_port_regs_t* port = &g_hba->ports[p];
        port->ie = 0;
        port->is = 0xFFFFFFFF;
        port->serr = 0xFFFFFFFF;
    }
    // 2. Pulisce registro globale IS dell'HBA prima di attivare GHC.IE
    g_hba->is = 0xFFFFFFFF;

    // 3. Registra l'ISR sulla tabella IDT (IRQ 0..15 -> INT 32..47)
    register_interrupt_handler(32 + g_ahci_irq, ahci_irq_handler);

    // 4. Smaschera l'IRQ nel Controller PIC
    pic_clear_mask(g_ahci_irq);
    if (g_ahci_irq >= 8) {
        pic_clear_mask(2); // Cascade slave PIC
    }

    // 5. Abilita gli interrupt globali nell'HBA (GHC.IE = bit 1)
    g_hba->ghc |= (1U << 1);

    g_ahci_irq_enabled = true;
}

bool ahci_is_interrupt_active(void) {
    return g_ahci_irq_enabled;
}

static bool ahci_exec_cmd(ahci_port_regs_t* port, int slot) {
    port->is = 0xFFFFFFFF; // Pulisce interrupt pendenti della porta
    if (g_hba) g_hba->is = 0xFFFFFFFF;

    if (g_ahci_irq_enabled) {
        port->ie = 0x3C000015; // Abilita interrupt per questo comando in volo
    }

    port->ci = (1U << slot);

    uint64_t start = rdtsc();

    // Fase 1: Quick-poll a latenza zero (primi ~200.000 cicli CPU = ~50-80 microsecondi)
    // Per trasferimenti DMA veloci, esce istantaneamente evitando qualsiasi latenza o overhead
    while (rdtsc() - start < 200000ULL) {
        if ((port->ci & (1U << slot)) == 0) {
            port->ie = 0; // Disabilita interrupt a comando concluso
            port->is = port->is;
            if (g_hba) g_hba->is = 0xFFFFFFFF;
            return (port->tfd & 0x01) == 0;
        }
        if (port->is & (1U << 30)) { // Fatal Task File Error (TFES)
            port->ie = 0;
            port->is = port->is;
            if (g_hba) g_hba->is = 0xFFFFFFFF;
            serial_puts("[AHCI_ERR] Quick TFES bit 30 fatal error!\n");
            return false;
        }
        io_wait();
    }

    // Fase 2: Per operazioni piu' lunghe (es. dischi magnetici o letture massive),
    // se gli interrupt sono attivi, cede la CPU con hlt() per evitare il consumo di cicli
    if (g_ahci_irq_enabled && are_interrupts_enabled()) {
        uint64_t start_tick = timer_get_ticks();
        while ((port->ci & (1U << slot)) != 0) {
            if (port->is & (1U << 30)) {
                port->ie = 0;
                return false;
            }
            if (timer_get_ticks() - start_tick > 300) { // Timeout di 3 secondi
                port->ie = 0;
                return false;
            }
            hlt(); // Sospende la CPU fino al prossimo interrupt hardware
        }
    } else {
        // Fallback di sicurezza con TSC x86_64 per la fase di pre-boot
        const uint64_t max_cycles = 12000000000ULL;
        while ((rdtsc() - start) < max_cycles) {
            if ((port->ci & (1U << slot)) == 0) {
                break;
            }
            if (port->is & (1U << 30)) {
                port->ie = 0;
                return false;
            }
            io_wait();
        }
    }

    port->ie = 0;
    port->is = port->is;
    if (g_hba) g_hba->is = 0xFFFFFFFF;

    if ((port->ci & (1U << slot)) != 0) {
        serial_puts("[AHCI_ERR] Timeout CI not cleared!\n");
        return false; // Timeout scaduto
    }
    if (port->tfd & 0x01) {
        serial_puts("[AHCI_ERR] Task File Error (TFD.ERR=1)!\n");
        return false;
    }
    return true;
}

static bool ahci_identify_device(ahci_port_regs_t* port, uint8_t hw_port, int dev_idx, ahci_device_t* dev) {
    dev->present = true;
    dev->port_num = hw_port;

    uint32_t sig = port->sig;
    dev->is_atapi = (sig == AHCI_DEV_SATAPI);

    static uint16_t identify_buf[256] __attribute__((aligned(16)));
    memset(identify_buf, 0, sizeof(identify_buf));

    int slot = ahci_alloc_slot(port);
    if (slot < 0) {
        dev->present = false;
        return false;
    }

    ahci_cmd_header_t* cmd_hdr = &g_cmd_list[dev_idx][slot];
    cmd_hdr->cfl = sizeof(fis_reg_h2d_t) / 4;
    cmd_hdr->w = 0; // Read
    cmd_hdr->a = 0;
    cmd_hdr->prdtl = 1;

    ahci_cmd_table_t* tbl = &g_cmd_table[dev_idx][slot];
    memset(tbl, 0, sizeof(ahci_cmd_table_t));

    uint64_t buf_addr = (uint64_t)identify_buf;
    tbl->prdt_entry[0].dba  = (uint32_t)(buf_addr & 0xFFFFFFFF);
    tbl->prdt_entry[0].dbau = (uint32_t)(buf_addr >> 32);
    tbl->prdt_entry[0].dbc  = 512 - 1;
    tbl->prdt_entry[0].i    = 1;

    fis_reg_h2d_t* fis = (fis_reg_h2d_t*)(&tbl->cfis[0]);
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1; // Command
    fis->command = dev->is_atapi ? ATA_CMD_IDENTIFY_PACKET : ATA_CMD_IDENTIFY;
    fis->device = 0;

    if (!ahci_exec_cmd(port, slot)) {
        // Fallback incrociato: se sig non era ancora pervenuto o la modalità era errata
        dev->is_atapi = !dev->is_atapi;
        fis->command = dev->is_atapi ? ATA_CMD_IDENTIFY_PACKET : ATA_CMD_IDENTIFY;
        if (!ahci_exec_cmd(port, slot)) {
            dev->present = false;
            serial_puts("[AHCI] IDENTIFY fallito su porta ");
            outb(0x3F8, '0' + hw_port);
            serial_puts("\n");
            return false;
        }
    }

    ahci_clean_string(dev->serial, &identify_buf[10], 10);
    ahci_clean_string(dev->firmware, &identify_buf[23], 4);
    ahci_clean_string(dev->model, &identify_buf[27], 20);

    if (dev->is_atapi) {
        dev->total_sectors = 350000;
        dev->size_bytes = dev->total_sectors * 2048;
        dev->size_mb = (uint32_t)(dev->size_bytes / (1024 * 1024));
        dev->size_gb = dev->size_mb / 1024;
        dev->boot_lba = 31111;
        strcpy(dev->part_fs, "ISO9660");
    } else {
        uint32_t lba28_sectors = (uint32_t)identify_buf[60] | ((uint32_t)identify_buf[61] << 16);
        uint64_t lba48_sectors = (uint64_t)identify_buf[100] |
                                ((uint64_t)identify_buf[101] << 16) |
                                ((uint64_t)identify_buf[102] << 32) |
                                ((uint64_t)identify_buf[103] << 48);

        dev->total_sectors = (lba48_sectors > 0) ? lba48_sectors : lba28_sectors;
        dev->size_bytes = dev->total_sectors * 512;
        dev->size_mb = (uint32_t)(dev->size_bytes / (1024 * 1024));
        dev->size_gb = dev->size_mb / 1024;

        static uint8_t mbr_buf[512] __attribute__((aligned(16)));
        if (ahci_read_sectors(hw_port, 0, 1, mbr_buf)) {
            if (mbr_buf[510] == 0x55 && mbr_buf[511] == 0xAA) {
                dev->has_mbr = true;
                const uint8_t* p = &mbr_buf[446];
                dev->part_type = p[4];
                dev->part_lba = (uint32_t)p[8] | ((uint32_t)p[9] << 8) |
                                ((uint32_t)p[10] << 16) | ((uint32_t)p[11] << 24);
                dev->part_sectors = (uint32_t)p[12] | ((uint32_t)p[13] << 8) |
                                    ((uint32_t)p[14] << 16) | ((uint32_t)p[15] << 24);

                if (dev->part_type == 0xEE) {
                    // Partizione GPT Protettiva (UEFI / Dischi > 2TB)
                    uint64_t gpt_lba = 0, gpt_sec = 0;
                    char gpt_fs[16] = "exFAT";
                    if (gpt_probe_table(ahci_probe_sector_reader, (void*)(uintptr_t)hw_port, &gpt_lba, &gpt_sec, gpt_fs, sizeof(gpt_fs))) {
                        dev->has_gpt = true;
                        dev->part_lba = gpt_lba;
                        dev->part_sectors = gpt_sec;
                        strncpy(dev->part_fs, gpt_fs, sizeof(dev->part_fs) - 1);
                    }
                } else if (dev->part_type == 0x07) {
                    strcpy(dev->part_fs, "exFAT");
                } else if (dev->part_type == 0x0B || dev->part_type == 0x0C) {
                    strcpy(dev->part_fs, "FAT32");
                } else {
                    strcpy(dev->part_fs, "RAW");
                }
            }
        }
    }

    dev->present = true;
    return true;
}

bool ahci_init(void) {
    if (g_ahci_active) return true;

    pci_device_t pci_dev;
    bool found = pci_find_class_prog_if(AHCI_PCI_CLASS, AHCI_PCI_SUBCLASS, AHCI_PCI_PROGIF, &pci_dev);
    if (!found) {
        found = pci_find_class(AHCI_PCI_CLASS, AHCI_PCI_SUBCLASS, &pci_dev);
    }

    if (!found) {
        return false;
    }

    g_ahci_pci_dev = pci_dev;
    g_ahci_irq = pci_dev.irq;

    // Abilita Bus Master e Memory Space I/O nel comando PCI
    pci_enable_bus_master(&pci_dev);
    uint16_t cmd = pci_read16(pci_dev.bus, pci_dev.slot, pci_dev.func, 0x04);
    cmd |= 0x0006;
    pci_write16(pci_dev.bus, pci_dev.slot, pci_dev.func, 0x04, cmd);

    uint64_t abar = (uint64_t)(pci_dev.bar5 & ~0x1F);
    if (abar == 0) return false;

    g_hba = (ahci_hba_mem_t*)abar;

    // 1. BIOS/OS Handoff standard (AHCI 1.2+ Section 10.6.3) per schede madri UEFI/BIOS reali
    if (g_hba->cap2 & 1) { // Bit 0: BIOS/OS Handoff (BOH) supportato
        g_hba->bohc |= (1U << 1); // Richiede la proprieta' impostando OOS (OS Owned Semaphore)
        uint64_t bohc_start = rdtsc();
        // Attende che il BIOS rilasci la proprieta' (BOS = 0) per max ~50 ms
        while ((g_hba->bohc & 1) && (rdtsc() - bohc_start < 200000000ULL)) {
            io_wait();
        }
        // Attende che il BIOS completi eventuali transazioni residue (BB = 0)
        bohc_start = rdtsc();
        while ((g_hba->bohc & (1U << 4)) && (rdtsc() - bohc_start < 200000000ULL)) {
            io_wait();
        }
    }

    // 2. Global HBA Reset per ripulire registri pendenti lasciati dal firmware
    g_hba->ghc |= 1; // HBA Reset (HR, bit 0)
    uint64_t hr_start = rdtsc();
    while ((g_hba->ghc & 1) && (rdtsc() - hr_start < 4000000000ULL)) {
        io_wait();
    }

    // 3. Abilita AHCI nel Global Host Control (GHC.AE = bit 31)
    g_hba->ghc |= (1U << 31);
    g_ahci_active = true;

    uint32_t pi = g_hba->pi;
    g_ahci_device_count = 0;

    for (int i = 0; i < 32 && g_ahci_device_count < AHCI_SUPPORTED_PORTS; i++) {
        if (!(pi & (1U << i))) continue;

        ahci_port_regs_t* port = &g_hba->ports[i];

        // 4. Staggered Spin-Up per dischi fisici reali (alimenta il disco se supportato)
        if (g_hba->cap & (1U << 27)) { // CAP.SSS (Staggered Spin-up Supported)
            port->cmd |= (1U << 1);    // PxCMD.SUD = 1 (Spin-Up Device)
        }

        // 5. Attesa negoziazione elettrica PHY SATA (fino a 50 ms su cavi fisici)
        uint64_t phy_start = rdtsc();
        uint8_t det = 0;
        while (rdtsc() - phy_start < 200000000ULL) {
            det = (uint8_t)(port->ssts & 0x0F);
            if (det == 3 || det == 0) break; // 3 = PHY stabilito, 0 = nessun disco
            io_wait();
        }

        if (det == 3) {
            int dev_idx = g_ahci_device_count;

            // Rebase e avvia la porta (FRE=1, ST=1) per ricevere il D2H FIS con la firma
            ahci_rebase_port(port, dev_idx);

            // Attende ricezione del D2H Register FIS iniziale (fino a 25 ms)
            uint64_t sig_start = rdtsc();
            while ((port->sig == 0xFFFFFFFF || port->sig == 0) && (rdtsc() - sig_start < 100000000ULL)) {
                io_wait();
            }

            uint32_t sig = port->sig;
            serial_puts("[AHCI] Port ");
            outb(0x3F8, '0' + i);
            serial_puts(" det=3 sig=0x");
            for (int sh = 28; sh >= 0; sh -= 4) {
                uint8_t hx = (sig >> sh) & 0x0F;
                outb(0x3F8, hx < 10 ? ('0' + hx) : ('A' + hx - 10));
            }
            serial_puts("\n");

            if (ahci_identify_device(port, (uint8_t)i, dev_idx, &g_ahci_devices[dev_idx])) {
                serial_puts("[AHCI] Dispositivo identificato: ");
                serial_puts(g_ahci_devices[dev_idx].model);
                serial_puts(g_ahci_devices[dev_idx].is_atapi ? " (ATAPI CD-ROM)\n" : " (Hard Disk SATA)\n");

                if (!g_ahci_devices[dev_idx].is_atapi && g_primary_port == 0) {
                    g_primary_port = (uint8_t)i;
                }
                g_ahci_device_count++;
            } else {
                ahci_stop_port(port);
            }
        }
    }

    if (g_ahci_device_count > 0) {
        // Rileva El Torito boot_lba dal Catalogo El Torito (settore 20) per le unità ottiche ATAPI
        for (int i = 0; i < g_ahci_device_count; i++) {
            if (g_ahci_devices[i].is_atapi) {
                static uint8_t cat_sec[2048] __attribute__((aligned(16)));
                if (ahci_read_cd_sector(g_ahci_devices[i].port_num, 20, cat_sec)) {
                    uint32_t cand = *(uint32_t*)&cat_sec[40];
                    if (cand > 0 && cand < 500000) {
                        g_ahci_devices[i].boot_lba = cand;
                    }
                }
            }
        }
        return true;
    }

    g_ahci_active = false;
    return false;
}

bool ahci_is_active(void) {
    return g_ahci_active;
}

int ahci_get_device_count(void) {
    return g_ahci_device_count;
}

const ahci_device_t* ahci_get_device(int index) {
    if (index >= 0 && index < g_ahci_device_count) {
        return &g_ahci_devices[index];
    }
    return NULL;
}

const ahci_device_t* ahci_get_primary_device(void) {
    for (int i = 0; i < g_ahci_device_count; i++) {
        if (!g_ahci_devices[i].is_atapi && g_ahci_devices[i].present) {
            return &g_ahci_devices[i];
        }
    }
    if (g_ahci_device_count > 0) return &g_ahci_devices[0];
    return NULL;
}

bool ahci_read_sectors(uint32_t port_num, uint64_t lba, uint32_t count, void* buffer) {
    if (!g_ahci_active || !g_hba || !buffer || count == 0) return false;
    if (port_num >= 32) return false;

    int port_idx = -1;
    for (int i = 0; i < g_ahci_device_count; i++) {
        if (g_ahci_devices[i].port_num == port_num) {
            port_idx = i;
            break;
        }
    }
    if (port_idx < 0) port_idx = 0;

    ahci_lock_port(port_idx);
    ahci_port_regs_t* port = &g_hba->ports[port_num];
    uint8_t* dst = (uint8_t*)buffer;

    while (count > 0) {
        uint32_t batch = (count > 128) ? 128 : count;
        uint32_t byte_count = batch * 512;

        int slot = ahci_alloc_slot(port);
        if (slot < 0) {
            ahci_unlock_port(port_idx);
            return false;
        }

        ahci_cmd_header_t* cmd_hdr = &g_cmd_list[port_idx][slot];
        cmd_hdr->cfl = sizeof(fis_reg_h2d_t) / 4;
        cmd_hdr->w = 0; // Read
        cmd_hdr->a = 0;
        cmd_hdr->prdtl = 1;

        ahci_cmd_table_t* tbl = &g_cmd_table[port_idx][slot];
        memset(tbl, 0, sizeof(ahci_cmd_table_t));

        // Zero-Copy I/O: se il buffer chiamante e' allineato in modo sicuro per DMA,
        // punta direttamente alla destinazione evitando qualsiasi memcpy!
        bool zero_copy = is_dma_safe_aligned(dst);
        uint64_t target_addr = zero_copy ? (uint64_t)(uintptr_t)dst : (uint64_t)g_ahci_bounce_buf;

        tbl->prdt_entry[0].dba  = (uint32_t)(target_addr & 0xFFFFFFFF);
        tbl->prdt_entry[0].dbau = (uint32_t)(target_addr >> 32);
        tbl->prdt_entry[0].dbc  = byte_count - 1;
        tbl->prdt_entry[0].i    = 1;

        fis_reg_h2d_t* fis = (fis_reg_h2d_t*)(&tbl->cfis[0]);
        fis->fis_type = FIS_TYPE_REG_H2D;
        fis->c = 1;
        fis->command = ATA_CMD_READ_DMA_EXT; // 0x25 (LBA48)

        fis->lba0 = (uint8_t)lba;
        fis->lba1 = (uint8_t)(lba >> 8);
        fis->lba2 = (uint8_t)(lba >> 16);
        fis->device = 1U << 6; // LBA mode

        fis->lba3 = (uint8_t)(lba >> 24);
        fis->lba4 = (uint8_t)(lba >> 32);
        fis->lba5 = (uint8_t)(lba >> 40);

        fis->countl = (uint8_t)batch;
        fis->counth = (uint8_t)(batch >> 8);

        if (!ahci_exec_cmd(port, slot)) {
            ahci_unlock_port(port_idx);
            return false;
        }

        // Se non era allineato per Zero-Copy, copia di sicurezza dal Bounce Buffer
        if (!zero_copy) {
            memcpy(dst, g_ahci_bounce_buf, byte_count);
        }

        lba += batch;
        dst += byte_count;
        count -= batch;
    }

    ahci_unlock_port(port_idx);
    return true;
}

bool ahci_write_sectors(uint32_t port_num, uint64_t lba, uint32_t count, const void* buffer) {
    if (!g_ahci_active || !g_hba || !buffer || count == 0) return false;
    if (port_num >= 32) return false;

    int port_idx = -1;
    for (int i = 0; i < g_ahci_device_count; i++) {
        if (g_ahci_devices[i].port_num == port_num) {
            port_idx = i;
            break;
        }
    }
    if (port_idx < 0) port_idx = 0;

    ahci_lock_port(port_idx);
    ahci_port_regs_t* port = &g_hba->ports[port_num];
    const uint8_t* src = (const uint8_t*)buffer;

    while (count > 0) {
        uint32_t batch = (count > 128) ? 128 : count;
        uint32_t byte_count = batch * 512;

        int slot = ahci_alloc_slot(port);
        if (slot < 0) {
            ahci_unlock_port(port_idx);
            return false;
        }

        bool zero_copy = is_dma_safe_aligned(src);
        uint64_t source_addr;

        if (zero_copy) {
            // Zero-Copy: l'HBA legge direttamente dalla memoria sorgente senza duplicazione
            source_addr = (uint64_t)(uintptr_t)src;
        } else {
            // Buffer disallineato: copia preventiva nel Bounce Buffer allineato
            memcpy(g_ahci_bounce_buf, src, byte_count);
            source_addr = (uint64_t)g_ahci_bounce_buf;
        }

        ahci_cmd_header_t* cmd_hdr = &g_cmd_list[port_idx][slot];
        cmd_hdr->cfl = sizeof(fis_reg_h2d_t) / 4;
        cmd_hdr->w = 1; // Write
        cmd_hdr->a = 0;
        cmd_hdr->prdtl = 1;

        ahci_cmd_table_t* tbl = &g_cmd_table[port_idx][slot];
        memset(tbl, 0, sizeof(ahci_cmd_table_t));

        tbl->prdt_entry[0].dba  = (uint32_t)(source_addr & 0xFFFFFFFF);
        tbl->prdt_entry[0].dbau = (uint32_t)(source_addr >> 32);
        tbl->prdt_entry[0].dbc  = byte_count - 1;
        tbl->prdt_entry[0].i    = 1;

        fis_reg_h2d_t* fis = (fis_reg_h2d_t*)(&tbl->cfis[0]);
        fis->fis_type = FIS_TYPE_REG_H2D;
        fis->c = 1;
        fis->command = ATA_CMD_WRITE_DMA_EXT; // 0x35 (LBA48)

        fis->lba0 = (uint8_t)lba;
        fis->lba1 = (uint8_t)(lba >> 8);
        fis->lba2 = (uint8_t)(lba >> 16);
        fis->device = 1U << 6;

        fis->lba3 = (uint8_t)(lba >> 24);
        fis->lba4 = (uint8_t)(lba >> 32);
        fis->lba5 = (uint8_t)(lba >> 40);

        fis->countl = (uint8_t)batch;
        fis->counth = (uint8_t)(batch >> 8);

        if (!ahci_exec_cmd(port, slot)) {
            ahci_unlock_port(port_idx);
            return false;
        }

        lba += batch;
        src += byte_count;
        count -= batch;
    }

    ahci_unlock_port(port_idx);
    return true;
}

bool ahci_read_primary_sectors(uint64_t lba, uint32_t count, void* buffer) {
    const ahci_device_t* dev = ahci_get_primary_device();
    if (!dev) return false;
    return ahci_read_sectors(dev->port_num, lba, count, buffer);
}

bool ahci_write_primary_sectors(uint64_t lba, uint32_t count, const void* buffer) {
    const ahci_device_t* dev = ahci_get_primary_device();
    if (!dev || dev->is_atapi) return false;
    return ahci_write_sectors(dev->port_num, lba, count, buffer);
}

bool ahci_read_cd_sectors(uint32_t port_num, uint32_t cd_lba, uint32_t count, void* buffer) {
    if (!g_ahci_active || !g_hba || !buffer || port_num >= 32 || count == 0) return false;

    int port_idx = -1;
    for (int i = 0; i < g_ahci_device_count; i++) {
        if (g_ahci_devices[i].port_num == port_num) {
            port_idx = i;
            break;
        }
    }
    if (port_idx < 0) port_idx = 0;

    ahci_port_regs_t* port = &g_hba->ports[port_num];
    uint8_t* dst = (uint8_t*)buffer;

    while (count > 0) {
        uint32_t batch = (count > 16) ? 16 : count;
        uint32_t byte_count = batch * 2048;

        ahci_lock_port(port_idx);

        int slot = ahci_alloc_slot(port);
        if (slot < 0) {
            ahci_unlock_port(port_idx);
            return false;
        }

        bool zero_copy = is_dma_safe_aligned(dst);
        uint64_t target_addr = zero_copy ? (uint64_t)(uintptr_t)dst : (uint64_t)g_ahci_bounce_buf;

        ahci_cmd_header_t* cmd_hdr = &g_cmd_list[port_idx][slot];
        cmd_hdr->cfl = sizeof(fis_reg_h2d_t) / 4;
        cmd_hdr->w = 0; // Read
        cmd_hdr->a = 1; // ATAPI Packet
        cmd_hdr->prdtl = 1;

        ahci_cmd_table_t* tbl = &g_cmd_table[port_idx][slot];
        memset(tbl, 0, sizeof(ahci_cmd_table_t));

        tbl->prdt_entry[0].dba  = (uint32_t)(target_addr & 0xFFFFFFFF);
        tbl->prdt_entry[0].dbau = (uint32_t)(target_addr >> 32);
        tbl->prdt_entry[0].dbc  = byte_count - 1;
        tbl->prdt_entry[0].i    = 1;

        // ATAPI Packet Command (SCSI READ (10) - Opcode 0x28)
        tbl->acmd[0] = 0x28;
        tbl->acmd[2] = (uint8_t)(cd_lba >> 24);
        tbl->acmd[3] = (uint8_t)(cd_lba >> 16);
        tbl->acmd[4] = (uint8_t)(cd_lba >> 8);
        tbl->acmd[5] = (uint8_t)(cd_lba & 0xFF);
        tbl->acmd[7] = (uint8_t)(batch >> 8);
        tbl->acmd[8] = (uint8_t)(batch & 0xFF);

        fis_reg_h2d_t* fis = (fis_reg_h2d_t*)(&tbl->cfis[0]);
        fis->fis_type = FIS_TYPE_REG_H2D;
        fis->c = 1;
        fis->command = ATA_CMD_PACKET; // 0xA0
        fis->featurel = 1; // DMA transfer for ATAPI
        fis->lba1 = (uint8_t)(byte_count & 0xFF);        // Byte Count Limit Low
        fis->lba2 = (uint8_t)((byte_count >> 8) & 0xFF); // Byte Count Limit High

        if (!ahci_exec_cmd(port, slot)) {
            ahci_unlock_port(port_idx);
            return false;
        }

        if (!zero_copy) {
            memcpy(dst, g_ahci_bounce_buf, byte_count);
        }

        ahci_unlock_port(port_idx);

        cd_lba += batch;
        dst += byte_count;
        count -= batch;
    }

    return true;
}

bool ahci_read_cd_sector(uint32_t port_num, uint32_t cd_lba, void* buf2048) {
    return ahci_read_cd_sectors(port_num, cd_lba, 1, buf2048);
}
