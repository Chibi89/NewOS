#ifndef NS_SHIM_STDINT_H
#define NS_SHIM_STDINT_H
#include <types.h>
typedef signed long long intptr_t;
#ifndef INT8_MIN
#define INT8_MIN (-128)
#define INT8_MAX 127
#define UINT8_MAX 255
#define INT16_MIN (-32768)
#define INT16_MAX 32767
#define UINT16_MAX 65535
#endif
#ifndef INT32_MAX
#define INT32_MAX 2147483647
#endif
#ifndef INT32_MIN
#define INT32_MIN (-2147483647 - 1)
#endif
#ifndef UINT32_MAX
#define UINT32_MAX 0xFFFFFFFFU
#endif
#ifndef INT64_MAX
#define INT64_MAX 9223372036854775807LL
#define INT64_MIN (-9223372036854775807LL - 1LL)
#define UINT64_MAX 0xFFFFFFFFFFFFFFFFULL
#endif
#ifndef INTPTR_MAX
#define INTPTR_MAX INT64_MAX
#define INTPTR_MIN INT64_MIN
#define UINTPTR_MAX UINT64_MAX
#endif
#ifndef SIZE_MAX
#define SIZE_MAX 0xFFFFFFFFFFFFFFFFULL
#endif
#ifndef INT64_C
#define INT64_C(c) c ## LL
#define UINT64_C(c) c ## ULL
#define INT32_C(c) c
#define UINT32_C(c) c ## U
#endif
#endif
