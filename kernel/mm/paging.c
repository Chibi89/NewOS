#include <mm/paging.h>
#include <mm/heap.h>
#include <string.h>

static uint64_t    g_kernel_cr3 = 0;
static vmm_stats_t g_vmm_stats = {0, 0, 0};

// Alloca una pagina fisica allineata a 4096 byte
static uint64_t* alloc_aligned_page(void) {
    void* ptr = kmalloc_aligned(PAGE_SIZE, PAGE_SIZE);
    if (!ptr) return NULL;
    memset(ptr, 0, PAGE_SIZE);
    return (uint64_t*)ptr;
}

void paging_init(void) {
    uint64_t cr3_val = 0;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3_val));
    g_kernel_cr3 = cr3_val;

    g_vmm_stats.cr3_phys = cr3_val;
    g_vmm_stats.total_mapped_pages = 2048; // 2048 pagine huge da 2MB (4GB)
    g_vmm_stats.user_mapped_pages = 0;
}

uint64_t paging_get_kernel_cr3(void) {
    return g_kernel_cr3;
}

void paging_get_stats(vmm_stats_t* stats) {
    if (stats) {
        *stats = g_vmm_stats;
    }
}

uint64_t* paging_create_address_space(void) {
    uint64_t* new_pml4 = alloc_aligned_page();
    if (!new_pml4) return NULL;

    // Se la PML4 del kernel e' nota, clona i mapping del kernel
    if (g_kernel_cr3) {
        uint64_t* kern_pml4 = (uint64_t*)g_kernel_cr3;
        // Ingressi bassi (Identity mapping 0..4GB) condivisi in Ring 0
        new_pml4[0] = kern_pml4[0];
        // Ingressi alti (Higher Half kernel se configurati)
        for (int i = 256; i < 512; i++) {
            new_pml4[i] = kern_pml4[i];
        }
    }

    return new_pml4;
}

void paging_destroy_address_space(uint64_t* pml4) {
    if (!pml4 || (uint64_t)pml4 == g_kernel_cr3) return;
    // Non deallocare gli ingressi del kernel (0 e 256..511)
    kfree(pml4);
}

void paging_switch(uint64_t cr3_phys) {
    if (cr3_phys == 0) cr3_phys = g_kernel_cr3;
    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3_phys) : "memory");
}

void paging_invalidate_page(uint64_t virt) {
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

bool paging_map_page(uint64_t* pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!pml4) {
        pml4 = (uint64_t*)g_kernel_cr3;
    }
    if (!pml4) return false;

    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    // 1. PML4 -> PDPT
    if (!(pml4[pml4_idx] & PAGE_PRESENT)) {
        uint64_t* pdpt = alloc_aligned_page();
        if (!pdpt) return false;
        pml4[pml4_idx] = (uint64_t)pdpt | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    } else {
        if (flags & PAGE_USER) pml4[pml4_idx] |= PAGE_USER;
    }
    uint64_t* pdpt = (uint64_t*)(pml4[pml4_idx] & ~0xFFFULL);

    // 2. PDPT -> PD
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) {
        uint64_t* pd = alloc_aligned_page();
        if (!pd) return false;
        pdpt[pdpt_idx] = (uint64_t)pd | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    } else {
        if (flags & PAGE_USER) pdpt[pdpt_idx] |= PAGE_USER;
    }
    uint64_t* pd = (uint64_t*)(pdpt[pdpt_idx] & ~0xFFFULL);

    // 3. PD -> PT
    if (!(pd[pd_idx] & PAGE_PRESENT)) {
        uint64_t* pt = alloc_aligned_page();
        if (!pt) return false;
        pd[pd_idx] = (uint64_t)pt | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    } else {
        if (flags & PAGE_USER) pd[pd_idx] |= PAGE_USER;
    }
    uint64_t* pt = (uint64_t*)(pd[pd_idx] & ~0xFFFULL);

    // 4. PT -> Pagina Fisica a 4KB
    pt[pt_idx] = (phys & ~0xFFFULL) | flags | PAGE_PRESENT;
    paging_invalidate_page(virt);

    if (flags & PAGE_USER) {
        g_vmm_stats.user_mapped_pages++;
    }
    g_vmm_stats.total_mapped_pages++;

    return true;
}

bool paging_unmap_page(uint64_t* pml4, uint64_t virt) {
    if (!pml4) pml4 = (uint64_t*)g_kernel_cr3;
    if (!pml4) return false;

    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & PAGE_PRESENT)) return false;
    uint64_t* pdpt = (uint64_t*)(pml4[pml4_idx] & ~0xFFFULL);

    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) return false;
    uint64_t* pd = (uint64_t*)(pdpt[pdpt_idx] & ~0xFFFULL);

    if (!(pd[pd_idx] & PAGE_PRESENT)) return false;
    uint64_t* pt = (uint64_t*)(pd[pd_idx] & ~0xFFFULL);

    if (!(pt[pt_idx] & PAGE_PRESENT)) return false;

    if (pt[pt_idx] & PAGE_USER) {
        if (g_vmm_stats.user_mapped_pages > 0) g_vmm_stats.user_mapped_pages--;
    }
    if (g_vmm_stats.total_mapped_pages > 0) g_vmm_stats.total_mapped_pages--;

    pt[pt_idx] = 0;
    paging_invalidate_page(virt);
    return true;
}

uint64_t paging_get_phys(uint64_t* pml4, uint64_t virt) {
    if (!pml4) pml4 = (uint64_t*)g_kernel_cr3;
    if (!pml4) return 0;

    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & PAGE_PRESENT)) return 0;
    uint64_t* pdpt = (uint64_t*)(pml4[pml4_idx] & ~0xFFFULL);

    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) return 0;
    uint64_t* pd = (uint64_t*)(pdpt[pdpt_idx] & ~0xFFFULL);

    if (!(pd[pd_idx] & PAGE_PRESENT)) return 0;
    // Se e' una Huge Page da 2MB
    if (pd[pd_idx] & PAGE_HUGE) {
        return (pd[pd_idx] & ~0x1FFFFFULL) | (virt & 0x1FFFFFULL);
    }

    uint64_t* pt = (uint64_t*)(pd[pd_idx] & ~0xFFFULL);
    if (!(pt[pt_idx] & PAGE_PRESENT)) return 0;

    return (pt[pt_idx] & ~0xFFFULL) | (virt & 0xFFFULL);
}

volatile void* paging_map_mmio(uint64_t phys_addr, uint64_t size) {
    if (phys_addr == 0 || size == 0) return NULL;
    uint64_t start = phys_addr & ~0xFFFULL;
    uint64_t end = (phys_addr + size + 0xFFFULL) & ~0xFFFULL;
    uint64_t* pml4 = (uint64_t*)g_kernel_cr3;

    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
        if (pml4) {
            uint64_t pml4_idx = (addr >> 39) & 0x1FF;
            uint64_t pdpt_idx = (addr >> 30) & 0x1FF;
            uint64_t pd_idx   = (addr >> 21) & 0x1FF;
            if (pml4[pml4_idx] & PAGE_PRESENT) {
                uint64_t* pdpt = (uint64_t*)(pml4[pml4_idx] & ~0xFFFULL);
                if (pdpt[pdpt_idx] & PAGE_PRESENT) {
                    uint64_t* pd = (uint64_t*)(pdpt[pdpt_idx] & ~0xFFFULL);
                    if ((pd[pd_idx] & (PAGE_PRESENT | PAGE_HUGE)) == (PAGE_PRESENT | PAGE_HUGE)) {
                        pd[pd_idx] |= (PAGE_WRITABLE | PAGE_NO_CACHE | PAGE_WRITETHROUGH);
                        paging_invalidate_page(addr);
                        continue;
                    }
                }
            }
        }
        paging_map_page(NULL, addr, addr, PAGE_PRESENT | PAGE_WRITABLE | PAGE_NO_CACHE | PAGE_WRITETHROUGH);
    }
    return (volatile void*)phys_addr;
}

void* dma_alloc(size_t size, size_t align, uint64_t* out_phys) {
    if (align < 16) align = 16;
    void* virt = kmalloc_aligned(size, align);
    if (!virt) {
        if (out_phys) *out_phys = 0;
        return NULL;
    }
    memset(virt, 0, size);
    if (out_phys) {
        uint64_t phys = paging_get_phys(NULL, (uint64_t)virt);
        *out_phys = phys ? phys : (uint64_t)virt;
    }
    return virt;
}
