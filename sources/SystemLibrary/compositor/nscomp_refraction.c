/**
 * SystemLibrary/compositor/nscomp_refraction.c
 * Effetto Rifrazione Liquid Glass continuo su tutta l'area interna della finestra
 */
#include "nscomp.h"

static uint32_t add_rim_caustic(uint32_t argb, int32_t boost) {
  uint32_t r = ((argb >> 16) & 0xFFu) + (uint32_t)boost;
  uint32_t g = ((argb >> 8) & 0xFFu) + (uint32_t)(boost + 2);
  uint32_t b = (argb & 0xFFu) + (uint32_t)(boost + 5);
  if (r > 255u)
    r = 255u;
  if (g > 255u)
    g = 255u;
  if (b > 255u)
    b = 255u;
  return (argb & 0xFF000000u) | (r << 16) | (g << 8) | b;
}

void nsc_liquid_refraction(uint32_t *dst, const uint32_t *bg, int32_t dst_pitch,
                           int32_t bg_pitch, int32_t w, int32_t h,
                           int32_t rim_px, int32_t strength_px) {
  if (!dst || !bg || w <= 2 || h <= 2)
    return;
  if (strength_px <= 0)
    strength_px = 12;
  int32_t half_w = w / 2;
  int32_t half_h = h / 2;
  if (half_w <= 0)
    half_w = 1;
  if (half_h <= 0)
    half_h = 1;
  (void)rim_px;

  for (int32_t py = 0; py < h; py++) {
    int32_t dy = py - half_h;
    int32_t dt = py;
    int32_t db = h - 1 - py;
    int32_t dy_min = (dt < db) ? dt : db;
    /* Q8 normalized Y^2 in [0..256] */
    int32_t ny_q8 = (dy * 256) / half_h;
    int32_t ny2 = (ny_q8 * ny_q8) >> 8;
    uint32_t *drow = dst + py * dst_pitch;

    for (int32_t px = 0; px < w; px++) {
      int32_t dx = px - half_w;
      int32_t nx_q8 = (dx * 256) / half_w;
      int32_t nx2 = (nx_q8 * nx_q8) >> 8;
      int32_t r2_q8 = nx2 + ny2; /* 0 al centro, ~512 agli angoli */

      /* Effetto lente Liquid Glass su TUTTA l'area interna (zoom + curvatura) */
      int32_t k_q8 = (strength_px * (64 + r2_q8)) >> 6;
      int32_t ox = (dx * k_q8) >> 10;
      int32_t oy = (dy * k_q8) >> 10;

      /* Leggera ondulazione ottica continua lungo la lastra di vetro */
      int32_t wave = ((px + py) & 15) - 8;
      ox += (wave * strength_px) >> 6;

      int32_t sx = px - ox;
      int32_t sy = py - oy;
      if (sx < 0)
        sx = 0;
      if (sx >= w)
        sx = w - 1;
      if (sy < 0)
        sy = 0;
      if (sy >= h)
        sy = h - 1;

      uint32_t sample = bg[sy * bg_pitch + sx];

      int32_t dl = px;
      int32_t dr = w - 1 - px;
      int32_t dx_min = (dl < dr) ? dl : dr;
      int32_t d = (dx_min < dy_min) ? dx_min : dy_min;
      if (d < 4)
        sample = add_rim_caustic(sample, (4 - d) * 3);

      drow[px] = sample;
    }
  }
}
