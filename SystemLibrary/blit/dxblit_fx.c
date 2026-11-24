/**
 * SystemLibrary/blit/dxblit_fx.c
 * Scaling veloce nearest-neighbor, gradienti verticali ARGB e conversione RGB565
 */
#include "dxblit.h"

void dxblit_scale_nearest(const dx_surface_desc_t *src, dx_rect_t sr,
                          const dx_surface_desc_t *dst, dx_rect_t dr) {
  if (!src || !dst || !src->pixels || !dst->pixels)
    return;
  if (sr.w <= 0 || sr.h <= 0 || dr.w <= 0 || dr.h <= 0)
    return;
  for (int32_t y = 0; y < dr.h; y++) {
    int32_t dy = dr.y + y;
    if (dy < 0 || dy >= dst->h)
      continue;
    int32_t sy = sr.y + (int32_t)(((int64_t)y * sr.h) / dr.h);
    if (sy < 0 || sy >= src->h)
      continue;
    uint32_t *drow = dst->pixels + dy * dst->pitch + dr.x;
    const uint32_t *srow = src->pixels + sy * src->pitch;
    for (int32_t x = 0; x < dr.w; x++) {
      int32_t dx = dr.x + x;
      if (dx < 0 || dx >= dst->w)
        continue;
      int32_t sx = sr.x + (int32_t)(((int64_t)x * sr.w) / dr.w);
      if (sx >= 0 && sx < src->w)
        drow[x] = srow[sx];
    }
  }
}

void dxblit_gradient_v(uint32_t *dst, int32_t dst_pitch, int32_t x, int32_t y,
                       int32_t w, int32_t h, uint32_t top_argb,
                       uint32_t bot_argb) {
  if (!dst || w <= 0 || h <= 0)
    return;
  int32_t ta = (int32_t)((top_argb >> 24) & 0xFFu);
  int32_t tr = (int32_t)((top_argb >> 16) & 0xFFu);
  int32_t tg = (int32_t)((top_argb >> 8) & 0xFFu);
  int32_t tb = (int32_t)(top_argb & 0xFFu);
  int32_t ba = (int32_t)((bot_argb >> 24) & 0xFFu);
  int32_t br = (int32_t)((bot_argb >> 16) & 0xFFu);
  int32_t bg = (int32_t)((bot_argb >> 8) & 0xFFu);
  int32_t bb = (int32_t)(bot_argb & 0xFFu);
  int32_t den = (h > 1) ? (h - 1) : 1;

  for (int32_t row = 0; row < h; row++) {
    uint32_t a = (uint32_t)(ta + ((ba - ta) * row) / den);
    uint32_t r = (uint32_t)(tr + ((br - tr) * row) / den);
    uint32_t g = (uint32_t)(tg + ((bg - tg) * row) / den);
    uint32_t b = (uint32_t)(tb + ((bb - tb) * row) / den);
    uint32_t c = ((a & 0xFFu) << 24) | ((r & 0xFFu) << 16) |
                 ((g & 0xFFu) << 8) | (b & 0xFFu);
    uint32_t *d = dst + (y + row) * dst_pitch + x;
    for (int32_t col = 0; col < w; col++)
      d[col] = c;
  }
}

void dxblit_convert_565_to_8888(uint32_t *dst, const uint16_t *src,
                                int32_t count) {
  if (!dst || !src || count <= 0)
    return;
  for (int32_t i = 0; i < count; i++) {
    uint16_t p = src[i];
    uint32_t r = ((uint32_t)(p >> 11) & 0x1Fu) * 255u / 31u;
    uint32_t g = ((uint32_t)(p >> 5) & 0x3Fu) * 255u / 63u;
    uint32_t b = ((uint32_t)p & 0x1Fu) * 255u / 31u;
    dst[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
  }
}
