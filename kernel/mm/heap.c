#include <mm/heap.h>
#include <string.h>

typedef struct block_header {
    size_t size;
    bool is_free;
    struct block_header* next;
} block_header_t;

static uint8_t* heap_start = NULL;
static size_t   heap_total_size = 0;
static block_header_t* free_list = NULL;

void heap_init(uint64_t start_address, size_t size) {
    heap_start = (uint8_t*)start_address;
    heap_total_size = size;

    free_list = (block_header_t*)heap_start;
    free_list->size = size - sizeof(block_header_t);
    free_list->is_free = true;
    free_list->next = NULL;
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;

    // Allineamento a 16 byte
    size = (size + 15) & ~15;

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
            return (void*)((uint8_t*)curr + sizeof(block_header_t));
        }
        curr = curr->next;
    }
    return NULL; // Out of memory
}

void* kmalloc_aligned(size_t size, size_t alignment) {
    (void)alignment;
    return kmalloc(size);
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
}
