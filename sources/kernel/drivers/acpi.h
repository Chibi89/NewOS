#ifndef DRIVERS_ACPI_H
#define DRIVERS_ACPI_H

#include <types.h>

#define ACPI_MAX_CPUS 32

typedef struct {
    bool     available;
    uint8_t  revision;
    uint64_t lapic_addr;
    uint32_t ioapic_addr;
    uint8_t  cpu_apic_ids[ACPI_MAX_CPUS];
    int      cpu_count;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
    uint16_t slp_typa;
    uint16_t slp_typb;
    bool     has_s5;
    bool     has_reset_reg;
    uint8_t  reset_addr_space; // 0 = Mem, 1 = I/O
    uint64_t reset_addr;
    uint8_t  reset_value;
} acpi_info_t;

bool               acpi_init(void);
const acpi_info_t* acpi_get_info(void);
bool               acpi_shutdown(void);
bool               acpi_reboot(void);

#endif // DRIVERS_ACPI_H
