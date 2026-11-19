#ifndef NS_SHIM_STDDEF_H
#define NS_SHIM_STDDEF_H
#include <types.h>
typedef signed long long ptrdiff_t;
#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif
#endif
