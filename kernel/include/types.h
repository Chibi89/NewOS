#ifndef TYPES_H
#define TYPES_H

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

typedef unsigned long long size_t;
typedef signed long long   ssize_t;
typedef unsigned long long uintptr_t;

#define NULL ((void*)0)

typedef enum {
    false = 0,
    true = 1
} bool;

// Struttura BootInfo popolata da Stage 2 a 0x9000
typedef struct {
    uint64_t framebuffer_base;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint64_t total_memory_bytes;
} __attribute__((packed)) BootInfo;

const BootInfo* kernel_get_boot_info(void);

#endif // TYPES_H
