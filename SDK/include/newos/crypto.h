/**
 * newos/crypto.h — Namespace Crittografia, Hash e Checksum Freestanding
 */
#ifndef NEWOS_SDK_CRYPTO_H
#define NEWOS_SDK_CRYPTO_H

#include "types.h"

static inline uint32_t ns_fnv1a_32(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t h = 2166136261u;
    if (!p) return 0;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static inline uint64_t ns_fnv1a_64(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint64_t h = 14695981039346656037ULL;
    if (!p) return 0;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static inline void ns_hex_encode(const uint8_t* src, size_t len, char* dst_hex) {
    static const char hex[] = "0123456789ABCDEF";
    if (!src || !dst_hex) return;
    for (size_t i = 0; i < len; i++) {
        dst_hex[i * 2]     = hex[(src[i] >> 4) & 0xF];
        dst_hex[i * 2 + 1] = hex[src[i] & 0xF];
    }
    dst_hex[len * 2] = '\0';
}

#endif /* NEWOS_SDK_CRYPTO_H */
