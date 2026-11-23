/**
 * newos/string.h — Helper stringa freestanding appoggiati a NSRT.DLL
 */
#ifndef NEWOS_SDK_STRING_H
#define NEWOS_SDK_STRING_H

#include <nsrt.h>

static inline bool ns_str_eq(const nsrt_api_t* rt, const char* a, const char* b) {
    return (rt && rt->strcmp) ? (rt->strcmp(a, b) == 0) : false;
}

static inline bool ns_str_starts_with(const nsrt_api_t* rt, const char* s, const char* pfx) {
    if (!rt || !rt->strlen || !rt->strncmp || !s || !pfx) return false;
    size_t len = rt->strlen(pfx);
    return rt->strncmp(s, pfx, len) == 0;
}

#endif /* NEWOS_SDK_STRING_H */
