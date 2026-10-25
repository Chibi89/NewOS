#ifndef MM_PAGING_H
#define MM_PAGING_H

#include <types.h>

#define PAGE_SIZE              4096
#define PAGE_ENTRIES_PER_TABLE 512

// Flag dei descrittori di pagina x86_64
#define PAGE_PRESENT           (1ULL << 0)
#define PAGE_WRITABLE          (1ULL << 1)
#define PAGE_USER              (1ULL << 2)
#define PAGE_WRITETHROUGH      (1ULL << 3)
#define PAGE_NO_CACHE          (1ULL << 4)
#define PAGE_ACCESSED          (1ULL << 5)
#define PAGE_DIRTY             (1ULL << 6)
#define PAGE_HUGE              (1ULL << 7)
#define PAGE_GLOBAL            (1ULL << 8)
#define PAGE_NX                (1ULL << 63)

// Selettori Segmenti GDT per Ring 0 e Ring 3
#define GDT_KERNEL_CODE_SEL    0x18
#define GDT_KERNEL_DATA_SEL    0x20
#define GDT_USER_DATA_SEL      0x2B // 0x28 | 3
#define GDT_USER_CODE_SEL      0x33 // 0x30 | 3

typedef struct {
    uint64_t cr3_phys;
    uint64_t total_mapped_pages;
    uint64_t user_mapped_pages;
} vmm_stats_t;

// Inizializzazione del sottosistema di paginazione e VMM
void     paging_init(void);
uint64_t paging_get_kernel_cr3(void);
void     paging_get_stats(vmm_stats_t* stats);

// Gestione degli spazi di indirizzamento (Address Spaces)
uint64_t* paging_create_address_space(void);
void      paging_destroy_address_space(uint64_t* pml4);
void      paging_switch(uint64_t cr3_phys);

// Mappatura e smontaggio pagine
bool     paging_map_page(uint64_t* pml4, uint64_t virt, uint64_t phys, uint64_t flags);
bool     paging_unmap_page(uint64_t* pml4, uint64_t virt);
uint64_t paging_get_phys(uint64_t* pml4, uint64_t virt);

// Invalida TLB per un singolo indirizzo virtuale (invlpg)
void     paging_invalidate_page(uint64_t virt);

#endif // MM_PAGING_H
