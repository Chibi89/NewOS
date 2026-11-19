#ifndef NS_SHIM_STDIO_H
#define NS_SHIM_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct { int unused; } FILE;
#define stderr ((FILE*)0)
#define stdout ((FILE*)0)
int snprintf(char *str, size_t size, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
static inline int printf(const char *fmt, ...) { (void)fmt; return 0; }
static inline int fprintf(FILE *f, const char *fmt, ...) { (void)f; (void)fmt; return 0; }
#endif
