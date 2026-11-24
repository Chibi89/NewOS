/**
 * SystemLibrary/blit/dxblit_simd.c
 * Primitive di copia, fill e alpha-blend veloci (64-bit / SSE2-friendly)
 */
#include "dxblit.h"

void dxblit_copy_rect(uint32_t *dst, int32_t dst_pitch, const uint32_t *src,
                      int32_t src_pitch, int32_t w, int32_t h) {
  if (!dst || !src || w <= 0 || h <= 0)
    return;
  for (int32_t y = 0; y < h; y++) {
    uint32_t *d = dst + y * dst_pitch;
    const uint32_t *s = src + y * src_pitch;
    int32_t x = 0;
    /* Copia a 64-bit (2 pixel per ciclo) */
    for (; x + 1 < w; x += 2) {
      uint64_t p2 = ((uint64_t)s[x + 1] << 32) | s[x];
      d[x] = (uint32_t)(p2 & 0xFFFFFFFFu);
      d[x + 1] = (uint32_t)(p2 >> 32);
    }
    for (; x < w; x++) {
      d[x] = s[x];
    }
  }
}

void dxblit_fill_rect(uint32_t *dst, int32_t dst_pitch, int32_t x, int32_t y,
                      int32_t w, int32_t h, uint32_t argb) {
  if (!dst || w <= 0 || h <= 0)
    return;
  for (int32_t row = 0; row < h; row++) {
    uint32_t *d = dst + (y + row) * dst_pitch + x;
    for (int32_t col = 0; col < w; col++) {
      d[col] = argb;
    }
  }
}

void dxblit_alpha_blend_rect(uint32_t *dst, int32_t dst_pitch,
                             const uint32_t *src, int32_t src_pitch, int32_t w,
                             int32_t h, uint8_t global_alpha) {
  if (!dst || !src || w <= 0 || h <= 0 || global_alpha == 0)
    return;
  if (global_alpha == 255) {
    dxblit_copy_rect(dst, dst_pitch, src, src_pitch, w, h);
    return;
  }
  uint32_t a = (uint32_t)global_alpha;
  uint32_t inv = 255u - a;
  for (int32_t y = 0; y < h; y++) {
    uint32_t *d = dst + y * dst_pitch;
    const uint32_t *s = src + y * src_pitch;
    for (int32_t x = 0; x < w; x++) {
      uint32_t sp = s[x];
      uint32_t dp = d[x];
      uint32_t r = (((sp >> 16) & 0xFFu) * a + ((dp >> 16) & 0xFFu) * inv) >> 8;
      uint32_t g = (((sp >> 8) & 0xFFu) * a + ((dp >> 8) & 0xFFu) * inv) >> 8;
      uint32_t b = ((sp & 0xFFu) * a + (dp & 0xFFu) * inv) >> 8;
      d[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
  }
}
