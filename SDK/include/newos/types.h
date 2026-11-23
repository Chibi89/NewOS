/**
 * newos/types.h — Copertura completa tipi di dato freestanding C99/C11 & NewOS V1 (x86_64)
 */
#ifndef NEWOS_SDK_TYPES_H
#define NEWOS_SDK_TYPES_H

#ifndef _NEWOS_TYPES_DEFINED
#define _NEWOS_TYPES_DEFINED

/* Tipi interi esatti */
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

/* Tipi least & fast */
typedef int8_t    int_least8_t;
typedef int16_t   int_least16_t;
typedef int32_t   int_least32_t;
typedef int64_t   int_least64_t;
typedef uint8_t   uint_least8_t;
typedef uint16_t  uint_least16_t;
typedef uint32_t  uint_least32_t;
typedef uint64_t  uint_least64_t;

typedef int8_t    int_fast8_t;
typedef int64_t   int_fast16_t;
typedef int64_t   int_fast32_t;
typedef int64_t   int_fast64_t;
typedef uint8_t   uint_fast8_t;
typedef uint64_t  uint_fast16_t;
typedef uint64_t  uint_fast32_t;
typedef uint64_t  uint_fast64_t;

/* Tipi puntatore, dimensione, differenza & max */
typedef uint64_t  size_t;
typedef int64_t   ssize_t;
typedef uint64_t  uintptr_t;
typedef int64_t   intptr_t;
typedef int64_t   ptrdiff_t;
typedef int64_t   intmax_t;
typedef uint64_t  uintmax_t;

/* Tipi carattere estesi & allineamento */
typedef uint16_t  char16_t;
typedef uint32_t  char32_t;
typedef uint16_t  wchar_t;
typedef long long max_align_t;

#ifndef __cplusplus
typedef _Bool bool;
#define true  1
#define false 0
#endif

#ifndef NULL
#define NULL ((void*)0)
#endif

#define offsetof(type, member) __builtin_offsetof(type, member)

/* Limiti interi standard */
#define CHAR_BIT    8
#define SCHAR_MIN   (-128)
#define SCHAR_MAX   127
#define UCHAR_MAX   255
#define SHRT_MIN    (-32768)
#define SHRT_MAX    32767
#define USHRT_MAX   65535
#define INT_MIN     (-2147483647 - 1)
#define INT_MAX     2147483647
#define UINT_MAX    4294967295U
#define LLONG_MIN   (-9223372036854775807LL - 1LL)
#define LLONG_MAX   9223372036854775807LL
#define ULLONG_MAX  18446744073709551615ULL

#define INT8_MIN    SCHAR_MIN
#define INT8_MAX    SCHAR_MAX
#define UINT8_MAX   UCHAR_MAX
#define INT16_MIN   SHRT_MIN
#define INT16_MAX   SHRT_MAX
#define UINT16_MAX  USHRT_MAX
#define INT32_MIN   INT_MIN
#define INT32_MAX   INT_MAX
#define UINT32_MAX  UINT_MAX
#define INT64_MIN   LLONG_MIN
#define INT64_MAX   LLONG_MAX
#define UINT64_MAX  ULLONG_MAX
#define SIZE_MAX    ULLONG_MAX
#define INTPTR_MIN  LLONG_MIN
#define INTPTR_MAX  LLONG_MAX
#define UINTPTR_MAX ULLONG_MAX
#define PTRDIFF_MIN LLONG_MIN
#define PTRDIFF_MAX LLONG_MAX

#endif /* _NEWOS_TYPES_DEFINED */

/* Tipi di dato fondamentali NewSoft SDK */
typedef void*    ns_handle_t;
typedef int32_t  ns_status_t;
typedef uint32_t ns_color32_t;
typedef uint64_t ns_timestamp_t;
typedef uint32_t ns_flags_t;

typedef struct {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    uint16_t build;
} ns_version_t;

typedef struct {
    uint8_t bytes[16];
} ns_uuid_t;

typedef struct {
    uint8_t* data;
    size_t   size;
    size_t   capacity;
} ns_buffer_t;

typedef struct {
    const char* ptr;
    size_t      len;
} ns_strview_t;

typedef struct {
    int32_t key;
    int32_t value;
} ns_kv_int_t;

typedef struct {
    const char* key;
    const char* value;
} ns_kv_str_t;

/* Macro di utilita' */
#define NS_ARRAY_LEN(arr)      (sizeof(arr) / sizeof((arr)[0]))
#define NS_MIN(a, b)           (((a) < (b)) ? (a) : (b))
#define NS_MAX(a, b)           (((a) > (b)) ? (a) : (b))
#define NS_CLAMP(v, lo, hi)    (NS_MAX((lo), NS_MIN((v), (hi))))
#define NS_BIT(n)              (1ULL << (n))
#define NS_ALIGN_UP(x, a)      (((x) + ((a) - 1)) & ~((a) - 1))
#define NS_ALIGN_DOWN(x, a)    ((x) & ~((a) - 1))
#define NS_UNUSED(x)           ((void)(x))
#define NS_PACKED              __attribute__((packed))
#define NS_ALIGNED(n)          __attribute__((aligned(n)))
#define NS_LIKELY(x)           __builtin_expect(!!(x), 1)
#define NS_UNLIKELY(x)         __builtin_expect(!!(x), 0)
#define NS_STATIC_ASSERT(c, m) _Static_assert((c), m)

#endif /* NEWOS_SDK_TYPES_H */
