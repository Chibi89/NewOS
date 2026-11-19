#ifndef QJS_SHIM_STDIO_H
#define QJS_SHIM_STDIO_H

#include <stddef.h>
#include <stdarg.h>

typedef struct { int unused; } FILE;
extern FILE *stdout;
extern FILE *stderr;
extern FILE *stdin;

#ifndef EOF
#define EOF (-1)
#endif

int snprintf(char *str, size_t size, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int vfprintf(FILE *stream, const char *format, va_list ap);
int sprintf(char *str, const char *format, ...);
int sscanf(const char *str, const char *format, ...);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fputc(int c, FILE *stream);
int fputs(const char *s, FILE *stream);
int putchar(int c);
int puts(const char *s);
int fflush(FILE *stream);

#endif
