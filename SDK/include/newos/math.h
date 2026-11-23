/**
 * newos/math.h — Namespace Matematica Intera, Fixed-Point 16.16 e Vettori 2D/3D/4D
 * Progettato per target freestanding senza FPU/SSE (-mgeneral-regs-only).
 */
#ifndef NEWOS_SDK_MATH_H
#define NEWOS_SDK_MATH_H

#include "types.h"

typedef int32_t ns_fixed16_t; /* 16.16 fixed-point */

#define NS_FIXED_SHIFT 16
#define NS_FIXED_ONE   (1 << NS_FIXED_SHIFT)
#define NS_INT_TO_FIXED(x) ((ns_fixed16_t)((x) << NS_FIXED_SHIFT))
#define NS_FIXED_TO_INT(f) ((int32_t)((f) >> NS_FIXED_SHIFT))
#define NS_FIXED_MUL(a, b) ((ns_fixed16_t)(((int64_t)(a) * (int64_t)(b)) >> NS_FIXED_SHIFT))
#define NS_FIXED_DIV(a, b) ((b) ? (ns_fixed16_t)(((int64_t)(a) << NS_FIXED_SHIFT) / (int64_t)(b)) : 0)

typedef struct { int32_t x, y; } ns_vec2i_t;
typedef struct { int32_t x, y, z; } ns_vec3i_t;
typedef struct { int32_t x, y, z, w; } ns_vec4i_t;

typedef struct { ns_fixed16_t x, y; } ns_vec2f_t;
typedef struct { ns_fixed16_t x, y, z; } ns_vec3f_t;
typedef struct { ns_fixed16_t m[4][4]; } ns_mat4f_t;

static inline int64_t ns_abs64(int64_t v) { return (v < 0) ? -v : v; }

static inline int32_t ns_lerp_i32(int32_t a, int32_t b, int32_t num, int32_t den) {
    if (den == 0) return a;
    return a + (int32_t)(((int64_t)(b - a) * (int64_t)num) / (int64_t)den);
}

static inline uint32_t ns_isqrt(uint64_t n) {
    uint64_t x = n;
    uint64_t y = (x + 1) >> 1;
    while (y < x) {
        x = y;
        y = (x + n / x) >> 1;
    }
    return (uint32_t)x;
}

#endif /* NEWOS_SDK_MATH_H */
