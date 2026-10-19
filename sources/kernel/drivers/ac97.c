#include <drivers/ac97.h>
#include <drivers/pci.h>
#include <drivers/timer.h>
#include <arch/x86_64/io.h>

#define AC97_NAM_RESET         0x00
#define AC97_NAM_MASTER_VOL    0x02
#define AC97_NAM_PCM_VOL       0x18
#define AC97_NAM_EXT_ID        0x28
#define AC97_NAM_EXT_STATUS    0x2A
#define AC97_NAM_FRONT_DAC_RATE 0x2C
#define AC97_NAM_PCM_DAC_RATE  0x32

#define AC97_NABM_PO_BDBAR     0x10
#define AC97_NABM_PO_CIV       0x14
#define AC97_NABM_PO_LVI       0x15
#define AC97_NABM_PO_SR        0x16
#define AC97_NABM_PO_PICB      0x18
#define AC97_NABM_PO_CR        0x1B
#define AC97_NABM_GLOB_CNT     0x2C
#define AC97_NABM_GLOB_STA     0x30

typedef struct {
    uint32_t buffer_ptr;
    uint16_t length;     // Campioni (bytes / 2)
    uint16_t flags;      // Bit 15: IOC (0x8000), Bit 14: BUP (0x4000)
} __attribute__((packed)) ac97_bdl_entry_t;

static bool ac97_present = false;
static uint16_t nambar = 0;
static uint16_t nabmbar = 0;
static uint8_t current_volume = 100;
static char device_name[48] = "Nessun dispositivo audio PCI rilevato";

static ac97_bdl_entry_t bdl[32] __attribute__((aligned(16)));

bool ac97_init(void) {
    pci_device_t dev;
    bool found = false;

    // 1. Cerca specificamente il controller Intel 82801AA AC'97 (usato da QEMU e VirtualBox)
    if (pci_find_device(0x8086, 0x2415, &dev)) {
        found = true;
        for (int i = 0; i < 48; i++) device_name[i] = '\0';
        const char* name = "Intel 82801AA AC'97 Audio (ICH)";
        for (int i = 0; name[i]; i++) device_name[i] = name[i];
    } else if (pci_find_class(0x04, 0x01, &dev)) { // Qualsiasi controller PCI Multimedia Audio
        found = true;
        const char* name = "Controller Audio PCI AC'97";
        for (int i = 0; name[i]; i++) device_name[i] = name[i];
    }

    if (!found) {
        ac97_present = false;
        outb(0x3F8, 'A');
        outb(0x3F8, 'X'); // Audio Not found
        return false;
    }

    nambar  = (uint16_t)(dev.bar0 & ~0x1);
    nabmbar = (uint16_t)(dev.bar1 & ~0x1);

    // Abilita PCI Bus Master
    pci_enable_bus_master(&dev);

    // Cold Reset del controller audio Bus Master
    outl(nabmbar + AC97_NABM_GLOB_CNT, 0x00000002);
    for (volatile int i = 0; i < 50000; i++) io_wait();

    // Reset del mixer
    outw(nambar + AC97_NAM_RESET, 0x0000);
    for (volatile int i = 0; i < 50000; i++) io_wait();

    // Imposta volume al massimo (0 dB di attenuazione)
    ac97_set_volume(100);

    // Abilita Variable Rate Audio (VRA) se supportato
    uint16_t ext_status = inw(nambar + AC97_NAM_EXT_STATUS);
    outw(nambar + AC97_NAM_EXT_STATUS, ext_status | 0x0001);

    // Imposta sample rate predefinito a 44100 Hz
    outw(nambar + AC97_NAM_FRONT_DAC_RATE, 44100);
    outw(nambar + AC97_NAM_PCM_DAC_RATE, 44100);

    // Reset canale PCM Out
    outb(nabmbar + AC97_NABM_PO_CR, 0x02);
    for (volatile int i = 0; i < 50000; i++) io_wait();

    ac97_present = true;
    outb(0x3F8, 'A');
    outb(0x3F8, 'C'); // Audio AC97 OK
    return true;
}

bool ac97_is_available(void) {
    return ac97_present;
}

const char* ac97_get_device_name(void) {
    if (ac97_present) {
        return device_name;
    }
    return "Altoparlante di sistema (PC Speaker)";
}

void ac97_set_volume(uint8_t volume_percent) {
    if (volume_percent > 100) volume_percent = 100;
    current_volume = volume_percent;

    if (!ac97_present) return;

    if (volume_percent == 0) {
        outw(nambar + AC97_NAM_MASTER_VOL, 0x8000); // Mute
        outw(nambar + AC97_NAM_PCM_VOL, 0x8000);
    } else {
        // Scala 0 (max) .. 31 (minimo)
        uint16_t att = (uint16_t)((100 - volume_percent) * 31 / 100);
        uint16_t val = (uint16_t)((att << 8) | att);
        outw(nambar + AC97_NAM_MASTER_VOL, val);
        outw(nambar + AC97_NAM_PCM_VOL, val);
    }
}

uint8_t ac97_get_volume(void) {
    return current_volume;
}

void ac97_stop(void) {
    if (!ac97_present) return;
    outb(nabmbar + AC97_NABM_PO_CR, 0x00); // Ferma Bus Master
    outb(nabmbar + AC97_NABM_PO_CR, 0x02); // Reset registri canale
}

bool ac97_is_playing(void) {
    if (!ac97_present) return false;
    uint8_t cr = inb(nabmbar + AC97_NABM_PO_CR);
    uint16_t sr = inw(nabmbar + AC97_NABM_PO_SR);

    // Se il bit Run (bit 0) è spento o il controller è fermo (DCH bit 0)
    if ((cr & 0x01) == 0) return false;
    if ((sr & 0x0001) != 0) return false; // DCH (DMA Controller Halted)
    return true;
}

void ac97_play_pcm(const void* data, uint32_t bytes, uint32_t sample_rate) {
    if (!ac97_present || !data || bytes == 0) return;

    ac97_stop();

    // Imposta frequenza di campionamento
    outw(nambar + AC97_NAM_EXT_STATUS, inw(nambar + AC97_NAM_EXT_STATUS) | 0x0001);
    outw(nambar + AC97_NAM_FRONT_DAC_RATE, (uint16_t)sample_rate);
    outw(nambar + AC97_NAM_PCM_DAC_RATE, (uint16_t)sample_rate);

    // Popola tabella Buffer Descriptor List (BDL)
    // Ciascuna voce gestisce fino a 32768 byte (16384 campioni a 16 bit)
    uint32_t remaining = bytes;
    const uint8_t* ptr = (const uint8_t*)data;
    int entries = 0;

    while (remaining > 0 && entries < 32) {
        uint32_t chunk = remaining;
        if (chunk > 32768) chunk = 32768;

        bdl[entries].buffer_ptr = (uint32_t)(uintptr_t)ptr;
        bdl[entries].length = (uint16_t)(chunk / 2); // Numero di campioni a 16 bit
        bdl[entries].flags = 0;

        remaining -= chunk;
        ptr += chunk;
        entries++;
    }

    if (entries == 0) return;

    // Imposta flag di completamento sull'ultimo descrittore
    bdl[entries - 1].flags = 0x8000; // Interrupt/halt on completion

    // Punta il registro BDBAR alla tabella BDL fisica
    outl(nabmbar + AC97_NABM_PO_BDBAR, (uint32_t)(uintptr_t)&bdl[0]);

    // Imposta l'ultimo indice valido (LVI)
    outb(nabmbar + AC97_NABM_PO_LVI, (uint8_t)(entries - 1));

    // Pulisce lo stato precedente
    outw(nabmbar + AC97_NABM_PO_SR, 0x001C);

    // Avvia la riproduzione DMA (Run bit = 1)
    outb(nabmbar + AC97_NABM_PO_CR, 0x01);
}
