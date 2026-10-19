#ifndef ARCH_X86_64_CPU_H
#define ARCH_X86_64_CPU_H

#include <types.h>

typedef struct {
    char vendor[16];
    char brand[64];
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    uint32_t logical_cores;
    bool has_sse;
    bool has_sse2;
    bool has_sse3;
    bool has_avx;
    bool has_fpu;
} cpu_info_t;

void cpu_init(void);
const cpu_info_t* cpu_get_info(void);

#endif // ARCH_X86_64_CPU_H
