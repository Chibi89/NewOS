/**
 * newos/gfx.h — Tipi Geometrici e Palette Colori ARGB Liquid Glass
 */
#ifndef NEWOS_SDK_GFX_H
#define NEWOS_SDK_GFX_H

#include "types.h"

#define NS_RGB(r, g, b)       ((uint32_t)(0xFF000000u | (((uint32_t)(r) & 0xFF) << 16) | (((uint32_t)(g) & 0xFF) << 8) | ((uint32_t)(b) & 0xFF)))
#define NS_RGBA(r, g, b, a)   ((uint32_t)((((uint32_t)(a) & 0xFF) << 24) | (((uint32_t)(r) & 0xFF) << 16) | (((uint32_t)(g) & 0xFF) << 8) | ((uint32_t)(b) & 0xFF)))

#define NS_COLOR_BG_GLASS     0xFFEFF4FAu
#define NS_COLOR_SURFACE      0xFFFFFFFFu
#define NS_COLOR_BORDER       0xFFC8D4E3u
#define NS_COLOR_TEXT         0xFF1A2230u
#define NS_COLOR_TEXT_MUTED   0xFF5B687Cu
#define NS_COLOR_ACCENT       0xFF1D6FE8u
#define NS_COLOR_SUCCESS      0xFF17803Du
#define NS_COLOR_WARNING      0xFFC27803u
#define NS_COLOR_ERROR        0xFFC62828u

typedef struct {
    int32_t x;
    int32_t y;
} ns_point_t;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} ns_rect_t;

static inline bool ns_rect_contains(ns_rect_t r, int32_t px, int32_t py) {
    return (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h);
}

#endif /* NEWOS_SDK_GFX_H */
