#include <arch/x86_64/cpu.h>
#include <string.h>

static cpu_info_t g_cpu_info;
static bool g_cpu_initialized = false;

static inline void x86_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx) {
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(subleaf));
}

void cpu_init(void) {
    if (g_cpu_initialized) return;
    memset(&g_cpu_info, 0, sizeof(cpu_info_t));

    uint32_t eax, ebx, ecx, edx;

    // 1. Vendor String (Leaf 0)
    x86_cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    uint32_t max_leaf = eax;

    // EBX, EDX, ECX
    char* v = g_cpu_info.vendor;
    *(uint32_t*)(v + 0) = ebx;
    *(uint32_t*)(v + 4) = edx;
    *(uint32_t*)(v + 8) = ecx;
    v[12] = '\0';

    // 2. Family, Model, Stepping & Features (Leaf 1)
    if (max_leaf >= 1) {
        x86_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
        g_cpu_info.stepping = eax & 0x0F;
        g_cpu_info.model = (eax >> 4) & 0x0F;
        g_cpu_info.family = (eax >> 8) & 0x0F;

        uint32_t ext_model = (eax >> 16) & 0x0F;
        uint32_t ext_family = (eax >> 20) & 0xFF;
        if (g_cpu_info.family == 6 || g_cpu_info.family == 15) {
            g_cpu_info.model |= (ext_model << 4);
        }
        if (g_cpu_info.family == 15) {
            g_cpu_info.family += ext_family;
        }

        g_cpu_info.logical_cores = (ebx >> 16) & 0xFF;
        if (g_cpu_info.logical_cores == 0) g_cpu_info.logical_cores = 1;

        g_cpu_info.has_fpu = (edx & (1 << 0)) != 0;
        g_cpu_info.has_sse = (edx & (1 << 25)) != 0;
        g_cpu_info.has_sse2 = (edx & (1 << 26)) != 0;
        g_cpu_info.has_sse3 = (ecx & (1 << 0)) != 0;
        g_cpu_info.has_avx = (ecx & (1 << 28)) != 0;
    }

    // 3. Extended Brand String (Leaf 0x80000000..0x80000004)
    x86_cpuid(0x80000000, 0, &eax, &ebx, &ecx, &edx);
    uint32_t max_ext_leaf = eax;

    if (max_ext_leaf >= 0x80000004) {
        char raw_brand[49];
        memset(raw_brand, 0, sizeof(raw_brand));

        for (uint32_t leaf = 0x80000002; leaf <= 0x80000004; leaf++) {
            x86_cpuid(leaf, 0, &eax, &ebx, &ecx, &edx);
            uint32_t off = (leaf - 0x80000002) * 16;
            *(uint32_t*)(raw_brand + off + 0) = eax;
            *(uint32_t*)(raw_brand + off + 4) = ebx;
            *(uint32_t*)(raw_brand + off + 8) = ecx;
            *(uint32_t*)(raw_brand + off + 12) = edx;
        }
        raw_brand[48] = '\0';

        // Pulisce spazi bianchi iniziali superflui
        const char* p = raw_brand;
        while (*p == ' ') p++;
        strncpy(g_cpu_info.brand, p, sizeof(g_cpu_info.brand) - 1);
        g_cpu_info.brand[sizeof(g_cpu_info.brand) - 1] = '\0';
    }

    if (g_cpu_info.brand[0] == '\0') {
        // Fallback a vendor generico
        mini_snprintf(g_cpu_info.brand, sizeof(g_cpu_info.brand), "%s x86_64 Processor", g_cpu_info.vendor);
    }

    g_cpu_initialized = true;
}

const cpu_info_t* cpu_get_info(void) {
    if (!g_cpu_initialized) cpu_init();
    return &g_cpu_info;
}
