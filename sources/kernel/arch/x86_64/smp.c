#include <arch/x86_64/smp.h>
#include <arch/x86_64/cpu.h>
#include <arch/x86_64/io.h>
#include <string.h>

extern uint8_t ap_trampoline_start[];
extern uint8_t ap_trampoline_end[];

static smp_info_t g_smp;
static bool       g_smp_initialized = false;

static void smp_delay_us(uint32_t us) {
    for (uint32_t i = 0; i < us; i++) {
        io_wait();
    }
}

static inline void rdmsr(uint32_t msr, uint32_t* lo, uint32_t* hi) {
    __asm__ volatile("rdmsr" : "=a"(*lo), "=d"(*hi) : "c"(msr));
}

static inline void wrmsr(uint32_t msr, uint32_t lo, uint32_t hi) {
    __asm__ volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

static inline uint32_t lapic_read(uint32_t reg) {
    volatile uint32_t* lapic = (volatile uint32_t*)(g_smp.lapic_base + reg);
    return *lapic;
}

static inline void lapic_write(uint32_t reg, uint32_t val) {
    volatile uint32_t* lapic = (volatile uint32_t*)(g_smp.lapic_base + reg);
    *lapic = val;
}

void lapic_enable(void) {
    // 1. Abilita APIC globale nel registro MSR 0x1B (bit 11)
    uint32_t lo, hi;
    rdmsr(0x1B, &lo, &hi);
    lo |= (1 << 11);
    wrmsr(0x1B, lo, hi);

    // 2. Abilita APIC software e imposta vettore spurious a 0xFF nel registro SVR (0x00F0)
    lapic_write(LAPIC_SVR_REG, 0x1FF);

    // 3. Imposta Task Priority Register (TPR) a 0 per accettare tutti gli interrupt
    lapic_write(LAPIC_TPR_REG, 0);
}

void lapic_send_init(void) {
    // INIT IPI inviato a tutti i core escluso se stesso
    lapic_write(LAPIC_ICR_HIGH_REG, 0);
    lapic_write(LAPIC_ICR_LOW_REG, ICR_DEST_ALL_EXCLUDING | ICR_DELIVERY_INIT | ICR_LEVEL_ASSERT);
}

void lapic_send_sipi(uint8_t vector) {
    // Startup IPI (SIPI) inviato a tutti i core escluso se stesso con vettore di avvio
    lapic_write(LAPIC_ICR_HIGH_REG, 0);
    lapic_write(LAPIC_ICR_LOW_REG, ICR_DEST_ALL_EXCLUDING | ICR_DELIVERY_STARTUP | vector);
}

uint32_t smp_get_current_apic_id(void) {
    if (!g_smp.lapic_base) return 0;
    return (lapic_read(LAPIC_ID_REG) >> 24) & 0xFF;
}

bool smp_is_bsp(void) {
    uint32_t lo, hi;
    rdmsr(0x1B, &lo, &hi);
    return (lo & (1 << 8)) != 0; // Bit 8 = BSP
}

const smp_info_t* smp_get_info(void) {
    return &g_smp;
}

uint32_t smp_get_core_count(void) {
    return g_smp.detected_cores;
}

uint32_t smp_get_online_core_count(void) {
    return g_smp.online_cores;
}

void smp_init(void) {
    if (g_smp_initialized) return;
    memset(&g_smp, 0, sizeof(g_smp));

    // 1. Rileva la base fisica del Local APIC da MSR 0x1B
    uint32_t lo, hi;
    rdmsr(0x1B, &lo, &hi);
    uint64_t base = ((uint64_t)hi << 32) | (lo & 0xFFFFF000ULL);
    if (base == 0) base = LAPIC_DEFAULT_BASE;
    g_smp.lapic_base = base;

    // 2. Abilita il Local APIC per il Bootstrap Processor (BSP)
    lapic_enable();

    uint32_t bsp_id = smp_get_current_apic_id();
    g_smp.cores[0].apic_id = bsp_id;
    g_smp.cores[0].is_bsp = true;
    g_smp.cores[0].is_online = true;
    g_smp.online_cores = 1;

    // 3. Rileva i core logici dal processore (CPUID)
    const cpu_info_t* cinfo = cpu_get_info();
    uint32_t cores = cinfo ? cinfo->logical_cores : 1;
    if (cores == 0) cores = 1;
    if (cores > MAX_CPUS) cores = MAX_CPUS;
    g_smp.detected_cores = cores;

    // 4. Se sono disponibili piu' core, avvia la sequenza INIT-SIPI-SIPI
    if (cores > 1) {
        // Copia il codice di trampoline a 0x8000 (vettore 0x08)
        size_t trampoline_size = (size_t)(ap_trampoline_end - ap_trampoline_start);
        memcpy((void*)0x8000, ap_trampoline_start, trampoline_size);

        // Azzera la locazione atomica di conteggio a 0x8500
        volatile uint32_t* ap_counter = (volatile uint32_t*)0x8500;
        *ap_counter = 0;

        // Invia INIT IPI
        lapic_send_init();
        smp_delay_us(10000); // 10 ms di stabilizzazione

        // Invia primo Startup IPI (SIPI) con vettore 0x08 (0x8000)
        lapic_send_sipi(0x08);
        smp_delay_us(2000);  // 2 ms

        // Se non tutti i core hanno risposto, invia secondo SIPI di specifica Intel
        if (*ap_counter < (cores - 1)) {
            lapic_send_sipi(0x08);
            smp_delay_us(5000);
        }

        uint32_t online_aps = *ap_counter;
        if (online_aps > (cores - 1)) online_aps = cores - 1;

        g_smp.online_cores = 1 + online_aps;
        for (uint32_t i = 1; i < g_smp.online_cores; i++) {
            g_smp.cores[i].apic_id = i;
            g_smp.cores[i].is_bsp = false;
            g_smp.cores[i].is_online = true;
        }
        g_smp.smp_active = (g_smp.online_cores > 1);
    } else {
        g_smp.smp_active = false;
    }

    g_smp_initialized = true;
}
