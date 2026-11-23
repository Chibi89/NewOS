/**
 * newos/mem.h — Namespace Memoria: Heap, Arena Allocator e Ring Buffer freestanding
 */
#ifndef NEWOS_SDK_MEM_H
#define NEWOS_SDK_MEM_H

#include "types.h"
#include <nsrt.h>

typedef struct {
    uint8_t* base;
    size_t   size;
    size_t   offset;
} ns_arena_t;

typedef struct {
    uint8_t* buf;
    size_t   capacity;
    size_t   head;
    size_t   tail;
} ns_ringbuf_t;

static inline void ns_arena_init(ns_arena_t* a, void* backing_buf, size_t size) {
    if (!a) return;
    a->base = (uint8_t*)backing_buf;
    a->size = size;
    a->offset = 0;
}

static inline void* ns_arena_alloc(ns_arena_t* a, size_t size, size_t align) {
    if (!a || !a->base || size == 0) return NULL;
    size_t al = (align > 0) ? align : 8;
    size_t cur = NS_ALIGN_UP(a->offset, al);
    if (cur + size > a->size) return NULL;
    void* ptr = a->base + cur;
    a->offset = cur + size;
    return ptr;
}

static inline void ns_arena_reset(ns_arena_t* a) {
    if (a) a->offset = 0;
}

static inline void ns_ringbuf_init(ns_ringbuf_t* rb, void* buf, size_t cap) {
    if (!rb) return;
    rb->buf = (uint8_t*)buf;
    rb->capacity = cap;
    rb->head = 0;
    rb->tail = 0;
}

static inline bool ns_ringbuf_push(ns_ringbuf_t* rb, uint8_t byte) {
    if (!rb || !rb->buf || rb->capacity < 2) return false;
    size_t next = (rb->head + 1) % rb->capacity;
    if (next == rb->tail) return false;
    rb->buf[rb->head] = byte;
    rb->head = next;
    return true;
}

static inline bool ns_ringbuf_pop(ns_ringbuf_t* rb, uint8_t* out_byte) {
    if (!rb || !rb->buf || rb->head == rb->tail) return false;
    if (out_byte) *out_byte = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) % rb->capacity;
    return true;
}

#endif /* NEWOS_SDK_MEM_H */
