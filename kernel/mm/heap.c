#include <mm/heap.h>
#include <string.h>

typedef struct block_header {
    size_t size;
    bool is_free;
    struct block_header* next;
    uint64_t reserved_align; // Garantisce sizeof(block_header_t) == 32 (allineato a 16 byte per SSE2)
} block_header_t;

static uint8_t* heap_start = NULL;
static size_t   heap_total_size = 0;
static block_header_t* free_list = NULL;
static volatile int g_heap_lock = 0;

static inline uint64_t heap_lock_irq(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    while (__atomic_test_and_set(&g_heap_lock, __ATOMIC_ACQUIRE)) {
        __asm__ volatile ("pause");
    }
    return flags;
}

static inline void heap_unlock_irq(uint64_t flags) {
    __atomic_clear(&g_heap_lock, __ATOMIC_RELEASE);
    if (flags & (1ULL << 9)) {
        __asm__ volatile ("sti" ::: "memory");
    }
}

void heap_init(uint64_t start_address, size_t size) {
    uint64_t aligned = (start_address + 15ULL) & ~15ULL;
    size_t diff = (size_t)(aligned - start_address);
    if (size <= diff + sizeof(block_header_t)) return;
    heap_start = (uint8_t*)aligned;
    heap_total_size = size - diff;

    free_list = (block_header_t*)heap_start;
    free_list->size = heap_total_size - sizeof(block_header_t);
    free_list->is_free = true;
    free_list->next = NULL;
    free_list->reserved_align = 0;
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;

    // Allineamento a 16 byte
    size = (size + 15) & ~15;
    uint64_t irq = heap_lock_irq();

    block_header_t* curr = free_list;
    while (curr != NULL) {
        if (curr->is_free && curr->size >= size) {
            // Se lo spazio restante è abbastanza grande per un nuovo blocco, dividi
            if (curr->size >= size + sizeof(block_header_t) + 32) {
                block_header_t* new_block = (block_header_t*)((uint8_t*)curr + sizeof(block_header_t) + size);
                new_block->size = curr->size - size - sizeof(block_header_t);
                new_block->is_free = true;
                new_block->next = curr->next;

                curr->size = size;
                curr->is_free = false;
                curr->next = new_block;
            } else {
                curr->is_free = false;
            }
            void* ret = (void*)((uint8_t*)curr + sizeof(block_header_t));
            heap_unlock_irq(irq);
            return ret;
        }
        curr = curr->next;
    }
    heap_unlock_irq(irq);
    return NULL; // Out of memory
}

void* kmalloc_aligned(size_t size, size_t alignment) {
    if (size == 0) return NULL;
    if (alignment <= 16) return kmalloc(size);

    // Assicura che alignment sia una potenza di 2
    if ((alignment & (alignment - 1)) != 0) {
        size_t p = 16;
        while (p < alignment) p <<= 1;
        alignment = p;
    }

    size = (size + 15) & ~15;
    size_t hdr_sz = sizeof(block_header_t);
    uint64_t irq = heap_lock_irq();

    block_header_t* curr = free_list;
    while (curr != NULL) {
        if (curr->is_free) {
            uintptr_t data_addr = (uintptr_t)curr + hdr_sz;
            if ((data_addr & (alignment - 1)) == 0) {
                if (curr->size >= size) {
                    if (curr->size >= size + hdr_sz + 32) {
                        block_header_t* rem = (block_header_t*)(data_addr + size);
                        rem->size = curr->size - size - hdr_sz;
                        rem->is_free = true;
                        rem->next = curr->next;
                        rem->reserved_align = 0;
                        curr->size = size;
                        curr->next = rem;
                    }
                    curr->is_free = false;
                    heap_unlock_irq(irq);
                    return (void*)data_addr;
                }
            } else {
                // Calcola un indirizzo dati allineato che lasci spazio a un nuovo header prima di esso
                uintptr_t min_aligned = data_addr + hdr_sz + 32;
                uintptr_t aligned_data = (min_aligned + alignment - 1) & ~(alignment - 1);
                size_t pad = (size_t)(aligned_data - data_addr);
                if (curr->size >= pad + size) {
                    block_header_t* aligned_hdr = (block_header_t*)(aligned_data - hdr_sz);
                    size_t avail_after_hdr = curr->size - pad;

                    aligned_hdr->size = avail_after_hdr;
                    aligned_hdr->is_free = false;
                    aligned_hdr->next = curr->next;
                    aligned_hdr->reserved_align = 0;

                    if (avail_after_hdr >= size + hdr_sz + 32) {
                        block_header_t* tail = (block_header_t*)(aligned_data + size);
                        tail->size = avail_after_hdr - size - hdr_sz;
                        tail->is_free = true;
                        tail->next = curr->next;
                        tail->reserved_align = 0;
                        aligned_hdr->size = size;
                        aligned_hdr->next = tail;
                    }

                    curr->size = pad - hdr_sz;
                    curr->is_free = true;
                    curr->next = aligned_hdr;
                    heap_unlock_irq(irq);
                    return (void*)aligned_data;
                }
            }
        }
        curr = curr->next;
    }
    heap_unlock_irq(irq);
    return NULL;
}

void* kcalloc(size_t num, size_t size) {
    size_t total = num * size;
    void* ptr = kmalloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void kfree(void* ptr) {
    if (!ptr) return;
    uint64_t irq = heap_lock_irq();

    block_header_t* block = (block_header_t*)((uint8_t*)ptr - sizeof(block_header_t));
    block->is_free = true;

    // Coalescenza con i blocchi liberi adiacenti
    block_header_t* curr = free_list;
    while (curr != NULL) {
        if (curr->is_free && curr->next && curr->next->is_free) {
            curr->size += sizeof(block_header_t) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
    heap_unlock_irq(irq);
}
