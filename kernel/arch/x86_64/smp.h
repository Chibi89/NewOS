#ifndef ARCH_X86_64_SMP_H
#define ARCH_X86_64_SMP_H

#include <types.h>

#define MAX_CPUS 16

// Registri Local APIC MMIO (Base di default: 0xFEE00000)
#define LAPIC_DEFAULT_BASE      0xFEE00000ULL
#define LAPIC_ID_REG            0x0020
#define LAPIC_VERSION_REG       0x0030
#define LAPIC_TPR_REG           0x0080
#define LAPIC_EOI_REG           0x00B0
#define LAPIC_SVR_REG           0x00F0
#define LAPIC_ESR_REG           0x0280
#define LAPIC_ICR_LOW_REG       0x0300
#define LAPIC_ICR_HIGH_REG      0x0310

// Delivery Mode ICR per IPI
#define ICR_DELIVERY_FIXED      (0x0 << 8)
#define ICR_DELIVERY_INIT       (0x5 << 8)
#define ICR_DELIVERY_STARTUP    (0x6 << 8)
#define ICR_LEVEL_ASSERT        (1 << 14)
#define ICR_TRIGGER_LEVEL       (1 << 15)
#define ICR_DEST_ALL_EXCLUDING  (0x3 << 18)

typedef struct {
    uint32_t apic_id;
    bool     is_bsp;
    bool     is_online;
    uint64_t ticks;
} cpu_core_t;

typedef struct {
    uint32_t   detected_cores;
    uint32_t   online_cores;
    uint64_t   lapic_base;
    bool       smp_active;
    cpu_core_t cores[MAX_CPUS];
} smp_info_t;

// Inizializzazione SMP e Boot dei Core AP (Application Processors)
void              smp_init(void);
const smp_info_t* smp_get_info(void);
uint32_t          smp_get_core_count(void);
uint32_t          smp_get_online_core_count(void);
uint32_t          smp_get_current_apic_id(void);
bool              smp_is_bsp(void);

// Operazioni Local APIC
void              lapic_enable(void);
void              lapic_send_init(void);
void              lapic_send_sipi(uint8_t vector);

#endif // ARCH_X86_64_SMP_H
