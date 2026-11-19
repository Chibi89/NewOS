#ifndef SYSTEM_LIBRARY_DLL_LIBC_H
#define SYSTEM_LIBRARY_DLL_LIBC_H

#include <types.h>
#include <stdarg.h>
#include <gui/screen_mgr.h>

static const kernel_api_t *g_dll_api = NULL;

// Funzioni freestanding string/memory per moduli out-of-kernel
static inline void *dll_memset(void *dst, int c, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)c;
    return dst;
}

static inline void *dll_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static inline int dll_memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return (pa[i] < pb[i]) ? -1 : 1;
    }
    return 0;
}

static inline size_t dll_strlen(const char *s) {
    size_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static inline char *dll_strcpy(char *dst, const char *src) {
    char *r = dst;
    while ((*dst++ = *src++));
    return r;
}

static inline char *dll_strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

static inline char *dll_strncat(char *dst, const char *src, size_t n) {
    size_t dl = dll_strlen(dst);
    size_t i = 0;
    while (i < n && src[i]) { dst[dl + i] = src[i]; i++; }
    dst[dl + i] = '\0';
    return dst;
}

static inline char *dll_strcat(char *dst, const char *src) {
    size_t dl = dll_strlen(dst);
    dll_strcpy(dst + dl, src);
    return dst;
}

static inline char *dll_itoa(int value, char *str, int base) {
    if (base < 2 || base > 36) { *str = '\0'; return str; }
    char *p = str;
    unsigned int v = (value < 0 && base == 10) ? (unsigned int)(-value) : (unsigned int)value;
    if (value < 0 && base == 10) *p++ = '-';
    char tmp[36]; int ti = 0;
    const char *digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    do { tmp[ti++] = digits[v % (unsigned)base]; v /= (unsigned)base; } while (v && ti < 35);
    while (ti > 0) *p++ = tmp[--ti];
    *p = '\0';
    return str;
}

static inline int dll_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static inline int dll_strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == '\0') return 0;
    }
    return 0;
}

static inline char dll_tolower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static inline int dll_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = dll_tolower(*a), cb = dll_tolower(*b);
        if (ca != cb) return (unsigned char)ca - (unsigned char)cb;
        a++; b++;
    }
    return (unsigned char)dll_tolower(*a) - (unsigned char)dll_tolower(*b);
}

static inline int dll_strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char ca = dll_tolower(a[i]), cb = dll_tolower(b[i]);
        if (ca != cb) return (unsigned char)ca - (unsigned char)cb;
        if (ca == '\0') return 0;
    }
    return 0;
}

static inline char *dll_strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == 0) ? (char *)s : NULL;
}

static inline char *dll_strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    return (c == 0) ? (char *)s : (char *)last;
}

static inline char *dll_strstr(const char *hay, const char *needle) {
    if (!*needle) return (char *)hay;
    for (const char *p = hay; *p; p++) {
        const char *h = p, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char *)p;
    }
    return NULL;
}

static inline int dll_snprintf(char *buf, size_t size, const char *fmt, ...) {
    if (!buf || size == 0) return 0;
    va_list ap;
    va_start(ap, fmt);
    size_t pos = 0;
    for (const char *p = fmt; *p && pos + 1 < size; p++) {
        if (*p != '%') { buf[pos++] = *p; continue; }
        p++;
        if (!*p) break;
        if (*p == '%') { buf[pos++] = '%'; continue; }
        if (*p == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s && pos + 1 < size) buf[pos++] = *s++;
        } else if (*p == 'd' || *p == 'i') {
            int v = va_arg(ap, int);
            if (v < 0) { buf[pos++] = '-'; v = -v; }
            char tmp[16]; int ti = 0;
            do { tmp[ti++] = (char)('0' + (v % 10)); v /= 10; } while (v && ti < 16);
            while (ti > 0 && pos + 1 < size) buf[pos++] = tmp[--ti];
        } else if (*p == 'u') {
            unsigned int v = va_arg(ap, unsigned int);
            char tmp[16]; int ti = 0;
            do { tmp[ti++] = (char)('0' + (v % 10)); v /= 10; } while (v && ti < 16);
            while (ti > 0 && pos + 1 < size) buf[pos++] = tmp[--ti];
        } else if (*p == 'x' || *p == 'X') {
            unsigned int v = va_arg(ap, unsigned int);
            const char *hx = (*p == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
            char tmp[16]; int ti = 0;
            do { tmp[ti++] = hx[v & 0xF]; v >>= 4; } while (v && ti < 16);
            while (ti > 0 && pos + 1 < size) buf[pos++] = tmp[--ti];
        } else if (*p == 'c') {
            char c = (char)va_arg(ap, int);
            buf[pos++] = c;
        } else {
            buf[pos++] = *p;
        }
    }
    buf[pos] = '\0';
    va_end(ap);
    return (int)pos;
}

#endif
