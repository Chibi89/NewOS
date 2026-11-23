/**
 * newos/types.h — Tipi standard freestanding per NewSoft SDK v2.0 (x86_64)
 */
#ifndef NEWOS_SDK_TYPES_H
#define NEWOS_SDK_TYPES_H

#ifndef _NEWOS_TYPES_DEFINED
#define _NEWOS_TYPES_DEFINED

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

typedef uint64_t size_t;
typedef int64_t  ssize_t;
typedef uint64_t uintptr_t;
typedef int64_t  intptr_t;

#ifndef __cplusplus
typedef _Bool bool;
#define true  1
#define false 0
#endif

#ifndef NULL
#define NULL ((void*)0)
#endif

#define offsetof(type, member) __builtin_offsetof(type, member)

#endif /* _NEWOS_TYPES_DEFINED */
#endif /* NEWOS_SDK_TYPES_H */
