#ifndef QJS_SHIM_STDLIB_H
#define QJS_SHIM_STDLIB_H

#include <stddef.h>

#ifndef alloca
#define alloca(sz) __builtin_alloca(sz)
#endif

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

void abort(void);
void exit(int status);
int abs(int j);
long labs(long j);
long long llabs(long long j);

long strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
double strtod(const char *nptr, char **endptr);
int atoi(const char *nptr);

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
char *getenv(const char *name);

#endif
