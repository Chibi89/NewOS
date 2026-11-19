#ifndef NS_SHIM_ICONV_H
#define NS_SHIM_ICONV_H
#include <stddef.h>
typedef void* iconv_t;
static inline iconv_t iconv_open(const char *t, const char *f) { (void)t; (void)f; return (iconv_t)(intptr_t)-1; }
static inline size_t iconv(iconv_t cd, char **ib, size_t *il, char **ob, size_t *ol) {
    (void)cd; (void)ib; (void)il; (void)ob; (void)ol; return (size_t)-1;
}
static inline int iconv_close(iconv_t cd) { (void)cd; return 0; }
#endif
