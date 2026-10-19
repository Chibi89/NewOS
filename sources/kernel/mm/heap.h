#ifndef MM_HEAP_H
#define MM_HEAP_H

#include <types.h>

void heap_init(uint64_t start_address, size_t size);
void* kmalloc(size_t size);
void* kmalloc_aligned(size_t size, size_t alignment);
void* kcalloc(size_t num, size_t size);
void kfree(void* ptr);

#endif // MM_HEAP_H
