#ifndef ARCH_X86_64_GDT_H
#define ARCH_X86_64_GDT_H

#include <types.h>

#define GDT_KERNEL_CODE 0x18
#define GDT_KERNEL_DATA 0x20
#define GDT_USER_DATA   0x2B /* 0x28 | RPL 3 */
#define GDT_USER_CODE   0x33 /* 0x30 | RPL 3 */
#define GDT_TSS_SEL     0x38

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) tss64_t;

void gdt_init(void);
void gdt_set_tss_rsp0(uint64_t rsp0);
uint64_t gdt_get_tss_rsp0(void);

#endif // ARCH_X86_64_GDT_H
