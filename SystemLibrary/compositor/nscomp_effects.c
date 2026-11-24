/**
 * SystemLibrary/compositor/nscomp_effects.c
 * Box blur 3x3, tint Liquid Glass, corner mask e decadimento alpha ombre
 */
#include "nscomp.h"

void nsc_blur_box_3x3(uint32_t *dst, const uint32_t *src, int32_t w,
                      int32_t h) {
  if (!dst || !src || w <= 2 || h <= 2)
    return;
  for (int32_t y = 0; y < h; y++) {
    for (int32_t x = 0; x < w; x++) {
      if (x == 0 || y == 0 || x == w - 1 || y == h - 1) {
        dst[y * w + x] = src[y * w + x];
        continue;
      }
      uint32_t sr = 0, sg = 0, sb = 0;
      for (int32_t ky = -1; ky <= 1; ky++) {
        const uint32_t *row = src + (y + ky) * w;
        for (int32_t kx = -1; kx <= 1; kx++) {
          uint32_t p = row[x + kx];
          sr += (p >> 16) & 0xFFu;
          sg += (p >> 8) & 0xFFu;
          sb += p & 0xFFu;
        }
      }
      dst[y * w + x] =
          0xFF000000u | ((sr / 9u) << 16) | ((sg / 9u) << 8) | (sb / 9u);
    }
  }
}

uint8_t nsc_shadow_alpha_edge(int32_t dist_px, int32_t radius_px,
                              uint8_t max_alpha) {
  if (dist_px <= 0)
    return max_alpha;
  if (radius_px <= 0 || dist_px >= radius_px)
    return 0;
  int32_t rem = radius_px - dist_px;
  int32_t num = rem * rem * (int32_t)max_alpha;
  int32_t den = radius_px * radius_px;
  return (uint8_t)(num / den);
}

void nsc_tint_rect(uint32_t *buf, int32_t pitch, int32_t w, int32_t h,
                   uint32_t tint_argb, uint8_t alpha) {
  if (!buf || w <= 0 || h <= 0 || alpha == 0)
    return;
  uint32_t tr = (tint_argb >> 16) & 0xFFu;
  uint32_t tg = (tint_argb >> 8) & 0xFFu;
  uint32_t tb = tint_argb & 0xFFu;
  uint32_t a = alpha;
  uint32_t inv = 255u - a;
  for (int32_t y = 0; y < h; y++) {
    uint32_t *row = buf + y * pitch;
    for (int32_t x = 0; x < w; x++) {
      uint32_t p = row[x];
      uint32_t r = (tr * a + ((p >> 16) & 0xFFu) * inv) >> 8;
      uint32_t g = (tg * a + ((p >> 8) & 0xFFu) * inv) >> 8;
      uint32_t b = (tb * a + (p & 0xFFu) * inv) >> 8;
      row[x] = (p & 0xFF000000u) | (r << 16) | (g << 8) | b;
    }
  }
}

void nsc_apply_round_mask(uint32_t *buf, int32_t pitch, int32_t w, int32_t h,
                          int32_t radius) {
  if (!buf || w <= 0 || h <= 0 || radius <= 0)
    return;
  int32_t r2 = 4 * radius * radius;
  for (int32_t y = 0; y < h; y++) {
    for (int32_t x = 0; x < w; x++) {
      int32_t cx = (x < radius) ? (radius - 1 - x)
                                : ((x >= w - radius) ? (x - (w - radius)) : -1);
      int32_t cy = (y < radius) ? (radius - 1 - y)
                                : ((y >= h - radius) ? (y - (h - radius)) : -1);
      if (cx >= 0 && cy >= 0) {
        int32_t dx = 2 * cx + 1;
        int32_t dy = 2 * cy + 1;
        if (dx * dx + dy * dy > r2)
          buf[y * pitch + x] = 0x00000000u;
      }
    }
  }
}
