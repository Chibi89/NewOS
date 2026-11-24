#include <arch/x86_64/gdt.h>
#include <string.h>

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdtr_t;

/*
 * GDT a 64-bit (9 slot da 8 byte):
 *  0x00: Null
 *  0x08: 32-bit Code (compat)
 *  0x10: 32-bit Data (compat)
 *  0x18: Kernel 64-bit Code (DPL=0)
 *  0x20: Kernel 64-bit Data (DPL=0)
 *  0x28: User 64-bit Data   (DPL=3 -> 0x2B)
 *  0x30: User 64-bit Code   (DPL=3 -> 0x33)
 *  0x38..0x47: 64-bit TSS (16 byte)
 */
static uint64_t g_gdt[9] __attribute__((aligned(16)));
static tss64_t  g_tss    __attribute__((aligned(16)));
static gdtr_t   g_gdtr;
static uint8_t  g_default_rsp0_stack[8192] __attribute__((aligned(16)));

static void gdt_set_tss_desc(int idx, uint64_t base, uint32_t limit) {
    uint64_t low = 0;
    low |= (uint64_t)(limit & 0xFFFF);
    low |= (uint64_t)(base & 0xFFFFFFULL) << 16;
    low |= (uint64_t)0x89 << 40; /* Present=1, DPL=0, Type=9 (64-bit TSS Available) */
    low |= (uint64_t)((limit >> 16) & 0x0F) << 48;
    low |= (uint64_t)((base >> 24) & 0xFFULL) << 56;

    uint64_t high = (base >> 32) & 0xFFFFFFFFULL;

    g_gdt[idx]     = low;
    g_gdt[idx + 1] = high;
}

void gdt_init(void) {
    memset(g_gdt, 0, sizeof(g_gdt));
    memset(&g_tss, 0, sizeof(g_tss));

    g_gdt[0] = 0x0000000000000000ULL; /* 0x00: Null */
    g_gdt[1] = 0x00CF9A000000FFFFULL; /* 0x08: 32-bit Code */
    g_gdt[2] = 0x00CF92000000FFFFULL; /* 0x10: 32-bit Data */
    g_gdt[3] = 0x00AF9A000000FFFFULL; /* 0x18: Kernel 64-bit Code (DPL=0) */
    g_gdt[4] = 0x00AF92000000FFFFULL; /* 0x20: Kernel 64-bit Data (DPL=0) */
    g_gdt[5] = 0x00AFF2000000FFFFULL; /* 0x28: User 64-bit Data   (DPL=3) */
    g_gdt[6] = 0x00AFFA000000FFFFULL; /* 0x30: User 64-bit Code   (DPL=3) */

    g_tss.rsp0 = (uint64_t)(g_default_rsp0_stack + sizeof(g_default_rsp0_stack));
    g_tss.iomap_base = (uint16_t)sizeof(tss64_t);
    gdt_set_tss_desc(7, (uint64_t)&g_tss, (uint32_t)(sizeof(tss64_t) - 1));

    g_gdtr.limit = (uint16_t)(sizeof(g_gdt) - 1);
    g_gdtr.base  = (uint64_t)&g_gdt[0];

    __asm__ volatile (
        "lgdt %0\n"
        "mov $0x20, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        "xor %%ax, %%ax\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "pushq $0x18\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "mov $0x38, %%ax\n"
        "ltr %%ax\n"
        :
        : "m"(g_gdtr)
        : "rax", "memory"
    );
}

void gdt_set_tss_rsp0(uint64_t rsp0) {
    if (rsp0) {
        g_tss.rsp0 = rsp0;
    }
}

uint64_t gdt_get_tss_rsp0(void) {
    return g_tss.rsp0;
}
